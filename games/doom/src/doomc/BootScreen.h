#pragma once

#include <cstdint>

#include "doomc/LevelFailure.h"

namespace doom {
namespace doomc {

/*
 * THE BOOT AND FAILURE SCREENS
 *
 * The panel must be able to say what happened before a level exists, so this
 * module owns its own pixels: a 5x7 column font for ASCII 32..95 ships in
 * `BootScreen.cpp`, and the failure it must report includes "the WAD is
 * missing", where no WAD font is available to borrow. Uppercase-only is
 * deliberate -- the board's 0.75 downscale keeps capitals legible -- and it keeps
 * the table at 320 bytes.
 *
 * The painter writes RGB332 bytes, not DOOM palette indices. That is the regime
 * of the engine's 8bpp surface *before* `setPalette8()` installs the PLAYPAL
 * table, and it is the only moment an error screen is ever painted: once a level
 * session is open the same byte means a palette index, so the scene must never
 * paint these screens afterwards. The header states that invariant here because
 * the same buffer changes meaning at exactly that call.
 *
 * Nothing here allocates, uses a float, or includes an engine header: the host
 * gate (`tools/boot_check.py`) compiles this file and hashes the exact bytes the
 * firmwares would present.
 */

/// RGB332 colours. The panel surface expands these bytes with the driver's
/// default RRRGGGBB table before `setPalette8()` exists, so black is 0x00 and
/// white is the all-ones byte rather than a palette index.
constexpr std::uint8_t kBootBackground = 0x00;
constexpr std::uint8_t kBootFrame = 0x6E;   ///< Mid grey.
constexpr std::uint8_t kBootTitle = 0xE0;   ///< Red.
constexpr std::uint8_t kBootBody = 0xFF;    ///< White.
constexpr std::uint8_t kBootFooter = 0x92;  ///< Dim grey.

/**
 * @brief The text of one boot screen: a title, up to four logical lines, a footer.
 *
 * A `lines[]` entry may contain a `\n`; that is the taxonomy table's wrap point
 * and the painter renders it as a second row. The strings are borrowed, never
 * copied. `bootScreenError()` formats its detail and footer into file-static
 * buffers, so a screen must be painted before the next `bootScreenError()` call;
 * the loading screen's pointers are literals and live forever.
 */
struct BootScreen {
    const char* title = "";
    const char* lines[4] = {nullptr, nullptr, nullptr, nullptr};
    const char* footer = "";
};

/**
 * @brief Builds the loading screen shown before the level session opens.
 *
 * Title `DOOM`; the port's name, the work in progress and @p level; footer
 * `SHAREWARE DOOM1.WAD`. It exists so the display is seen working before
 * the game does -- a boot that dies in the level loader is otherwise a black
 * panel with no evidence that the panel ever worked.
 *
 * @param level The level marker, e.g. "E1M1"; an empty string is used when NULL.
 * @return A screen whose pointers are literals.
 */
BootScreen bootScreenLoading(const char* level);

/**
 * @brief Builds the screen for a failed level open.
 *
 * The title, body and hint come from the taxonomy; the body/hint wrap points are
 * already in those strings. The detail (a lump or baked-error name) is rendered
 * uppercase on the third line when it is non-empty, and the footer carries the
 * machine pair (`CODE <code> <LEVEL>`) so a photograph of the panel is enough to
 * identify the branch in the serial log.
 *
 * @param failure The classified failure recorded by the session.
 * @param level   The level marker; an empty string is used when NULL.
 * @return A screen whose detail/footer pointers are valid until the next call.
 */
BootScreen bootScreenError(const LevelFailure& failure, const char* level);

/**
 * @brief Paints @p screen into an RGB332 framebuffer.
 *
 * Black background, a 2-pixel frame, the title at scale 3, the logical lines at
 * scale 2 and the footer at scale 2. Integer scaling only. Every write is
 * clipped to `[0, width) x [0, height)`, so a smaller-than-expected surface is
 * a smaller picture rather than a wild write.
 *
 * @param framebuffer The surface's bytes, one per pixel, stride = @p width.
 * @param width       Surface stride in bytes; must be positive.
 * @param height      Surface rows; must be positive.
 * @param screen      The text to paint.
 */
void bootScreenPaint(std::uint8_t* framebuffer, int width, int height,
                     const BootScreen& screen);

/**
 * @brief Whether @p screen can be drawn on a @p width x @p height surface.
 *
 * The same layout constants the painter uses, measured rather than assumed:
 * every rendered line's width at its scale, the number of rendered rows and the
 * footer's position. `tools/boot_check.py` asserts it for 320x200 and the debug
 * build of the scene uses it as an assert, so a longer sentence cannot silently
 * run off the panel.
 *
 * @param screen The screen to measure.
 * @param width  Surface width in pixels.
 * @param height Surface height in pixels.
 * @return true when every line and the layout fit.
 */
bool bootScreenFits(const BootScreen& screen, int width, int height);

}  // namespace doomc
}  // namespace doom
