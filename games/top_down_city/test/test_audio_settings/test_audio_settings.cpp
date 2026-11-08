/**
 * @brief Audio settings: on/off plus one shared volume level.
 *
 * Pinned here: defaults reproduce today's mix exactly (master 0.85, SFX 0.65),
 * off silences both gains, the Start-button cycle visits every state and comes
 * back, and the payload round-trips while anything malformed is refused
 * without touching the caller's settings.
 */
#include <unity.h>

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "game/rules/AudioSettings.h"

namespace as = top_down_city::audio_settings;

void setUp() {}
void tearDown() {}

static bool same(const as::Settings& a, const as::Settings& b) {
    return a.audioOn == b.audioOn && a.level == b.level;
}

static as::Settings make(bool on, std::uint8_t level) {
    as::Settings s;
    s.audioOn = on;
    s.level = level;
    return s;
}

void test_defaults_reproduce_todays_mix() {
    const as::Settings d = as::defaults();
    TEST_ASSERT_TRUE(d.audioOn);
    TEST_ASSERT_EQUAL_FLOAT(0.85f, as::masterGain(d));
    TEST_ASSERT_EQUAL_FLOAT(0.65f, as::sfxGain(d));
}

void test_off_silences_both_gains() {
    for (std::uint8_t level = 0; level < as::kLevelCount; ++level) {
        const as::Settings off = make(false, level);
        TEST_ASSERT_EQUAL_FLOAT(0.0f, as::masterGain(off));
        TEST_ASSERT_EQUAL_FLOAT(0.0f, as::sfxGain(off));
    }
}

void test_levels_rise_and_sfx_stays_under_master() {
    for (std::uint8_t level = 0; level + 1 < as::kLevelCount; ++level) {
        TEST_ASSERT_TRUE(as::masterGain(make(true, level)) <
                         as::masterGain(make(true, level + 1)));
        TEST_ASSERT_TRUE(as::sfxGain(make(true, level)) <
                         as::sfxGain(make(true, level + 1)));
    }
    for (std::uint8_t level = 0; level < as::kLevelCount; ++level) {
        TEST_ASSERT_TRUE(as::sfxGain(make(true, level)) > 0.0f);
        TEST_ASSERT_TRUE(as::sfxGain(make(true, level)) <
                         as::masterGain(make(true, level)));
    }
}

void test_cycle_order_from_off() {
    as::Settings s = make(false, 0);
    for (std::uint8_t level = 0; level < as::kLevelCount; ++level) {
        s = as::cycle(s);
        TEST_ASSERT_TRUE(same(make(true, level), s));
    }
    s = as::cycle(s);
    TEST_ASSERT_TRUE(same(make(false, 0), s));
}

void test_cycle_visits_every_state_and_returns_to_start() {
    const as::Settings start = as::defaults();
    bool seenLevel[as::kLevelCount] = {};
    bool seenOff = false;
    as::Settings s = start;
    std::size_t steps = 0;
    do {
        if (s.audioOn) seenLevel[s.level] = true; else seenOff = true;
        s = as::cycle(s);
        ++steps;
    } while (!same(s, start) && steps < 100);
    TEST_ASSERT_TRUE(same(s, start));
    TEST_ASSERT_EQUAL_UINT32(as::kLevelCount + 1u, steps);
    TEST_ASSERT_TRUE(seenOff);
    for (std::uint8_t level = 0; level < as::kLevelCount; ++level) {
        TEST_ASSERT_TRUE(seenLevel[level]);
    }
}

void test_encode_decode_round_trip() {
    for (int on = 0; on <= 1; ++on) {
        for (std::uint8_t level = 0; level < as::kLevelCount; ++level) {
            const as::Settings in = make(on == 1, level);
            std::uint8_t buf[as::kPayloadSize] = {};
            TEST_ASSERT_EQUAL_UINT32(as::kPayloadSize,
                                     as::encode(in, buf, sizeof(buf)));
            as::Settings out = make(on == 0, 0);
            TEST_ASSERT_TRUE(as::decode(buf, sizeof(buf), out));
            TEST_ASSERT_TRUE(same(in, out));
        }
    }
}

void test_encode_refuses_small_buffer_and_null() {
    std::uint8_t buf[as::kPayloadSize] = {0xAA, 0xAA};
    TEST_ASSERT_EQUAL_UINT32(0, as::encode(as::defaults(), buf, as::kPayloadSize - 1));
    TEST_ASSERT_EQUAL_UINT8(0xAA, buf[0]);
    TEST_ASSERT_EQUAL_UINT8(0xAA, buf[1]);
    TEST_ASSERT_EQUAL_UINT32(0, as::encode(as::defaults(), nullptr, as::kPayloadSize));
}

static void expectRejected(const std::uint8_t* in, std::size_t size) {
    const as::Settings before = make(false, 1);
    as::Settings s = before;
    TEST_ASSERT_FALSE(as::decode(in, size, s));
    TEST_ASSERT_TRUE(same(before, s));
}

void test_decode_rejects_malformed_without_touching_output() {
    const std::uint8_t good[3] = {1, 0, 0};
    expectRejected(nullptr, as::kPayloadSize);
    expectRejected(good, as::kPayloadSize - 1);
    expectRejected(good, as::kPayloadSize + 1);
    expectRejected(good, 0);
    const std::uint8_t badLevel[2] = {1, as::kLevelCount};
    expectRejected(badLevel, sizeof(badLevel));
    const std::uint8_t badBool[2] = {2, 0};
    expectRejected(badBool, sizeof(badBool));
    const std::uint8_t bothBad[2] = {0xFF, 0xFF};
    expectRejected(bothBad, sizeof(bothBad));
}

void test_label_names_every_reachable_state() {
    // Walk the Start cycle from the defaults: every state must have its text.
    as::Settings s = as::defaults();
    const char* expected[] = {"VOLUME 3/3", "AUDIO OFF", "VOLUME 1/3", "VOLUME 2/3"};
    for (std::size_t i = 0; i < 4; ++i) {
        char out[as::kLabelCapacity];
        const std::size_t n = as::label(s, out, sizeof out);
        TEST_ASSERT_EQUAL_UINT32(std::strlen(expected[i]), n);
        TEST_ASSERT_EQUAL_STRING(expected[i], out);
        s = as::cycle(s);
    }
    TEST_ASSERT_TRUE(same(as::defaults(), s));
}

void test_label_refuses_small_buffer_and_null() {
    char out[as::kLabelCapacity];
    std::memset(out, 'x', sizeof out);
    TEST_ASSERT_EQUAL_UINT32(0, as::label(as::defaults(), out, 10));  // no room for the NUL
    TEST_ASSERT_EQUAL_UINT8('x', static_cast<std::uint8_t>(out[0]));
    TEST_ASSERT_EQUAL_UINT32(0, as::label(as::defaults(), nullptr, as::kLabelCapacity));
    TEST_ASSERT_EQUAL_UINT32(10, as::label(as::defaults(), out, 11));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_defaults_reproduce_todays_mix);
    RUN_TEST(test_off_silences_both_gains);
    RUN_TEST(test_levels_rise_and_sfx_stays_under_master);
    RUN_TEST(test_cycle_order_from_off);
    RUN_TEST(test_cycle_visits_every_state_and_returns_to_start);
    RUN_TEST(test_encode_decode_round_trip);
    RUN_TEST(test_encode_refuses_small_buffer_and_null);
    RUN_TEST(test_decode_rejects_malformed_without_touching_output);
    RUN_TEST(test_label_names_every_reachable_state);
    RUN_TEST(test_label_refuses_small_buffer_and_null);
    return UNITY_END();
}
