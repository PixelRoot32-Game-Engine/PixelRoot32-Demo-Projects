/*
 * The compressed container reader: see whd.h for the layout, the validation
 * order and the public contract.
 *
 * Two properties are worth naming here because they are the reason the code
 * looks the way it does:
 *
 *  - every integer is reconstructed from its bytes in explicit little-endian
 *    order. memcpy into an integer and a cast of the buffer to a wider type both
 *    assume alignment and host byte order, and a container inside a flash
 *    mapping is not guaranteed to be aligned;
 *  - nothing escapes a failed open. The state is cleared before any validation
 *    and only filled at the very end, so a rejected container can never leave a
 *    reader that answers some lookups and not others.
 *
 * The DEFLATE decoder is a raw RFC 1951 stream reader: no zlib wrapper, no
 * allocation, and no output beyond the size the directory declared. It accepts
 * stored, fixed-Huffman and dynamic-Huffman blocks, and it holds one canonical
 * Huffman table per alphabet rather than a lookup table, so its whole working
 * set is about a kilobyte of stack.
 *
 * On top of that walk it keeps one root lookup table per alphabet, rebuilt per
 * block, which resolves every code of at most WHD_LITERAL_ROOT_BITS bits
 * (literal/length) or WHD_DISTANCE_ROOT_BITS bits (distance) in a single step;
 * a longer code falls back to the canonical walk, so the decoded bytes cannot
 * differ, only the work does. The tables are static, not stack: WHD_DEFLATE_
 * ROOT_TABLE_BYTES is their exact cost, and whd_state_t does not carry them.
 * That makes the decode path single-threaded, which whd.h states as contract.
 *
 * Compiled by the C compiler under the firmware's C99 flags; no C++ construct,
 * no allocation, no stdio, no floating point, and no mutable global other than
 * those two tables.
 */
#include "whd.h"

/* --- Little-endian field readers ----------------------------------------- */

static uint16_t readUint16(const uint8_t* at) {
    return (uint16_t)((uint16_t)at[0] | (uint16_t)((uint16_t)at[1] << 8));
}

static uint32_t readUint32(const uint8_t* at) {
    return (uint32_t)at[0]
         | ((uint32_t)at[1] << 8)
         | ((uint32_t)at[2] << 16)
         | ((uint32_t)at[3] << 24);
}

/* ASCII upper case for the name query. The 1993 format stores names upper case
 * and the port does not pull in <ctype.h>, which is locale-dependent. */
static unsigned char upperAscii(unsigned char character) {
    if (character >= (unsigned char)'a' && character <= (unsigned char)'z') {
        return (unsigned char)(character - 32u);
    }

    return character;
}

/* --- Reader state helpers ------------------------------------------------ */

/* Closes the reader and records why. Every rejection goes through here. */
static whd_error_t reject(whd_state_t* state, whd_error_t error) {
    state->base = NULL;
    state->size = 0;
    state->numLumps = 0;
    state->nameCount = 0;
    state->directoryOffset = 0;
    state->nameTableOffset = 0;
    state->dataOffset = 0;
    state->open = 0;
    state->error = error;

    return error;
}

/* Address of one validated directory entry. NULL for a closed reader or an
 * index outside the directory, which is also the guard every accessor uses. */
static const uint8_t* directoryEntry(const whd_state_t* state, int lump) {
    if (state == NULL || state->open == 0) {
        return NULL;
    }
    if (lump < 0 || (uint32_t)lump >= state->numLumps) {
        return NULL;
    }

    return state->base
         + (size_t)state->directoryOffset
         + (size_t)lump * (size_t)WHD_DIRECTORY_ENTRY_SIZE;
}

/* Compares the query against the fixed 8-byte field the way DOOM's name8
 * comparison does: the query is upper-cased, its terminator ends the query, and
 * the remaining field bytes must be the NUL padding the format promises. A
 * query longer than 8 characters is compared on its first 8. */
static int nameMatchesField(const char* name, const uint8_t* field) {
    size_t index;
    int terminated = 0;

    for (index = 0; index < (size_t)WHD_NAME_SIZE; ++index) {
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

/* --- DEFLATE (RFC 1951) -------------------------------------------------- */

/* Longest Huffman code the format allows. */
#define WHD_MAX_CODE_BITS 15u

/* The largest alphabets the format defines, plus the two reserved literal
 * codes 286 and 287 that a fixed-Huffman block still has room for. */
#define WHD_MAX_LITERAL_CODES 288
#define WHD_MAX_DISTANCE_CODES 32
#define WHD_CODE_LENGTH_CODES 19
#define WHD_MAX_CODE_LENGTHS (WHD_MAX_LITERAL_CODES + WHD_MAX_DISTANCE_CODES)

/* Returned by buildHuffman() when the code lengths describe more codes than
 * the bit space can hold. */
#define WHD_HUFFMAN_OVERSUBSCRIBED (-1)

/* A root-table entry packs the decoded symbol and the number of bits it
 * consumed into one uint16_t: the length in the low nibble, the symbol in the
 * twelve bits above it (287, the largest literal/length symbol, needs nine).
 * A zero entry means "no code of at most rootBits bits starts here", which the
 * decoder answers by falling back to the canonical walk; a real entry always
 * has a length of at least one. */
#define WHD_TABLE_ENTRY(symbol, length) \
    ((uint16_t)(((uint16_t)(symbol) << 4) | (uint16_t)(length)))
#define WHD_TABLE_LENGTH(entry) ((int)((entry) & 0x0Fu))
#define WHD_TABLE_SYMBOL(entry) ((int)((entry) >> 4))

/* The root tables, one row per alphabet. Static storage on purpose: the
 * literal table alone is a kilobyte and the stack already carries the canonical
 * tables the fallback needs, so both together would triple the decoder's stack
 * cost. The total is WHD_DEFLATE_ROOT_TABLE_BYTES (1152 bytes), stated in
 * whd.h, which also states the one thing this costs -- the decode path is not
 * reentrant. */
static uint16_t g_literalRoot[1u << WHD_LITERAL_ROOT_BITS];
static uint16_t g_distanceRoot[1u << WHD_DISTANCE_ROOT_BITS];

/* Bit reader over the payload. Bits are consumed least significant bit first,
 * as DEFLATE packs them; at most seven bits of the current byte are ever held
 * back, which is what lets a stored block resume on a byte boundary by dropping
 * the buffer and keeping the pointer. */
typedef struct whdBitReader {
    const uint8_t* at;
    const uint8_t* end;
    uint32_t buffer;
    int count;
} whdBitReader_t;

static int inflateBits(whdBitReader_t* reader, int need, uint32_t* value) {
    uint32_t accumulator = reader->buffer;

    while (reader->count < need) {
        if (reader->at >= reader->end) {
            return 0;
        }
        accumulator |= (uint32_t)(*reader->at) << reader->count;
        ++reader->at;
        reader->count += 8;
    }

    reader->buffer = accumulator >> need;
    reader->count -= need;
    *value = accumulator & (((uint32_t)1u << need) - 1u);

    return 1;
}

/* True when the reader can still produce @p need bits from the payload.
 *
 * Compared as a byte count rather than as a bit total on purpose: a stream this
 * container can address may be longer than 2^28 bytes, and a bit total for one
 * would overflow a 32-bit int. @p need - count is always below 16 here, so the
 * right-hand side cannot overflow either. */
static int hasBits(const whdBitReader_t* reader, int need) {
    if (reader->count >= need) {
        return 1;
    }

    return (size_t)(reader->end - reader->at)
        >= ((size_t)(need - reader->count) + 7u) / 8u;
}

/* The next @p need bits without consuming them. Bits past the end of the
 * payload read as zero, which is why the caller calls hasBits() first: only
 * then is the answer a real code prefix. */
static uint32_t peekBits(const whdBitReader_t* reader, int need) {
    uint32_t accumulator = reader->buffer;
    const uint8_t* at = reader->at;
    int count = reader->count;

    while (count < need && at < reader->end) {
        accumulator |= (uint32_t)(*at) << count;
        ++at;
        count += 8;
    }

    return accumulator & (((uint32_t)1u << need) - 1u);
}

/* Drops @p bits the caller has already proved available. Kept separate from
 * inflateBits() so the table path pays one refill decision instead of a loop
 * per bit; the invariant is the same -- fewer than eight bits stay buffered. */
static void dropBits(whdBitReader_t* reader, int bits) {
    while (reader->count < bits) {
        reader->buffer |= (uint32_t)(*reader->at) << reader->count;
        ++reader->at;
        reader->count += 8;
    }

    reader->buffer >>= bits;
    reader->count -= bits;
}

/*
 * Builds the canonical decoding tables for one alphabet.
 *
 * counts[length] receives how many codes have that length and symbols[] the
 * symbols ordered by code, which is all a canonical code needs: a symbol is
 * decoded by walking the code bits until the accumulated value falls inside the
 * range that length covers.
 *
 * @return 0 for a complete code, a positive number of unused codes for an
 *         incomplete one, WHD_HUFFMAN_OVERSUBSCRIBED for an impossible one.
 */
static int buildHuffman(uint16_t* counts, uint16_t* symbols, const uint8_t* lengths,
                        int count, int* maxLength) {
    int offsets[WHD_MAX_CODE_BITS + 1u];
    int symbol;
    int length;
    int left = 1;

    for (length = 0; length <= (int)WHD_MAX_CODE_BITS; ++length) {
        counts[length] = 0u;
    }
    for (symbol = 0; symbol < count; ++symbol) {
        ++counts[lengths[symbol]];
    }

    *maxLength = 0;
    for (symbol = 0; symbol < count; ++symbol) {
        if ((int)lengths[symbol] > *maxLength) {
            *maxLength = (int)lengths[symbol];
        }
    }
    if (counts[0] == (uint16_t)count) {
        return 0; /* no codes at all: nothing to decode, legal */
    }
    if (*maxLength > (int)WHD_MAX_CODE_BITS) {
        return WHD_HUFFMAN_OVERSUBSCRIBED;
    }

    for (length = 1; length <= (int)WHD_MAX_CODE_BITS; ++length) {
        left <<= 1;
        left -= (int)counts[length];
        if (left < 0) {
            return WHD_HUFFMAN_OVERSUBSCRIBED;
        }
    }

    offsets[1] = 0;
    for (length = 1; length < (int)WHD_MAX_CODE_BITS; ++length) {
        offsets[length + 1] = offsets[length] + (int)counts[length];
    }
    for (symbol = 0; symbol < count; ++symbol) {
        if (lengths[symbol] != 0u) {
            symbols[offsets[lengths[symbol]]++] = (uint16_t)symbol;
        }
    }

    return left;
}

/* Decodes one symbol of the alphabet described by counts[] and symbols[]. */
static int decodeSymbol(whdBitReader_t* reader, const uint16_t* counts,
                        const uint16_t* symbols, int* symbolOut) {
    int code = 0;
    int first = 0;
    int index = 0;
    int length;

    for (length = 1; length <= (int)WHD_MAX_CODE_BITS; ++length) {
        uint32_t bit = 0u;
        int count;

        if (inflateBits(reader, 1, &bit) == 0) {
            return 0;
        }
        code |= (int)bit;
        count = (int)counts[length];
        if (code - first < count) {
            *symbolOut = (int)symbols[index + (code - first)];
            return 1;
        }
        index += count;
        first += count;
        first <<= 1;
        code <<= 1;
    }

    return 0;
}

/*
 * Fills a root lookup table from the canonical table buildHuffman() produced.
 *
 * Every code of at most @p rootBits bits is written into every slot it can be
 * reached from: the index is the code with its bits reversed, because DEFLATE
 * transmits the most significant code bit first while the reader assembles the
 * next bits least significant first; the suffixes the code does not cover are
 * then all the slots that share it, `1 << length` apart. Codes longer than
 * @p rootBits leave their slots at zero, which is the decoder's signal to walk
 * that code instead.
 *
 * symbols[] is grouped by ascending code length and, inside a length, by
 * symbol -- the order canonical codes are assigned in -- so one pass over
 * counts[] and symbols[] visits every code exactly once.
 */
static void buildRootTable(uint16_t* table, int rootBits, const uint16_t* counts,
                           const uint16_t* symbols) {
    const int size = 1 << rootBits;
    int firstCode = 0;
    int index = 0;
    int length;
    int slot;

    for (slot = 0; slot < size; ++slot) {
        table[slot] = 0u;
    }

    for (length = 1; length <= (int)WHD_MAX_CODE_BITS; ++length) {
        const int count = (int)counts[length];
        int item;

        for (item = 0; item < count; ++item) {
            if (length <= rootBits) {
                const int code = firstCode + item;
                const uint16_t entry =
                    WHD_TABLE_ENTRY((int)symbols[index + item], length);
                int reversed = 0;
                int bit;

                for (bit = 0; bit < length; ++bit) {
                    reversed |= ((code >> bit) & 1) << (length - 1 - bit);
                }

                for (slot = reversed; slot < size; slot += 1 << length) {
                    table[slot] = entry;
                }
            }
        }

        index += count;
        firstCode = (firstCode + count) << 1;
    }
}

/* Peeks the root-table entry for the next code.
 *
 * Returns the packed entry, or zero when the payload has fewer than @p rootBits
 * bits left or when no code of at most @p rootBits bits starts here. Both are
 * the caller's signal to hand the code to decodeSymbol(), so a table hit never
 * invents bits past the end of the stream and a miss never consumes any.
 *
 * This stays small on purpose: it is inlined into the block payload loop, where
 * it is the whole cost of the common case. */
static uint16_t peekRootEntry(whdBitReader_t* reader, const uint16_t* table,
                              int rootBits) {
    if (hasBits(reader, rootBits) == 0) {
        return 0u;
    }

    return table[peekBits(reader, rootBits)];
}

/* The length and distance tables of RFC 1951 section 3.2.5. */
static const uint16_t kLengthBase[29] = {
    3u, 4u, 5u, 6u, 7u, 8u, 9u, 10u, 11u, 13u, 15u, 17u, 19u, 23u, 27u,
    31u, 35u, 43u, 51u, 59u, 67u, 83u, 99u, 115u, 131u, 163u, 195u, 227u, 258u
};
static const uint8_t kLengthExtra[29] = {
    0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 1u, 1u, 1u, 1u, 2u, 2u, 2u,
    2u, 3u, 3u, 3u, 3u, 4u, 4u, 4u, 4u, 5u, 5u, 5u, 5u, 0u
};
static const uint16_t kDistanceBase[30] = {
    1u, 2u, 3u, 4u, 5u, 7u, 9u, 13u, 17u, 25u, 33u, 49u, 65u, 97u, 129u,
    193u, 257u, 385u, 513u, 769u, 1025u, 1537u, 2049u, 3073u, 4097u, 6145u,
    8193u, 12289u, 16385u, 24577u
};
static const uint8_t kDistanceExtra[30] = {
    0u, 0u, 0u, 0u, 1u, 1u, 2u, 2u, 3u, 3u, 4u, 4u, 5u, 5u, 6u,
    6u, 7u, 7u, 8u, 8u, 9u, 9u, 10u, 10u, 11u, 11u, 12u, 12u, 13u, 13u
};

/* The code-length code lengths are transmitted in this order. */
static const uint8_t kCodeLengthOrder[WHD_CODE_LENGTH_CODES] = {
    16u, 17u, 18u, 0u, 8u, 7u, 9u, 6u, 10u, 5u,
    11u, 4u, 12u, 3u, 13u, 2u, 14u, 1u, 15u
};

/* Lengths of the fixed literal/length alphabet, RFC 1951 section 3.2.6. */
static uint8_t fixedLiteralLength(int symbol) {
    if (symbol <= 143) {
        return 8u;
    }
    if (symbol <= 255) {
        return 9u;
    }
    if (symbol <= 279) {
        return 7u;
    }

    return 8u;
}

/*
 * Decodes one raw DEFLATE stream into exactly @p outSize bytes.
 *
 * @return 1 when the stream ended on its final block having produced exactly
 *         @p outSize bytes, 0 for anything else: a malformed stream, a stream
 *         that ends early, one that would overrun the buffer, or a distance
 *         that reaches before the start of the output.
 */
static int inflateStream(const uint8_t* payload, uint32_t payloadSize,
                         uint8_t* out, uint32_t outSize) {
    whdBitReader_t reader;
    uint16_t literalCounts[WHD_MAX_CODE_BITS + 1u];
    uint16_t literalSymbols[WHD_MAX_LITERAL_CODES];
    uint16_t distanceCounts[WHD_MAX_CODE_BITS + 1u];
    uint16_t distanceSymbols[WHD_MAX_DISTANCE_CODES];
    uint16_t codeLengthCounts[WHD_MAX_CODE_BITS + 1u];
    uint16_t codeLengthSymbols[WHD_CODE_LENGTH_CODES];
    uint8_t codeLengths[WHD_CODE_LENGTH_CODES];
    uint8_t lengths[WHD_MAX_CODE_LENGTHS];
    uint32_t produced = 0u;
    int final = 0;
    int maxLength = 0;
    int left = 0;

    reader.at = payload;
    reader.end = payload + (size_t)payloadSize;
    reader.buffer = 0u;
    reader.count = 0;

    while (final == 0) {
        uint32_t blockFinal = 0u;
        uint32_t blockType = 0u;

        if (inflateBits(&reader, 1, &blockFinal) == 0) {
            return 0;
        }
        if (inflateBits(&reader, 2, &blockType) == 0) {
            return 0;
        }
        final = (int)blockFinal;

        if (blockType == 0u) {
            uint32_t length = 0u;
            uint32_t complement = 0u;
            uint32_t index;

            /* A stored block resumes on a byte boundary: only the remainder of
             * the current byte is dropped, because the reader never holds more
             * than seven bits. */
            reader.buffer = 0u;
            reader.count = 0;

            if (inflateBits(&reader, 16, &length) == 0) {
                return 0;
            }
            if (inflateBits(&reader, 16, &complement) == 0) {
                return 0;
            }
            if ((length ^ 0xFFFFu) != complement) {
                return 0;
            }
            if (length > outSize - produced) {
                return 0;
            }
            if ((size_t)(reader.end - reader.at) < (size_t)length) {
                return 0;
            }
            for (index = 0u; index < length; ++index) {
                out[produced + index] = reader.at[index];
            }
            reader.at += (size_t)length;
            produced += length;

            /* A stored block carries no symbols, so this is the whole block:
             * the literal/match loop below belongs to the Huffman blocks. */
            continue;
        } else if (blockType == 1u) {
            int symbol;

            for (symbol = 0; symbol < WHD_MAX_LITERAL_CODES; ++symbol) {
                lengths[symbol] = fixedLiteralLength(symbol);
            }
            left = buildHuffman(literalCounts, literalSymbols, lengths,
                                WHD_MAX_LITERAL_CODES, &maxLength);
            if (left != 0) {
                return 0;
            }
            buildRootTable(g_literalRoot, (int)WHD_LITERAL_ROOT_BITS,
                           literalCounts, literalSymbols);
            for (symbol = 0; symbol < WHD_MAX_DISTANCE_CODES; ++symbol) {
                lengths[symbol] = 5u;
            }
            left = buildHuffman(distanceCounts, distanceSymbols, lengths,
                                WHD_MAX_DISTANCE_CODES, &maxLength);
            if (left != 0) {
                return 0;
            }
            buildRootTable(g_distanceRoot, (int)WHD_DISTANCE_ROOT_BITS,
                           distanceCounts, distanceSymbols);
        } else if (blockType == 2u) {
            uint32_t literalCount = 0u;
            uint32_t distanceCount = 0u;
            uint32_t codeLengthCount = 0u;
            uint32_t total = 0u;
            uint32_t index;

            if (inflateBits(&reader, 5, &literalCount) == 0) {
                return 0;
            }
            literalCount += 257u;
            if (literalCount > (uint32_t)WHD_MAX_LITERAL_CODES - 2u) {
                return 0; /* 286 and 287 are reserved, never a table size */
            }
            if (inflateBits(&reader, 5, &distanceCount) == 0) {
                return 0;
            }
            distanceCount += 1u;
            if (distanceCount > (uint32_t)WHD_MAX_DISTANCE_CODES - 2u) {
                return 0;
            }
            if (inflateBits(&reader, 4, &codeLengthCount) == 0) {
                return 0;
            }
            codeLengthCount += 4u;

            for (index = 0u; index < (uint32_t)WHD_CODE_LENGTH_CODES; ++index) {
                codeLengths[index] = 0u;
            }
            for (index = 0u; index < codeLengthCount; ++index) {
                uint32_t value = 0u;
                if (inflateBits(&reader, 3, &value) == 0) {
                    return 0;
                }
                codeLengths[kCodeLengthOrder[index]] = (uint8_t)value;
            }

            left = buildHuffman(codeLengthCounts, codeLengthSymbols, codeLengths,
                                WHD_CODE_LENGTH_CODES, &maxLength);
            if (left < 0 || (left > 0 && maxLength != 1)) {
                return 0;
            }

            /* The two alphabets are transmitted as one run of code lengths, and
             * the repeat symbols are allowed to cross from the first into the
             * second, which is why this loop is per length and not per table. */
            total = literalCount + distanceCount;
            index = 0u;
            while (index < total) {
                int symbol = 0;
                uint32_t repeat = 0u;
                uint8_t value = 0u;

                if (decodeSymbol(&reader, codeLengthCounts, codeLengthSymbols,
                                 &symbol) == 0) {
                    return 0;
                }

                if (symbol < 16) {
                    lengths[index] = (uint8_t)symbol;
                    ++index;
                    continue;
                }

                if (symbol == 16) {
                    if (index == 0u) {
                        return 0; /* nothing to repeat */
                    }
                    value = lengths[index - 1u];
                    if (inflateBits(&reader, 2, &repeat) == 0) {
                        return 0;
                    }
                    repeat += 3u;
                } else if (symbol == 17) {
                    if (inflateBits(&reader, 3, &repeat) == 0) {
                        return 0;
                    }
                    repeat += 3u;
                } else {
                    if (inflateBits(&reader, 7, &repeat) == 0) {
                        return 0;
                    }
                    repeat += 11u;
                }

                if (repeat > total - index) {
                    return 0;
                }
                while (repeat > 0u) {
                    lengths[index] = value;
                    ++index;
                    --repeat;
                }
            }

            left = buildHuffman(literalCounts, literalSymbols, lengths,
                                (int)literalCount, &maxLength);
            if (left < 0 || (left > 0 && maxLength != 1)) {
                return 0;
            }
            left = buildHuffman(distanceCounts, distanceSymbols,
                                lengths + literalCount, (int)distanceCount,
                                &maxLength);
            if (left < 0 || (left > 0 && maxLength != 1)) {
                return 0;
            }
            buildRootTable(g_literalRoot, (int)WHD_LITERAL_ROOT_BITS,
                           literalCounts, literalSymbols);
            buildRootTable(g_distanceRoot, (int)WHD_DISTANCE_ROOT_BITS,
                           distanceCounts, distanceSymbols);
        } else {
            return 0; /* BTYPE = 11 is not defined */
        }

        /* One block's payload: literals and length/distance pairs, ending on
         * the end-of-block symbol. Both alphabets go through their root table
         * first; a code the table cannot resolve falls back to the walk. */
        for (;;) {
            int symbol = 0;
            uint16_t entry =
                peekRootEntry(&reader, g_literalRoot,
                              (int)WHD_LITERAL_ROOT_BITS);

            if (entry != 0u) {
                dropBits(&reader, WHD_TABLE_LENGTH(entry));
                symbol = WHD_TABLE_SYMBOL(entry);
            } else if (decodeSymbol(&reader, literalCounts, literalSymbols,
                                    &symbol) == 0) {
                return 0;
            }

            if (symbol < 256) {
                if (produced >= outSize) {
                    return 0;
                }
                out[produced] = (uint8_t)symbol;
                ++produced;
                continue;
            }
            if (symbol == 256) {
                break;
            }
            if (symbol > 285) {
                return 0; /* 286 and 287 are reserved */
            }

            {
                const uint32_t lengthIndex = (uint32_t)symbol - 257u;
                uint32_t extra = 0u;
                uint32_t length = 0u;
                uint32_t distance = 0u;
                int distanceSymbol = 0;
                uint32_t index;
                uint16_t distanceEntry;

                if (inflateBits(&reader, (int)kLengthExtra[lengthIndex], &extra) == 0) {
                    return 0;
                }
                length = (uint32_t)kLengthBase[lengthIndex] + extra;

                distanceEntry = peekRootEntry(&reader, g_distanceRoot,
                                              (int)WHD_DISTANCE_ROOT_BITS);
                if (distanceEntry != 0u) {
                    dropBits(&reader, WHD_TABLE_LENGTH(distanceEntry));
                    distanceSymbol = WHD_TABLE_SYMBOL(distanceEntry);
                } else if (decodeSymbol(&reader, distanceCounts, distanceSymbols,
                                        &distanceSymbol) == 0) {
                    return 0;
                }
                if (distanceSymbol > 29) {
                    return 0;
                }
                if (inflateBits(&reader, (int)kDistanceExtra[distanceSymbol], &extra) == 0) {
                    return 0;
                }
                distance = (uint32_t)kDistanceBase[distanceSymbol] + extra;

                if (distance > produced) {
                    return 0; /* before the start of the output */
                }
                if (length > outSize - produced) {
                    return 0; /* would overrun the caller's buffer */
                }

                /* Byte by byte on purpose: a match may overlap the bytes it is
                 * producing, which is exactly how a run is encoded. */
                for (index = 0u; index < length; ++index) {
                    out[produced] = out[produced - distance];
                    ++produced;
                }
            }
        }
    }

    return produced == outSize ? 1 : 0;
}

/* --- Public API ---------------------------------------------------------- */

whd_error_t whd_open(whd_state_t* state, const uint8_t* base, size_t size) {
    uint32_t numLumps = 0u;
    uint32_t nameCount = 0u;
    uint32_t directoryOffset = 0u;
    uint32_t nameTableOffset = 0u;
    uint32_t dataOffset = 0u;
    uint32_t totalSize = 0u;
    uint64_t previousPayloadEnd = 0u;
    uint32_t lump;

    if (state == NULL) {
        return WHD_ERR_NULL_STATE;
    }

    /* Closed and empty from the first line: no validation path below can leave
     * a half-open reader behind, because only the final assignments open it. */
    reject(state, WHD_ERR_OK);

    if (base == NULL) {
        return reject(state, WHD_ERR_NULL_BUFFER);
    }
    /* The format addresses everything with uint32_t, so a buffer larger than
     * that cannot be described by it. On a 32-bit target size_t cannot exceed
     * the limit at all, which is why the check is compiled out there instead of
     * left to warn about a comparison that can never be true. */
#if SIZE_MAX > WHD_MAX_BUFFER_SIZE
    if (size > (size_t)WHD_MAX_BUFFER_SIZE) {
        return reject(state, WHD_ERR_SIZE_RANGE);
    }
#endif
    if (size < (size_t)WHD_HEADER_SIZE) {
        return reject(state, WHD_ERR_TOO_SMALL);
    }
    if (base[0] != WHD_MAGIC_0 || base[1] != WHD_MAGIC_1
        || base[2] != WHD_MAGIC_2 || base[3] != WHD_MAGIC_3) {
        return reject(state, WHD_ERR_BAD_MAGIC);
    }
    if (readUint16(base + 4) != (uint16_t)WHD_VERSION) {
        return reject(state, WHD_ERR_VERSION);
    }
    if (readUint16(base + 6) != 0u) {
        return reject(state, WHD_ERR_FLAGS);
    }
    if (readUint32(base + 36) != 0u) {
        return reject(state, WHD_ERR_RESERVED);
    }

    /* The container states its own length, so a truncated file is caught here
     * rather than by whichever entry happens to run past the end. */
    totalSize = readUint32(base + 28);
    if ((uint64_t)totalSize != (uint64_t)size) {
        return reject(state, WHD_ERR_TOTAL_SIZE);
    }

    numLumps = readUint32(base + 8);
    nameCount = readUint32(base + 12);

    /* The three sections are contiguous and their offsets are fixed, which is
     * stricter than "they do not overlap" and leaves no room for a directory
     * that a reader and a writer could disagree about. */
    if (numLumps > (uint32_t)((size - (size_t)WHD_HEADER_SIZE)
                              / (size_t)WHD_DIRECTORY_ENTRY_SIZE)) {
        return reject(state, WHD_ERR_NUMLUMPS);
    }

    directoryOffset = readUint32(base + 16);
    if (directoryOffset != (uint32_t)WHD_HEADER_SIZE
        || (uint64_t)directoryOffset
               + (uint64_t)numLumps * (uint64_t)WHD_DIRECTORY_ENTRY_SIZE
           > (uint64_t)size) {
        return reject(state, WHD_ERR_DIRECTORY_RANGE);
    }

    nameTableOffset = readUint32(base + 20);
    if ((uint64_t)nameTableOffset
            != (uint64_t)directoryOffset
                   + (uint64_t)numLumps * (uint64_t)WHD_DIRECTORY_ENTRY_SIZE
        || (uint64_t)nameCount * (uint64_t)WHD_NAME_ENTRY_SIZE
               > (uint64_t)size - (uint64_t)nameTableOffset) {
        return reject(state, WHD_ERR_NAME_TABLE_RANGE);
    }

    dataOffset = readUint32(base + 24);
    if ((uint64_t)dataOffset
            != (uint64_t)nameTableOffset
                   + (uint64_t)nameCount * (uint64_t)WHD_NAME_ENTRY_SIZE
        || (uint64_t)dataOffset > (uint64_t)size) {
        return reject(state, WHD_ERR_DATA_RANGE);
    }

    /* Per entry: the method decides how the two sizes relate, and every payload
     * must live inside the data area in directory order. Payloads are stored
     * back to back, but a gap is not an error -- only an overlap is, because it
     * means the directory disagrees with the data area. */
    for (lump = 0u; lump < numLumps; ++lump) {
        const uint8_t* entry = base
                             + (size_t)directoryOffset
                             + (size_t)lump * (size_t)WHD_DIRECTORY_ENTRY_SIZE;
        const uint32_t payloadOffset = readUint32(entry);
        const uint32_t storedSize = readUint32(entry + 4);
        const uint32_t uncompressedSize = readUint32(entry + 8);
        const uint8_t method = entry[12];
        const uint64_t payloadStart = (uint64_t)dataOffset + (uint64_t)payloadOffset;
        const uint64_t payloadEnd = payloadStart + (uint64_t)storedSize;

        if (method != (uint8_t)WHD_METHOD_STORED
            && method != (uint8_t)WHD_METHOD_DEFLATE) {
            return reject(state, WHD_ERR_METHOD);
        }
        if (method == (uint8_t)WHD_METHOD_STORED && storedSize != uncompressedSize) {
            return reject(state, WHD_ERR_STORED_SIZE);
        }
        if (method == (uint8_t)WHD_METHOD_DEFLATE && storedSize == 0u
            && uncompressedSize != 0u) {
            return reject(state, WHD_ERR_LUMP_RANGE);
        }
        if (payloadStart < previousPayloadEnd) {
            return reject(state, WHD_ERR_LUMP_ORDER);
        }
        if (payloadEnd > (uint64_t)size) {
            return reject(state, WHD_ERR_LUMP_RANGE);
        }

        previousPayloadEnd = payloadEnd;
    }

    /* Names are a convenience for the caller, not part of the directory, so a
     * name that points nowhere is rejected here and not on lookup. */
    for (lump = 0u; lump < nameCount; ++lump) {
        const uint8_t* entry = base
                             + (size_t)nameTableOffset
                             + (size_t)lump * (size_t)WHD_NAME_ENTRY_SIZE;
        if (readUint32(entry + 8) >= numLumps) {
            return reject(state, WHD_ERR_NAME_RANGE);
        }
    }

    state->base = base;
    state->size = size;
    state->numLumps = numLumps;
    state->nameCount = nameCount;
    state->directoryOffset = directoryOffset;
    state->nameTableOffset = nameTableOffset;
    state->dataOffset = dataOffset;
    state->open = 1;
    state->error = WHD_ERR_OK;

    return WHD_ERR_OK;
}

int32_t whd_lump_count(const whd_state_t* state) {
    if (state == NULL || state->open == 0) {
        return 0;
    }

    return (int32_t)state->numLumps;
}

int32_t whd_lump_size(const whd_state_t* state, int lump) {
    const uint8_t* entry = directoryEntry(state, lump);

    if (entry == NULL) {
        return 0;
    }

    /* whd_open() accepted this entry, so the size is a plain uint32_t that the
     * container proved fits its own buffer. */
    return (int32_t)readUint32(entry + 8);
}

int32_t whd_lump_stored_size(const whd_state_t* state, int lump) {
    const uint8_t* entry = directoryEntry(state, lump);

    if (entry == NULL) {
        return 0;
    }

    return (int32_t)readUint32(entry + 4);
}

int whd_lump_index_for_name(const whd_state_t* state, const char* name) {
    uint32_t entryIndex;

    if (state == NULL || state->open == 0 || name == NULL) {
        return -1;
    }

    /* First match wins: the scan never continues past a matching entry, so a
     * later duplicate cannot shadow an earlier one. */
    for (entryIndex = 0u; entryIndex < state->nameCount; ++entryIndex) {
        const uint8_t* entry = state->base
                             + (size_t)state->nameTableOffset
                             + (size_t)entryIndex * (size_t)WHD_NAME_ENTRY_SIZE;

        if (nameMatchesField(name, entry) != 0) {
            return (int)readUint32(entry + 8);
        }
    }

    return -1;
}

whd_error_t whd_read_lump(const whd_state_t* state, int lump, uint8_t* out,
                          size_t capacity, uint32_t* requiredSize) {
    const uint8_t* entry = directoryEntry(state, lump);
    const uint8_t* payload;
    uint32_t storedSize;
    uint32_t uncompressedSize;
    uint8_t method;
    uint32_t index;

    if (state == NULL) {
        return WHD_ERR_NULL_STATE;
    }
    if (entry == NULL) {
        return WHD_ERR_LUMP_INDEX;
    }

    storedSize = readUint32(entry + 4);
    uncompressedSize = readUint32(entry + 8);
    method = entry[12];

    if (requiredSize != NULL) {
        *requiredSize = uncompressedSize;
    }

    /* The size checks run before anything is written, so a caller that guessed
     * its buffer too small still holds its own data. */
    if (out == NULL) {
        if (uncompressedSize != 0u) {
            return WHD_ERR_NULL_OUTPUT;
        }
        return WHD_ERR_OK;
    }
    if (capacity < (size_t)uncompressedSize) {
        return WHD_ERR_BUFFER_TOO_SMALL;
    }

    payload = state->base + (size_t)state->dataOffset + (size_t)readUint32(entry);

    if (method == (uint8_t)WHD_METHOD_STORED) {
        for (index = 0u; index < uncompressedSize; ++index) {
            out[index] = payload[index];
        }
        return WHD_ERR_OK;
    }

    if (uncompressedSize == 0u) {
        return WHD_ERR_OK;
    }
    if (inflateStream(payload, storedSize, out, uncompressedSize) == 0) {
        return WHD_ERR_CORRUPT_PAYLOAD;
    }

    return WHD_ERR_OK;
}
