/*
 * Derived from id Software's DOOM (linuxdoom-1.10), Copyright (C) 1993-1996
 * id Software, Inc.  Rewritten for the PixelRoot32 port, 2026.  See NOTICE and
 * LICENSE in this directory for the provenance and the terms.
 */

/*
 * The WAD directory reader: see w_wad.h for the format, the validation order
 * and the public contract.
 *
 * Two properties are worth naming here because they are the reason the code
 * looks the way it does:
 *
 *  - every integer is reconstructed from its bytes in explicit little-endian
 *    order. memcpy into an int32_t and a cast of the buffer to int32_t* both
 *    assume alignment and host byte order, and a flash directory is not
 *    guaranteed to be aligned because a WAD only guarantees the directory is
 *    somewhere inside the file;
 *  - nothing escapes a failed open. The state is cleared before any validation
 *    and only filled at the very end, so a rejected file can never leave a
 *    reader that answers some lookups and not others.
 *
 * Compiled by the C compiler under the firmware's C99 flags; no C++ construct,
 * no allocation, no stdio, no floating point, no mutable global.
 */
#include "w_wad.h"

/* Reads a signed little-endian 32-bit integer without assuming alignment or
 * host byte order. The range is widened to int64_t so the caller can compare
 * a signed field against a size_t without a signed/unsigned tangle. */
static int64_t readLittleEndianInt32(const uint8_t* at) {
    const uint32_t raw = (uint32_t)at[0]
                       | ((uint32_t)at[1] << 8)
                       | ((uint32_t)at[2] << 16)
                       | ((uint32_t)at[3] << 24);

    /* Reconstructing the sign through subtraction avoids the
     * implementation-defined conversion of a value above INT32_MAX. */
    if ((raw & 0x80000000u) != 0u) {
        return (int64_t)raw - 4294967296LL;
    }

    return (int64_t)raw;
}

/* ASCII upper case for the name query. The 1993 format stores names upper case
 * and the port does not pull in <ctype.h>, which is locale-dependent. */
static unsigned char upperAscii(unsigned char character) {
    if (character >= (unsigned char)'a' && character <= (unsigned char)'z') {
        return (unsigned char)(character - 32u);
    }

    return character;
}

static int identificationIsWad(const uint8_t* base) {
    const int isIwad = base[0] == (uint8_t)'I'
                    && base[1] == (uint8_t)'W'
                    && base[2] == (uint8_t)'A'
                    && base[3] == (uint8_t)'D';
    const int isPwad = base[0] == (uint8_t)'P'
                    && base[1] == (uint8_t)'W'
                    && base[2] == (uint8_t)'A'
                    && base[3] == (uint8_t)'D';

    return isIwad || isPwad;
}

/* Closes the reader and records why. Every rejection goes through here. */
static wad_error_t reject(wad_state_t* state, wad_error_t error) {
    state->base = NULL;
    state->size = 0;
    state->numLumps = 0;
    state->infoTableOffset = 0;
    state->open = 0;
    state->error = error;

    return error;
}

/* Address of one validated directory entry. NULL for a closed reader or an
 * index outside the directory, which is also the guard every accessor uses. */
static const uint8_t* directoryEntry(const wad_state_t* state, int lump) {
    if (state == NULL || state->open == 0) {
        return NULL;
    }
    if (lump < 0 || lump >= (int)state->numLumps) {
        return NULL;
    }

    return state->base
         + (size_t)state->infoTableOffset
         + (size_t)lump * (size_t)WAD_DIRECTORY_ENTRY_SIZE;
}

/* Compares the query against the fixed 8-byte field the way DOOM's name8
 * comparison does: the query is upper-cased, its terminator ends the query, and
 * the remaining field bytes must be the NUL padding the format promises. A
 * query longer than 8 characters is compared on its first 8. */
static int nameMatchesField(const char* name, const uint8_t* field) {
    size_t index;
    int terminated = 0;

    for (index = 0; index < (size_t)WAD_NAME_SIZE; ++index) {
        unsigned char query = 0u;

        if (terminated == 0) {
            query = (unsigned char)name[index];
            if (query == 0u) {
                terminated = 1;
            } else {
                query = upperAscii(query);
            }
        }

        if (query != field[index]) {
            return 0;
        }
    }

    return 1;
}

wad_error_t wad_open(wad_state_t* state, const uint8_t* base, size_t size) {
    int64_t numLumps;
    int64_t infoTableOffset;
    int64_t lump;

    if (state == NULL) {
        return WAD_ERR_NULL_STATE;
    }

    /* Closed and empty from the first line: no validation path below can leave
     * a half-open reader behind, because only the final assignment opens it. */
    reject(state, WAD_ERR_OK);

    if (base == NULL) {
        return reject(state, WAD_ERR_NULL_BUFFER);
    }
    if (size > (size_t)WAD_MAX_BUFFER_SIZE) {
        return reject(state, WAD_ERR_SIZE_RANGE);
    }
    if (size < (size_t)WAD_HEADER_SIZE) {
        return reject(state, WAD_ERR_TOO_SMALL);
    }
    if (identificationIsWad(base) == 0) {
        return reject(state, WAD_ERR_BAD_IDENTIFICATION);
    }

    /* numlumps first, as the format's own largest field: a negative count or a
     * directory that cannot fit the buffer is reported as a count failure, not
     * as a displaced-directory failure. */
    numLumps = readLittleEndianInt32(base + 4);
    if (numLumps < 0
        || numLumps > (int64_t)((size - (size_t)WAD_HEADER_SIZE)
                                / (size_t)WAD_DIRECTORY_ENTRY_SIZE)) {
        return reject(state, WAD_ERR_NUMLUMPS);
    }

    infoTableOffset = readLittleEndianInt32(base + 8);
    if (infoTableOffset < 0 || infoTableOffset > (int64_t)size) {
        return reject(state, WAD_ERR_INFOTABLE_RANGE);
    }
    if (infoTableOffset < (int64_t)WAD_HEADER_SIZE) {
        return reject(state, WAD_ERR_INFOTABLE_OVERLAP);
    }
    if (numLumps > (int64_t)((size - (size_t)infoTableOffset)
                             / (size_t)WAD_DIRECTORY_ENTRY_SIZE)) {
        return reject(state, WAD_ERR_INFOTABLE_RANGE);
    }

    /* Last: every lump must live inside the same buffer. The subtraction form
     * keeps the addition from overflowing, and a zero-length lump at exactly
     * `size` is accepted: it owns no bytes, and its one-past-the-end pointer is
     * legal to form and never dereferenced. */
    for (lump = 0; lump < numLumps; ++lump) {
        const uint8_t* entry = base
                             + (size_t)infoTableOffset
                             + (size_t)lump * (size_t)WAD_DIRECTORY_ENTRY_SIZE;
        const int64_t filePosition = readLittleEndianInt32(entry);
        const int64_t lumpSize = readLittleEndianInt32(entry + 4);

        if (filePosition < 0 || lumpSize < 0
            || filePosition > (int64_t)size
            || lumpSize > (int64_t)size - filePosition) {
            return reject(state, WAD_ERR_LUMP_RANGE);
        }
    }

    state->base = base;
    state->size = size;
    state->numLumps = (int32_t)numLumps;
    state->infoTableOffset = (int32_t)infoTableOffset;
    state->open = 1;
    state->error = WAD_ERR_OK;

    return WAD_ERR_OK;
}

int W_CheckNumForName(const wad_state_t* state, const char* name) {
    int lump;

    if (state == NULL || state->open == 0 || name == NULL) {
        return -1;
    }

    /* First match wins: the scan never continues past a matching entry, so a
     * later duplicate cannot shadow an earlier one. */
    for (lump = 0; lump < (int)state->numLumps; ++lump) {
        const uint8_t* entry = directoryEntry(state, lump);

        if (nameMatchesField(name, entry + 8) != 0) {
            return lump;
        }
    }

    return -1;
}

int32_t W_LumpLength(const wad_state_t* state, int lump) {
    const uint8_t* entry = directoryEntry(state, lump);

    if (entry == NULL) {
        return 0;
    }

    /* wad_open() accepted this entry, so the size is in 0..INT32_MAX. */
    return (int32_t)readLittleEndianInt32(entry + 4);
}

const uint8_t* W_LumpPointer(const wad_state_t* state, int lump) {
    const uint8_t* entry = directoryEntry(state, lump);

    if (entry == NULL) {
        return NULL;
    }

    /* wad_open() accepted this entry, so base + filepos is inside the buffer. */
    return state->base + (size_t)readLittleEndianInt32(entry);
}
