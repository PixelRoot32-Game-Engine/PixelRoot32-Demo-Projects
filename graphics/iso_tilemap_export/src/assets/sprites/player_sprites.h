#pragma once

#include <cstdint>

#include <graphics/Renderer.h>

/**
 * The player's exported animation frames, addressed by direction.
 *
 * Two Sprite Compiler exports back this: `player-idle-sprites.h` (16 frames)
 * and `player-run-sprites.h` (32 frames), both 64x64 at 4bpp, both laid out as
 * four consecutive blocks of one direction each in the order Left, Right, Up,
 * Down. Neither export is edited here -- they are the compiler's output, byte
 * for byte, and this header is the only thing that knows how to read them.
 *
 * They are wrapped, not modified, and the wrapping is what this file exists
 * for: both exports declare `SPRITE_0_4BPP`, `SPRITE_1_4BPP` ... at file scope
 * under the same names, so a translation unit that included both directly
 * would not compile. Including them in the .cpp inside a namespace each keeps
 * the collision from ever reaching a call site.
 */
namespace player_sprites {

/// The four blocks each export ships, in the order they appear in the file.
enum class Direction : std::uint8_t {
    Left = 0,
    Right,
    Up,
    Down,
};

inline constexpr std::uint8_t kDirectionCount = 4;
inline constexpr std::uint8_t kIdleFrameCount = 4;
inline constexpr std::uint8_t kRunFrameCount  = 8;

inline constexpr std::uint8_t kSpriteWidth  = 64;
inline constexpr std::uint8_t kSpriteHeight = 64;

/// Sprite palette slot the player is drawn through. NOT slot 0: setting slot 0
/// also overwrites the global sprite palette, which every other sprite in the
/// program would then be resolved against.
inline constexpr std::uint8_t kPaletteSlot = 1;

/**
 * One animation frame, with everything a caller needs to draw it: the bitmap,
 * whether it is drawn mirrored, and the bitmap row that lands on the cell's
 * diamond centre.
 *
 * The three arrive together rather than through separate lookups: half the
 * directions are drawn as the mirror of another block's art, and each block's
 * feet sit on its own row, so a caller that asked for those separately would
 * have three chances to disagree with itself.
 *
 * `footY` is the same anchor idea the tilemap export ships as
 * `<LAYER>_TILESET_FOOT_Y`, and needed here for the same reason: a 64x64 frame
 * is mostly empty padding, so the row the art's feet occupy is the only row
 * that means anything to the projection. It is measured PER ANIMATION because
 * the two exports disagree -- idle's feet land on row 47 (Left/Right) and 49
 * (Up/Down), run's on 44 and 49. Anchoring both to one number would sink or
 * float the player by up to three pixels on the frame the animation changes.
 */
struct Frame {
    const pixelroot32::graphics::Sprite4bpp* sprite;
    bool flipX;
    int  footY;
};

/// Frame `frame` of the idle loop for `direction`. Out-of-range indices wrap,
/// so a caller's animation clock cannot walk off the end of the table.
Frame idleFrame(Direction direction, std::uint8_t frame);

/// Frame `frame` of the run loop for `direction`. Same wrapping rule.
Frame runFrame(Direction direction, std::uint8_t frame);

}  // namespace player_sprites
