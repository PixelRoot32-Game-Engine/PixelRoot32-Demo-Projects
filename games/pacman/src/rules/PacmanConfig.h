#pragma once

#include <cstdint>

namespace pacman {

/* ---------------------------------------------------------------------------
 * Input
 * ------------------------------------------------------------------------- */

/**
 * @brief The engine's six-button pad, in its fixed order: up, down, left,
 *        right, A, B.
 *
 * The scene maps the pad onto these ids; the rules layer never talks to the
 * input device itself.
 */
constexpr std::uint8_t kBtnUp = 0;
constexpr std::uint8_t kBtnDown = 1;
constexpr std::uint8_t kBtnLeft = 2;
constexpr std::uint8_t kBtnRight = 3;
constexpr std::uint8_t kBtnA = 4;
constexpr std::uint8_t kBtnB = 5;

/* ---------------------------------------------------------------------------
 * Maze geometry
 * ------------------------------------------------------------------------- */

/**
 * @brief Columns in the level layout.
 *
 * The layout is 28 cells wide so that a 240-pixel screen holds it at 8 pixels
 * per cell with an 8-pixel margin on each side.
 */
constexpr int kMazeCols = 28;

/**
 * @brief Rows in the level layout.
 *
 * 26 rows of 8 pixels is 208 pixels, which leaves 24 pixels for the status
 * band and 8 pixels of margin on a 240-pixel screen.
 */
constexpr int kMazeRows = 26;

/**
 * @brief Total number of cells in the maze.
 */
constexpr int kMazeCells = kMazeCols * kMazeRows;

/**
 * @brief Edge length of one maze cell, in pixels.
 */
constexpr int kCellSize = 8;

/**
 * @brief Height of the status band above the maze, in pixels.
 *
 * Three rows of the engine's 5x7 font (8 pixels per line) leave room for a
 * label row and a value row, the same shape the arcade original uses. The
 * band never overlaps the playfield.
 */
constexpr int kStatusBandHeight = 24;

/**
 * @brief Row of the maze that holds the side tunnels.
 *
 * The tunnel row sits just below the middle of the 26-row layout, which keeps
 * the ghost house visually centred.
 */
constexpr int kTunnelRow = 14;

/* ---------------------------------------------------------------------------
 * Level configuration
 * ------------------------------------------------------------------------- */

/**
 * @brief Column of the player's starting cell.
 */
constexpr int kPlayerStartCol = 13;

/**
 * @brief Row of the player's starting cell.
 *
 * The player starts on the corridor that runs directly below the ghost house,
 * the same anchor the arcade original uses.
 */
constexpr int kPlayerStartRow = 17;

/**
 * @brief Number of enemies in the level.
 */
constexpr int kEnemyCount = 4;

/**
 * @brief Starting columns of the enemies, in enemy order.
 *
 * The first enemy waits outside the house, above the doorway; the other three
 * wait inside it. Enemies that start inside the house have to leave before
 * they can hunt.
 */
constexpr int kEnemyStartCols[kEnemyCount] = {13, 12, 14, 15};

/**
 * @brief Starting rows of the enemies, in enemy order.
 */
constexpr int kEnemyStartRows[kEnemyCount] = {11, 14, 14, 14};

/* ---------------------------------------------------------------------------
 * Timing
 * ------------------------------------------------------------------------- */

/**
 * @brief Duration of one logic step, in milliseconds.
 *
 * 20 ms is exactly 50 logic steps per second. Every rule below is expressed
 * in steps rather than milliseconds so that the simulation is reproducible
 * regardless of how often the screen is redrawn.
 */
constexpr int kLogicStepMs = 20;

/**
 * @brief Maximum number of logic steps a single frame may run.
 *
 * A frame that arrives after a long pause catches up at most this many steps
 * (80 ms) and then drops the backlog, so a stall cannot make the game
 * fast-forward.
 */
constexpr int kMaxLogicStepsPerFrame = 4;

/**
 * @brief Logic steps the player needs to cross one cell.
 *
 * 8 steps is 160 ms per cell, which is roughly six cells per second.
 */
constexpr int kPlayerStepsPerCell = 8;

/**
 * @brief Logic steps an enemy needs to cross one cell.
 *
 * 10 steps is 200 ms per cell. The enemy is deliberately slower than the
 * player, so a player who knows the maze can always pull away.
 */
constexpr int kEnemyStepsPerCell = 10;

/**
 * @brief Number of logic steps between two changes of the player's mouth.
 *
 * The mouth alternates between open and closed every three sub-steps, which
 * is roughly the pace the arcade original uses.
 */
constexpr int kMouthAnimationDivisor = 3;

/**
 * @brief Column of the cell just outside the ghost house doorway.
 *
 * A hunter that is released from the house walks to this cell before it
 * joins the maze.
 */
constexpr int kHouseExitCol = 13;

/**
 * @brief Row of the cell just outside the ghost house doorway.
 */
constexpr int kHouseExitRow = 11;

/**
 * @brief Column of each hunter's own corner, in enemy order.
 *
 * The corners are where a hunter patrols before it starts hunting and where
 * the timid hunter retreats to.
 */
constexpr int kCornerCols[kEnemyCount] = {1, 26, 1, 26};

/**
 * @brief Row of each hunter's own corner, in enemy order.
 */
constexpr int kCornerRows[kEnemyCount] = {1, 1, 24, 24};

/**
 * @brief Logic steps a hunter patrols its own corner before it starts
 *        hunting.
 *
 * 200 steps is 4 seconds. The pause gives the player a moment to get
 * oriented, the same way the arcade original opens a level.
 */
constexpr int kScatterSteps = 200;

/**
 * @brief Cells the timid hunter looks ahead of the player.
 */
constexpr int kAmbusherLookAhead = 4;

/**
 * @brief Cells the flanking hunter looks ahead of the player.
 */
constexpr int kFlankerLookAhead = 2;

/**
 * @brief Distance, in cells, beyond which the timid hunter hunts the player.
 *
 * Closer than this it retreats to its own corner instead.
 */
constexpr int kTimidHuntDistance = 8;

/**
 * @brief Duration of the power effect, in logic steps.
 *
 * 300 steps is 6 seconds.
 */
constexpr int kPowerDurationSteps = 300;

/**
 * @brief Duration of the post-death invulnerability, in logic steps.
 *
 * 100 steps is 2 seconds.
 */
constexpr int kInvulnerableSteps = 100;

/**
 * @brief Length of the respawn sequence, in logic steps.
 *
 * The board is frozen for this long after a life is lost, and for the same
 * length at the start of a game: the "ready" beat the arcade original shows
 * before the player is allowed to move. Nothing moves and no contact is
 * resolved while the sequence runs.
 */
constexpr int kRespawnSteps = 60;

/**
 * @brief How long a defeated enemy waits in the house before it re-enters play.
 *
 * 50 steps is 1 second.
 */
constexpr int kEnemyRespawnDelaySteps = 50;

/**
 * @brief How long a newly released enemy waits inside the house before it
 *        leaves, in logic steps.
 *
 * The three enemies that start inside the house are released one after
 * another, so the player is not greeted by four hunters at once.
 */
constexpr int kEnemyReleaseDelaySteps[kEnemyCount] = {0, 50, 150, 250};

/* ---------------------------------------------------------------------------
 * Scoring
 * ------------------------------------------------------------------------- */

/**
 * @brief Points awarded for one dot.
 */
constexpr int kScoreDot = 10;

/**
 * @brief Points awarded for one power pellet.
 */
constexpr int kScorePower = 50;

/**
 * @brief Points awarded for defeating an enemy, before the chain multiplier.
 *
 * Consecutive defeats inside one power effect multiply this value by 2, 4, 8
 * and 16, capped at the fourth defeat.
 */
constexpr int kScoreEnemy = 200;

/**
 * @brief Score at which the player earns an extra life.
 */
constexpr int kScoreExtraLife = 10000;

/**
 * @brief Number of lives the player starts with.
 */
constexpr int kStartingLives = 3;

/**
 * @brief Maximum number of defeats that still increase the score multiplier.
 */
constexpr int kEnemyChainLength = 4;

}  // namespace pacman
