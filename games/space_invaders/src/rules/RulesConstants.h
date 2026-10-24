#pragma once

#include <cstdint>

namespace spaceinvaders::rules {

/**
 * @file RulesConstants.h
 * @brief Reference geometry, timing and scoring numbers for the 1978 rules.
 *
 * This layer is deliberately engine-free: it includes <cstdint> only, so the
 * same numbers compile for the host test environment and for both device
 * targets without pulling in a renderer, an allocator or a singleton.
 */

/// Playfield width in arcade pixels, drawn 1:1 inside the 240 px panel.
constexpr int FieldWidth = 224;
/// Playfield height after re-pitching the vertical layout into the panel.
constexpr int FieldHeight = 240;

/// Rack rows and columns: 5 x 11 = 55 aliens.
constexpr int RackRows = 5;
constexpr int RackCols = 11;
/// Height of every alien mask: all three variants are eight pixels tall (S2).
constexpr int AlienSpriteHeight = 8;
/// Horizontal and vertical pitch between adjacent rack cells.
constexpr int RackPitchX = 16;
constexpr int RackPitchY = 16;
/// Vertical extent of the full formation: the four gaps between its five rows
/// plus the bottom row's sprite.
constexpr int RackHeight = (RackRows - 1) * RackPitchY + AlienSpriteHeight;
/// Top-left start of the formation inside the playfield.
constexpr int RackOriginX = 26;
constexpr int RackOriginY = 48;
/// Horizontal shift per completed sweep, the one-alien shift, and the edge drop.
constexpr int RackStepX = 2;
constexpr int RackLastStepX = 3;
constexpr int RackDropY = 8;
/// Extra drop a new wave starts with (S12). It is the edge drop by design: the
/// reference never distinguishes the two, so a run accumulates one 8 px step per
/// cleared rack and per edge turn alike.
constexpr int RackWaveDropY = RackDropY;
/// Deepest formation top the field can hold. It is also well below the point
/// where a freshly spawned wave already counts as an invasion (S13), so the
/// per-wave drop saturates here: no run can reach a wave any lower than this.
constexpr int RackMaxOriginY = FieldHeight - RackHeight;
/// Outer x bounds the rack may occupy before it has to reverse.
constexpr int RackMinLeft = 2;
constexpr int RackMaxRight = 222;
/// Ticks the rack stays frozen while an alien explosion plays (S5). The sweep
/// resumes exactly where it stopped, so the hold costs time but never distance.
constexpr uint16_t RackHoldTicks = 10;

/// Ticks the formation stays frozen while the cannon is dead (S14).
///
/// The value is documented, not measured: Computer Archaeology's disassembly of
/// the player task describes the reference flipping between two images for half
/// a second, and half a second at this 60 Hz step is 30 ticks. The exact ROM
/// flip cadence is not documented, so comparing this count against a reference
/// capture is still pending (T6.4); until that measurement exists, 30 is the
/// stated "half a second" and no finer cadence is claimed.
constexpr uint16_t PlayerDeathHoldTicks = 30;

/// Cannon sprite size and its one-pixel-per-tick movement clamp.
constexpr int CannonWidth = 13;
constexpr int CannonHeight = 8;
constexpr int CannonStepX = 1;
constexpr int CannonMaxX = FieldWidth - CannonWidth;

/// Player shot travel in pixels per tick (S8). It is a per-tick step rather than
/// a velocity, so the shot covers the field in a fixed number of ticks.
constexpr int PlayerShotStepY = 4;

/// Enemy-shot step sizes and cadence.
constexpr int EnemyShotStepY = 4;
constexpr int EnemyShotFastStepY = 5;
constexpr int EnemyShotTicksPerStep = 3;
constexpr int EnemyShotMax = 3;

/// Saucer period, minimum alien count, step and score-table length.
constexpr int SaucerPeriodTicks = 1536;
constexpr int SaucerMinAliens = 8;
constexpr int SaucerStepX = 2;
constexpr int SaucerScoreCount = 15;
/// Ticks between the siren's sweeps while a saucer is on screen (S11, S17).
///
/// The reference siren is a warble, not one held tone, and the APU gives a
/// game no way to say "this voice belongs to the siren": a looping event would
/// have to be stopped by slot index, which the scene never learns. Re-firing a
/// short sweep this often produces the warble out of ordinary one-shots that
/// expire on their own. Twelve ticks is 0.2 s at the 60 Hz rules clock, so each
/// sweep is still audible when the next one replaces it.
constexpr uint16_t SaucerSirenPeriodTicks = 12;

/// Shield geometry: four 22x16 masks at fixed x offsets, one shared y.
constexpr int ShieldCount = 4;
constexpr int ShieldWidth = 22;
constexpr int ShieldHeight = 16;
constexpr int16_t ShieldX[ShieldCount] = {32, 77, 122, 167};
constexpr int16_t ShieldY = 184;

/// One extra ship at 1500 points; three ships at the start of a run.
constexpr int ExtraShipScore = 1500;
constexpr int StartLives = 3;

/**
 * @brief Clamp a cannon x to the playfield (S7).
 *
 * The cannon is continuous and steps one pixel per tick, so the field edge is
 * the only thing that limits it. Keeping the bound here means the actor has no
 * geometry of its own and a wrong limit fails a host test instead of walking
 * the cannon off the panel.
 *
 * @param x Cannon left edge in playfield coordinates.
 * @return 0 when x is negative, CannonMaxX when it is past the right edge,
 *         otherwise x unchanged.
 */
int clampCannonX(int x);

/**
 * @enum AlienType
 * @brief Rack variant; the type selects the sprite size and the score value.
 */
enum class AlienType : uint8_t { Squid, Crab, Octopus };

/**
 * @brief Sprite width in pixels for an alien variant.
 *
 * The width is what the edge check and the hit box consume, so it belongs
 * with the other reference numbers rather than in the renderer.
 *
 * @param t Alien variant.
 * @return 8 for the squid, 11 for the crab, 12 for the octopus.
 */
int8_t alienWidth(AlienType t);

/**
 * @brief Sprite height in pixels for an alien variant.
 *
 * All three variants are eight pixels tall in the reference art; the function
 * exists so callers never hard-code the constant that the art may revise.
 *
 * @param t Alien variant.
 * @return 8 for every variant.
 */
int8_t alienHeight(AlienType t);

/**
 * @brief Score awarded when an alien of this variant is destroyed.
 * @param t Alien variant.
 * @return 30 for the squid, 20 for the crab, 10 for the octopus.
 */
uint8_t alienScore(AlienType t);

/**
 * @brief Alien variant that occupies a rack row.
 *
 * Row 0 is the squid, rows 1-2 the crab and rows 3-4 the octopus, which is the
 * reference layout (S4) rather than the demo's old four-row formation.
 *
 * @param row Rack row, 0-based from the top.
 * @return Variant for that row.
 */
AlienType alienTypeForRow(int row);

}  // namespace spaceinvaders::rules
