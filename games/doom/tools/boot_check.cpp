/*
 * The boot/error screen harness for tools/boot_check.py (D7.2).
 *
 * It compiles the firmware's own BootScreen.cpp and LevelFailure.cpp on the host
 * -- no engine, no WAD, no APU -- builds the loading screen and one screen per
 * LevelFailureReason, renders each into a 320x200 heap buffer and dumps the text
 * and the SHA-256 of the exact bytes the panel would receive. The Python gate
 * owns the pass/fail rules and the committed record; this harness only produces
 * evidence.
 *
 *   --stub      leave the buffer black instead of painting it: the gate must see
 *               the difference, or it proves only that the program runs.
 *   --perturb   shift one string (a leading space in the title): same point, a
 *               change too small to break any text rule but visible in pixels.
 *
 * Exit 0 when the harness ran; the gate's findings are what the caller parses.
 */
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "doomc/BootScreen.h"
#include "doomc/LevelFailure.h"

using doom::doomc::BootScreen;
using doom::doomc::LevelFailure;
using doom::doomc::LevelFailureReason;
using doom::doomc::kBootBackground;
using doom::doomc::kLevelFailureReasonCount;
using doom::doomc::kLevelFailureReasons;

namespace {

constexpr int kWidth = 320;
constexpr int kHeight = 200;

// --- SHA-256 (the port's own style: lowercase hex of the exact bytes) --------

struct Sha256 {
    std::uint32_t state[8];
    std::uint64_t length;
    std::uint8_t block[64];
    std::size_t used;
};

constexpr std::uint32_t kRound[64] = {
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u, 0x923f82a4u,
    0xab1c5ed5u, 0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu,
    0x9bdc06a7u, 0xc19bf174u, 0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu,
    0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau, 0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u,
    0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u, 0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu,
    0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u, 0xa2bfe8a1u, 0xa81a664bu,
    0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u, 0x19a4c116u,
    0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
    0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u, 0x90befffau, 0xa4506cebu, 0xbef9a3f7u,
    0xc67178f2u};

std::uint32_t rotr(std::uint32_t value, int bits) {
    return (value >> bits) | (value << (32 - bits));
}

void compress(Sha256* sha, const std::uint8_t* block) {
    std::uint32_t words[64];
    for (int index = 0; index < 16; ++index) {
        words[index] = (static_cast<std::uint32_t>(block[index * 4]) << 24)
                     | (static_cast<std::uint32_t>(block[index * 4 + 1]) << 16)
                     | (static_cast<std::uint32_t>(block[index * 4 + 2]) << 8)
                     | static_cast<std::uint32_t>(block[index * 4 + 3]);
    }
    for (int index = 16; index < 64; ++index) {
        const std::uint32_t s0 = rotr(words[index - 15], 7) ^ rotr(words[index - 15], 18)
                               ^ (words[index - 15] >> 3);
        const std::uint32_t s1 = rotr(words[index - 2], 17) ^ rotr(words[index - 2], 19)
                               ^ (words[index - 2] >> 10);
        words[index] = words[index - 16] + s0 + words[index - 7] + s1;
    }

    std::uint32_t a = sha->state[0];
    std::uint32_t b = sha->state[1];
    std::uint32_t c = sha->state[2];
    std::uint32_t d = sha->state[3];
    std::uint32_t e = sha->state[4];
    std::uint32_t f = sha->state[5];
    std::uint32_t g = sha->state[6];
    std::uint32_t h = sha->state[7];

    for (int index = 0; index < 64; ++index) {
        const std::uint32_t s1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
        const std::uint32_t choose = (e & f) ^ (~e & g);
        const std::uint32_t temp1 = h + s1 + choose + kRound[index] + words[index];
        const std::uint32_t s0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
        const std::uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
        const std::uint32_t temp2 = s0 + majority;
        h = g;
        g = f;
        f = e;
        e = d + temp1;
        d = c;
        c = b;
        b = a;
        a = temp1 + temp2;
    }

    sha->state[0] += a;
    sha->state[1] += b;
    sha->state[2] += c;
    sha->state[3] += d;
    sha->state[4] += e;
    sha->state[5] += f;
    sha->state[6] += g;
    sha->state[7] += h;
}

void shaInit(Sha256* sha) {
    sha->state[0] = 0x6a09e667u;
    sha->state[1] = 0xbb67ae85u;
    sha->state[2] = 0x3c6ef372u;
    sha->state[3] = 0xa54ff53au;
    sha->state[4] = 0x510e527fu;
    sha->state[5] = 0x9b05688cu;
    sha->state[6] = 0x1f83d9abu;
    sha->state[7] = 0x5be0cd19u;
    sha->length = 0u;
    sha->used = 0u;
}

void shaUpdate(Sha256* sha, const std::uint8_t* data, std::size_t size) {
    sha->length += static_cast<std::uint64_t>(size) * 8u;
    while (size > 0u) {
        const std::size_t take = size < 64u - sha->used ? size : 64u - sha->used;
        std::memcpy(sha->block + sha->used, data, take);
        sha->used += take;
        data += take;
        size -= take;
        if (sha->used == 64u) {
            compress(sha, sha->block);
            sha->used = 0u;
        }
    }
}

void shaFinal(Sha256* sha, char out[65]) {
    // The message's bit length is fixed before padding: the padding bytes are
    // not part of it, so they are written straight into the block rather than
    // through shaUpdate().
    const std::uint64_t bits = sha->length;

    sha->block[sha->used++] = 0x80u;
    if (sha->used > 56u) {
        while (sha->used < 64u) {
            sha->block[sha->used++] = 0u;
        }
        compress(sha, sha->block);
        sha->used = 0u;
    }
    while (sha->used < 56u) {
        sha->block[sha->used++] = 0u;
    }
    for (int index = 0; index < 8; ++index) {
        sha->block[56 + index] = static_cast<std::uint8_t>(bits >> (56 - index * 8));
    }
    compress(sha, sha->block);

    static const char hex[] = "0123456789abcdef";
    for (int index = 0; index < 8; ++index) {
        for (int nibble = 0; nibble < 8; ++nibble) {
            const int shift = 28 - nibble * 4;
            out[index * 8 + nibble] = hex[(sha->state[index] >> shift) & 0x0fu];
        }
    }
    out[64] = '\0';
}

// --- The screens ------------------------------------------------------------

void printLines(const char* text) {
    if (text == nullptr) {
        return;
    }
    const char* start = text;
    for (const char* at = text;; ++at) {
        if (*at == '\n' || *at == '\0') {
            std::printf("LINE %.*s\n", static_cast<int>(at - start), start);
            if (*at == '\0') {
                break;
            }
            start = at + 1;
        }
    }
}

/// A representative detail per reason: the longest real one (`Baked`'s
/// `index_out_of_range`) proves the layout with the least headroom.
const char* sampleDetail(LevelFailureReason reason) {
    switch (reason) {
        case LevelFailureReason::Baked:
            return "index_out_of_range";
        case LevelFailureReason::MapLump:
            return "SIDEDEFS";
        case LevelFailureReason::World:
            return "no_fit";
        case LevelFailureReason::Level:
            return "E1M1";
        default:
            return "";
    }
}

void emit(const char* state, int code, BootScreen screen, bool stub, bool perturb) {
    static std::uint8_t* pixels = nullptr;
    if (pixels == nullptr) {
        pixels = new std::uint8_t[static_cast<std::size_t>(kWidth) * kHeight];
    }

    char title[32];
    if (perturb) {
        std::snprintf(title, sizeof title, " %s", screen.title);
        screen.title = title;
    }

    if (stub) {
        std::memset(pixels, 0, static_cast<std::size_t>(kWidth) * kHeight);
    } else {
        doom::doomc::bootScreenPaint(pixels, kWidth, kHeight, screen);
    }

    const bool fits = doom::doomc::bootScreenFits(screen, kWidth, kHeight);
    std::size_t lit = 0u;
    for (int index = 0; index < kWidth * kHeight; ++index) {
        if (pixels[index] != kBootBackground) {
            ++lit;
        }
    }

    Sha256 sha;
    shaInit(&sha);
    shaUpdate(&sha, pixels, static_cast<std::size_t>(kWidth) * kHeight);
    char hash[65];
    shaFinal(&sha, hash);

    std::printf("STATE %s\n", state);
    std::printf("TITLE %s\n", screen.title);
    printLines(screen.lines[0]);
    printLines(screen.lines[1]);
    printLines(screen.lines[2]);
    printLines(screen.lines[3]);
    std::printf("FOOTER %s\n", screen.footer);
    std::printf("CODE %d\n", code);
    std::printf("FITS %d\n", fits ? 1 : 0);
    std::printf("LIT %zu\n", lit);
    std::printf("HASH %s\n", hash);
    std::printf("END\n");
}

}  // namespace

int main(int argc, char** argv) {
    bool stub = false;
    bool perturb = false;
    for (int index = 1; index < argc; ++index) {
        if (std::strcmp(argv[index], "--stub") == 0) {
            stub = true;
        } else if (std::strcmp(argv[index], "--perturb") == 0) {
            perturb = true;
        }
    }

    emit("LOADING", -1, doom::doomc::bootScreenLoading("E1M1"), stub, perturb);

    std::printf("REASONS %d\n", kLevelFailureReasonCount);
    for (int index = 0; index < kLevelFailureReasonCount; ++index) {
        const LevelFailureReason reason = kLevelFailureReasons[index];
        LevelFailure failure;
        failure.reason = reason;
        // A code with two digits exercises the footer's formatting; the reason
        // itself keeps the title/text relationship under test.
        failure.code = 3 + static_cast<int>(reason);
        std::snprintf(failure.detail, sizeof failure.detail, "%s", sampleDetail(reason));

        const char* detail = doom::doomc::levelFailureName(reason);
        char state[32];
        std::snprintf(state, sizeof state, "%s", detail);
        for (char* at = state; *at != '\0'; ++at) {
            if (*at >= 'a' && *at <= 'z') {
                *at = static_cast<char>(*at - ('a' - 'A'));
            }
        }
        emit(state, failure.code, doom::doomc::bootScreenError(failure, "E1M1"), stub, perturb);
    }

    return 0;
}
