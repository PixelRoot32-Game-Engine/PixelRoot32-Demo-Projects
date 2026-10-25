/*
 * Unit tests for the engine-free presentation mapping (S5, S7, S8, S10).
 *
 * These cases pin the rules-to-sprite decisions that the drawing layer will
 * consume: which tile a cell shows, which smiley the header shows, the native
 * cell size per difficulty, the three-digit counter format and the cursor
 * animation phase. Nothing here touches the engine, so the suite runs on the
 * host with no SDL2.
 */
#include <unity.h>

#include <cstring>

#include "presentation/PresentationMap.h"

using namespace minesweeper;

namespace {

/** Builds a cell without relying on aggregate-init order at the call site. */
Cell makeCell(CellState state, std::uint8_t adjacentMines, bool hasMine) {
    Cell cell;
    cell.adjacentMines = adjacentMines;
    cell.state = state;
    cell.hasMine = hasMine;
    return cell;
}

/** Logical screen edge in pixels; src/GameConstants.h is engine-dependent. */
constexpr int LOGICAL_SCREEN = 240;

}  // namespace

// --- tileFor ----------------------------------------------------------------

void test_tileFor_hidden_is_covered(void) {
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(TileId::Covered),
                            static_cast<std::uint8_t>(
                                tileFor(makeCell(CellState::Hidden, 0, false), false, false)));
}

void test_tileFor_flagged_is_covered_flag(void) {
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(TileId::CoveredFlag),
                            static_cast<std::uint8_t>(
                                tileFor(makeCell(CellState::Flagged, 0, false), false, false)));
}

void test_tileFor_open_zero_is_open0(void) {
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(TileId::Open0),
                            static_cast<std::uint8_t>(tileFor(
                                makeCell(CellState::Revealed, 0, false), false, false)));
}

void test_tileFor_open_three_is_open3(void) {
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(TileId::Open3),
                            static_cast<std::uint8_t>(tileFor(
                                makeCell(CellState::Revealed, 3, false), false, false)));
}

void test_tileFor_open_eight_is_open8(void) {
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(TileId::Open8),
                            static_cast<std::uint8_t>(tileFor(
                                makeCell(CellState::Revealed, 8, false), false, false)));
}

void test_tileFor_nine_adjacent_clamps_to_open8(void) {
    // No digit tile above 8 exists; a 9 must not fall off the enum.
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(TileId::Open8),
                            static_cast<std::uint8_t>(tileFor(
                                makeCell(CellState::Revealed, 9, false), false, false)));
}

void test_tileFor_revealed_mine_is_mine(void) {
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(TileId::Mine),
                            static_cast<std::uint8_t>(tileFor(
                                makeCell(CellState::Revealed, 0, true), false, false)));
}

void test_tileFor_hidden_mine_needs_reveal(void) {
    const Cell mine = makeCell(CellState::Hidden, 0, true);
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(TileId::Covered),
                            static_cast<std::uint8_t>(tileFor(mine, false, false)));
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(TileId::Mine),
                            static_cast<std::uint8_t>(tileFor(mine, true, false)));
}

void test_tileFor_detonated_beats_every_rule(void) {
    // Same cell would otherwise resolve to Covered, CoveredFlag, Open3 or Mine.
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(TileId::Explosion),
                            static_cast<std::uint8_t>(tileFor(
                                makeCell(CellState::Hidden, 0, true), true, true)));
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(TileId::Explosion),
                            static_cast<std::uint8_t>(tileFor(
                                makeCell(CellState::Flagged, 0, false), true, true)));
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(TileId::Explosion),
                            static_cast<std::uint8_t>(tileFor(
                                makeCell(CellState::Revealed, 3, false), true, true)));
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(TileId::Explosion),
                            static_cast<std::uint8_t>(tileFor(
                                makeCell(CellState::Revealed, 0, true), true, true)));
}

// --- smileyFor --------------------------------------------------------------

void test_smileyFor_won_is_cool(void) {
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(SmileyId::Cool),
                            static_cast<std::uint8_t>(smileyFor(GameStatus::Won)));
}

void test_smileyFor_lost_is_dead(void) {
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(SmileyId::Dead),
                            static_cast<std::uint8_t>(smileyFor(GameStatus::Lost)));
}

void test_smileyFor_ready_and_playing_are_normal(void) {
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(SmileyId::Normal),
                            static_cast<std::uint8_t>(smileyFor(GameStatus::Ready)));
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(SmileyId::Normal),
                            static_cast<std::uint8_t>(smileyFor(GameStatus::Playing)));
}

// --- cellPixelsFor ----------------------------------------------------------

void test_cellPixelsFor_native_sizes(void) {
    TEST_ASSERT_EQUAL_UINT8(16, cellPixelsFor(Difficulty::Beginner));
    TEST_ASSERT_EQUAL_UINT8(12, cellPixelsFor(Difficulty::Intermediate));
    TEST_ASSERT_EQUAL_UINT8(8, cellPixelsFor(Difficulty::Expert));
}

void test_cellPixelsFor_every_board_fits_the_screen(void) {
    const Difficulty levels[3] = {Difficulty::Beginner, Difficulty::Intermediate,
                                  Difficulty::Expert};
    for (std::uint8_t i = 0; i < 3; ++i) {
        const BoardConfig config = boardConfigFor(levels[i]);
        const std::uint8_t cell = cellPixelsFor(levels[i]);
        TEST_ASSERT_LESS_OR_EQUAL_UINT16(
            LOGICAL_SCREEN, static_cast<std::uint16_t>(config.width) * cell);
        TEST_ASSERT_LESS_OR_EQUAL_UINT16(
            LOGICAL_SCREEN, static_cast<std::uint16_t>(config.height) * cell);
    }
}

// --- formatCounter ----------------------------------------------------------

void test_formatCounter_pads_to_three_digits(void) {
    char out[4];

    formatCounter(out, 0);
    TEST_ASSERT_EQUAL_CHAR('0', out[0]);
    TEST_ASSERT_EQUAL_CHAR('0', out[1]);
    TEST_ASSERT_EQUAL_CHAR('0', out[2]);
    TEST_ASSERT_EQUAL_CHAR('\0', out[3]);

    formatCounter(out, 7);
    TEST_ASSERT_EQUAL_CHAR('0', out[0]);
    TEST_ASSERT_EQUAL_CHAR('0', out[1]);
    TEST_ASSERT_EQUAL_CHAR('7', out[2]);
    TEST_ASSERT_EQUAL_CHAR('\0', out[3]);

    formatCounter(out, 10);
    TEST_ASSERT_EQUAL_CHAR('0', out[0]);
    TEST_ASSERT_EQUAL_CHAR('1', out[1]);
    TEST_ASSERT_EQUAL_CHAR('0', out[2]);
    TEST_ASSERT_EQUAL_CHAR('\0', out[3]);

    formatCounter(out, 99);
    TEST_ASSERT_EQUAL_CHAR('0', out[0]);
    TEST_ASSERT_EQUAL_CHAR('9', out[1]);
    TEST_ASSERT_EQUAL_CHAR('9', out[2]);
    TEST_ASSERT_EQUAL_CHAR('\0', out[3]);
}

void test_formatCounter_keeps_three_digits_and_clamps_above_999(void) {
    char out[4];

    formatCounter(out, 999);
    TEST_ASSERT_EQUAL_CHAR('9', out[0]);
    TEST_ASSERT_EQUAL_CHAR('9', out[1]);
    TEST_ASSERT_EQUAL_CHAR('9', out[2]);
    TEST_ASSERT_EQUAL_CHAR('\0', out[3]);

    formatCounter(out, 1000);
    TEST_ASSERT_EQUAL_CHAR('9', out[0]);
    TEST_ASSERT_EQUAL_CHAR('9', out[1]);
    TEST_ASSERT_EQUAL_CHAR('9', out[2]);
    TEST_ASSERT_EQUAL_CHAR('\0', out[3]);
}

// --- cursorPhase ------------------------------------------------------------

void test_cursorPhase_alternates_every_300ms(void) {
    TEST_ASSERT_EQUAL_UINT8(0, cursorPhase(0));
    TEST_ASSERT_EQUAL_UINT8(0, cursorPhase(299));
    TEST_ASSERT_EQUAL_UINT8(1, cursorPhase(300));
    TEST_ASSERT_EQUAL_UINT8(1, cursorPhase(599));
    TEST_ASSERT_EQUAL_UINT8(0, cursorPhase(600));
}

// --- menu rows --------------------------------------------------------------

/**
 * The engine's UIButton keeps a non-owning string_view and has no label
 * setter, so a row's text is rewritten in place when a best time appears.
 * That is only safe while every rewrite has the same character count, which
 * is what menuRowWidth() promises and these cases pin.
 */
void test_menuRowWidth_matches_every_formatted_row(void) {
    struct Row {
        const char* name;
        std::uint8_t width;
        std::uint8_t height;
        std::uint16_t mines;
    };
    const Row rows[3] = {
        {"EASY", 9, 9, 10},
        {"MEDIUM", 16, 16, 40},
        {"EXPERT", 30, 16, 99},
    };
    const std::uint16_t bests[4] = {0, 7, 999, 1000};

    for (int r = 0; r < 3; ++r) {
        const std::size_t expected =
            menuRowWidth(rows[r].name, rows[r].width, rows[r].height, rows[r].mines);
        for (int b = 0; b < 4; ++b) {
            char out[48];
            const std::size_t written = formatMenuRow(out, sizeof(out), rows[r].name, rows[r].width,
                                                      rows[r].height, rows[r].mines, bests[b]);
            TEST_ASSERT_EQUAL_UINT32(expected, written);
            TEST_ASSERT_EQUAL_UINT32(expected, static_cast<std::uint32_t>(strlen(out)));
        }
    }
}

void test_menuRow_marks_a_missing_best_and_zero_pads_the_rest(void) {
    char out[48];

    TEST_ASSERT_GREATER_THAN(0, formatMenuRow(out, sizeof(out), "EASY", 9, 9, 10, 0));
    TEST_ASSERT_NOT_NULL(strstr(out, "EASY 9X9-10 BEST ---"));

    TEST_ASSERT_GREATER_THAN(0, formatMenuRow(out, sizeof(out), "EASY", 9, 9, 10, 42));
    TEST_ASSERT_NOT_NULL(strstr(out, "EASY 9X9-10 BEST 042"));

    TEST_ASSERT_GREATER_THAN(0, formatMenuRow(out, sizeof(out), "EXPERT", 30, 16, 99, 1000));
    TEST_ASSERT_NOT_NULL(strstr(out, "EXPERT 30X16-99 BEST 999"));
}

void test_menuRow_refuses_a_short_buffer(void) {
    char out[8];
    TEST_ASSERT_EQUAL_UINT32(0, formatMenuRow(out, sizeof(out), "MEDIUM", 16, 16, 40, 12));
    TEST_ASSERT_EQUAL_STRING("", out);
}

// --- Runner -----------------------------------------------------------------

void setUp(void) {}
void tearDown(void) {}

int main(void) {
    UNITY_BEGIN();

    RUN_TEST(test_tileFor_hidden_is_covered);
    RUN_TEST(test_tileFor_flagged_is_covered_flag);
    RUN_TEST(test_tileFor_open_zero_is_open0);
    RUN_TEST(test_tileFor_open_three_is_open3);
    RUN_TEST(test_tileFor_open_eight_is_open8);
    RUN_TEST(test_tileFor_nine_adjacent_clamps_to_open8);
    RUN_TEST(test_tileFor_revealed_mine_is_mine);
    RUN_TEST(test_tileFor_hidden_mine_needs_reveal);
    RUN_TEST(test_tileFor_detonated_beats_every_rule);

    RUN_TEST(test_smileyFor_won_is_cool);
    RUN_TEST(test_smileyFor_lost_is_dead);
    RUN_TEST(test_smileyFor_ready_and_playing_are_normal);

    RUN_TEST(test_cellPixelsFor_native_sizes);
    RUN_TEST(test_cellPixelsFor_every_board_fits_the_screen);

    RUN_TEST(test_formatCounter_pads_to_three_digits);
    RUN_TEST(test_formatCounter_keeps_three_digits_and_clamps_above_999);

    RUN_TEST(test_cursorPhase_alternates_every_300ms);

    RUN_TEST(test_menuRowWidth_matches_every_formatted_row);
    RUN_TEST(test_menuRow_marks_a_missing_best_and_zero_pads_the_rest);
    RUN_TEST(test_menuRow_refuses_a_short_buffer);

    return UNITY_END();
}
