/*
 * The zone allocator: see z_zone.h for the arena layout, the tag model, the
 * accounting identity and the public contract.
 *
 * Three properties are worth naming here because they are the reason the code
 * looks the way it does:
 *
 *  - the block headers are part of the arena, laid out with 32-bit offsets
 *    rather than pointers. The header is therefore the same 16 bytes on the
 *    xtensa target and on a 64-bit host, and no size differs between the build
 *    that ships and the build the tests run;
 *  - free blocks are always coalesced immediately. A free merges forward and
 *    then backward, so two free blocks are never adjacent and a first-fit scan
 *    is also a complete one: if the scan misses, no rearrangement of the same
 *    bytes could help;
 *  - every validation happens before the first arena write. A rejected free
 *    never touches a header, which is what makes a caller's misuse a no-op
 *    instead of corruption.
 *
 * Compiled by the C compiler under the firmware's C99 flags; no C++ construct,
 * no allocation, no stdio, no floating point, no mutable global.
 */
#include "z_zone.h"

/* --- Block layout -------------------------------------------------------- */

/* End of the block list. No real block offset can take this value: a block
 * needs ZONE_HEADER_SIZE bytes, so the largest possible offset is
 * UINT32_MAX - ZONE_HEADER_SIZE. */
#define ZONE_OFFSET_NONE 0xFFFFFFFFu

/*
 * One block, stored inside the arena at its own offset. `next` and `prev` are
 * byte offsets from the arena base, so the struct is 16 bytes everywhere.
 * `size` is the payload capacity in bytes; the payload begins immediately
 * after the header and is the only part z_alloc() ever returns.
 *
 * A free block keeps the tag of the block that last owned it; the tag of a
 * free block is never read.
 */
typedef struct zone_block {
    uint32_t next;    /* offset of the next block, or ZONE_OFFSET_NONE */
    uint32_t prev;    /* offset of the previous block, or ZONE_OFFSET_NONE */
    uint32_t size;    /* payload capacity in bytes */
    uint8_t tag;      /* PU_STATIC, PU_LEVEL or PU_CACHE while in use */
    uint8_t inUse;    /* 1 for a live block, 0 for a free one */
    uint16_t reserved;/* keeps the header 16 bytes; always 0 */
} zone_block_t;

/* The header is part of the arena's byte budget, so its size is not an
 * implementation detail: a change here must move ZONE_HEADER_SIZE in the
 * header, and this array refuses to compile otherwise. */
typedef char zone_block_size_check[
    (sizeof(zone_block_t) == ZONE_HEADER_SIZE) ? 1 : -1];

/* --- State helpers ------------------------------------------------------- */

/* Clears the public state. Every rejection goes through here, the same way
 * w_wad.c's reject() does: a rejected call can never leave a half-open zone. */
static zone_error_t clearState(zone_state_t* state, zone_error_t error) {
    state->base = NULL;
    state->arenaSize = 0u;
    state->firstBlock = ZONE_OFFSET_NONE;
    state->inUse = 0u;
    state->freeBytes = 0u;
    state->largestFree = 0u;
    state->peakInUse = 0u;
    state->overhead = 0u;
    state->failures = 0u;
    state->purges = 0u;
    state->open = 0;
    state->error = error;

    return error;
}

/* Address of a block. Every offset in the list has already been validated
 * against the arena by construction; z_init() made the first one and splits and
 * merges only ever stay inside it. */
static zone_block_t* blockAt(const zone_state_t* state, uint32_t offset) {
    return (zone_block_t*)(void*)(state->base + (size_t)offset);
}

/* The free-list scan below is the only place largestFree is decided. Every
 * mutation calls it once, so z_stats() stays O(1) and never walks the arena on
 * the frame path. */
static void refreshLargestFree(zone_state_t* state) {
    uint32_t largest = 0u;
    uint32_t offset = state->firstBlock;

    while (offset != ZONE_OFFSET_NONE) {
        const zone_block_t* block = blockAt(state, offset);

        if (block->inUse == 0u && block->size > largest) {
            largest = block->size;
        }
        offset = block->next;
    }

    state->largestFree = largest;
}

/* Finds the block whose header starts at `offset`. Returns ZONE_OFFSET_NONE if
 * no block starts there, which is how a pointer into the middle of a payload is
 * rejected. */
static uint32_t findBlock(const zone_state_t* state, uint32_t offset) {
    uint32_t at = state->firstBlock;

    while (at != ZONE_OFFSET_NONE) {
        if (at == offset) {
            return at;
        }
        at = blockAt(state, at)->next;
    }

    return ZONE_OFFSET_NONE;
}

/* --- Free and coalesce --------------------------------------------------- */

/*
 * Marks the block at `offset` free, then merges it with its following free
 * neighbour and then with its preceding one. Merging absorbs the neighbour's
 * header into payload capacity, which is why freeBytes grows by a header and
 * overhead shrinks by one for each merge.
 *
 * Returns the offset of the block that survives the merge: `offset` itself when
 * nothing merged backward, or the preceding block when it did. The caller needs
 * that because the block it passed in may no longer exist.
 */
static uint32_t releaseBlock(zone_state_t* state, uint32_t offset) {
    zone_block_t* block = blockAt(state, offset);

    block->inUse = 0u;
    state->inUse -= block->size;
    state->freeBytes += block->size;

    if (block->next != ZONE_OFFSET_NONE) {
        zone_block_t* next = blockAt(state, block->next);

        if (next->inUse == 0u) {
            block->size += ZONE_HEADER_SIZE + next->size;
            block->next = next->next;
            if (next->next != ZONE_OFFSET_NONE) {
                blockAt(state, next->next)->prev = offset;
            }
            state->freeBytes += ZONE_HEADER_SIZE;
            state->overhead -= ZONE_HEADER_SIZE;
        }
    }

    if (block->prev != ZONE_OFFSET_NONE) {
        zone_block_t* prev = blockAt(state, block->prev);

        if (prev->inUse == 0u) {
            prev->size += ZONE_HEADER_SIZE + block->size;
            prev->next = block->next;
            if (block->next != ZONE_OFFSET_NONE) {
                blockAt(state, block->next)->prev = block->prev;
            }
            state->freeBytes += ZONE_HEADER_SIZE;
            state->overhead -= ZONE_HEADER_SIZE;
            return block->prev;
        }
    }

    return offset;
}

/*
 * Frees every live block carrying `tag`, in block order, and returns the total
 * payload capacity reclaimed.
 *
 * The walk re-reads `next` from the surviving block after each release:
 * releaseBlock() may have absorbed both the block it was given and the one that
 * followed it, so neither the old offset nor a captured next-offset is safe to
 * step to.
 */
static uint32_t releaseTag(zone_state_t* state, zone_tag_t tag) {
    uint32_t recovered = 0u;
    uint32_t offset = state->firstBlock;

    while (offset != ZONE_OFFSET_NONE) {
        zone_block_t* block = blockAt(state, offset);

        if (block->inUse != 0u && block->tag == (uint8_t)tag) {
            recovered += block->size;
            offset = releaseBlock(state, offset);
        }

        offset = blockAt(state, offset)->next;
    }

    refreshLargestFree(state);
    return recovered;
}

/* --- Allocation ---------------------------------------------------------- */

/* Rounds a validated request up to the alignment, with no overflow: the caller
 * has already rejected anything above ZONE_MAX_ALLOC. */
static uint32_t roundRequest(uint32_t size) {
    return (size + (ZONE_ALIGNMENT - 1u)) & ~(ZONE_ALIGNMENT - 1u);
}

/*
 * First-fit over the block list. Free blocks are never adjacent (releaseBlock()
 * coalesces), so this is also best-effort in the only sense that matters: if no
 * single free block fits, no chain of them can be combined without moving live
 * data, which a zone never does.
 *
 * A split only happens when the remainder can itself hold a header and one
 * minimum payload. Otherwise the whole block is handed over and its full
 * capacity is charged to inUse, so no bytes fall out of the accounting.
 */
static void* allocateBlock(zone_state_t* state, uint32_t size, zone_tag_t tag) {
    const uint32_t rounded = roundRequest(size);
    uint32_t offset = state->firstBlock;

    while (offset != ZONE_OFFSET_NONE) {
        zone_block_t* block = blockAt(state, offset);

        if (block->inUse == 0u && block->size >= rounded) {
            const uint32_t leftover = block->size - rounded;

            if (leftover >= ZONE_HEADER_SIZE + ZONE_ALIGNMENT) {
                const uint32_t restOffset = offset + ZONE_HEADER_SIZE + rounded;
                zone_block_t* rest = blockAt(state, restOffset);

                rest->prev = offset;
                rest->next = block->next;
                rest->size = leftover - ZONE_HEADER_SIZE;
                rest->tag = block->tag;
                rest->inUse = 0u;
                rest->reserved = 0u;

                if (block->next != ZONE_OFFSET_NONE) {
                    blockAt(state, block->next)->prev = restOffset;
                }
                block->next = restOffset;
                block->size = rounded;

                state->overhead += ZONE_HEADER_SIZE;
                state->freeBytes -= rounded + ZONE_HEADER_SIZE;
            } else {
                state->freeBytes -= block->size;
            }

            block->inUse = 1u;
            block->tag = (uint8_t)tag;
            state->inUse += block->size;
            if (state->inUse > state->peakInUse) {
                state->peakInUse = state->inUse;
            }

            refreshLargestFree(state);
            return state->base + (size_t)offset + ZONE_HEADER_SIZE;
        }

        offset = block->next;
    }

    return NULL;
}

/* Shared argument validation for both allocation entry points. */
static zone_error_t checkAllocArguments(const zone_state_t* state, size_t size,
                                        zone_tag_t tag) {
    if (state == NULL) {
        return ZONE_ERR_NULL_STATE;
    }
    if (state->open == 0) {
        return ZONE_ERR_NOT_OPEN;
    }
    if (tag != PU_STATIC && tag != PU_LEVEL && tag != PU_CACHE) {
        return ZONE_ERR_BAD_TAG;
    }
    if (size == 0u) {
        return ZONE_ERR_SIZE_ZERO;
    }
    if (size > (size_t)ZONE_MAX_ALLOC) {
        return ZONE_ERR_SIZE_RANGE;
    }

    return ZONE_ERR_OK;
}

/* One purge operation. Shared by z_purge() and z_alloc_purging() so both count
 * exactly one purge and both use the same coalescing walk. */
static uint32_t purgeCache(zone_state_t* state) {
    const uint32_t recovered = releaseTag(state, PU_CACHE);

    state->purges += 1u;
    return recovered;
}

/* --- Public API ---------------------------------------------------------- */

zone_error_t z_init(zone_state_t* state, void* buffer, size_t size) {
    zone_block_t* first;

    if (state == NULL) {
        return ZONE_ERR_NULL_STATE;
    }

    /* Closed and empty from the first line: only the final assignments below
     * open it, so no rejection can leave a partial arena behind. */
    clearState(state, ZONE_ERR_OK);

    if (buffer == NULL) {
        return clearState(state, ZONE_ERR_NULL_BUFFER);
    }
    if (size > (size_t)UINT32_MAX) {
        return clearState(state, ZONE_ERR_SIZE_RANGE);
    }
    if (((uintptr_t)buffer % ZONE_ALIGNMENT) != 0u) {
        return clearState(state, ZONE_ERR_BAD_ARENA);
    }
    if (size < (size_t)ZONE_MIN_ARENA) {
        return clearState(state, ZONE_ERR_BAD_ARENA);
    }

    state->base = (uint8_t*)buffer;
    state->arenaSize = (uint32_t)size;
    state->firstBlock = 0u;
    state->inUse = 0u;
    state->overhead = ZONE_HEADER_SIZE;
    state->freeBytes = state->arenaSize - ZONE_HEADER_SIZE;
    state->largestFree = state->freeBytes;
    state->peakInUse = 0u;
    state->failures = 0u;
    state->purges = 0u;
    state->open = 1;
    state->error = ZONE_ERR_OK;

    first = blockAt(state, 0u);
    first->next = ZONE_OFFSET_NONE;
    first->prev = ZONE_OFFSET_NONE;
    first->size = state->freeBytes;
    first->tag = (uint8_t)PU_STATIC;
    first->inUse = 0u;
    first->reserved = 0u;

    return ZONE_ERR_OK;
}

void* z_alloc(zone_state_t* state, size_t size, zone_tag_t tag, zone_error_t* error) {
    zone_error_t code = checkAllocArguments(state, size, tag);
    void* result = NULL;

    if (code == ZONE_ERR_OK) {
        result = allocateBlock(state, (uint32_t)size, tag);
        if (result == NULL) {
            code = ZONE_ERR_NO_BLOCK;
            state->failures += 1u;
        }
    }

    if (error != NULL) {
        *error = code;
    }

    return result;
}

void* z_alloc_purging(zone_state_t* state, size_t size, zone_tag_t tag,
                      zone_error_t* error) {
    zone_error_t code = checkAllocArguments(state, size, tag);
    void* result = NULL;

    if (code == ZONE_ERR_OK) {
        result = allocateBlock(state, (uint32_t)size, tag);
        if (result == NULL) {
            /* One purge, one retry. A purge that recovers nothing still spends
             * the retry: there is no second pass and no second purge. */
            (void)purgeCache(state);
            result = allocateBlock(state, (uint32_t)size, tag);
            if (result == NULL) {
                code = ZONE_ERR_NO_BLOCK;
                state->failures += 1u;
            }
        }
    }

    if (error != NULL) {
        *error = code;
    }

    return result;
}

zone_error_t z_free(zone_state_t* state, void* pointer) {
    uint32_t offset;

    if (state == NULL) {
        return ZONE_ERR_NULL_STATE;
    }
    if (state->open == 0) {
        return ZONE_ERR_NOT_OPEN;
    }
    if (pointer == NULL) {
        return ZONE_ERR_NULL_POINTER;
    }

    {
        const uintptr_t base = (uintptr_t)state->base;
        const uintptr_t value = (uintptr_t)pointer;
        uintptr_t delta;

        if (value < base || value - base >= (uintptr_t)state->arenaSize) {
            return ZONE_ERR_NOT_OWNED;
        }

        delta = value - base;

        /* A payload starts a header after a block offset, and every block
         * offset is a multiple of the alignment, so a payload pointer must be
         * too. A pointer one byte into a payload lands here. */
        if ((delta % ZONE_ALIGNMENT) != 0u) {
            return ZONE_ERR_NOT_OWNED;
        }
        if (delta < ZONE_HEADER_SIZE) {
            return ZONE_ERR_NOT_OWNED;
        }

        /* A pointer into the middle of a block is aligned but does not resolve
         * to a block start, so the walk rejects it before anything is written. */
        offset = findBlock(state, (uint32_t)(delta - ZONE_HEADER_SIZE));
        if (offset == ZONE_OFFSET_NONE) {
            return ZONE_ERR_NOT_OWNED;
        }
        if (blockAt(state, offset)->inUse == 0u) {
            return ZONE_ERR_DOUBLE_FREE;
        }
    }

    (void)releaseBlock(state, offset);
    refreshLargestFree(state);

    return ZONE_ERR_OK;
}

zone_error_t z_free_tag(zone_state_t* state, zone_tag_t tag, uint32_t* freedBytes) {
    if (state == NULL) {
        if (freedBytes != NULL) {
            *freedBytes = 0u;
        }
        return ZONE_ERR_NULL_STATE;
    }

    if (freedBytes != NULL) {
        *freedBytes = 0u;
    }

    if (state->open == 0) {
        return ZONE_ERR_NOT_OPEN;
    }
    if (tag != PU_STATIC && tag != PU_LEVEL && tag != PU_CACHE) {
        return ZONE_ERR_BAD_TAG;
    }

    if (freedBytes != NULL) {
        *freedBytes = releaseTag(state, tag);
    } else {
        (void)releaseTag(state, tag);
    }

    return ZONE_ERR_OK;
}

zone_error_t z_purge(zone_state_t* state, uint32_t* freedBytes) {
    if (state == NULL) {
        if (freedBytes != NULL) {
            *freedBytes = 0u;
        }
        return ZONE_ERR_NULL_STATE;
    }

    if (freedBytes != NULL) {
        *freedBytes = 0u;
    }

    if (state->open == 0) {
        return ZONE_ERR_NOT_OPEN;
    }

    if (freedBytes != NULL) {
        *freedBytes = purgeCache(state);
    } else {
        (void)purgeCache(state);
    }

    return ZONE_ERR_OK;
}

zone_stats_t z_stats(const zone_state_t* state) {
    zone_stats_t stats;

    if (state == NULL || state->open == 0) {
        stats.arenaSize = 0u;
        stats.inUse = 0u;
        stats.freeBytes = 0u;
        stats.overhead = 0u;
        stats.largestFree = 0u;
        stats.peakInUse = 0u;
        stats.failures = 0u;
        stats.purges = 0u;
        return stats;
    }

    stats.arenaSize = state->arenaSize;
    stats.inUse = state->inUse;
    stats.freeBytes = state->freeBytes;
    stats.overhead = state->overhead;
    stats.largestFree = state->largestFree;
    stats.peakInUse = state->peakInUse;
    stats.failures = state->failures;
    stats.purges = state->purges;

    return stats;
}

uint32_t z_largest_free(const zone_state_t* state) {
    if (state == NULL || state->open == 0) {
        return 0u;
    }

    return state->largestFree;
}
