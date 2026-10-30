/*
 * Behaviour tests for the compressed container reader (lib/doom/whd.c).
 *
 * Every container here is assembled byte for byte by this file. No binary
 * fixture is committed and no copyrighted data is used: each validation rule
 * and each decode path is stated at the exact boundary it names, on a container
 * whose bytes the test itself wrote.
 *
 * Two kinds of payload appear:
 *
 *  - a STORED lump, whose bytes are the lump;
 *  - a DEFLATE lump, whose bytes are an RFC 1951 stream built here with a bit
 *    writer. Synthetic streams cover the three block types the device decoder
 *    must accept: stored (BTYPE=0), fixed Huffman (BTYPE=1) and dynamic Huffman
 *    (BTYPE=2), plus the code-length repeat symbols of the dynamic header.
 *    The dynamic stream uses the smallest legal alphabet -- one literal, one
 *    length code and the end-of-block code -- so the header logic is exercised
 *    without a fixture.
 *
 * The tests go through the public C API only. They never read the reader's
 * state struct to decide whether an open succeeded, so a reader that filled its
 * fields but rejected the container would still fail here.
 */
#include <cstddef>
#include <cstdint>
#include <vector>

#include <unity.h>

#include "whd.h"

#include "../../test_config.h"

namespace {

constexpr std::size_t kHeaderSize = 40u;
constexpr std::size_t kDirectoryEntrySize = 16u;
constexpr std::size_t kNameEntrySize = 12u;
constexpr std::size_t kNameSize = 8u;

constexpr std::uint8_t kMethodStored = 0u;
constexpr std::uint8_t kMethodDeflate = 1u;

void putLe16(std::uint8_t* at, std::uint16_t value) {
    at[0] = static_cast<std::uint8_t>(value & 0xFFu);
    at[1] = static_cast<std::uint8_t>((value >> 8) & 0xFFu);
}

void putLe32(std::uint8_t* at, std::uint32_t value) {
    at[0] = static_cast<std::uint8_t>(value & 0xFFu);
    at[1] = static_cast<std::uint8_t>((value >> 8) & 0xFFu);
    at[2] = static_cast<std::uint8_t>((value >> 16) & 0xFFu);
    at[3] = static_cast<std::uint8_t>((value >> 24) & 0xFFu);
}

std::uint32_t le32At(const std::uint8_t* at) {
    return static_cast<std::uint32_t>(at[0])
         | (static_cast<std::uint32_t>(at[1]) << 8)
         | (static_cast<std::uint32_t>(at[2]) << 16)
         | (static_cast<std::uint32_t>(at[3]) << 24);
}

/// One lump of a synthetic container: the bytes actually stored, the size they
/// decode to, and which of the two methods produced them.
struct LumpSpec {
    const char* name;
    std::vector<std::uint8_t> payload;
    std::uint32_t uncompressedSize;
    std::uint8_t method;
};

LumpSpec storedLump(const char* name, const std::vector<std::uint8_t>& bytes) {
    return LumpSpec{name, bytes, static_cast<std::uint32_t>(bytes.size()),
                    kMethodStored};
}

/// Builds a container: header, directory, name table, then the payloads in
/// declaration order. Tests that need a malformed image patch one field of the
/// returned vector afterwards; the patch sites are documented in the test that
/// uses them.
std::vector<std::uint8_t> buildContainer(const std::vector<LumpSpec>& lumps,
                                         std::uint32_t sourceCrc = 0u) {
    const std::uint32_t numLumps = static_cast<std::uint32_t>(lumps.size());
    const std::uint32_t directoryOffset = static_cast<std::uint32_t>(kHeaderSize);
    const std::uint32_t nameTableOffset =
        static_cast<std::uint32_t>(directoryOffset + lumps.size() * kDirectoryEntrySize);
    const std::uint32_t dataOffset =
        static_cast<std::uint32_t>(nameTableOffset + lumps.size() * kNameEntrySize);

    std::uint32_t totalSize = dataOffset;
    for (const LumpSpec& lump : lumps) {
        totalSize += static_cast<std::uint32_t>(lump.payload.size());
    }

    std::vector<std::uint8_t> container(totalSize, 0u);

    container[0] = static_cast<std::uint8_t>('P');
    container[1] = static_cast<std::uint8_t>('W');
    container[2] = static_cast<std::uint8_t>('H');
    container[3] = static_cast<std::uint8_t>('D');
    putLe16(&container[4], 1u);
    putLe16(&container[6], 0u);
    putLe32(&container[8], numLumps);
    putLe32(&container[12], numLumps);
    putLe32(&container[16], directoryOffset);
    putLe32(&container[20], nameTableOffset);
    putLe32(&container[24], dataOffset);
    putLe32(&container[28], totalSize);
    putLe32(&container[32], sourceCrc);
    putLe32(&container[36], 0u);

    std::uint32_t cursor = dataOffset;
    for (std::size_t index = 0; index < lumps.size(); ++index) {
        const LumpSpec& lump = lumps[index];
        std::uint8_t* entry = &container[directoryOffset + index * kDirectoryEntrySize];
        putLe32(entry, cursor - dataOffset);
        putLe32(entry + 4, static_cast<std::uint32_t>(lump.payload.size()));
        putLe32(entry + 8, lump.uncompressedSize);
        entry[12] = lump.method;
        entry[13] = 0u;
        entry[14] = 0u;
        entry[15] = 0u;

        std::uint8_t* name = &container[nameTableOffset + index * kNameEntrySize];
        std::size_t byte = 0;
        while (byte < kNameSize && lump.name[byte] != '\0') {
            name[byte] = static_cast<std::uint8_t>(lump.name[byte]);
            ++byte;
        }
        putLe32(name + 8, static_cast<std::uint32_t>(index));

        for (const std::uint8_t value : lump.payload) {
            container[cursor] = value;
            ++cursor;
        }
    }

    return container;
}

/// Lease on the mutable bytes of a built container, for the malformed cases.
std::uint8_t* directoryEntryAt(std::vector<std::uint8_t>& container, std::size_t index) {
    return container.data() + kHeaderSize + index * kDirectoryEntrySize;
}

std::uint8_t* nameEntryAt(std::vector<std::uint8_t>& container, std::size_t index) {
    return container.data() + le32At(container.data() + 20) + index * kNameEntrySize;
}

// --- RFC 1951 bit writer -----------------------------------------------------

/// Packs bits the way DEFLATE reads them: least significant bit first for
/// integer fields, most significant bit of the code first for Huffman codes.
class BitWriter {
public:
    void put(std::uint32_t value, int count) {
        for (int bit = 0; bit < count; ++bit) {
            accumulator_ |= ((value >> bit) & 1u) << used_;
            ++used_;
            if (used_ == 8) {
                bytes_.push_back(static_cast<std::uint8_t>(accumulator_));
                accumulator_ = 0u;
                used_ = 0;
            }
        }
    }

    void putCode(std::uint32_t code, int length) {
        for (int bit = length - 1; bit >= 0; --bit) {
            put((code >> bit) & 1u, 1);
        }
    }

    /// Pads the final byte with zero bits, as every DEFLATE encoder does.
    std::vector<std::uint8_t> take() {
        if (used_ != 0) {
            bytes_.push_back(static_cast<std::uint8_t>(accumulator_));
            accumulator_ = 0u;
            used_ = 0;
        }
        return bytes_;
    }

private:
    std::vector<std::uint8_t> bytes_;
    std::uint32_t accumulator_ = 0u;
    int used_ = 0;
};

/// The fixed literal/length code of RFC 1951 section 3.2.6.
void fixedLiteralCode(int symbol, std::uint32_t& code, int& length) {
    if (symbol <= 143) {
        length = 8;
        code = static_cast<std::uint32_t>(0x30 + symbol);
    } else if (symbol <= 255) {
        length = 9;
        code = static_cast<std::uint32_t>(0x190 + (symbol - 144));
    } else if (symbol <= 279) {
        length = 7;
        code = static_cast<std::uint32_t>(symbol - 256);
    } else {
        length = 8;
        code = static_cast<std::uint32_t>(0xC0 + (symbol - 280));
    }
}

const int kLengthBase[29] = {3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23,
                             27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163,
                             195, 227, 258};
const int kLengthExtra[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2,
                              3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
const int kDistanceBase[30] = {1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97,
                               129, 193, 257, 385, 513, 769, 1025, 1537, 2049,
                               3073, 4097, 6145, 8193, 12289, 16385, 24577};
const int kDistanceExtra[30] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6,
                                7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

/// Emits a fixed-Huffman block: the literals, then the matches, then the end of
/// block marker. This is the decoder's LZ77 path without a real compressor.
class FixedBlockWriter {
public:
    explicit FixedBlockWriter(bool final) {
        bits_.put(final ? 1u : 0u, 1);
        bits_.put(1u, 2);  // BTYPE = 01, fixed Huffman
    }

    void literal(std::uint8_t value) {
        std::uint32_t code = 0u;
        int length = 0;
        fixedLiteralCode(value, code, length);
        bits_.putCode(code, length);
    }

    void match(int length, int distance) {
        int index = 0;
        for (int candidate = 0; candidate < 29; ++candidate) {
            if (kLengthBase[candidate] <= length) {
                index = candidate;
            }
        }

        std::uint32_t code = 0u;
        int codeLength = 0;
        fixedLiteralCode(257 + index, code, codeLength);
        bits_.putCode(code, codeLength);
        bits_.put(static_cast<std::uint32_t>(length - kLengthBase[index]),
                  kLengthExtra[index]);

        int distanceIndex = 0;
        for (int candidate = 0; candidate < 30; ++candidate) {
            if (kDistanceBase[candidate] <= distance) {
                distanceIndex = candidate;
            }
        }
        // Fixed distance codes are five bits each, symbol value in code order.
        bits_.putCode(static_cast<std::uint32_t>(distanceIndex), 5);
        bits_.put(static_cast<std::uint32_t>(distance - kDistanceBase[distanceIndex]),
                  kDistanceExtra[distanceIndex]);
    }

    void end() {
        std::uint32_t code = 0u;
        int length = 0;
        fixedLiteralCode(256, code, length);
        bits_.putCode(code, length);
    }

    std::vector<std::uint8_t> take() { return bits_.take(); }

private:
    BitWriter bits_;
};

/// A complete DEFLATE stream of one fixed block that stores @p text as literals
/// and then repeats it with a single overlapping match: "ABCD" then a
/// length-8 distance-4 copy decodes to "ABCDABCDABCD".
std::vector<std::uint8_t> overlappingMatchStream() {
    FixedBlockWriter block(true);
    block.literal(static_cast<std::uint8_t>('A'));
    block.literal(static_cast<std::uint8_t>('B'));
    block.literal(static_cast<std::uint8_t>('C'));
    block.literal(static_cast<std::uint8_t>('D'));
    block.match(8, 4);
    block.end();
    return block.take();
}

/// Canonical Huffman codes for a length table, so a test can emit the code it
/// just described instead of hard-coding one.
std::vector<std::uint32_t> canonicalCodes(const std::vector<int>& lengths) {
    int maxLength = 0;
    for (const int length : lengths) {
        if (length > maxLength) {
            maxLength = length;
        }
    }

    std::vector<int> count(static_cast<std::size_t>(maxLength) + 1u, 0);
    for (const int length : lengths) {
        if (length > 0) {
            ++count[static_cast<std::size_t>(length)];
        }
    }

    std::vector<std::uint32_t> nextCode(static_cast<std::size_t>(maxLength) + 1u, 0u);
    std::uint32_t code = 0u;
    for (int bits = 1; bits <= maxLength; ++bits) {
        code = (code + static_cast<std::uint32_t>(count[static_cast<std::size_t>(bits) - 1u])) << 1;
        nextCode[static_cast<std::size_t>(bits)] = code;
    }

    std::vector<std::uint32_t> codes(lengths.size(), 0u);
    for (std::size_t symbol = 0; symbol < lengths.size(); ++symbol) {
        if (lengths[symbol] > 0) {
            codes[symbol] = nextCode[static_cast<std::size_t>(lengths[symbol])]++;
        }
    }

    return codes;
}

/// One dynamic-Huffman block with the smallest legal alphabet: literal 'A',
/// end of block, and one distance code. @p useRepeats selects whether the long
/// runs of zero code lengths are written one symbol at a time or with the
/// code-length repeat symbol 18, which is what a real compressor emits.
std::vector<std::uint8_t> dynamicStream(bool useRepeats) {
    // literal/length alphabet: 257 entries, only 65 ('A') and 256 (end) used,
    // both one bit long -- a complete code of two symbols.
    std::vector<int> literalLengths(257, 0);
    literalLengths[65] = 1;
    literalLengths[256] = 1;

    // distance alphabet: one entry, one bit long. An incomplete code is legal
    // here, and it is the shape a real encoder emits for a block with no match.
    std::vector<int> distanceLengths(1, 1);

    const std::vector<std::uint32_t> literalCodes = canonicalCodes(literalLengths);

    BitWriter bits;
    bits.put(1u, 1);   // BFINAL
    bits.put(2u, 2);   // BTYPE = 10, dynamic Huffman
    bits.put(0u, 5);   // HLIT  = 0 -> 257 literal/length codes
    bits.put(0u, 5);   // HDIST = 0 -> 1 distance code
    bits.put(14u, 4);  // HCLEN = 14 -> 18 code-length codes

    // RFC 1951's transmission order for the code-length code lengths.
    const int order[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};

    // Code-length alphabet: symbols 0, 1 and (in the repeat stream) 18.
    std::vector<int> codeLengthLengths(19, 0);
    codeLengthLengths[0] = useRepeats ? 2 : 1;
    codeLengthLengths[1] = useRepeats ? 2 : 1;
    if (useRepeats) {
        codeLengthLengths[18] = 1;  // 1 bit: the shortest code, so 1/2 + 1/4 + 1/4 = 1
    }
    const std::vector<std::uint32_t> codeLengthCodes = canonicalCodes(codeLengthLengths);

    for (int index = 0; index < 18; ++index) {
        bits.put(static_cast<std::uint32_t>(codeLengthLengths[static_cast<std::size_t>(order[index])]), 3);
    }

    const auto emitCodeLength = [&](int symbol, std::uint32_t extra, int extraBits) {
        bits.putCode(codeLengthCodes[static_cast<std::size_t>(symbol)],
                     codeLengthLengths[static_cast<std::size_t>(symbol)]);
        if (extraBits > 0) {
            bits.put(extra, extraBits);
        }
    };

    if (useRepeats) {
        emitCodeLength(18, 65u - 11u, 7);   // 65 zeros: symbols 0..64
        emitCodeLength(1, 0u, 0);           // symbol 65 gets length 1
        emitCodeLength(18, 127u, 7);        // 138 zeros: symbols 66..203
        emitCodeLength(18, 190u - 138u - 11u, 7);  // 52 zeros: symbols 204..255
        emitCodeLength(1, 0u, 0);           // symbol 256 gets length 1
        emitCodeLength(1, 0u, 0);           // distance symbol 0 gets length 1
    } else {
        for (int symbol = 0; symbol < 258; ++symbol) {
            const bool isLengthOne = (symbol == 65 || symbol == 256 || symbol == 257);
            emitCodeLength(isLengthOne ? 1 : 0, 0u, 0);
        }
    }

    bits.putCode(literalCodes[65], 1);   // the literal 'A'
    bits.putCode(literalCodes[256], 1);  // end of block
    return bits.take();
}

/// One stored (BTYPE=0) block. @p final selects the block's BFINAL bit, so a
/// test can put a stream of several blocks together.
std::vector<std::uint8_t> storedBlockStream(const std::vector<std::uint8_t>& bytes,
                                            bool final = true) {
    BitWriter bits;
    bits.put(final ? 1u : 0u, 1);  // BFINAL
    bits.put(0u, 2);               // BTYPE = 00, stored
    std::vector<std::uint8_t> stream = bits.take();

    const std::uint16_t length = static_cast<std::uint16_t>(bytes.size());
    const std::uint16_t complement = static_cast<std::uint16_t>(~length);
    stream.push_back(static_cast<std::uint8_t>(length & 0xFFu));
    stream.push_back(static_cast<std::uint8_t>((length >> 8) & 0xFFu));
    stream.push_back(static_cast<std::uint8_t>(complement & 0xFFu));
    stream.push_back(static_cast<std::uint8_t>((complement >> 8) & 0xFFu));
    stream.insert(stream.end(), bytes.begin(), bytes.end());

    return stream;
}

LumpSpec deflateLump(const char* name, const std::vector<std::uint8_t>& stream,
                     std::uint32_t uncompressedSize) {
    return LumpSpec{name, stream, uncompressedSize, kMethodDeflate};
}

/// One dynamic-Huffman block whose alphabets are wider than the decoder's root
/// tables, so its payload forces the bit-by-bit fallback AND the one-step path
/// inside the same block. This is the equivalence boundary of a table-driven
/// inflater: a code of at most the root width must decode in one step and a
/// longer one must decode exactly as the canonical walk always did.
///
/// The literal/length alphabet is a complete code of 258 entries whose lengths
/// are 1,2,...,9 for nine symbols and 10,10 for two more (1/2 + ... + 1/512 +
/// 2/1024 = 1). The distance alphabet is a complete code of eight entries with
/// lengths 1,2,3,4,5,6,7,7 (1/2 + ... + 1/64 + 2/128 = 1). Both are therefore
/// legal complete codes and both end past the root width.
///
/// The payload is 13 literals ('H' with a ten-bit code, then twelve 'A's with a
/// three-bit code), one length-3 match at distance 13 whose distance code is
/// seven bits long, and the end-of-block symbol.
std::vector<std::uint8_t> longCodeStream() {
    // Literal/length alphabet: 258 entries (HLIT = 1, so 286 and 287 still stay
    // reserved). 256 is the end of block, 257 is "length 3, no extra bits".
    std::vector<int> literalLengths(258, 0);
    literalLengths[256] = 1;
    literalLengths[257] = 2;
    literalLengths[65] = 3;   // 'A'
    literalLengths[66] = 4;
    literalLengths[67] = 5;
    literalLengths[68] = 6;
    literalLengths[69] = 7;
    literalLengths[70] = 8;
    literalLengths[71] = 9;   // exactly the literal root width
    literalLengths[72] = 10;  // 'H', past the root width
    literalLengths[73] = 10;

    // Distance alphabet: 8 entries (HDIST = 7). Symbol 7 is a seven-bit code,
    // past the distance root width.
    std::vector<int> distanceLengths(8, 0);
    for (int symbol = 0; symbol < 6; ++symbol) {
        distanceLengths[static_cast<std::size_t>(symbol)] = symbol + 1;
    }
    distanceLengths[6] = 7;
    distanceLengths[7] = 7;

    const std::vector<std::uint32_t> literalCodes = canonicalCodes(literalLengths);
    const std::vector<std::uint32_t> distanceCodes = canonicalCodes(distanceLengths);

    // The code-length alphabet has to name every length value that appears:
    // 0..10. Five three-bit codes and six four-bit codes are complete
    // (5/8 + 6/16 = 1), and no repeat symbol is used.
    std::vector<int> codeLengthLengths(19, 0);
    for (int symbol = 0; symbol <= 4; ++symbol) {
        codeLengthLengths[static_cast<std::size_t>(symbol)] = 3;
    }
    for (int symbol = 5; symbol <= 10; ++symbol) {
        codeLengthLengths[static_cast<std::size_t>(symbol)] = 4;
    }
    const std::vector<std::uint32_t> codeLengthCodes = canonicalCodes(codeLengthLengths);

    BitWriter bits;
    bits.put(1u, 1);    // BFINAL
    bits.put(2u, 2);    // BTYPE = 10, dynamic Huffman
    bits.put(1u, 5);    // HLIT  = 1 -> 258 literal/length codes
    bits.put(7u, 5);    // HDIST = 7 -> 8 distance codes
    bits.put(14u, 4);   // HCLEN = 14 -> 18 code-length codes (symbol 15 unused)

    const int order[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
    for (int index = 0; index < 18; ++index) {
        bits.put(static_cast<std::uint32_t>(
                     codeLengthLengths[static_cast<std::size_t>(order[index])]),
                 3);
    }

    const auto emitCodeLength = [&](int value) {
        bits.putCode(codeLengthCodes[static_cast<std::size_t>(value)],
                     codeLengthLengths[static_cast<std::size_t>(value)]);
    };

    for (const int value : literalLengths) {
        emitCodeLength(value);
    }
    for (const int value : distanceLengths) {
        emitCodeLength(value);
    }

    bits.putCode(literalCodes[72], 10);  // 'H' by its ten-bit code
    for (int count = 0; count < 12; ++count) {
        bits.putCode(literalCodes[65], 3);  // 'A' by its three-bit code
    }
    bits.putCode(literalCodes[257], 2);     // length 3, no extra bits
    bits.putCode(distanceCodes[7], 7);      // distance base 13, two extra bits
    bits.put(0u, 2);                        // extra = 0 -> distance 13
    bits.putCode(literalCodes[256], 1);     // end of block
    return bits.take();
}

}  // namespace

// The smallest complete case: one stored lump, reported by size and by name and
// read back byte for byte.
void test_minimal_container_reports_and_decodes_a_lump(void) {
    const std::vector<std::uint8_t> container =
        buildContainer({storedLump("PLAYPAL", {1u, 2u, 3u, 4u, 5u})});
    whd_state_t state;

    TEST_ASSERT_EQUAL_INT(WHD_ERR_OK, whd_open(&state, container.data(), container.size()));
    TEST_ASSERT_EQUAL_INT32(1, whd_lump_count(&state));
    TEST_ASSERT_EQUAL_INT32(5, whd_lump_size(&state, 0));
    TEST_ASSERT_EQUAL_INT32(5, whd_lump_stored_size(&state, 0));
    TEST_ASSERT_EQUAL_INT(0, whd_lump_index_for_name(&state, "PLAYPAL"));

    std::uint8_t out[8] = {0u};
    std::uint32_t required = 0u;
    TEST_ASSERT_EQUAL_INT(WHD_ERR_OK, whd_read_lump(&state, 0, out, sizeof out, &required));
    TEST_ASSERT_EQUAL_UINT32(5u, required);
    TEST_ASSERT_EQUAL_UINT8(1u, out[0]);
    TEST_ASSERT_EQUAL_UINT8(5u, out[4]);
    TEST_ASSERT_EQUAL_UINT8(0u, out[5]);
}

// A stored lump is copied, not interpreted: every byte value survives.
void test_stored_lump_bytes_are_returned_verbatim(void) {
    std::vector<std::uint8_t> bytes(256u);
    for (std::size_t index = 0; index < bytes.size(); ++index) {
        bytes[index] = static_cast<std::uint8_t>(index);
    }

    const std::vector<std::uint8_t> container = buildContainer({storedLump("TABLE", bytes)});
    whd_state_t state;
    TEST_ASSERT_EQUAL_INT(WHD_ERR_OK, whd_open(&state, container.data(), container.size()));

    std::vector<std::uint8_t> out(bytes.size(), 0u);
    TEST_ASSERT_EQUAL_INT(WHD_ERR_OK,
                          whd_read_lump(&state, 0, out.data(), out.size(), nullptr));
    TEST_ASSERT_EQUAL_UINT32(static_cast<std::uint32_t>(bytes.size()),
                             static_cast<std::uint32_t>(out.size()));
    for (std::size_t index = 0; index < bytes.size(); ++index) {
        TEST_ASSERT_EQUAL_UINT8(bytes[index], out[index]);
    }
}

// The fixed-Huffman path, including an overlapping copy: the eight bytes of the
// match are read from output the decoder is still writing.
void test_fixed_huffman_match_is_expanded(void) {
    const std::vector<std::uint8_t> stream = overlappingMatchStream();
    const std::vector<std::uint8_t> container =
        buildContainer({deflateLump("SPRITE", stream, 12u)});
    whd_state_t state;

    TEST_ASSERT_EQUAL_INT(WHD_ERR_OK, whd_open(&state, container.data(), container.size()));
    TEST_ASSERT_EQUAL_INT32(12, whd_lump_size(&state, 0));

    std::uint8_t out[12] = {0u};
    TEST_ASSERT_EQUAL_INT(WHD_ERR_OK, whd_read_lump(&state, 0, out, sizeof out, nullptr));
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>('A'), out[0]);
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>('D'), out[3]);
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>('A'), out[4]);
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>('A'), out[8]);
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>('B'), out[9]);
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>('C'), out[10]);
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>('D'), out[11]);
}

// The dynamic-Huffman header path: HLIT/HDIST/HCLEN, a code-length alphabet and
// a one-code distance table, which is legal only as the incomplete special case.
void test_dynamic_huffman_block_decodes(void) {
    const std::vector<std::uint8_t> stream = dynamicStream(false);
    const std::vector<std::uint8_t> container =
        buildContainer({deflateLump("TITLEPIC", stream, 1u)});
    whd_state_t state;

    TEST_ASSERT_EQUAL_INT(WHD_ERR_OK, whd_open(&state, container.data(), container.size()));
    std::uint8_t out[1] = {0u};
    TEST_ASSERT_EQUAL_INT(WHD_ERR_OK, whd_read_lump(&state, 0, out, sizeof out, nullptr));
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>('A'), out[0]);
}

// The same block with the long zero runs written by repeat symbol 18, which is
// what a real compressor emits for a sparsely used alphabet.
void test_code_length_repeat_symbol_decodes(void) {
    const std::vector<std::uint8_t> stream = dynamicStream(true);
    const std::vector<std::uint8_t> container =
        buildContainer({deflateLump("TITLEPIC", stream, 1u)});
    whd_state_t state;

    TEST_ASSERT_EQUAL_INT(WHD_ERR_OK, whd_open(&state, container.data(), container.size()));
    std::uint8_t out[1] = {0u};
    TEST_ASSERT_EQUAL_INT(WHD_ERR_OK, whd_read_lump(&state, 0, out, sizeof out, nullptr));
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>('A'), out[0]);
}

// Stored blocks are the uncompressed fallback of the format: a length, its
// complement, and the bytes.
void test_stored_deflate_block_decodes(void) {
    const std::vector<std::uint8_t> stream = storedBlockStream({0x10u, 0x20u, 0x30u});
    const std::vector<std::uint8_t> container =
        buildContainer({deflateLump("STORED", stream, 3u)});
    whd_state_t state;

    TEST_ASSERT_EQUAL_INT(WHD_ERR_OK, whd_open(&state, container.data(), container.size()));
    std::uint8_t out[3] = {0u};
    TEST_ASSERT_EQUAL_INT(WHD_ERR_OK, whd_read_lump(&state, 0, out, sizeof out, nullptr));
    TEST_ASSERT_EQUAL_UINT8(0x10u, out[0]);
    TEST_ASSERT_EQUAL_UINT8(0x20u, out[1]);
    TEST_ASSERT_EQUAL_UINT8(0x30u, out[2]);
}

// A stream is a sequence of blocks: the reader continues past a stored block
// that is not final until the block that carries BFINAL.
void test_multiple_blocks_in_one_stream_decode(void) {
    std::vector<std::uint8_t> blocks = storedBlockStream({0x10u, 0x20u, 0x30u}, false);
    const std::vector<std::uint8_t> tail = overlappingMatchStream();
    blocks.insert(blocks.end(), tail.begin(), tail.end());

    const std::vector<std::uint8_t> container =
        buildContainer({deflateLump("MULTI", blocks, 15u)});
    whd_state_t state;

    TEST_ASSERT_EQUAL_INT(WHD_ERR_OK, whd_open(&state, container.data(), container.size()));
    std::uint8_t out[15] = {0u};
    TEST_ASSERT_EQUAL_INT(WHD_ERR_OK, whd_read_lump(&state, 0, out, sizeof out, nullptr));
    TEST_ASSERT_EQUAL_UINT8(0x10u, out[0]);
    TEST_ASSERT_EQUAL_UINT8(0x30u, out[2]);
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>('A'), out[3]);
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>('D'), out[14]);
}

// One block that mixes both decode shapes: codes of at most the root width are
// resolved in one step and longer codes fall back to the canonical walk. The
// bytes must be exactly the bytes the walk alone produces -- 'H', twelve 'A's,
// then a three-byte copy from distance 13.
void test_codes_past_the_root_width_decode_identically(void) {
    const std::vector<std::uint8_t> stream = longCodeStream();
    const std::vector<std::uint8_t> container =
        buildContainer({deflateLump("DYNROOT", stream, 16u)});
    whd_state_t state;

    TEST_ASSERT_EQUAL_INT(WHD_ERR_OK, whd_open(&state, container.data(), container.size()));
    TEST_ASSERT_EQUAL_INT32(16, whd_lump_size(&state, 0));

    std::uint8_t out[16] = {0u};
    TEST_ASSERT_EQUAL_INT(WHD_ERR_OK, whd_read_lump(&state, 0, out, sizeof out, nullptr));

    const std::uint8_t expected[16] = {
        static_cast<std::uint8_t>('H'),
        static_cast<std::uint8_t>('A'), static_cast<std::uint8_t>('A'),
        static_cast<std::uint8_t>('A'), static_cast<std::uint8_t>('A'),
        static_cast<std::uint8_t>('A'), static_cast<std::uint8_t>('A'),
        static_cast<std::uint8_t>('A'), static_cast<std::uint8_t>('A'),
        static_cast<std::uint8_t>('A'), static_cast<std::uint8_t>('A'),
        static_cast<std::uint8_t>('A'), static_cast<std::uint8_t>('A'),
        static_cast<std::uint8_t>('H'),
        static_cast<std::uint8_t>('A'), static_cast<std::uint8_t>('A')
    };
    for (std::size_t index = 0; index < sizeof expected; ++index) {
        TEST_ASSERT_EQUAL_UINT8(expected[index], out[index]);
    }
}

// A lookup table must not manufacture bits it does not have: the same block,
// truncated inside its final code, is still a corrupt payload and not a short
// success.
void test_truncated_code_past_the_root_width_is_corrupt(void) {
    std::vector<std::uint8_t> stream = longCodeStream();
    stream.resize(stream.size() - 1u);  // the last byte always carries EOB bits

    const std::vector<std::uint8_t> container =
        buildContainer({deflateLump("TRUNC", stream, 16u)});
    whd_state_t state;

    TEST_ASSERT_EQUAL_INT(WHD_ERR_OK, whd_open(&state, container.data(), container.size()));

    std::uint8_t out[16] = {0u};
    TEST_ASSERT_EQUAL_INT(WHD_ERR_CORRUPT_PAYLOAD,
                          whd_read_lump(&state, 0, out, sizeof out, nullptr));
}

// The fixed-Huffman alphabet is nine bits deep in its middle range, so its
// boundary codes are the widest the root table resolves and its longest match
// (length 258) is the largest copy the format defines.
void test_fixed_block_boundary_codes_decode(void) {
    FixedBlockWriter block(true);
    block.literal(200u);                        // nine-bit fixed code
    block.literal(255u);                        // nine-bit fixed code
    block.literal(static_cast<std::uint8_t>('A'));  // eight-bit fixed code
    block.match(258, 3);                        // length 258, five-bit distance
    block.end();
    const std::vector<std::uint8_t> stream = block.take();

    const std::vector<std::uint8_t> container =
        buildContainer({deflateLump("FIXEDGE", stream, 261u)});
    whd_state_t state;

    TEST_ASSERT_EQUAL_INT(WHD_ERR_OK, whd_open(&state, container.data(), container.size()));

    std::uint8_t out[261] = {0u};
    TEST_ASSERT_EQUAL_INT(WHD_ERR_OK, whd_read_lump(&state, 0, out, sizeof out, nullptr));

    // A distance-3 copy repeats the three literals, so every byte is decided by
    // its position modulo three.
    TEST_ASSERT_EQUAL_UINT8(200u, out[0]);
    TEST_ASSERT_EQUAL_UINT8(255u, out[1]);
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>('A'), out[2]);
    TEST_ASSERT_EQUAL_UINT8(200u, out[3]);
    TEST_ASSERT_EQUAL_UINT8(255u, out[4]);
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>('A'), out[260]);
}

// The tables are a fixed, documented cost held outside the stack, so the header
// states the exact number of bytes rather than leaving it to the build.
void test_root_tables_have_the_documented_footprint(void) {
    TEST_ASSERT_EQUAL_UINT32(9u, WHD_LITERAL_ROOT_BITS);
    TEST_ASSERT_EQUAL_UINT32(6u, WHD_DISTANCE_ROOT_BITS);
    TEST_ASSERT_EQUAL_UINT32(
        ((1u << WHD_LITERAL_ROOT_BITS) + (1u << WHD_DISTANCE_ROOT_BITS)) * 2u,
        WHD_DEFLATE_ROOT_TABLE_BYTES);
    TEST_ASSERT_EQUAL_UINT32(1152u, WHD_DEFLATE_ROOT_TABLE_BYTES);
}

// An empty lump owns no payload and needs no output buffer.
void test_empty_lump_needs_no_buffer(void) {    const std::vector<std::uint8_t> container =
        buildContainer({storedLump("EMPTY", {}), storedLump("AFTER", {7u})});
    whd_state_t state;

    TEST_ASSERT_EQUAL_INT(WHD_ERR_OK, whd_open(&state, container.data(), container.size()));
    TEST_ASSERT_EQUAL_INT32(0, whd_lump_size(&state, 0));
    TEST_ASSERT_EQUAL_INT32(0, whd_lump_stored_size(&state, 0));

    std::uint32_t required = 99u;
    TEST_ASSERT_EQUAL_INT(WHD_ERR_OK, whd_read_lump(&state, 0, nullptr, 0u, &required));
    TEST_ASSERT_EQUAL_UINT32(0u, required);
    TEST_ASSERT_EQUAL_INT32(1, whd_lump_size(&state, 1));
}

void test_name_lookup_is_case_insensitive(void) {
    const std::vector<std::uint8_t> container = buildContainer(
        {storedLump("MAP01", {1u}), storedLump("THINGS", {2u})});
    whd_state_t state;

    TEST_ASSERT_EQUAL_INT(WHD_ERR_OK, whd_open(&state, container.data(), container.size()));
    TEST_ASSERT_EQUAL_INT(0, whd_lump_index_for_name(&state, "MAP01"));
    TEST_ASSERT_EQUAL_INT(0, whd_lump_index_for_name(&state, "map01"));
    TEST_ASSERT_EQUAL_INT(1, whd_lump_index_for_name(&state, "things"));
}

void test_missing_name_returns_minus_one(void) {
    const std::vector<std::uint8_t> container = buildContainer({storedLump("MAP01", {1u})});
    whd_state_t state;

    TEST_ASSERT_EQUAL_INT(WHD_ERR_OK, whd_open(&state, container.data(), container.size()));
    TEST_ASSERT_EQUAL_INT(-1, whd_lump_index_for_name(&state, "MAP02"));
    TEST_ASSERT_EQUAL_INT(-1, whd_lump_index_for_name(&state, "MAP0"));
    TEST_ASSERT_EQUAL_INT(-1, whd_lump_index_for_name(&state, ""));
}

void test_bad_magic_is_rejected(void) {
    std::vector<std::uint8_t> container = buildContainer({storedLump("LUMP", {1u})});
    container[0] = static_cast<std::uint8_t>('I');  // "IWHD", the upstream magic
    container[1] = static_cast<std::uint8_t>('W');
    whd_state_t state;

    TEST_ASSERT_EQUAL_INT(WHD_ERR_BAD_MAGIC, whd_open(&state, container.data(), container.size()));
}

void test_too_small_buffer_is_rejected(void) {
    const std::uint8_t bytes[kHeaderSize - 1u] = {0u};
    whd_state_t state;

    TEST_ASSERT_EQUAL_INT(WHD_ERR_TOO_SMALL, whd_open(&state, bytes, sizeof bytes));
}

// A container cut short is caught by the size it declares for itself, before
// any directory entry is trusted.
void test_truncated_container_is_rejected(void) {
    const std::vector<std::uint8_t> container =
        buildContainer({storedLump("LUMP", {1u, 2u, 3u, 4u})});
    whd_state_t state;

    TEST_ASSERT_EQUAL_INT(WHD_ERR_TOTAL_SIZE,
                          whd_open(&state, container.data(), container.size() - 1u));
}

void test_declared_total_size_mismatch_is_rejected(void) {
    std::vector<std::uint8_t> container = buildContainer({storedLump("LUMP", {1u})});
    putLe32(&container[28], static_cast<std::uint32_t>(container.size() + 4u));
    whd_state_t state;

    TEST_ASSERT_EQUAL_INT(WHD_ERR_TOTAL_SIZE, whd_open(&state, container.data(), container.size()));
}

void test_unsupported_version_is_rejected(void) {
    std::vector<std::uint8_t> container = buildContainer({storedLump("LUMP", {1u})});
    putLe16(&container[4], 2u);
    whd_state_t state;

    TEST_ASSERT_EQUAL_INT(WHD_ERR_VERSION, whd_open(&state, container.data(), container.size()));
}

void test_nonzero_reserved_field_is_rejected(void) {
    std::vector<std::uint8_t> container = buildContainer({storedLump("LUMP", {1u})});
    putLe32(&container[36], 1u);
    whd_state_t state;

    TEST_ASSERT_EQUAL_INT(WHD_ERR_RESERVED, whd_open(&state, container.data(), container.size()));
}

// The directory lives at a fixed offset, so a container that moves it is not
// this format at all.
void test_directory_offset_mismatch_is_rejected(void) {
    std::vector<std::uint8_t> container = buildContainer({storedLump("LUMP", {1u})});
    putLe32(&container[16], static_cast<std::uint32_t>(kHeaderSize + 4u));
    whd_state_t state;

    TEST_ASSERT_EQUAL_INT(WHD_ERR_DIRECTORY_RANGE,
                          whd_open(&state, container.data(), container.size()));
}

// The name table follows the directory; pointing it past the end is a range
// failure, and it is reported as one.
void test_name_table_out_of_range_is_rejected(void) {
    std::vector<std::uint8_t> container = buildContainer({storedLump("LUMP", {1u})});
    putLe32(&container[20], static_cast<std::uint32_t>(container.size() - 2u));
    whd_state_t state;

    TEST_ASSERT_EQUAL_INT(WHD_ERR_NAME_TABLE_RANGE,
                          whd_open(&state, container.data(), container.size()));
}

// A count that cannot fit the buffer is a count failure, not a displaced
// directory failure.
void test_huge_numlumps_is_rejected(void) {
    std::vector<std::uint8_t> container = buildContainer({storedLump("LUMP", {1u})});
    putLe32(&container[8], 0x7FFFFFFFu);
    whd_state_t state;

    TEST_ASSERT_EQUAL_INT(WHD_ERR_NUMLUMPS, whd_open(&state, container.data(), container.size()));
}

// A payload that runs past the data area is rejected at open, not at read time.
void test_lump_payload_past_data_area_is_rejected(void) {
    std::vector<std::uint8_t> container = buildContainer({storedLump("LUMP", {1u, 2u})});
    // Both sizes move together so this is a range failure and not a stored-size
    // disagreement: the entry now claims three bytes where one exists.
    putLe32(directoryEntryAt(container, 0) + 4, 3u);
    putLe32(directoryEntryAt(container, 0) + 8, 3u);
    whd_state_t state;

    TEST_ASSERT_EQUAL_INT(WHD_ERR_LUMP_RANGE, whd_open(&state, container.data(), container.size()));
}

// Payloads are stored back to back: a second entry that starts inside the first
// one is a malformed directory.
void test_overlapping_lump_payloads_are_rejected(void) {
    std::vector<std::uint8_t> container =
        buildContainer({storedLump("ONE", {1u, 2u}), storedLump("TWO", {3u})});
    putLe32(directoryEntryAt(container, 1), 0u);
    whd_state_t state;

    TEST_ASSERT_EQUAL_INT(WHD_ERR_LUMP_ORDER, whd_open(&state, container.data(), container.size()));
}

void test_unknown_method_is_rejected(void) {
    std::vector<std::uint8_t> container = buildContainer({storedLump("LUMP", {1u})});
    directoryEntryAt(container, 0)[12] = 2u;
    whd_state_t state;

    TEST_ASSERT_EQUAL_INT(WHD_ERR_METHOD, whd_open(&state, container.data(), container.size()));
}

// A stored lump carries no framing of its own, so its two sizes must agree.
void test_stored_method_size_mismatch_is_rejected(void) {
    std::vector<std::uint8_t> container = buildContainer({storedLump("LUMP", {1u, 2u})});
    putLe32(directoryEntryAt(container, 0) + 8, 1u);
    whd_state_t state;

    TEST_ASSERT_EQUAL_INT(WHD_ERR_STORED_SIZE,
                          whd_open(&state, container.data(), container.size()));
}

// A name entry that points outside the directory cannot be answered.
void test_name_index_out_of_range_is_rejected(void) {
    std::vector<std::uint8_t> container = buildContainer({storedLump("LUMP", {1u})});
    putLe32(nameEntryAt(container, 0) + 8, 7u);
    whd_state_t state;

    TEST_ASSERT_EQUAL_INT(WHD_ERR_NAME_RANGE, whd_open(&state, container.data(), container.size()));
}

// The format addresses everything with 32-bit fields, so a buffer larger than
// that cannot be described. On a 32-bit target size_t cannot reach the case, so
// the check is compiled out there and this test only exists on a wider host.
#if SIZE_MAX > UINT32_MAX
void test_size_beyond_uint32_is_rejected(void) {
    const std::vector<std::uint8_t> container = buildContainer({storedLump("LUMP", {1u})});
    whd_state_t state;

    TEST_ASSERT_EQUAL_INT(
        WHD_ERR_SIZE_RANGE,
        whd_open(&state, container.data(),
                 static_cast<std::size_t>(WHD_MAX_BUFFER_SIZE) + 1u));
    TEST_ASSERT_EQUAL_INT(-1, whd_lump_index_for_name(&state, "LUMP"));
}
#endif

void test_null_arguments_are_rejected(void) {
    const std::vector<std::uint8_t> container = buildContainer({storedLump("LUMP", {1u})});
    whd_state_t state;

    TEST_ASSERT_EQUAL_INT(WHD_ERR_NULL_STATE, whd_open(nullptr, container.data(), container.size()));
    TEST_ASSERT_EQUAL_INT(WHD_ERR_NULL_BUFFER, whd_open(&state, nullptr, container.size()));
    TEST_ASSERT_EQUAL_INT(-1, whd_lump_index_for_name(nullptr, "LUMP"));
    TEST_ASSERT_EQUAL_INT32(0, whd_lump_size(nullptr, 0));
    TEST_ASSERT_EQUAL_INT(WHD_ERR_NULL_STATE, whd_read_lump(nullptr, 0, nullptr, 0u, nullptr));
}

// Out-of-range indices answer the documented neutral values on a reader that is
// open and has real lumps.
void test_invalid_lump_index_answers_neutral(void) {
    const std::vector<std::uint8_t> container =
        buildContainer({storedLump("ONE", {1u}), storedLump("TWO", {2u, 3u})});
    whd_state_t state;

    TEST_ASSERT_EQUAL_INT(WHD_ERR_OK, whd_open(&state, container.data(), container.size()));
    TEST_ASSERT_EQUAL_INT32(0, whd_lump_size(&state, -1));
    TEST_ASSERT_EQUAL_INT32(0, whd_lump_size(&state, 2));
    TEST_ASSERT_EQUAL_INT32(0, whd_lump_stored_size(&state, 2));
    TEST_ASSERT_EQUAL_INT(WHD_ERR_LUMP_INDEX, whd_read_lump(&state, -1, nullptr, 0u, nullptr));
    TEST_ASSERT_EQUAL_INT(WHD_ERR_LUMP_INDEX, whd_read_lump(&state, 2, nullptr, 0u, nullptr));
}

// The buffer passed in is never written when it is too small, and the required
// size is reported so the caller can size one and come back.
void test_buffer_too_small_reports_required_size_and_leaves_output_untouched(void) {
    const std::vector<std::uint8_t> stream = overlappingMatchStream();
    const std::vector<std::uint8_t> container =
        buildContainer({deflateLump("SPRITE", stream, 12u)});
    whd_state_t state;

    TEST_ASSERT_EQUAL_INT(WHD_ERR_OK, whd_open(&state, container.data(), container.size()));

    std::uint8_t out[11];
    for (std::uint8_t& byte : out) {
        byte = 0xAAu;
    }

    std::uint32_t required = 0u;
    TEST_ASSERT_EQUAL_INT(WHD_ERR_BUFFER_TOO_SMALL,
                          whd_read_lump(&state, 0, out, sizeof out, &required));
    TEST_ASSERT_EQUAL_UINT32(12u, required);
    for (const std::uint8_t byte : out) {
        TEST_ASSERT_EQUAL_UINT8(0xAAu, byte);
    }
}

// Garbage where a stream should be is a payload failure, and it is distinct from
// every directory failure above.
void test_corrupt_payload_is_reported(void) {
    const std::vector<std::uint8_t> container =
        buildContainer({deflateLump("BROKEN", {0xFFu, 0xFFu, 0x00u}, 8u)});
    whd_state_t state;

    TEST_ASSERT_EQUAL_INT(WHD_ERR_OK, whd_open(&state, container.data(), container.size()));

    std::uint8_t out[8] = {0u};
    TEST_ASSERT_EQUAL_INT(WHD_ERR_CORRUPT_PAYLOAD,
                          whd_read_lump(&state, 0, out, sizeof out, nullptr));
}

// A stream that decodes to fewer bytes than the directory declares is corrupt:
// the declared size is what a caller sized its buffer from.
void test_uncompressed_size_mismatch_is_reported(void) {
    const std::vector<std::uint8_t> stream = overlappingMatchStream();  // 12 bytes
    const std::vector<std::uint8_t> container =
        buildContainer({deflateLump("SHORT", stream, 16u)});
    whd_state_t state;

    TEST_ASSERT_EQUAL_INT(WHD_ERR_OK, whd_open(&state, container.data(), container.size()));

    std::uint8_t out[16] = {0u};
    TEST_ASSERT_EQUAL_INT(WHD_ERR_CORRUPT_PAYLOAD,
                          whd_read_lump(&state, 0, out, sizeof out, nullptr));
}

// A rejected open must not leave the previous reader half usable.
void test_failed_open_clears_previous_reader(void) {
    const std::vector<std::uint8_t> good = buildContainer({storedLump("LUMP", {1u, 2u, 3u})});
    std::vector<std::uint8_t> bad = buildContainer({storedLump("LUMP", {1u})});
    putLe32(&bad[28], 0u);

    whd_state_t state;
    TEST_ASSERT_EQUAL_INT(WHD_ERR_OK, whd_open(&state, good.data(), good.size()));
    TEST_ASSERT_EQUAL_INT(0, whd_lump_index_for_name(&state, "LUMP"));

    TEST_ASSERT_EQUAL_INT(WHD_ERR_TOTAL_SIZE, whd_open(&state, bad.data(), bad.size()));
    TEST_ASSERT_EQUAL_INT(-1, whd_lump_index_for_name(&state, "LUMP"));
    TEST_ASSERT_EQUAL_INT32(0, whd_lump_size(&state, 0));

    std::uint8_t out[3] = {0u};
    TEST_ASSERT_EQUAL_INT(WHD_ERR_LUMP_INDEX, whd_read_lump(&state, 0, out, sizeof out, nullptr));

    // And a good container opens again afterwards: a rejection is not sticky.
    TEST_ASSERT_EQUAL_INT(WHD_ERR_OK, whd_open(&state, good.data(), good.size()));
    TEST_ASSERT_EQUAL_INT(WHD_ERR_OK, whd_read_lump(&state, 0, out, sizeof out, nullptr));
    TEST_ASSERT_EQUAL_UINT8(3u, out[2]);
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;

    test_setup();
    UNITY_BEGIN();

    RUN_TEST(test_minimal_container_reports_and_decodes_a_lump);
    RUN_TEST(test_stored_lump_bytes_are_returned_verbatim);
    RUN_TEST(test_fixed_huffman_match_is_expanded);
    RUN_TEST(test_dynamic_huffman_block_decodes);
    RUN_TEST(test_code_length_repeat_symbol_decodes);
    RUN_TEST(test_stored_deflate_block_decodes);
    RUN_TEST(test_multiple_blocks_in_one_stream_decode);
    RUN_TEST(test_codes_past_the_root_width_decode_identically);
    RUN_TEST(test_truncated_code_past_the_root_width_is_corrupt);
    RUN_TEST(test_fixed_block_boundary_codes_decode);
    RUN_TEST(test_root_tables_have_the_documented_footprint);
    RUN_TEST(test_empty_lump_needs_no_buffer);
    RUN_TEST(test_name_lookup_is_case_insensitive);
    RUN_TEST(test_missing_name_returns_minus_one);
    RUN_TEST(test_bad_magic_is_rejected);
    RUN_TEST(test_too_small_buffer_is_rejected);
    RUN_TEST(test_truncated_container_is_rejected);
    RUN_TEST(test_declared_total_size_mismatch_is_rejected);
    RUN_TEST(test_unsupported_version_is_rejected);
    RUN_TEST(test_nonzero_reserved_field_is_rejected);
    RUN_TEST(test_directory_offset_mismatch_is_rejected);
    RUN_TEST(test_name_table_out_of_range_is_rejected);
    RUN_TEST(test_huge_numlumps_is_rejected);
    RUN_TEST(test_lump_payload_past_data_area_is_rejected);
    RUN_TEST(test_overlapping_lump_payloads_are_rejected);
    RUN_TEST(test_unknown_method_is_rejected);
    RUN_TEST(test_stored_method_size_mismatch_is_rejected);
    RUN_TEST(test_name_index_out_of_range_is_rejected);
#if SIZE_MAX > UINT32_MAX
    RUN_TEST(test_size_beyond_uint32_is_rejected);
#endif
    RUN_TEST(test_null_arguments_are_rejected);
    RUN_TEST(test_invalid_lump_index_answers_neutral);
    RUN_TEST(test_buffer_too_small_reports_required_size_and_leaves_output_untouched);
    RUN_TEST(test_corrupt_payload_is_reported);
    RUN_TEST(test_uncompressed_size_mismatch_is_reported);
    RUN_TEST(test_failed_open_clears_previous_reader);

    const int result = UNITY_END();
    test_teardown();
    return result;
}
