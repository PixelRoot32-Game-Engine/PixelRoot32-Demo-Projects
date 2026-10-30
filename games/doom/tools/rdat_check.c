/*
 * Host harness for the real WAD (D2.4): decodes patches, composite textures and
 * flats through lib/doom/r_data.c and prints one SHA-256 per item.
 *
 * This is a desktop tool, not firmware: it reads the WAD into malloc'd memory
 * and prints through stdio, neither of which the decoders do. It exists so the
 * C decoder and tools/doom_ref.py can be compared on the real 1993 data, and
 * tools/rdat_check.py is what compiles it, runs both sides and diffs them.
 *
 * HASH PROTOCOL (identical to doom_ref.py; stated in both so neither has to be
 * read to compare them)
 *
 *   PATCH    for every column: uint16 little-endian length of the normalised
 *            post stream that r_patch_column() returns, then the stream.
 *   TEXTURE  the composed pixel surface (width * height, column-major) followed
 *            by the mask plane of the same length.
 *   FLAT     the 4096 bytes.
 *
 * Output lines:
 *
 *   WAD <IWAD|PWAD> <lumps>
 *   PATCH <name> <sha256>
 *   PATCH-INVALID <name> <r_error>
 *   TEXTURE <name> <sha256>
 *   TEXTURE-UNRESOLVED <name>
 *   TEXTURE-INVALID <name> <r_error>
 *   FLAT <name> <sha256>
 *   FLAT-SKIP <name> <bytes>
 *
 * Names come from the WAD directory; no decoded byte is printed. The tool exits
 * 0 when the WAD opened and 1 when it did not, so a caller can tell a clean run
 * from a broken input. Per-item failures are reported as lines, not as the exit
 * code, because comparing them is the caller's job.
 *
 * Build:
 *   gcc -std=gnu99 -O2 -Wall -Wextra -Ilib/doom -o rdat_check \
 *       tools/rdat_check.c lib/doom/r_data.c lib/doom/w_wad.c
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "r_data.h"
#include "w_wad.h"

/* --- SHA-256 (FIPS 180-4) ------------------------------------------------ */

typedef struct sha256 {
    uint32_t state[8];
    uint64_t bits;
    uint8_t buffer[64];
    size_t used;
} sha256_t;

static const uint32_t kRound[64] = {
    0x428A2F98ul, 0x71374491ul, 0xB5C0FBCFul, 0xE9B5DBA5ul,
    0x3956C25Bul, 0x59F111F1ul, 0x923F82A4ul, 0xAB1C5ED5ul,
    0xD807AA98ul, 0x12835B01ul, 0x243185BEul, 0x550C7DC3ul,
    0x72BE5D74ul, 0x80DEB1FEul, 0x9BDC06A7ul, 0xC19BF174ul,
    0xE49B69C1ul, 0xEFBE4786ul, 0x0FC19DC6ul, 0x240CA1CCul,
    0x2DE92C6Ful, 0x4A7484AAul, 0x5CB0A9DCul, 0x76F988DAul,
    0x983E5152ul, 0xA831C66Dul, 0xB00327C8ul, 0xBF597FC7ul,
    0xC6E00BF3ul, 0xD5A79147ul, 0x06CA6351ul, 0x14292967ul,
    0x27B70A85ul, 0x2E1B2138ul, 0x4D2C6DFCul, 0x53380D13ul,
    0x650A7354ul, 0x766A0ABBul, 0x81C2C92Eul, 0x92722C85ul,
    0xA2BFE8A1ul, 0xA81A664Bul, 0xC24B8B70ul, 0xC76C51A3ul,
    0xD192E819ul, 0xD6990624ul, 0xF40E3585ul, 0x106AA070ul,
    0x19A4C116ul, 0x1E376C08ul, 0x2748774Cul, 0x34B0BCB5ul,
    0x391C0CB3ul, 0x4ED8AA4Aul, 0x5B9CCA4Ful, 0x682E6FF3ul,
    0x748F82EEul, 0x78A5636Ful, 0x84C87814ul, 0x8CC70208ul,
    0x90BEFFFAul, 0xA4506CEBul, 0xBEF9A3F7ul, 0xC67178F2ul
};

static uint32_t rotateRight(uint32_t value, unsigned int count) {
    return (value >> count) | (value << (32u - count));
}

static void sha256Block(sha256_t* context, const uint8_t* block) {
    uint32_t words[64];
    uint32_t a;
    uint32_t b;
    uint32_t c;
    uint32_t d;
    uint32_t e;
    uint32_t f;
    uint32_t g;
    uint32_t h;
    unsigned int index;

    for (index = 0; index < 16u; ++index) {
        words[index] = ((uint32_t)block[index * 4u] << 24)
                     | ((uint32_t)block[index * 4u + 1u] << 16)
                     | ((uint32_t)block[index * 4u + 2u] << 8)
                     | (uint32_t)block[index * 4u + 3u];
    }
    for (index = 16u; index < 64u; ++index) {
        const uint32_t s0 = rotateRight(words[index - 15u], 7u)
                          ^ rotateRight(words[index - 15u], 18u)
                          ^ (words[index - 15u] >> 3);
        const uint32_t s1 = rotateRight(words[index - 2u], 17u)
                          ^ rotateRight(words[index - 2u], 19u)
                          ^ (words[index - 2u] >> 10);
        words[index] = words[index - 16u] + s0 + words[index - 7u] + s1;
    }

    a = context->state[0];
    b = context->state[1];
    c = context->state[2];
    d = context->state[3];
    e = context->state[4];
    f = context->state[5];
    g = context->state[6];
    h = context->state[7];

    for (index = 0; index < 64u; ++index) {
        const uint32_t s1 = rotateRight(e, 6u) ^ rotateRight(e, 11u) ^ rotateRight(e, 25u);
        const uint32_t choose = (e & f) ^ (~e & g);
        const uint32_t temp1 = h + s1 + choose + kRound[index] + words[index];
        const uint32_t s0 = rotateRight(a, 2u) ^ rotateRight(a, 13u) ^ rotateRight(a, 22u);
        const uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
        const uint32_t temp2 = s0 + majority;

        h = g;
        g = f;
        f = e;
        e = d + temp1;
        d = c;
        c = b;
        b = a;
        a = temp1 + temp2;
    }

    context->state[0] += a;
    context->state[1] += b;
    context->state[2] += c;
    context->state[3] += d;
    context->state[4] += e;
    context->state[5] += f;
    context->state[6] += g;
    context->state[7] += h;
}

static void sha256Init(sha256_t* context) {
    context->state[0] = 0x6A09E667ul;
    context->state[1] = 0xBB67AE85ul;
    context->state[2] = 0x3C6EF372ul;
    context->state[3] = 0xA54FF53Aul;
    context->state[4] = 0x510E527Ful;
    context->state[5] = 0x9B05688Cul;
    context->state[6] = 0x1F83D9ABul;
    context->state[7] = 0x5BE0CD19ul;
    context->bits = 0u;
    context->used = 0u;
}

static void sha256Update(sha256_t* context, const void* data, size_t size) {
    const uint8_t* bytes = (const uint8_t*)data;
    size_t index = 0u;

    context->bits += (uint64_t)size * 8u;

    while (index < size) {
        const size_t room = 64u - context->used;
        const size_t take = (size - index) < room ? (size - index) : room;

        memcpy(context->buffer + context->used, bytes + index, take);
        context->used += take;
        index += take;

        if (context->used == 64u) {
            sha256Block(context, context->buffer);
            context->used = 0u;
        }
    }
}

static void sha256Final(sha256_t* context, uint8_t out[32]) {
    const uint64_t bits = context->bits;
    static const uint8_t pad = 0x80u;
    uint8_t zero = 0u;
    uint8_t length[8];
    unsigned int index;

    sha256Update(context, &pad, 1u);
    while (context->used != 56u) {
        sha256Update(context, &zero, 1u);
    }
    for (index = 0; index < 8u; ++index) {
        length[7u - index] = (uint8_t)((bits >> (index * 8u)) & 0xFFu);
    }
    sha256Update(context, length, sizeof length);

    for (index = 0; index < 8u; ++index) {
        out[index * 4u] = (uint8_t)((context->state[index] >> 24) & 0xFFu);
        out[index * 4u + 1u] = (uint8_t)((context->state[index] >> 16) & 0xFFu);
        out[index * 4u + 2u] = (uint8_t)((context->state[index] >> 8) & 0xFFu);
        out[index * 4u + 3u] = (uint8_t)(context->state[index] & 0xFFu);
    }
}

static void digestToHex(const uint8_t digest[32], char hex[65]) {
    static const char digits[] = "0123456789abcdef";
    unsigned int index;

    for (index = 0; index < 32u; ++index) {
        hex[index * 2u] = digits[(digest[index] >> 4) & 0x0Fu];
        hex[index * 2u + 1u] = digits[digest[index] & 0x0Fu];
    }
    hex[64] = '\0';
}

static void* xmalloc(size_t size) {
    void* block = malloc(size == 0u ? 1u : size);

    if (block == NULL) {
        fprintf(stderr, "rdat_check: out of memory (%lu bytes)\n", (unsigned long)size);
        exit(1);
    }

    return block;
}

/* Three published vectors plus the million-'a' one, so a broken round function,
 * a broken padding rule or a broken 64-bit length would be caught before any
 * comparison is trusted. */
static int sha256SelfTest(void) {
    static const char* vectors[] = {
        "",
        "abc",
        "The quick brown fox jumps over the lazy dog",
    };
    static const char* expected[] = {
        "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
        "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
        "d7a8fbb307d7809469ca9abcb0082e4f8d5651e46d3cdb762d02d0bf37c9e592",
    };
    static const char millionExpected[] =
        "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0";
    char hex[65];
    unsigned int index;
    sha256_t context;
    uint8_t digest[32];

    for (index = 0; index < 3u; ++index) {
        sha256_t one;
        sha256Init(&one);
        sha256Update(&one, vectors[index], strlen(vectors[index]));
        sha256Final(&one, digest);
        digestToHex(digest, hex);
        if (strcmp(hex, expected[index]) != 0) {
            return 0;
        }
    }

    sha256Init(&context);
    {
        uint8_t chunk[1000];
        memset(chunk, 'a', sizeof chunk);
        for (index = 0; index < 1000u; ++index) {
            sha256Update(&context, chunk, sizeof chunk);
        }
    }
    sha256Final(&context, digest);
    digestToHex(digest, hex);

    return strcmp(hex, millionExpected) == 0;
}

/* --- WAD helpers --------------------------------------------------------- */

static int32_t le32(const uint8_t* at) {
    return (int32_t)((uint32_t)at[0]
                   | ((uint32_t)at[1] << 8)
                   | ((uint32_t)at[2] << 16)
                   | ((uint32_t)at[3] << 24));
}

/* The directory w_wad already validated; this reads only the names, which
 * w_wad does not expose by index. */
static void directoryName(const uint8_t* directory, int32_t index, char out[9]) {
    const uint8_t* field = directory + (size_t)index * 16u + 8u;
    size_t byte;

    for (byte = 0; byte < 8u; ++byte) {
        out[byte] = (char)field[byte];
    }
    out[8] = '\0';
}

typedef struct NameList {
    char (*names)[9];
    int32_t count;
    int32_t capacity;
} NameList;

static int nameListAdd(NameList* list, const char* name) {
    int32_t index;

    for (index = 0; index < list->count; ++index) {
        if (strcmp(list->names[index], name) == 0) {
            return 1; /* already present, nothing to do */
        }
    }

    if (list->count == list->capacity) {
        const int32_t capacity = list->capacity == 0 ? 64 : list->capacity * 2;
        char (*grown)[9] = (char (*)[9])realloc(list->names, sizeof(*grown) * (size_t)capacity);
        if (grown == NULL) {
            return 0;
        }
        list->names = grown;
        list->capacity = capacity;
    }

    memcpy(list->names[list->count], name, 9u);
    ++list->count;
    return 1;
}

static void upperAscii(char* text) {
    size_t index;

    for (index = 0; text[index] != '\0'; ++index) {
        if (text[index] >= 'a' && text[index] <= 'z') {
            text[index] = (char)(text[index] - 32);
        }
    }
}

/* --- Decoding ------------------------------------------------------------ */

static r_error_t fetchPatch(void* user, const char* name, const uint8_t** data,
                            size_t* size) {
    const wad_state_t* wad = (const wad_state_t*)user;
    const int index = W_CheckNumForName(wad, name);

    if (index < 0) {
        return R_ERR_PATCH_FETCH;
    }
    *data = W_LumpPointer(wad, index);
    *size = (size_t)W_LumpLength(wad, index);

    return R_ERR_OK;
}

/* Hashes every column of one patch, framing each with its length. */
static r_error_t hashPatch(const wad_state_t* wad, const char* name, char hex[65]) {
    const int lump = W_CheckNumForName(wad, name);
    const uint8_t* data;
    size_t size;
    r_patch_info_t info;
    sha256_t context;
    r_error_t error;
    int32_t column;

    if (lump < 0) {
        return R_ERR_PATCH_FETCH;
    }
    data = W_LumpPointer(wad, lump);
    size = (size_t)W_LumpLength(wad, lump);

    error = r_patch_validate(data, size, &info);
    if (error != R_ERR_OK) {
        return error;
    }

    sha256Init(&context);
    for (column = 0; column < info.width; ++column) {
        size_t required = 0u;
        uint8_t framing[2];
        uint8_t* stream;

        /* Ask for the size first: the contract every decoder here follows. */
        error = r_patch_column(&info, data, size, column, NULL, 0u, &required);
        if (error != R_ERR_NULL_OUTPUT) {
            return error;
        }

        stream = (uint8_t*)xmalloc(required);
        error = r_patch_column(&info, data, size, column, stream, required, &required);
        if (error != R_ERR_OK) {
            free(stream);
            return error;
        }

        framing[0] = (uint8_t)(required & 0xFFu);
        framing[1] = (uint8_t)((required >> 8) & 0xFFu);
        sha256Update(&context, framing, sizeof framing);
        sha256Update(&context, stream, required);
        free(stream);
    }

    {
        uint8_t digest[32];
        sha256Final(&context, digest);
        digestToHex(digest, hex);
    }

    return R_ERR_OK;
}

static void reportPatch(const wad_state_t* wad, const char* name) {
    char hex[65];
    const r_error_t error = hashPatch(wad, name, hex);

    if (error != R_ERR_OK) {
        printf("PATCH-INVALID %s %d\n", name, (int)error);
        return;
    }
    printf("PATCH %s %s\n", name, hex);
}

static void reportFlat(const wad_state_t* wad, const char* name) {
    const int lump = W_CheckNumForName(wad, name);
    const uint8_t* data;
    size_t size;
    sha256_t context;
    uint8_t digest[32];
    char hex[65];

    if (lump < 0) {
        printf("FLAT-SKIP %s 0\n", name);
        return;
    }
    data = W_LumpPointer(wad, lump);
    size = (size_t)W_LumpLength(wad, lump);
    if (r_flat_validate(data, size) != R_ERR_OK) {
        printf("FLAT-SKIP %s %lu\n", name, (unsigned long)size);
        return;
    }

    sha256Init(&context);
    sha256Update(&context, data, size);
    sha256Final(&context, digest);
    digestToHex(digest, hex);
    printf("FLAT %s %s\n", name, hex);
}

/* Every texture name in one TEXTURE1/TEXTURE2 lump, in directory order. */
static int32_t textureCount(const uint8_t* lump, size_t size) {
    if (size < 4u) {
        return -1;
    }
    return le32(lump);
}

static void textureNameAt(const uint8_t* lump, size_t size, int32_t index, char out[9]) {
    const int32_t count = textureCount(lump, size);
    size_t offset = 0u;

    out[0] = '\0';
    if (count <= 0 || index < 0 || index >= count) {
        return;
    }
    offset = (size_t)le32(lump + 4u + (size_t)index * 4u);
    if (offset + 8u > size) {
        return;
    }
    memcpy(out, lump + offset, 8u);
    out[8] = '\0';
}

static void reportTexture(const wad_state_t* wad, const uint8_t* texture1, size_t size1,
                          const uint8_t* texture2, size_t size2, const char* name) {
    const int pnamesIndex = W_CheckNumForName(wad, "PNAMES");
    const uint8_t* pnames;
    size_t pnamesSize;
    r_texture_info_t info;
    r_error_t error;
    size_t surface;
    uint8_t* pixels;
    uint8_t* mask;
    sha256_t context;
    uint8_t digest[32];
    char hex[65];

    if (pnamesIndex < 0) {
        printf("TEXTURE-UNRESOLVED %s PNAMES\n", name);
        return;
    }
    pnames = W_LumpPointer(wad, pnamesIndex);
    pnamesSize = (size_t)W_LumpLength(wad, pnamesIndex);

    error = r_texture_lookup(texture1, size1, texture2, size2, name, &info);
    if (error != R_ERR_OK) {
        printf("TEXTURE-INVALID %s %d\n", name, (int)error);
        return;
    }

    surface = (size_t)info.width * (size_t)info.height;
    pixels = (uint8_t*)xmalloc(surface);
    mask = (uint8_t*)xmalloc(surface);

    error = r_texture_compose(&info, pnames, pnamesSize, fetchPatch, (void*)wad, pixels,
                              surface, mask, surface, NULL);
    if (error == R_ERR_PATCH_FETCH) {
        printf("TEXTURE-UNRESOLVED %s\n", name);
        free(pixels);
        free(mask);
        return;
    }
    if (error != R_ERR_OK) {
        printf("TEXTURE-INVALID %s %d\n", name, (int)error);
        free(pixels);
        free(mask);
        return;
    }

    sha256Init(&context);
    sha256Update(&context, pixels, surface);
    sha256Update(&context, mask, surface);
    sha256Final(&context, digest);
    digestToHex(digest, hex);
    printf("TEXTURE %s %s\n", name, hex);

    free(pixels);
    free(mask);
}

/* --- main ---------------------------------------------------------------- */

static uint8_t* readFile(const char* path, size_t* size) {
    FILE* file = fopen(path, "rb");
    long length;
    uint8_t* bytes;

    if (file == NULL) {
        return NULL;
    }
    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return NULL;
    }
    length = ftell(file);
    if (length < 0 || fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return NULL;
    }

    bytes = (uint8_t*)malloc((size_t)length);
    if (bytes == NULL) {
        fclose(file);
        return NULL;
    }
    if (fread(bytes, 1u, (size_t)length, file) != (size_t)length) {
        free(bytes);
        fclose(file);
        return NULL;
    }
    fclose(file);

    *size = (size_t)length;
    return bytes;
}

int main(int argc, char** argv) {
    uint8_t* bytes;
    size_t size = 0u;
    wad_state_t wad;
    const uint8_t* directory;
    int32_t infoOffset;
    int32_t index;
    int32_t pnamesIndex;
    int32_t firstSprite;
    int32_t lastSprite;
    int32_t firstFlat;
    int32_t lastFlat;
    int32_t texture1Index;
    int32_t texture2Index;
    const uint8_t* texture1;
    size_t size1 = 0u;
    const uint8_t* texture2;
    size_t size2 = 0u;
    NameList patches = {NULL, 0, 0};

    if (argc != 2) {
        fprintf(stderr, "usage: rdat_check <path-to-wad>\n");
        return 2;
    }
    if (sha256SelfTest() == 0) {
        fprintf(stderr, "rdat_check: the internal SHA-256 self-test failed\n");
        return 2;
    }

    bytes = readFile(argv[1], &size);
    if (bytes == NULL) {
        fprintf(stderr, "rdat_check: cannot read %s\n", argv[1]);
        return 1;
    }
    if (wad_open(&wad, bytes, size) != WAD_ERR_OK) {
        fprintf(stderr, "rdat_check: %s is not a WAD this reader accepts\n", argv[1]);
        free(bytes);
        return 1;
    }

    printf("WAD %s %d\n", memcmp(bytes, "IWAD", 4u) == 0 ? "IWAD" : "PWAD",
           (int)wad.numLumps);

    infoOffset = wad.infoTableOffset;
    directory = bytes + (size_t)infoOffset;

    /* Patch names: PNAMES first, then the sprites between S_START and S_END,
     * deduplicated -- the same set the oracle builds. */
    pnamesIndex = W_CheckNumForName(&wad, "PNAMES");
    if (pnamesIndex >= 0) {
        const uint8_t* pnames = W_LumpPointer(&wad, pnamesIndex);
        const size_t pnamesSize = (size_t)W_LumpLength(&wad, pnamesIndex);

        if (pnamesSize >= 4u) {
            const int32_t count = le32(pnames);
            if (count > 0 && 4u + (size_t)count * 8u <= pnamesSize) {
                int32_t patch;
                for (patch = 0; patch < count; ++patch) {
                    char name[9];
                    memcpy(name, pnames + 4u + (size_t)patch * 8u, 8u);
                    name[8] = '\0';
                    upperAscii(name);
                    if (W_CheckNumForName(&wad, name) >= 0) {
                        nameListAdd(&patches, name);
                    }
                }
            }
        }
    }

    firstSprite = W_CheckNumForName(&wad, "S_START");
    lastSprite = W_CheckNumForName(&wad, "S_END");
    if (firstSprite >= 0 && lastSprite > firstSprite) {
        for (index = firstSprite + 1; index < lastSprite; ++index) {
            char name[9];
            directoryName(directory, index, name);
            nameListAdd(&patches, name);
        }
    }

    for (index = 0; index < patches.count; ++index) {
        reportPatch(&wad, patches.names[index]);
    }
    free(patches.names);

    texture1Index = W_CheckNumForName(&wad, "TEXTURE1");
    texture1 = texture1Index >= 0 ? W_LumpPointer(&wad, texture1Index) : NULL;
    if (texture1 != NULL) {
        size1 = (size_t)W_LumpLength(&wad, texture1Index);
    }
    texture2Index = W_CheckNumForName(&wad, "TEXTURE2");
    texture2 = texture2Index >= 0 ? W_LumpPointer(&wad, texture2Index) : NULL;
    if (texture2 != NULL) {
        size2 = (size_t)W_LumpLength(&wad, texture2Index);
    }

    {
        int32_t which;
        for (which = 0; which < 2; ++which) {
            const uint8_t* lump = which == 0 ? texture1 : texture2;
            const size_t lumpSize = which == 0 ? size1 : size2;
            const int32_t count = lump != NULL ? textureCount(lump, lumpSize) : 0;
            int32_t texture;

            for (texture = 0; texture < count; ++texture) {
                char name[9];
                textureNameAt(lump, lumpSize, texture, name);
                if (name[0] != '\0') {
                    reportTexture(&wad, texture1, size1, texture2, size2, name);
                }
            }
        }
    }

    firstFlat = W_CheckNumForName(&wad, "F_START");
    lastFlat = W_CheckNumForName(&wad, "F_END");
    if (firstFlat >= 0 && lastFlat > firstFlat) {
        for (index = firstFlat + 1; index < lastFlat; ++index) {
            char name[9];
            directoryName(directory, index, name);
            reportFlat(&wad, name);
        }
    }

    free(bytes);
    return 0;
}
