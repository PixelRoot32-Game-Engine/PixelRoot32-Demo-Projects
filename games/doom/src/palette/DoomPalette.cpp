#include "palette/DoomPalette.h"

namespace doom {
namespace palette {
namespace {

/// One PLAYPAL-shaped source palette as a value, so a constexpr builder can
/// return it whole and the static_asserts below can inspect single entries.
struct Rgb888Palette {
    std::uint8_t bytes[kRgb888Bytes];
};

/// PLAYPAL shape: 16 rows of 16 entries.
constexpr int kColumns = 16;
constexpr int kRows = 16;

/// Six sectors of 256 steps: the integer hue wheel `hueToRgb()` walks.
constexpr int kHueSteps = 1536;

/**
 * @brief Integer HSV -> RGB at full saturation.
 *
 * @param hue    0..1535, sector-major: 0 is red, 512 green, 1024 blue.
 * @param value  0..255 brightness.
 * @param out    Receives R, G, B.
 *
 * No float anywhere: the sector is a division by 256 and the two intermediate
 * channels are one multiply and one divide each. The whole synthetic palette is
 * built from this at compile time, so the table path stays integer at run time
 * too (the only run-time work is the shift-and-mask in `rgb888To332()`).
 */
constexpr void hueToRgb(int hue, int value, std::uint8_t* out) {
    const int sector = hue / 256;
    const int fraction = hue % 256;
    const int p = 0;
    const int q = value * (255 - fraction) / 255;
    const int t = value * fraction / 255;

    int r = 0;
    int g = 0;
    int b = 0;
    switch (sector) {
        case 0: r = value; g = t; b = p; break;
        case 1: r = q; g = value; b = p; break;
        case 2: r = p; g = value; b = t; break;
        case 3: r = p; g = q; b = value; break;
        case 4: r = t; g = p; b = value; break;
        default: r = value; g = p; b = q; break;
    }

    out[0] = static_cast<std::uint8_t>(r);
    out[1] = static_cast<std::uint8_t>(g);
    out[2] = static_cast<std::uint8_t>(b);
}

/**
 * @brief The stand-in for the WAD's palette, in PLAYPAL shape.
 *
 * Row 0 is the 16-step grey ramp DOOM's palette starts with: index 0 is black
 * and index 15 is white. Rows 1-15 are hue families spread evenly around the
 * wheel -- red at row 1, green at row 6, blue at row 11 -- and each row ramps
 * its brightness from near-black to full value, so every row is a readable
 * gradient and the columns of a row stay the same hue.
 *
 * Generated, not tabulated: 768 literals would hide a channel swap in a wall
 * of numbers, while this function states the layout the static_asserts below
 * then pin.
 *
 * Column 0 of every hue row is deliberately near-black (value 8), the way
 * PLAYPAL's own ramps start: a palette with no dark entries would hide an
 * off-by-one in the low indices behind a bright block.
 */
constexpr Rgb888Palette makeSyntheticPalette() {
    Rgb888Palette palette{};

    for (int column = 0; column < kColumns; ++column) {
        const std::uint8_t grey = static_cast<std::uint8_t>(column * 255 / (kColumns - 1));
        const int base = column * 3;
        palette.bytes[base + 0] = grey;
        palette.bytes[base + 1] = grey;
        palette.bytes[base + 2] = grey;
    }

    for (int row = 1; row < kRows; ++row) {
        const int hue = (row - 1) * kHueSteps / (kRows - 1);
        for (int column = 0; column < kColumns; ++column) {
            // Brightness starts just above black and ends at 255 on the last
            // column, so the pure primaries sit at the row's right edge.
            const int value = 8 + column * 247 / (kColumns - 1);
            hueToRgb(hue, value, &palette.bytes[(row * kColumns + column) * 3]);
        }
    }

    return palette;
}

constexpr Rgb888Palette kSyntheticPalette = makeSyntheticPalette();

/// The wire byte one known entry of the synthetic palette must produce.
constexpr std::uint8_t wireAt(int index) {
    const int base = index * 3;
    return rgb888To332(kSyntheticPalette.bytes[base + 0], kSyntheticPalette.bytes[base + 1],
                       kSyntheticPalette.bytes[base + 2]);
}

// The palette layout and the conversion pinned together, at build time. These
// are the entries the palette test screen makes visible; if the generator or
// the bit layout drifts, this file stops compiling instead of the panel lying.
static_assert(wireAt(0) == 0x00, "synthetic palette: index 0 must be black");
static_assert(wireAt(15) == 0xFF, "synthetic palette: index 15 must be white");
static_assert(wireAt(31) == 0xE0, "synthetic palette: index 31 must be pure red");
static_assert(wireAt(111) == 0x1C, "synthetic palette: index 111 must be pure green");
static_assert(wireAt(191) == 0x03, "synthetic palette: index 191 must be pure blue");

/// The built table, in RAM: 256 bytes written once by `init()`.
std::uint8_t g_indexTable[kEntryCount];
bool g_built = false;

}  // namespace

void buildIndexTable(const std::uint8_t* rgb888, std::uint8_t* out) {
    for (int index = 0; index < kEntryCount; ++index) {
        const int base = index * 3;
        out[index] = rgb888To332(rgb888[base + 0], rgb888[base + 1], rgb888[base + 2]);
    }
}

void init() {
    if (g_built) {
        return;
    }
    buildIndexTable(kSyntheticPalette.bytes, g_indexTable);
    g_built = true;
}

const std::uint8_t* indexTable() {
    // Safe by construction: a caller that forgot init() still gets the table,
    // and the second caller pays nothing.
    init();
    return g_indexTable;
}

}  // namespace palette
}  // namespace doom
