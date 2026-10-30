/*
 * The zone allocator of the DOOM port.
 *
 * DOOM allocates everything it needs from one arena it owns: level geometry,
 * textures, sound effects, sprites. This file is that arena over a
 * caller-supplied buffer, with no allocation of its own, so the firmware can
 * keep the whole working set in internal DRAM and a host test can put the same
 * arena on the stack. It knows nothing about WADs, lumps or textures -- tags
 * are just three numbers a caller attaches to memory.
 *
 * WHY A ZONE AND NOT malloc()
 *
 * The board reports about 201 KB of free internal DRAM with the display path
 * live. A 64,000 B twin framebuffer is planned on top of that, leaving about
 * 137 KB for the world. That number is a hard budget, and malloc gives no way
 * to say "this is all the world may ever have": the first level that asks for
 * too much fails somewhere else, later, inside a different subsystem. A zone
 * with one fixed arena turns the budget into an invariant, and its stats make
 * the remaining bytes and the largest hole readable every frame.
 *
 * The largest lump of the real shareware container is 68,168 B (`HELP1`), which
 * is more than a 64 KB zone. That is deliberate: `HELP1` is a full-screen image
 * that never belongs to the level working set, so the allocator must report
 * `ZONE_ERR_NO_BLOCK` cheaply and clearly and the port handles that lump outside
 * the zone. The cap is not raised to make one lump fit.
 *
 * ARENA LAYOUT
 *
 *   base + 0                +-----------------------------+
 *                           | block header (16 bytes)     |
 *   base + 16               | payload capacity bytes      | <- z_alloc() pointer
 *                           +-----------------------------+
 *                           | next block header (16)      |
 *                           | ...                         |
 *                           +-----------------------------+
 *
 * Every byte of the arena belongs to exactly one of three places: a block
 * header, a live block's payload capacity, or a free block's payload capacity.
 * There is no separate metadata area, no bitmap and no header outside the
 * arena, so the accounting identity
 *
 *     stats.inUse + stats.freeBytes + stats.overhead == stats.arenaSize
 *
 * holds after every call. `inUse` counts whole block capacities, not the sizes
 * requested, which is what makes the identity exact: a request that consumes a
 * small final block owns that block's spare bytes too, and they are reported as
 * in use rather than silently disappearing.
 *
 * The 16-byte header is laid out with uint32_t offsets instead of pointers, so
 * it is byte-for-byte the same size on the 32-bit target and a 64-bit host.
 * The header is never part of a returned payload: z_alloc() always returns
 * `base + blockOffset + ZONE_HEADER_SIZE`.
 *
 * THE TAG MODEL
 *
 *   PU_STATIC  memory that lives until it is freed explicitly
 *   PU_LEVEL   the level working set; freed as a group on level change
 *   PU_CACHE   disposable memory; z_purge() reclaims it under pressure
 *
 * z_free_tag() frees one whole tag group and returns the bytes it recovered.
 * z_purge() frees PU_CACHE blocks only and reports the bytes it recovered;
 * statics and level blocks are untouched. z_alloc_purging() retries one
 * allocation after one purge, which is the pressure valve a level load needs
 * when the previous level's cache is still resident.
 *
 * MISUSE NEVER TOUCHES THE ARENA
 *
 * Every entry point validates before it writes. A free of a foreign pointer, a
 * free of a pointer into the middle of a block, a double free or a null
 * pointer returns its own code and changes no byte of the arena and no
 * accounting field. The exact validation order is documented per function.
 *
 * This is C99 only: <stddef.h> and <stdint.h>, no allocation, no globals, no
 * stdio, no floating point. The caller owns the one state struct.
 */
#ifndef DOOM_Z_ZONE_H
#define DOOM_Z_ZONE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* --- Layout constants ---------------------------------------------------- */

/* Returned pointers are aligned to this many bytes. A block offset is always a
 * multiple of it, so the payload after the header keeps the alignment. */
#define ZONE_ALIGNMENT 8u

/* One block header, inside the arena. Identical on 32-bit and 64-bit builds;
 * see the layout comment above. */
#define ZONE_HEADER_SIZE 16u

/* The smallest arena that can hold one header and one minimum payload. */
#define ZONE_MIN_ARENA (ZONE_HEADER_SIZE + ZONE_ALIGNMENT)

/* The largest request z_alloc() accepts, so rounding it up to ZONE_ALIGNMENT
 * cannot overflow the 32-bit capacity field. */
#define ZONE_MAX_ALLOC 0xFFFFFFF8u

/* --- Tag values ---------------------------------------------------------- */

/* The three tags DOOM needs. An enum, not macros, so a debugger can name the
 * value; the numeric order is an implementation detail callers must not rely
 * on, but these three are the only values z_alloc() accepts. */
typedef enum zone_tag {
    PU_STATIC = 0, /* lives until freed explicitly */
    PU_LEVEL = 1,  /* level working set, freed as a group */
    PU_CACHE = 2   /* disposable, reclaimed by z_purge() */
} zone_tag_t;

/* --- Error codes --------------------------------------------------------- */

/*
 * One code per failure class, never a shared "zone error" catch-all:
 *
 *   ZONE_ERR_OK             the call did what it says
 *   ZONE_ERR_NULL_STATE     the caller passed no state struct
 *   ZONE_ERR_NOT_OPEN       the state was never z_init()ed, or was rejected
 *   ZONE_ERR_NULL_BUFFER    z_init() was given no arena
 *   ZONE_ERR_BAD_ARENA      the arena is smaller than ZONE_MIN_ARENA or its
 *                           address is not ZONE_ALIGNMENT-aligned
 *   ZONE_ERR_SIZE_RANGE     the size cannot be described by the 32-bit arena
 *                           fields (init: > UINT32_MAX, alloc: > ZONE_MAX_ALLOC)
 *   ZONE_ERR_SIZE_ZERO      z_alloc() was asked for zero bytes
 *   ZONE_ERR_NO_BLOCK       no free block is large enough
 *   ZONE_ERR_NULL_POINTER   z_free() was given NULL
 *   ZONE_ERR_NOT_OWNED      the pointer is outside this arena, misaligned, or
 *                           not the payload start of a block
 *   ZONE_ERR_DOUBLE_FREE    the pointer belongs to a block that is already free
 *   ZONE_ERR_BAD_TAG        the tag is not PU_STATIC, PU_LEVEL or PU_CACHE
 *
 * A failed z_alloc()/z_alloc_purging() records exactly one failure in
 * `stats.failures` -- and it only ever increments that counter; the byte
 * accounting is left exactly as it was, as every misuse does.
 *
 * With a C99 compiler these are enumeration constants, not macros, so a
 * debugger can name the value and no call site needs a magic number.
 */
typedef enum zone_error {
    ZONE_ERR_OK = 0,
    ZONE_ERR_NULL_STATE = 1,
    ZONE_ERR_NOT_OPEN = 2,
    ZONE_ERR_NULL_BUFFER = 3,
    ZONE_ERR_BAD_ARENA = 4,
    ZONE_ERR_SIZE_RANGE = 5,
    ZONE_ERR_SIZE_ZERO = 6,
    ZONE_ERR_NO_BLOCK = 7,
    ZONE_ERR_NULL_POINTER = 8,
    ZONE_ERR_NOT_OWNED = 9,
    ZONE_ERR_DOUBLE_FREE = 10,
    ZONE_ERR_BAD_TAG = 11
} zone_error_t;

/* --- Zone state ---------------------------------------------------------- */

/*
 * Everything the zone knows, owned by the caller. There is no static data in
 * this translation unit, so two zones can coexist (a test creates one per case)
 * and the state can live on the stack or next to the scene.
 *
 * Treat the fields as read-only after a successful z_init(): the public
 * accessor is z_stats(), which returns the same numbers by value. `open` is 1
 * and `error` is ZONE_ERR_OK only for an accepted arena. A rejected init zeroes
 * all fields, so a stale state can never answer a later allocation.
 */
typedef struct zone_state {
    uint8_t* base;       /* start of the caller's arena */
    uint32_t arenaSize;  /* bytes available at base */
    uint32_t firstBlock; /* offset of the first block, or ZONE_OFFSET_NONE */
    uint32_t inUse;      /* payload capacity of live blocks */
    uint32_t freeBytes;  /* payload capacity of free blocks */
    uint32_t largestFree;/* biggest single free payload */
    uint32_t peakInUse;  /* highest inUse ever reached */
    uint32_t overhead;   /* bytes held by block headers */
    uint32_t failures;   /* valid allocations that found no block */
    uint32_t purges;     /* purge operations performed */
    int open;            /* 1 after a successful z_init() */
    zone_error_t error;  /* result of the last z_init() */
} zone_state_t;

/* --- Accounting snapshot ------------------------------------------------- */

/*
 * The numbers to read every frame. Returned by value so a caller can copy one
 * snapshot without racing on a field it did not ask for.
 *
 * `inUse + freeBytes + overhead == arenaSize` always. `largestFree` reports
 * fragmentation honestly: free bytes split across small holes can be a large
 * number while no request of that size can be satisfied.
 */
typedef struct zone_stats {
    uint32_t arenaSize;  /* bytes the caller supplied */
    uint32_t inUse;      /* payload capacity of live blocks */
    uint32_t freeBytes;  /* payload capacity of free blocks */
    uint32_t overhead;   /* bytes held by block headers */
    uint32_t largestFree;/* biggest single free payload */
    uint32_t peakInUse;  /* highest inUse ever reached */
    uint32_t failures;   /* valid allocations that found no block */
    uint32_t purges;     /* purge operations performed */
} zone_stats_t;

/* --- Public API ---------------------------------------------------------- */

/**
 * @brief Places the zone over a caller-supplied arena.
 *
 * Validation order, and the code each step reports:
 *
 *   1. state == NULL                              ZONE_ERR_NULL_STATE
 *   2. buffer == NULL                             ZONE_ERR_NULL_BUFFER
 *   3. size > UINT32_MAX                          ZONE_ERR_SIZE_RANGE
 *   4. buffer address % ZONE_ALIGNMENT != 0       ZONE_ERR_BAD_ARENA
 *   5. size < ZONE_MIN_ARENA                      ZONE_ERR_BAD_ARENA
 *
 * On success the whole arena is one free block and every counter starts at
 * zero. The buffer stays owned by the caller; z_init() does not clear it.
 * A rejected call zeroes the state and records the code in `state->error`.
 *
 * @param state  Caller-owned zone state; rewritten on every call.
 * @param buffer Arena start; must be ZONE_ALIGNMENT-aligned.
 * @param size   Bytes available at @p buffer.
 * @return ZONE_ERR_OK when the arena was accepted, one of the codes above
 *         otherwise.
 */
zone_error_t z_init(zone_state_t* state, void* buffer, size_t size);

/**
 * @brief Allocates @p size bytes with a tag, or reports why it could not.
 *
 * The request is rounded up to a multiple of ZONE_ALIGNMENT and the first free
 * block that fits is used. When the block has at least one header plus one
 * minimum payload left over, the remainder becomes a new free block; otherwise
 * the whole block is consumed and `inUse` counts its full capacity.
 *
 * Validation order:
 *
 *   1. state == NULL                              ZONE_ERR_NULL_STATE
 *   2. state was never accepted                   ZONE_ERR_NOT_OPEN
 *   3. tag is not one of the three                ZONE_ERR_BAD_TAG
 *   4. size == 0                                  ZONE_ERR_SIZE_ZERO
 *   5. size > ZONE_MAX_ALLOC                      ZONE_ERR_SIZE_RANGE
 *   6. no free block is large enough              ZONE_ERR_NO_BLOCK
 *
 * Only case 6 increments `failures` (once), and no case changes the byte
 * accounting. The returned pointer is ZONE_ALIGNMENT-aligned and points after
 * the block header; the caller may use exactly the requested size.
 *
 * @param state Caller-owned zone state.
 * @param size  Bytes requested; may not be zero.
 * @param tag   PU_STATIC, PU_LEVEL or PU_CACHE.
 * @param error Optional out parameter: receives ZONE_ERR_OK on success or the
 *              failure code. Untouched when NULL.
 * @return The block payload, or NULL on any failure.
 */
void* z_alloc(zone_state_t* state, size_t size, zone_tag_t tag, zone_error_t* error);

/**
 * @brief Allocates, and purges once if the first attempt found no block.
 *
 * The argument rules are exactly z_alloc()'s. When the first attempt reports
 * ZONE_ERR_NO_BLOCK, every PU_CACHE block is freed and the allocation is tried
 * exactly once more. A purge that recovers nothing still counts as the one
 * allowed retry: there is no second purge. `failures` is incremented once only
 * when the retry also fails, so one call is one failure and never two.
 *
 * @param state Caller-owned zone state.
 * @param size  Bytes requested; may not be zero.
 * @param tag   PU_STATIC, PU_LEVEL or PU_CACHE.
 * @param error Optional out parameter, as z_alloc().
 * @return The block payload, or NULL on any failure.
 */
void* z_alloc_purging(zone_state_t* state, size_t size, zone_tag_t tag,
                      zone_error_t* error);

/**
 * @brief Returns one block to the arena and coalesces its neighbours.
 *
 * The freed block is merged with an immediately following free block and then
 * with an immediately preceding one, so no two free blocks are ever adjacent.
 * A merge turns the absorbed block's header back into payload capacity and
 * subtracts it from `overhead`.
 *
 * Validation order:
 *
 *   1. state == NULL                              ZONE_ERR_NULL_STATE
 *   2. state was never accepted                   ZONE_ERR_NOT_OPEN
 *   3. pointer == NULL                            ZONE_ERR_NULL_POINTER
 *   4. pointer outside [base, base + arenaSize)   ZONE_ERR_NOT_OWNED
 *   5. pointer not ZONE_ALIGNMENT-aligned         ZONE_ERR_NOT_OWNED
 *   6. pointer does not start a block payload     ZONE_ERR_NOT_OWNED
 *   7. that block is already free                 ZONE_ERR_DOUBLE_FREE
 *
 * An invalid free is a no-op: not one arena byte and not one accounting field
 * changes, so a bug in a caller can never corrupt the zone.
 *
 * @param state   Caller-owned zone state.
 * @param pointer A pointer previously returned by z_alloc()/z_alloc_purging()
 *                from this same zone.
 * @return ZONE_ERR_OK when the block was freed, one of the codes above
 *         otherwise.
 */
zone_error_t z_free(zone_state_t* state, void* pointer);

/**
 * @brief Frees every live block carrying @p tag, coalescing as it goes.
 *
 * The walk covers the arena in block order and behaves exactly like calling
 * z_free() on each matching block. Non-matching tags are untouched.
 *
 * @param state      Caller-owned zone state.
 * @param tag        PU_STATIC, PU_LEVEL or PU_CACHE.
 * @param freedBytes Optional out parameter: receives the total payload capacity
 *                   reclaimed, or 0 on any error. Untouched when NULL.
 * @return ZONE_ERR_OK, ZONE_ERR_NULL_STATE, ZONE_ERR_NOT_OPEN or
 *         ZONE_ERR_BAD_TAG.
 */
zone_error_t z_free_tag(zone_state_t* state, zone_tag_t tag, uint32_t* freedBytes);

/**
 * @brief Frees PU_CACHE blocks only and reports the bytes it recovered.
 *
 * This is the level-load pressure valve: statics and the level working set are
 * never touched, no matter how much cache is resident. Each call that reaches
 * a valid, open state increments `stats.purges`, even when no cache block
 * exists. `freedBytes` is the total payload capacity of the blocks freed.
 *
 * @param state      Caller-owned zone state.
 * @param freedBytes Optional out parameter: bytes reclaimed, or 0 on error.
 * @return ZONE_ERR_OK, ZONE_ERR_NULL_STATE or ZONE_ERR_NOT_OPEN.
 */
zone_error_t z_purge(zone_state_t* state, uint32_t* freedBytes);

/**
 * @brief Returns one accounting snapshot by value.
 *
 * A NULL state, or a state that was never accepted, answers an all-zero
 * snapshot: there is no arena to describe. Otherwise the identity
 * `inUse + freeBytes + overhead == arenaSize` holds for the returned copy.
 *
 * @param state Zone state, open or not.
 * @return The snapshot.
 */
zone_stats_t z_stats(const zone_state_t* state);

/**
 * @brief Returns the largest single free payload, in bytes.
 *
 * Equal to `z_stats(state).largestFree`, for callers that need only the
 * fragmentation number. A smaller value than `freeBytes` is fragmentation, not
 * an error: the portfolio of holes cannot satisfy one request of the larger
 * size.
 *
 * @param state Zone state, open or not.
 * @return The largest free payload, or 0 for a NULL or unopened state.
 */
uint32_t z_largest_free(const zone_state_t* state);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* DOOM_Z_ZONE_H */
