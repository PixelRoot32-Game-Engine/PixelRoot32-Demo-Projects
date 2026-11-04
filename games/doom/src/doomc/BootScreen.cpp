#include "doomc/BootScreen.h"

#include <cstdio>
#include <cstring>

/*
 * The screen's own font and painter.
 *
 * See BootScreen.h for why the font is embedded, why the bytes are RGB332 and
 * why the same buffer must never be painted after a level session opens. The
 * table below is column-major: five bytes per glyph, one byte per column, bit 0
 * is the top row. That layout lets the painter test one bit per pixel with no
 * transposition table, and it is the layout the gate hashes.
 */

namespace doom {
namespace doomc {

namespace {

// --- The font ---------------------------------------------------------------

constexpr int kGlyphWidth = 5;
constexpr int kGlyphHeight = 7;
constexpr int kGlyphAdvance = 6;  ///< Five columns plus one of letter spacing.
constexpr int kFirstGlyph = 32;
constexpr int kLastGlyph = 95;

/*
 * ASCII 32..95, 64 glyphs x 5 bytes = 320 bytes, column-major, bit 0 = top row.
 * Generated from a 5x7 uppercase bitmap set and committed, like every other
 * raster table in this port: the gate hashes the result, so a transcription slip
 * is a named failure rather than a font that "looks a bit odd" on a shelf.
 */
constexpr std::uint8_t kFont[(kLastGlyph - kFirstGlyph + 1) * kGlyphWidth] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x5F, 0x00, 0x00, 0x00, 0x03, 0x00, 0x03, 0x00, 0x14,
    0x7F, 0x14, 0x7F, 0x14, 0x24, 0x2A, 0x7F, 0x2A, 0x12, 0x23, 0x13, 0x08, 0x64, 0x62, 0x36, 0x49,
    0x55, 0x22, 0x50, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x1C, 0x22, 0x41, 0x00, 0x00, 0x41, 0x22,
    0x1C, 0x00, 0x2A, 0x1C, 0x3E, 0x1C, 0x2A, 0x08, 0x08, 0x3E, 0x08, 0x08, 0x00, 0x50, 0x30, 0x00,
    0x00, 0x08, 0x08, 0x08, 0x08, 0x08, 0x00, 0x60, 0x60, 0x00, 0x00, 0x40, 0x30, 0x08, 0x06, 0x01,
    0x3E, 0x51, 0x49, 0x45, 0x3E, 0x00, 0x42, 0x7F, 0x40, 0x00, 0x42, 0x61, 0x51, 0x49, 0x46, 0x21,
    0x41, 0x45, 0x4B, 0x31, 0x18, 0x14, 0x12, 0x7F, 0x10, 0x27, 0x45, 0x45, 0x45, 0x39, 0x3C, 0x4A,
    0x49, 0x49, 0x30, 0x01, 0x71, 0x09, 0x05, 0x03, 0x36, 0x49, 0x49, 0x49, 0x36, 0x06, 0x49, 0x49,
    0x29, 0x1E, 0x00, 0x36, 0x36, 0x00, 0x00, 0x00, 0x56, 0x36, 0x00, 0x00, 0x08, 0x14, 0x22, 0x41,
    0x00, 0x14, 0x14, 0x14, 0x14, 0x14, 0x00, 0x41, 0x22, 0x14, 0x08, 0x02, 0x01, 0x51, 0x09, 0x06,
    0x3E, 0x41, 0x5D, 0x55, 0x1E, 0x7C, 0x12, 0x11, 0x12, 0x7C, 0x7F, 0x49, 0x49, 0x49, 0x36, 0x3E,
    0x41, 0x41, 0x41, 0x22, 0x7F, 0x41, 0x41, 0x22, 0x1C, 0x7F, 0x49, 0x49, 0x49, 0x41, 0x7F, 0x09,
    0x09, 0x09, 0x01, 0x3E, 0x41, 0x49, 0x49, 0x3A, 0x7F, 0x08, 0x08, 0x08, 0x7F, 0x00, 0x41, 0x7F,
    0x41, 0x00, 0x20, 0x40, 0x41, 0x3F, 0x01, 0x7F, 0x08, 0x14, 0x22, 0x41, 0x7F, 0x40, 0x40, 0x40,
    0x40, 0x7F, 0x02, 0x0C, 0x02, 0x7F, 0x7F, 0x02, 0x04, 0x08, 0x7F, 0x3E, 0x41, 0x41, 0x41, 0x3E,
    0x7F, 0x09, 0x09, 0x09, 0x06, 0x3E, 0x41, 0x51, 0x21, 0x5E, 0x7F, 0x09, 0x19, 0x29, 0x46, 0x46,
    0x49, 0x49, 0x49, 0x31, 0x01, 0x01, 0x7F, 0x01, 0x01, 0x3F, 0x40, 0x40, 0x40, 0x3F, 0x1F, 0x20,
    0x40, 0x20, 0x1F, 0x7F, 0x20, 0x18, 0x20, 0x7F, 0x63, 0x14, 0x08, 0x14, 0x63, 0x03, 0x04, 0x78,
    0x04, 0x03, 0x61, 0x51, 0x49, 0x45, 0x43, 0x00, 0x7F, 0x41, 0x41, 0x00, 0x01, 0x06, 0x08, 0x30,
    0x40, 0x00, 0x41, 0x41, 0x7F, 0x00, 0x04, 0x02, 0x01, 0x02, 0x04, 0x40, 0x40, 0x40, 0x40, 0x40,
};

// --- Layout -----------------------------------------------------------------

constexpr int kFrameInset = 4;
constexpr int kFrameThickness = 2;
constexpr int kTextMargin = 10;
constexpr int kTitleScale = 3;
constexpr int kTitleTop = 18;
constexpr int kBodyScale = 2;
constexpr int kBodyTop = 58;
constexpr int kLineStep = 22;
constexpr int kFooterScale = 2;
constexpr int kFooterBottom = 20;  ///< Footer top = height - 20 - 14.
constexpr int kMaxRenderedRows = 5;

constexpr int lineHeight(int scale) { return kGlyphHeight * scale; }

/// Width of @p count characters at @p scale, with no trailing letter spacing.
constexpr int runWidth(int count, int scale) {
    return count > 0 ? ((count * kGlyphAdvance) - 1) * scale : 0;
}

int textWidth(const char* text, int scale) {
    if (text == nullptr) {
        return 0;
    }
    // A rendered line is the substring between newlines: the table's wrap point
    // is not a character on the panel.
    int longest = 0;
    int current = 0;
    for (const char* at = text; *at != '\0'; ++at) {
        if (*at == '\n') {
            const int width = runWidth(current, scale);
            if (width > longest) {
                longest = width;
            }
            current = 0;
        } else {
            ++current;
        }
    }
    const int width = runWidth(current, scale);
    return width > longest ? width : longest;
}

/// Rendered rows in one logical line: one plus its newline count.
int lineRows(const char* text) {
    if (text == nullptr) {
        return 0;
    }
    int rows = 1;
    for (const char* at = text; *at != '\0'; ++at) {
        if (*at == '\n') {
            ++rows;
        }
    }
    return rows;
}

void fillRect(std::uint8_t* framebuffer, int width, int height, int x, int y, int w, int h,
              std::uint8_t color) {
    if (framebuffer == nullptr || w <= 0 || h <= 0) {
        return;
    }
    int x0 = x < 0 ? 0 : x;
    int y0 = y < 0 ? 0 : y;
    int x1 = x + w > width ? width : x + w;
    int y1 = y + h > height ? height : y + h;
    if (x0 >= x1 || y0 >= y1) {
        return;
    }
    for (int row = y0; row < y1; ++row) {
        std::memset(framebuffer + static_cast<std::size_t>(row) * static_cast<std::size_t>(width)
                        + static_cast<std::size_t>(x0),
                    color, static_cast<std::size_t>(x1 - x0));
    }
}

/// Draws one glyph; out-of-range bytes become '?' rather than reading past the
/// table, and a stray lowercase byte still renders as its capital.
void drawGlyph(std::uint8_t* framebuffer, int width, int height, int x, int y, char character,
               int scale, std::uint8_t color) {
    int code = static_cast<unsigned char>(character);
    if (code >= 'a' && code <= 'z') {
        code -= ('a' - 'A');
    }
    if (code < kFirstGlyph || code > kLastGlyph) {
        code = '?';
    }
    const std::uint8_t* glyph = kFont + (code - kFirstGlyph) * kGlyphWidth;

    for (int column = 0; column < kGlyphWidth; ++column) {
        const std::uint8_t bits = glyph[column];
        for (int row = 0; row < kGlyphHeight; ++row) {
            if ((bits & (1u << row)) != 0u) {
                fillRect(framebuffer, width, height, x + column * scale, y + row * scale, scale,
                         scale, color);
            }
        }
    }
}

/// Draws a logical line block, honouring the table's `\n` wrap points. Returns
/// the y of the next free row so the caller can stack blocks without a second
/// measurement.
int drawTextBlock(std::uint8_t* framebuffer, int width, int height, int x, int y,
                  const char* text, int scale, std::uint8_t color) {
    if (text == nullptr) {
        return y;
    }
    int cursorX = x;
    int cursorY = y;
    for (const char* at = text; *at != '\0'; ++at) {
        if (*at == '\n') {
            cursorX = x;
            cursorY += kLineStep;
            continue;
        }
        drawGlyph(framebuffer, width, height, cursorX, cursorY, *at, scale, color);
        cursorX += kGlyphAdvance * scale;
    }
    return cursorY + kLineStep;
}

// --- Error-screen formatting ------------------------------------------------

/*
 * The builder has to format two pieces of text (the detail and the machine pair)
 * and the struct stores only pointers. Two file-static buffers hold them; one
 * screen is built and painted at a time on the boot path, and the header states
 * the lifetime. Sizes are the struct's own so a longer detail cannot overrun.
 */
char gDetailLine[sizeof(LevelFailure::detail)];
char gFooterLine[40];

/// Copies @p source into @p out as uppercase printable ASCII, dropping anything
/// a 5x7 uppercase font cannot render. Returns @p out.
char* uppercaseInto(char* out, std::size_t capacity, const char* source) {
    std::size_t used = 0;
    if (source != nullptr && capacity > 0u) {
        for (const char* at = source; *at != '\0' && used + 1u < capacity; ++at) {
            unsigned char code = static_cast<unsigned char>(*at);
            if (code >= 'a' && code <= 'z') {
                code = static_cast<unsigned char>(code - ('a' - 'A'));
            }
            if (code < 32u || code > 95u) {
                code = ' ';
            }
            out[used++] = static_cast<char>(code);
        }
    }
    out[used] = '\0';
    return out;
}

const char* detailLine(const LevelFailure& failure) {
    if (failure.detail[0] == '\0') {
        return nullptr;
    }
    return uppercaseInto(gDetailLine, sizeof gDetailLine, failure.detail);
}

const char* footerLine(const LevelFailure& failure, const char* level) {
    char upper[8] = {};
    if (level != nullptr && level[0] != '\0') {
        uppercaseInto(upper, sizeof upper, level);
        std::snprintf(gFooterLine, sizeof gFooterLine, "CODE %d %s", failure.code, upper);
    } else {
        std::snprintf(gFooterLine, sizeof gFooterLine, "CODE %d", failure.code);
    }
    return gFooterLine;
}

}  // namespace

BootScreen bootScreenLoading(const char* level) {
    BootScreen screen;
    screen.title = "DOOM";
    screen.lines[0] = "PIXELROOT32 ESP32";
    screen.lines[1] = "OPENING THE WAD";
    screen.lines[2] = level != nullptr ? level : "";
    screen.lines[3] = nullptr;
    screen.footer = "SHAREWARE DOOM1.WAD";
    return screen;
}

BootScreen bootScreenError(const LevelFailure& failure, const char* level) {
    BootScreen screen;
    screen.title = levelFailureTitle(failure);
    screen.lines[0] = levelFailureBody(failure);
    screen.lines[1] = levelFailureHint(failure);
    screen.lines[2] = detailLine(failure);
    screen.lines[3] = nullptr;
    screen.footer = footerLine(failure, level);
    return screen;
}

void bootScreenPaint(std::uint8_t* framebuffer, int width, int height, const BootScreen& screen) {
    if (framebuffer == nullptr || width <= 0 || height <= 0) {
        return;
    }

    // The whole surface goes black first: a previous frame's bytes are not part
    // of this screen, and a failure screen must not look like a corrupted game.
    for (int row = 0; row < height; ++row) {
        std::memset(framebuffer + static_cast<std::size_t>(row) * static_cast<std::size_t>(width),
                    kBootBackground, static_cast<std::size_t>(width));
    }

    // The frame: four bars, two pixels thick, inset from the edge.
    const int outerX = kFrameInset;
    const int outerY = kFrameInset;
    const int outerW = width - 2 * kFrameInset;
    const int outerH = height - 2 * kFrameInset;
    fillRect(framebuffer, width, height, outerX, outerY, outerW, kFrameThickness, kBootFrame);
    fillRect(framebuffer, width, height, outerX, outerY + outerH - kFrameThickness, outerW,
             kFrameThickness, kBootFrame);
    fillRect(framebuffer, width, height, outerX, outerY, kFrameThickness, outerH, kBootFrame);
    fillRect(framebuffer, width, height, outerX + outerW - kFrameThickness, outerY,
             kFrameThickness, outerH, kBootFrame);

    drawTextBlock(framebuffer, width, height, kTextMargin, kTitleTop, screen.title, kTitleScale,
                  kBootTitle);

    int y = kBodyTop;
    for (int index = 0; index < 4; ++index) {
        y = drawTextBlock(framebuffer, width, height, kTextMargin, y, screen.lines[index],
                          kBodyScale, kBootBody);
    }

    drawTextBlock(framebuffer, width, height, kTextMargin,
                  height - kFooterBottom - lineHeight(kFooterScale), screen.footer,
                  kFooterScale, kBootFooter);
}

bool bootScreenFits(const BootScreen& screen, int width, int height) {
    if (width <= 0 || height <= 0) {
        return false;
    }
    // The frame needs two bars and enough inside them for the text margin.
    if (width < 2 * (kFrameInset + kFrameThickness) + 2 * kTextMargin) {
        return false;
    }
    const int content = width - 2 * kTextMargin;
    if (content <= 0) {
        return false;
    }

    if (screen.title == nullptr || screen.title[0] == '\0') {
        return false;
    }
    if (textWidth(screen.title, kTitleScale) > content) {
        return false;
    }
    if (textWidth(screen.footer, kFooterScale) > content) {
        return false;
    }

    int rows = 0;
    for (int index = 0; index < 4; ++index) {
        const char* line = screen.lines[index];
        if (line == nullptr) {
            continue;
        }
        if (textWidth(line, kBodyScale) > content) {
            return false;
        }
        rows += lineRows(line);
    }
    if (rows > kMaxRenderedRows) {
        return false;
    }

    // Vertical: the title must clear the first body row, the rows must clear the
    // footer, and the footer must stay inside the frame.
    const int titleBottom = kTitleTop + lineHeight(kTitleScale);
    const int bodyBottom =
        rows > 0 ? kBodyTop + (rows - 1) * kLineStep + lineHeight(kBodyScale) : kBodyTop;
    const int footerTop = height - kFooterBottom - lineHeight(kFooterScale);
    if (titleBottom + 6 > kBodyTop) {
        return false;
    }
    if (bodyBottom + 4 > footerTop) {
        return false;
    }
    return footerTop + lineHeight(kFooterScale) <= height - kFrameInset - kFrameThickness;
}

}  // namespace doomc
}  // namespace doom
