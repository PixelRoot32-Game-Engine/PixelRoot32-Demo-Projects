/*
 * Behaviour tests for the boot/failure screen taxonomy and painter.
 *
 * The pixel record itself is gated by tools/boot_check.py (the firmware's own
 * painter compiled on the host and hashed). This suite owns what a hash cannot
 * state: every `doom_whd_source` code reaches a non-`None` reason, the taxonomy's
 * text obeys the font's alphabet and the screen's width, the two builders
 * produce the documented screens, and the painter stays inside a 1x1 and a
 * 320x200 buffer.
 *
 * Engine-free by construction: it links only src/doomc/BootScreen.cpp and
 * src/doomc/LevelFailure.cpp, so a stray engine include or allocation cannot
 * compile here. `doomc/WhdSource.h` contributes macros only; no flash adapter
 * source is built.
 */
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#include <unity.h>

#include "doomc/BootScreen.h"
#include "doomc/LevelFailure.h"
#include "doomc/WhdSource.h"

#include "../../test_config.h"

using doom::doomc::BootScreen;
using doom::doomc::LevelFailure;
using doom::doomc::LevelFailureReason;
using doom::doomc::kLevelFailureReasonCount;
using doom::doomc::kLevelFailureReasons;

void setUp(void) { test_setup(); }
void tearDown() { test_teardown(); }

namespace {

constexpr int kWidth = 320;
constexpr int kHeight = 200;
constexpr int kMaxLine = 25;

/// One character the 5x7 uppercase font can draw; `\n` is a table wrap point.
bool fontCharacter(char character) {
    const unsigned char code = static_cast<unsigned char>(character);
    return code == '\n' || (code >= 32u && code <= 95u);
}

/// Every rendered line in @p text, in the same split the painter performs.
int renderedLines(const char* text, bool (*visit)(const char*, int, void*), void* context) {
    if (text == nullptr) {
        return 0;
    }
    int count = 0;
    const char* start = text;
    for (const char* at = text;; ++at) {
        if (*at == '\n' || *at == '\0') {
            if (visit != nullptr) {
                visit(start, static_cast<int>(at - start), context);
            }
            ++count;
            if (*at == '\0') {
                break;
            }
            start = at + 1;
        }
    }
    return count;
}

struct LineCheck {
    bool valid = true;
};

bool checkLine(const char* start, int length, void* context) {
    LineCheck* check = static_cast<LineCheck*>(context);
    if (length > kMaxLine) {
        check->valid = false;
    }
    for (int index = 0; index < length; ++index) {
        if (!fontCharacter(start[index])) {
            check->valid = false;
        }
    }
    return true;
}

bool lineFitsAndIsDrawable(const char* text) {
    if (text == nullptr || text[0] == '\0') {
        return false;
    }
    LineCheck check;
    renderedLines(text, checkLine, &check);
    return check.valid;
}

bool contains(const char* haystack, const char* needle) {
    return haystack != nullptr && std::strstr(haystack, needle) != nullptr;
}

}  // namespace

// --- The WHD mapping --------------------------------------------------------

void test_whd_codes_reach_a_reason() {
    TEST_ASSERT_EQUAL_INT(static_cast<int>(LevelFailureReason::None),
                          static_cast<int>(
                              doom::doomc::levelFailureFromWhd(DOOM_WHD_SOURCE_OK).reason));
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(LevelFailureReason::PartitionMissing),
        static_cast<int>(
            doom::doomc::levelFailureFromWhd(DOOM_WHD_SOURCE_ERR_PARTITION_MISSING).reason));
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(LevelFailureReason::PartitionMapFailed),
        static_cast<int>(
            doom::doomc::levelFailureFromWhd(DOOM_WHD_SOURCE_ERR_MMAP_FAILED).reason));
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(LevelFailureReason::ContainerInvalid),
        static_cast<int>(
            doom::doomc::levelFailureFromWhd(DOOM_WHD_SOURCE_ERR_CONTAINER_INVALID).reason));
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(LevelFailureReason::Unavailable),
        static_cast<int>(
            doom::doomc::levelFailureFromWhd(DOOM_WHD_SOURCE_ERR_UNAVAILABLE).reason));

    // Every code the adapter declares, including the accessor-only ones, must
    // reach some reason: an unclassified code would put `None` on the panel and
    // claim the level opened. The code itself is preserved for the serial line.
    for (int code = 0; code <= 11; ++code) {
        const LevelFailure failure = doom::doomc::levelFailureFromWhd(code);
        TEST_ASSERT_EQUAL_INT(code, failure.code);
        if (code == DOOM_WHD_SOURCE_OK) {
            TEST_ASSERT_EQUAL_INT(static_cast<int>(LevelFailureReason::None),
                                  static_cast<int>(failure.reason));
        } else {
            TEST_ASSERT_TRUE(failure.reason != LevelFailureReason::None);
        }
    }
}

// --- The taxonomy's text ----------------------------------------------------

void test_names_and_text_are_distinct_per_reason() {
    for (int left = 0; left < kLevelFailureReasonCount; ++left) {
        const LevelFailure a{kLevelFailureReasons[left], 0, {}};
        TEST_ASSERT_TRUE(doom::doomc::levelFailureName(a.reason)[0] != '\0');

        for (int right = left + 1; right < kLevelFailureReasonCount; ++right) {
            const LevelFailure b{kLevelFailureReasons[right], 0, {}};
            TEST_ASSERT_TRUE(std::strcmp(doom::doomc::levelFailureName(a.reason),
                                         doom::doomc::levelFailureName(b.reason))
                             != 0);
            TEST_ASSERT_TRUE(std::strcmp(doom::doomc::levelFailureTitle(a),
                                         doom::doomc::levelFailureTitle(b))
                             != 0);
            TEST_ASSERT_TRUE(std::strcmp(doom::doomc::levelFailureBody(a),
                                         doom::doomc::levelFailureBody(b))
                             != 0);
            TEST_ASSERT_TRUE(std::strcmp(doom::doomc::levelFailureHint(a),
                                         doom::doomc::levelFailureHint(b))
                             != 0);
        }
    }
}

void test_text_obeys_the_font_and_the_width() {
    for (int index = 0; index < kLevelFailureReasonCount; ++index) {
        const LevelFailure failure{kLevelFailureReasons[index], 0, {}};
        const char* title = doom::doomc::levelFailureTitle(failure);
        const char* body = doom::doomc::levelFailureBody(failure);
        const char* hint = doom::doomc::levelFailureHint(failure);

        TEST_ASSERT_TRUE(title != nullptr && title[0] != '\0');
        TEST_ASSERT_TRUE(body != nullptr && body[0] != '\0');
        TEST_ASSERT_TRUE(hint != nullptr && hint[0] != '\0');

        TEST_ASSERT_TRUE(lineFitsAndIsDrawable(title));
        TEST_ASSERT_TRUE(lineFitsAndIsDrawable(body));
        TEST_ASSERT_TRUE(lineFitsAndIsDrawable(hint));

        // At most two rendered lines each, as the screen's layout reserves.
        TEST_ASSERT_TRUE(renderedLines(body, nullptr, nullptr) <= 2);
        TEST_ASSERT_TRUE(renderedLines(hint, nullptr, nullptr) <= 2);

        // No sentence ends in a full stop: the panel's style is a label, not a
        // paragraph, and a trailing '.' was the slip this rule catches.
        const std::size_t bodyLength = std::strlen(body);
        const std::size_t hintLength = std::strlen(hint);
        TEST_ASSERT_TRUE(body[bodyLength - 1] != '.');
        TEST_ASSERT_TRUE(hint[hintLength - 1] != '.');
    }
}

// --- The builders -----------------------------------------------------------

void test_loading_screen_names_the_port_and_the_work() {
    const BootScreen screen = doom::doomc::bootScreenLoading("E1M1");
    TEST_ASSERT_EQUAL_STRING("DOOM", screen.title);
    TEST_ASSERT_EQUAL_STRING("PIXELROOT32 ESP32", screen.lines[0]);
    TEST_ASSERT_EQUAL_STRING("OPENING THE WAD", screen.lines[1]);
    TEST_ASSERT_EQUAL_STRING("E1M1", screen.lines[2]);
    TEST_ASSERT_NULL(screen.lines[3]);
    TEST_ASSERT_EQUAL_STRING("SHAREWARE DOOM1.WAD", screen.footer);
    TEST_ASSERT_TRUE(doom::doomc::bootScreenFits(screen, kWidth, kHeight));
}

void test_error_screen_carries_the_machine_pair() {
    for (int index = 0; index < kLevelFailureReasonCount; ++index) {
        LevelFailure failure;
        failure.reason = kLevelFailureReasons[index];
        failure.code = 3 + index;
        const char* detail = index == static_cast<int>(LevelFailureReason::Baked)
                                 ? "INDEX_OUT_OF_RANGE"
                                 : "SIDEDEFS";
        std::snprintf(failure.detail, sizeof failure.detail, "%s", detail);

        const BootScreen screen = doom::doomc::bootScreenError(failure, "E1M1");
        TEST_ASSERT_EQUAL_STRING(doom::doomc::levelFailureTitle(failure), screen.title);
        TEST_ASSERT_EQUAL_STRING(doom::doomc::levelFailureBody(failure), screen.lines[0]);
        TEST_ASSERT_EQUAL_STRING(doom::doomc::levelFailureHint(failure), screen.lines[1]);
        TEST_ASSERT_TRUE(screen.lines[2] != nullptr);
        TEST_ASSERT_EQUAL_STRING(detail, screen.lines[2]);
        TEST_ASSERT_TRUE(contains(screen.footer, "CODE"));
        TEST_ASSERT_TRUE(contains(screen.footer, "E1M1"));
        TEST_ASSERT_TRUE(doom::doomc::bootScreenFits(screen, kWidth, kHeight));
    }
}

// --- The painter's bounds ---------------------------------------------------

void test_painter_stays_inside_a_one_pixel_buffer() {
    std::uint8_t canvas[3 * 3];
    std::memset(canvas, 0xAA, sizeof canvas);

    const BootScreen screen = doom::doomc::bootScreenLoading("E1M1");
    doom::doomc::bootScreenPaint(&canvas[4], 1, 1, screen);

    TEST_ASSERT_EQUAL_HEX8(0x00, canvas[4]);
    for (int index = 0; index < 9; ++index) {
        if (index != 4) {
            TEST_ASSERT_EQUAL_HEX8(0xAA, canvas[index]);
        }
    }
}

void test_painter_stays_inside_a_320x200_buffer() {
    constexpr std::size_t kGuard = 16u;
    constexpr std::size_t kPixels = static_cast<std::size_t>(kWidth) * kHeight;
    std::vector<std::uint8_t> canvas(kPixels + 2u * kGuard, 0xAA);
    std::uint8_t* target = canvas.data() + kGuard;

    LevelFailure failure;
    failure.reason = LevelFailureReason::Baked;
    failure.code = 9;
    std::snprintf(failure.detail, sizeof failure.detail, "%s", "index_out_of_range");
    const BootScreen screen = doom::doomc::bootScreenError(failure, "E1M1");

    doom::doomc::bootScreenPaint(target, kWidth, kHeight, screen);

    for (std::size_t index = 0; index < kGuard; ++index) {
        TEST_ASSERT_EQUAL_HEX8(0xAA, canvas[index]);
        TEST_ASSERT_EQUAL_HEX8(0xAA, canvas[kGuard + kPixels + index]);
    }
    // The frame alone guarantees a visible screen; a blank result would mean the
    // painter clipped everything away.
    std::size_t lit = 0u;
    for (std::size_t index = 0; index < kPixels; ++index) {
        if (target[index] != doom::doomc::kBootBackground) {
            ++lit;
        }
    }
    TEST_ASSERT_TRUE(lit > 0u);
}

void test_fits_rejects_a_surface_that_is_too_small() {
    const BootScreen screen = doom::doomc::bootScreenLoading("E1M1");
    TEST_ASSERT_FALSE(doom::doomc::bootScreenFits(screen, 100, 50));
    TEST_ASSERT_FALSE(doom::doomc::bootScreenFits(screen, 0, kHeight));
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;

    test_setup();
    UNITY_BEGIN();

    RUN_TEST(test_whd_codes_reach_a_reason);
    RUN_TEST(test_names_and_text_are_distinct_per_reason);
    RUN_TEST(test_text_obeys_the_font_and_the_width);
    RUN_TEST(test_loading_screen_names_the_port_and_the_work);
    RUN_TEST(test_error_screen_carries_the_machine_pair);
    RUN_TEST(test_painter_stays_inside_a_one_pixel_buffer);
    RUN_TEST(test_painter_stays_inside_a_320x200_buffer);
    RUN_TEST(test_fits_rejects_a_surface_that_is_too_small);

    const int result = UNITY_END();
    test_teardown();
    return result;
}
