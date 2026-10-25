#include "ui/TileCatalog.h"

#include "assets/HudSprites.h"
#include "assets/TileSprites.h"

namespace minesweeper {

namespace {

using pixelroot32::graphics::Sprite4bpp;

/** Radius of the table: one row per native tile size. */
constexpr std::uint8_t kSizeCount = 3;

/** Animation phases emitted per size: cursor frame A and frame B. */
constexpr std::uint8_t kCursorPhaseCount = 2;

/** Smileys emitted at 16 px, in SmileyId order. */
constexpr std::uint8_t kSmileyCount = static_cast<std::uint8_t>(SmileyId::Dead) + 1;

/** Decimal glyphs emitted at 8 px: '0'..'9'. */
constexpr std::uint8_t kDigitCount = 10;

/** Letters emitted at 8 px: exactly the ones MINES and TIME need. */
constexpr std::uint8_t kLetterCount = 6;

/** 9-slice pieces emitted at 8 px, in PanelPiece order. */
constexpr std::uint8_t kPanelPieceCount = static_cast<std::uint8_t>(PanelPiece::BottomRight) + 1;

/**
 * @brief Number of enumerators in TileId.
 *
 * TileId has no gap and ends at Explosion, so "last value + 1" is the count.
 * Every row below must have exactly this width; the static_asserts after the
 * tables enforce it so adding a tile id without adding its art fails to build.
 */
constexpr std::uint8_t kTileIdCount = static_cast<std::uint8_t>(TileId::Explosion) + 1;

/**
 * @brief One row per native tile size (16, 12, 8 px), columns in TileId order.
 *
 * Flat and explicit on purpose: a switch per lookup would hide a missing size
 * behind a default branch, while a missing cell here is a null the
 * completeness check below rejects at compile time.
 */
constexpr const Sprite4bpp* kTiles[kSizeCount][kTileIdCount] = {
    {
        // 16 px -- EASY (9x9).
        &COVERED_16,           // Covered
        &COVERED_FLAG_16,      // CoveredFlag
        &COVERED_QUESTION_16,  // CoveredQuestion
        &OPEN_0_16,            // Open0
        &OPEN_1_16,            // Open1
        &OPEN_2_16,            // Open2
        &OPEN_3_16,            // Open3
        &OPEN_4_16,            // Open4
        &OPEN_5_16,            // Open5
        &OPEN_6_16,            // Open6
        &OPEN_7_16,            // Open7
        &OPEN_8_16,            // Open8
        &MINE_16,              // Mine
        &EXPLOSION_16,         // Explosion
    },
    {
        // 12 px -- MEDIUM (16x16).
        &COVERED_12,           // Covered
        &COVERED_FLAG_12,      // CoveredFlag
        &COVERED_QUESTION_12,  // CoveredQuestion
        &OPEN_0_12,            // Open0
        &OPEN_1_12,            // Open1
        &OPEN_2_12,            // Open2
        &OPEN_3_12,            // Open3
        &OPEN_4_12,            // Open4
        &OPEN_5_12,            // Open5
        &OPEN_6_12,            // Open6
        &OPEN_7_12,            // Open7
        &OPEN_8_12,            // Open8
        &MINE_12,              // Mine
        &EXPLOSION_12,         // Explosion
    },
    {
        // 8 px -- EXPERT (30x16).
        &COVERED_8,            // Covered
        &COVERED_FLAG_8,       // CoveredFlag
        &COVERED_QUESTION_8,   // CoveredQuestion
        &OPEN_0_8,             // Open0
        &OPEN_1_8,             // Open1
        &OPEN_2_8,             // Open2
        &OPEN_3_8,             // Open3
        &OPEN_4_8,             // Open4
        &OPEN_5_8,             // Open5
        &OPEN_6_8,             // Open6
        &OPEN_7_8,             // Open7
        &OPEN_8_8,             // Open8
        &MINE_8,               // Mine
        &EXPLOSION_8,          // Explosion
    },
};

/** Cursor frames: one row per native tile size, phase A then phase B. */
constexpr const Sprite4bpp* kCursors[kSizeCount][kCursorPhaseCount] = {
    {&CURSOR_A_16, &CURSOR_B_16},
    {&CURSOR_A_12, &CURSOR_B_12},
    {&CURSOR_A_8, &CURSOR_B_8},
};

/**
 * @brief Header faces, indexed by SmileyId.
 *
 * The band and both overlays are fixed-pixel geometry, so there is no size
 * axis here and the array is one-dimensional.
 */
constexpr const Sprite4bpp* kSmileys[kSmileyCount] = {
    &SMILEY_NORMAL_16,     // Normal
    &SMILEY_COOL_16,       // Cool
    &SMILEY_SURPRISED_16,  // Surprised
    &SMILEY_DEAD_16,       // Dead
};

/** Decimal glyphs, indexed by value: kDigits[3] is '3'. */
constexpr const Sprite4bpp* kDigits[kDigitCount] = {
    &DIGIT_0_8, &DIGIT_1_8, &DIGIT_2_8, &DIGIT_3_8, &DIGIT_4_8,
    &DIGIT_5_8, &DIGIT_6_8, &DIGIT_7_8, &DIGIT_8_8, &DIGIT_9_8,
};

/**
 * @brief Characters kLetters is indexed by, in the same order.
 *
 * Kept beside the sprite row so a glyph cannot be swapped without its
 * character: letterSprite() walks this string instead of a second switch.
 */
constexpr char kLetterChars[kLetterCount + 1] = "MINEST";

/** Letter glyphs, in kLetterChars order. */
constexpr const Sprite4bpp* kLetters[kLetterCount] = {
    &LETTER_M_8, &LETTER_I_8, &LETTER_N_8,
    &LETTER_E_8, &LETTER_S_8, &LETTER_T_8,
};

/** 9-slice frame pieces, in PanelPiece order. */
constexpr const Sprite4bpp* kPanelPieces[kPanelPieceCount] = {
    &PANEL_TL_8,  // TopLeft
    &PANEL_T_8,   // Top
    &PANEL_TR_8,  // TopRight
    &PANEL_L_8,   // Left
    &PANEL_R_8,   // Right
    &PANEL_BL_8,  // BottomLeft
    &PANEL_B_8,   // Bottom
    &PANEL_BR_8,  // BottomRight
};

/**
 * @brief Reports whether every table cell is populated.
 *
 * A null entry would otherwise surface only when a player reached that one
 * tile at that one difficulty, which is exactly the kind of bug the sprite
 * pipeline is meant to make impossible.
 *
 * @return True when all tile ids and cursor phases exist at all three sizes.
 */
constexpr bool allSpritesFilled() {
    for (std::uint8_t size = 0; size < kSizeCount; ++size) {
        for (std::uint8_t id = 0; id < kTileIdCount; ++id) {
            if (kTiles[size][id] == nullptr) {
                return false;
            }
        }
        for (std::uint8_t phase = 0; phase < kCursorPhaseCount; ++phase) {
            if (kCursors[size][phase] == nullptr) {
                return false;
            }
        }
    }
    return true;
}

// Row width must equal the TileId count: a new enumerator without art would
// leave that column short and every later column shifted.
static_assert(sizeof(kTiles[0]) / sizeof(kTiles[0][0]) == kTileIdCount,
              "kTiles needs exactly one column per TileId value");
static_assert(sizeof(kCursors[0]) / sizeof(kCursors[0][0]) == kCursorPhaseCount,
              "kCursors needs exactly one column per animation phase");
// The largest lookup index is the last enumerator, so preserving that bound is
// what keeps tileSprite()'s runtime guard from ever rejecting a real id.
static_assert(static_cast<std::uint8_t>(TileId::Explosion) < kTileIdCount,
              "The last TileId must stay inside a kTiles row");
static_assert(allSpritesFilled(),
              "Every TileId and cursor phase needs art at 16, 12 and 8 px");

/**
 * @brief Reports whether every header-band and panel table cell is populated.
 *
 * Same reasoning as allSpritesFilled(): a null would surface only in one
 * overlay state, so the completeness of the generated set is asserted here.
 *
 * @return True when every smiley, digit, letter and panel piece has art.
 */
constexpr bool allHudSpritesFilled() {
    for (std::uint8_t i = 0; i < kSmileyCount; ++i) {
        if (kSmileys[i] == nullptr) {
            return false;
        }
    }
    for (std::uint8_t i = 0; i < kDigitCount; ++i) {
        if (kDigits[i] == nullptr) {
            return false;
        }
    }
    for (std::uint8_t i = 0; i < kLetterCount; ++i) {
        if (kLetters[i] == nullptr) {
            return false;
        }
    }
    for (std::uint8_t i = 0; i < kPanelPieceCount; ++i) {
        if (kPanelPieces[i] == nullptr) {
            return false;
        }
    }
    return true;
}

// The character row and the sprite row are two parallel lists; a mismatch
// would make letterSprite('E') hand back the glyph authored for another letter.
static_assert(sizeof(kLetterChars) / sizeof(kLetterChars[0]) == kLetterCount + 1,
              "kLetterChars needs one character per kLetters entry, plus NUL");
static_assert(static_cast<std::uint8_t>(SmileyId::Dead) < kSmileyCount,
              "The last SmileyId must stay inside kSmileys");
static_assert(static_cast<std::uint8_t>(PanelPiece::BottomRight) < kPanelPieceCount,
              "The last PanelPiece must stay inside kPanelPieces");
static_assert(allHudSpritesFilled(),
              "Every smiley, digit, letter and panel piece needs its generated art");

/**
 * @brief Table row for a native pixel size.
 * @param pixels Cell edge in pixels.
 * @return 0 for 16 px, 1 for 12 px, 2 for 8 px, -1 for any other size.
 */
constexpr int rowForPixels(std::uint8_t pixels) {
    switch (pixels) {
        case 16:
            return 0;
        case 12:
            return 1;
        case 8:
            return 2;
        default:
            return -1;
    }
}

}  // namespace

const Sprite4bpp* tileSprite(TileId id, std::uint8_t pixels) {
    const int row = rowForPixels(pixels);
    const std::uint8_t column = static_cast<std::uint8_t>(id);
    if (row < 0 || column >= kTileIdCount) {
        return nullptr;
    }
    return kTiles[row][column];
}

const Sprite4bpp* cursorSprite(std::uint8_t phase, std::uint8_t pixels) {
    const int row = rowForPixels(pixels);
    if (row < 0 || phase >= kCursorPhaseCount) {
        return nullptr;
    }
    return kCursors[row][phase];
}

const Sprite4bpp* smileySprite(SmileyId id) {
    const auto index = static_cast<std::uint8_t>(id);
    if (index >= kSmileyCount) {
        return nullptr;
    }
    return kSmileys[index];
}

const Sprite4bpp* digitSprite(char digit) {
    // Bounds are checked in character space, not by nibbling the char: the
    // subtraction below is only meaningful for the ten ASCII digits.
    if (digit < '0' || digit > '9') {
        return nullptr;
    }
    return kDigits[static_cast<std::uint8_t>(digit - '0')];
}

const Sprite4bpp* letterSprite(char letter) {
    for (std::uint8_t i = 0; i < kLetterCount; ++i) {
        if (kLetterChars[i] == letter) {
            return kLetters[i];
        }
    }
    return nullptr;
}

const Sprite4bpp* panelPieceSprite(PanelPiece piece) {
    const auto index = static_cast<std::uint8_t>(piece);
    if (index >= kPanelPieceCount) {
        return nullptr;
    }
    return kPanelPieces[index];
}

}  // namespace minesweeper
