/*
 * palflash_pal.cpp - dumps the panel's palette tables for one PLAYPAL page at one
 * gamma level (D5.3).
 *
 * The other half of tools/palflash_check.py's palette comparison. It runs the
 * firmware's own src/palette/DoomPalette.cpp: palette 0 builds the tables once
 * (`initFromPlaypal`, as the frame driver does at boot), then each request hands
 * a page's 768 bytes to `applyGamma()`, exactly what the scene does when the
 * flash page or the gamma changes. It owns no expectation.
 *
 * Usage: palflash_pal <DOOM1.WAD> <script>
 * Script lines: `pal PAGE GAMMA`. Output per request:
 *   PAL <page> <gamma>
 *   <256 x 4 hex digits: the RGB565 table>
 *   <256 x 2 hex digits: the RGB332 table>
 */
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "palette/DoomPalette.h"

namespace {

std::vector<std::uint8_t> readFile(const char* path) {
    std::vector<std::uint8_t> data;
    FILE* handle = std::fopen(path, "rb");
    if (handle == nullptr) {
        return data;
    }
    std::fseek(handle, 0, SEEK_END);
    const long length = std::ftell(handle);
    std::fseek(handle, 0, SEEK_SET);
    data.resize(static_cast<std::size_t>(length));
    if (std::fread(data.data(), 1, data.size(), handle) != data.size()) {
        data.clear();
    }
    std::fclose(handle);
    return data;
}

std::uint32_t le32(const std::uint8_t* p) {
    return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8) |
           (static_cast<std::uint32_t>(p[2]) << 16) | (static_cast<std::uint32_t>(p[3]) << 24);
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 3) {
        std::fprintf(stderr, "usage: palflash_pal <path-to-WAD> <script>\n");
        return 2;
    }
    const std::vector<std::uint8_t> wad = readFile(argv[1]);
    if (wad.size() < 12) {
        std::fprintf(stderr, "palflash_pal: cannot read %s\n", argv[1]);
        return 2;
    }
    const std::uint32_t count = le32(&wad[4]);
    const std::uint32_t directory = le32(&wad[8]);
    const std::uint8_t* playpal = nullptr;
    std::uint32_t playpalBytes = 0;
    for (std::uint32_t i = 0; i < count; ++i) {
        const std::uint8_t* entry = &wad[directory + i * 16u];
        if (std::strncmp(reinterpret_cast<const char*>(entry + 8), "PLAYPAL", 8) == 0) {
            playpal = &wad[le32(entry)];
            playpalBytes = le32(entry + 4);
            break;
        }
    }
    const std::uint32_t pages = playpalBytes / doom::palette::kRgb888Bytes;
    if (playpal == nullptr || pages != 14u) {
        std::fprintf(stderr, "palflash_pal: PLAYPAL missing or not 14 pages\n");
        return 2;
    }
    doom::palette::initFromPlaypal(playpal);

    std::FILE* script = std::fopen(argv[2], "r");
    if (script == nullptr) {
        return 2;
    }
    char line[128];
    while (std::fgets(line, sizeof line, script) != nullptr) {
        int page = 0;
        int gamma = 0;
        if (std::sscanf(line, "pal %d %d", &page, &gamma) != 2 || page < 0 ||
            page >= static_cast<int>(pages)) {
            continue;
        }
        doom::palette::applyGamma(playpal + page * doom::palette::kRgb888Bytes,
                                  static_cast<std::uint8_t>(gamma));
        std::printf("PAL %d %d\n", page, gamma);
        const std::uint16_t* t565 = doom::palette::playpal565Table();
        for (int i = 0; i < 256; ++i) {
            std::printf("%04x", static_cast<unsigned>(t565[i]));
        }
        std::printf("\n");
        const std::uint8_t* t332 = doom::palette::playpalTable();
        for (int i = 0; i < 256; ++i) {
            std::printf("%02x", static_cast<unsigned>(t332[i]));
        }
        std::printf("\n");
    }
    std::fclose(script);
    return 0;
}
