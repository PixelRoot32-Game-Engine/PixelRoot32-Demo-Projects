#include "player_sprites.h"

#include <graphics/Color.h>

#include "player_palette.h"

namespace gfx = pixelroot32::graphics;

namespace player_sprites {

// The exports go in NAMESPACES OF THEIR OWN, and that is the whole reason this
// file exists. Both of them declare `SPRITE_0_4BPP`, `SPRITE_1_4BPP` ... at file
// scope: included side by side any other way, the second one redefines every
// symbol the first one brought in. Wrapping them costs the exports nothing --
// neither file is touched, and this stays the only translation unit that names
// them, so no frame is linked in twice.
namespace idle_art {
#include "player_idle_sprites_sheet.h"
}  // namespace idle_art

namespace run_art {
#include "player_run_sprites_sheet.h"
}  // namespace run_art

namespace {

/// All 16 entries, not just the six the idle art uses. A 4bpp pixel can name
/// any index 0..15 whatever the descriptor declares, and the renderer fills its
/// lookup table from `paletteSize` entries before reading it without a range
/// check.
constexpr std::uint8_t kPaletteSize = 16;

/**
 * Both exports ship each pose TWICE, once mirrored, and neither of them says
 * so. What each block actually holds:
 *
 *   idle  0-3   head left,  feet row 47      travelling down-left
 *   idle  4-7   head right, feet row 47      travelling down-right
 *   idle  8-11  head left,  feet row 49      travelling up-left
 *   idle 12-15  head right, feet row 49      travelling up-right
 *
 *   run   0-7   head left,  feet row 44      travelling down-left
 *   run   8-15  head right, feet row 44      travelling down-right
 *   run  16-23  head left,  feet rows 47-49  travelling up-left
 *   run  24-31  head right, feet rows 47-49  travelling up-right
 *
 * The two files do NOT agree on block order, and the disagreement is not even
 * consistent within a file: idle's down-facing pair is (left, right) while
 * run's is (right, left), yet both up-facing pairs are (left, right). There is
 * no rule to derive here -- each block was identified by looking at it, and the
 * only defence against a re-export silently reordering them is that this table
 * and the two below are the single place that claims to know.
 *
 * Only two blocks per animation are named. The other two are the same art
 * mirrored, drawn with `flipX`, so leaving them unreferenced hands the linker
 * ~48 KB of frames it can drop -- and means a re-export only has to get half as
 * many blocks right.
 *
 * The up-facing pairs are pixel-exact mirrors, so nothing is lost there. The
 * down-facing pairs are not: idle's two differ by ~60 pixels of a ~450-pixel
 * silhouette and run's by ~75, which is somebody retouching one half by hand
 * after mirroring it. Each animation therefore keeps the half its most-used
 * direction needs UNFLIPPED, and spends the flip on the other one.
 */
constexpr std::uint8_t kBlockCount = 2;
constexpr std::uint8_t kDownBlock = 0;  ///< The down-facing pair's kept half.
constexpr std::uint8_t kUpBlock   = 1;  ///< The up-facing pair's kept half.

const gfx::Sprite4bpp kIdleArt[kBlockCount][kIdleFrameCount] = {
    {  // idle 0-3, down-LEFT
        {reinterpret_cast<const std::uint8_t*>(idle_art::SPRITE_0_4BPP),
         PALETTE_PLAYER_MAPPING, kSpriteWidth, kSpriteHeight, kPaletteSize},
        {reinterpret_cast<const std::uint8_t*>(idle_art::SPRITE_1_4BPP),
         PALETTE_PLAYER_MAPPING, kSpriteWidth, kSpriteHeight, kPaletteSize},
        {reinterpret_cast<const std::uint8_t*>(idle_art::SPRITE_2_4BPP),
         PALETTE_PLAYER_MAPPING, kSpriteWidth, kSpriteHeight, kPaletteSize},
        {reinterpret_cast<const std::uint8_t*>(idle_art::SPRITE_3_4BPP),
         PALETTE_PLAYER_MAPPING, kSpriteWidth, kSpriteHeight, kPaletteSize},
    },
    {  // idle 8-11, up-left
        {reinterpret_cast<const std::uint8_t*>(idle_art::SPRITE_8_4BPP),
         PALETTE_PLAYER_MAPPING, kSpriteWidth, kSpriteHeight, kPaletteSize},
        {reinterpret_cast<const std::uint8_t*>(idle_art::SPRITE_9_4BPP),
         PALETTE_PLAYER_MAPPING, kSpriteWidth, kSpriteHeight, kPaletteSize},
        {reinterpret_cast<const std::uint8_t*>(idle_art::SPRITE_10_4BPP),
         PALETTE_PLAYER_MAPPING, kSpriteWidth, kSpriteHeight, kPaletteSize},
        {reinterpret_cast<const std::uint8_t*>(idle_art::SPRITE_11_4BPP),
         PALETTE_PLAYER_MAPPING, kSpriteWidth, kSpriteHeight, kPaletteSize},
    },
};

const gfx::Sprite4bpp kRunArt[kBlockCount][kRunFrameCount] = {
    {  // run 8-15, down-RIGHT -- the other way round from idle's kept half
        {reinterpret_cast<const std::uint8_t*>(run_art::SPRITE_8_4BPP),
         PALETTE_PLAYER_MAPPING, kSpriteWidth, kSpriteHeight, kPaletteSize},
        {reinterpret_cast<const std::uint8_t*>(run_art::SPRITE_9_4BPP),
         PALETTE_PLAYER_MAPPING, kSpriteWidth, kSpriteHeight, kPaletteSize},
        {reinterpret_cast<const std::uint8_t*>(run_art::SPRITE_10_4BPP),
         PALETTE_PLAYER_MAPPING, kSpriteWidth, kSpriteHeight, kPaletteSize},
        {reinterpret_cast<const std::uint8_t*>(run_art::SPRITE_11_4BPP),
         PALETTE_PLAYER_MAPPING, kSpriteWidth, kSpriteHeight, kPaletteSize},
        {reinterpret_cast<const std::uint8_t*>(run_art::SPRITE_12_4BPP),
         PALETTE_PLAYER_MAPPING, kSpriteWidth, kSpriteHeight, kPaletteSize},
        {reinterpret_cast<const std::uint8_t*>(run_art::SPRITE_13_4BPP),
         PALETTE_PLAYER_MAPPING, kSpriteWidth, kSpriteHeight, kPaletteSize},
        {reinterpret_cast<const std::uint8_t*>(run_art::SPRITE_14_4BPP),
         PALETTE_PLAYER_MAPPING, kSpriteWidth, kSpriteHeight, kPaletteSize},
        {reinterpret_cast<const std::uint8_t*>(run_art::SPRITE_15_4BPP),
         PALETTE_PLAYER_MAPPING, kSpriteWidth, kSpriteHeight, kPaletteSize},
    },
    {  // run 16-23, up-left
        {reinterpret_cast<const std::uint8_t*>(run_art::SPRITE_16_4BPP),
         PALETTE_PLAYER_MAPPING, kSpriteWidth, kSpriteHeight, kPaletteSize},
        {reinterpret_cast<const std::uint8_t*>(run_art::SPRITE_17_4BPP),
         PALETTE_PLAYER_MAPPING, kSpriteWidth, kSpriteHeight, kPaletteSize},
        {reinterpret_cast<const std::uint8_t*>(run_art::SPRITE_18_4BPP),
         PALETTE_PLAYER_MAPPING, kSpriteWidth, kSpriteHeight, kPaletteSize},
        {reinterpret_cast<const std::uint8_t*>(run_art::SPRITE_19_4BPP),
         PALETTE_PLAYER_MAPPING, kSpriteWidth, kSpriteHeight, kPaletteSize},
        {reinterpret_cast<const std::uint8_t*>(run_art::SPRITE_20_4BPP),
         PALETTE_PLAYER_MAPPING, kSpriteWidth, kSpriteHeight, kPaletteSize},
        {reinterpret_cast<const std::uint8_t*>(run_art::SPRITE_21_4BPP),
         PALETTE_PLAYER_MAPPING, kSpriteWidth, kSpriteHeight, kPaletteSize},
        {reinterpret_cast<const std::uint8_t*>(run_art::SPRITE_22_4BPP),
         PALETTE_PLAYER_MAPPING, kSpriteWidth, kSpriteHeight, kPaletteSize},
        {reinterpret_cast<const std::uint8_t*>(run_art::SPRITE_23_4BPP),
         PALETTE_PLAYER_MAPPING, kSpriteWidth, kSpriteHeight, kPaletteSize},
    },
};

/**
 * Where each block's feet sit inside its 64x64 frame.
 *
 * One row per BLOCK, not per frame, and that is the point rather than a
 * rounding-off. Run's up-facing frames end on rows 47 to 49 depending on the
 * frame, because a running body rises and falls -- anchoring each frame to its
 * own lowest row would hold the feet at a constant height and flatten the
 * bounce out of the animation. The block's LOWEST row is the one taken, so the
 * planted frames sit on the ground and the airborne ones rise off it, which is
 * the way round that reads as running rather than as sinking.
 */
constexpr int kIdleFootY[kBlockCount] = {47, 49};
constexpr int kRunFootY[kBlockCount]  = {44, 49};

/**
 * Which block each button draws, and whether it draws it mirrored.
 *
 * Read as SCREEN diagonals, because that is what the art was drawn for. Under
 * ISO_PROJECTION the cell axes project to the four diagonals: +cellX (Right)
 * travels down-right, +cellY (Down) down-left, -cellX (Left) up-left, -cellY
 * (Up) up-right.
 *
 * One table per animation, and they are not the same table: idle's kept
 * down-facing block faces left, run's faces right, so the flip falls on Right
 * in one and on Down in the other. Sharing one table here is exactly the bug
 * that put run's Right animation on Down and Down's on Right.
 */
struct BlockRef {
    std::uint8_t block;
    bool         flipX;
};

constexpr BlockRef kIdleDirection[kDirectionCount] = {
    {kUpBlock,   false},  // Left  -> up-left
    {kDownBlock, true},   // Right -> down-right, mirrored from down-left
    {kUpBlock,   true},   // Up    -> up-right,   mirrored from up-left
    {kDownBlock, false},  // Down  -> down-left
};

constexpr BlockRef kRunDirection[kDirectionCount] = {
    {kUpBlock,   false},  // Left  -> up-left
    {kDownBlock, false},  // Right -> down-right
    {kUpBlock,   true},   // Up    -> up-right,  mirrored from up-left
    {kDownBlock, true},   // Down  -> down-left, mirrored from down-right
};

std::uint8_t directionIndex(Direction direction) {
    const std::uint8_t index = static_cast<std::uint8_t>(direction);
    return index < kDirectionCount ? index : 0;
}

}  // namespace

Frame idleFrame(Direction direction, std::uint8_t frame) {
    const BlockRef& ref = kIdleDirection[directionIndex(direction)];
    return Frame{&kIdleArt[ref.block][frame % kIdleFrameCount],
                 ref.flipX,
                 kIdleFootY[ref.block]};
}

Frame runFrame(Direction direction, std::uint8_t frame) {
    const BlockRef& ref = kRunDirection[directionIndex(direction)];
    return Frame{&kRunArt[ref.block][frame % kRunFrameCount],
                 ref.flipX,
                 kRunFootY[ref.block]};
}

}  // namespace player_sprites
