/*
 * Behaviour tests for the menu's persistence seam and its palette effect (D5.2).
 *
 * The state machine itself is gated by tools/menu_check.py (a C harness against a
 * reference model); this suite owns what that gate cannot see from C: the C++ seam
 * between the settings bytes and a storage medium, driven with an in-memory store,
 * and the gamma step of the palette module.
 *
 *   - a saved record survives a "reboot" (a new menu over the same medium)
 *   - a missing record, a record of the wrong size, damaged bytes and a record
 *     with an out-of-range value all fall back to the DEFAULTS, and say so
 *   - a failed write reports failure and leaves the previous record intact
 *   - the button bits the menu reads are the bits InputMap.h defines
 *   - gamma 0 reproduces the PLAYPAL tables, higher gamma is brighter, and the
 *     table pointer the panel holds does not move when the values change
 */
#include <cstdint>

#include <unity.h>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#include "doomc/InputMap.h"
#include "doomc/SettingsStore.h"
#include "m_menu.h"
#pragma GCC diagnostic pop
#include "palette/DoomPalette.h"

#include "../../test_config.h"

using doom::doomc::loadSettings;
using doom::doomc::MemorySettingsStore;
using doom::doomc::saveSettings;

void setUp(void) { test_setup(); }
void tearDown() { test_teardown(); }

namespace {

void assertSettings(int skill, int volume, int gamma, const m_settings_t& settings) {
    TEST_ASSERT_EQUAL_INT(skill, settings.skill);
    TEST_ASSERT_EQUAL_INT(volume, settings.volume);
    TEST_ASSERT_EQUAL_INT(gamma, settings.gamma);
}

}  // namespace

void test_the_defaults_are_dooms_own(void) {
    m_settings_t settings;
    m_settings_default(&settings);
    assertSettings(2, 8, 0, settings);  // "Hurt me plenty", half volume, no gamma
}

void test_the_menu_reads_the_input_maps_bits(void) {
    TEST_ASSERT_EQUAL_UINT8(doom::doomc::kButtonUp, M_BTN_UP);
    TEST_ASSERT_EQUAL_UINT8(doom::doomc::kButtonDown, M_BTN_DOWN);
    TEST_ASSERT_EQUAL_UINT8(doom::doomc::kButtonLeft, M_BTN_LEFT);
    TEST_ASSERT_EQUAL_UINT8(doom::doomc::kButtonRight, M_BTN_RIGHT);
    TEST_ASSERT_EQUAL_UINT8(doom::doomc::kButtonA, M_BTN_A);
    TEST_ASSERT_EQUAL_UINT8(doom::doomc::kButtonB, M_BTN_B);
}

void test_an_empty_store_gives_the_defaults_and_says_so(void) {
    MemorySettingsStore store;
    m_settings_t settings;
    settings.skill = 4;
    settings.volume = 1;
    settings.gamma = 3;
    TEST_ASSERT_FALSE(loadSettings(store, &settings));
    assertSettings(2, 8, 0, settings);
}

void test_a_saved_record_survives_a_reboot(void) {
    MemorySettingsStore medium;
    {
        // First boot: the player changes all three through the menu and leaves it.
        m_menu_t menu;
        m_menu_init(&menu, nullptr);
        m_menu_open(&menu);
        (void)m_menu_key(&menu, M_KEY_DOWN);
        (void)m_menu_key(&menu, M_KEY_ENTER);  // OPTIONS
        (void)m_menu_key(&menu, M_KEY_RIGHT);  // skill 2 -> 3
        (void)m_menu_key(&menu, M_KEY_DOWN);
        (void)m_menu_key(&menu, M_KEY_LEFT);   // volume 8 -> 7
        (void)m_menu_key(&menu, M_KEY_DOWN);
        (void)m_menu_key(&menu, M_KEY_RIGHT);  // gamma 0 -> 1
        (void)m_menu_key(&menu, M_KEY_RIGHT);  // gamma 1 -> 2
        TEST_ASSERT_TRUE(m_menu_take_settings_changed(&menu) != 0);
        TEST_ASSERT_TRUE(saveSettings(medium, menu.settings));
    }
    // Second boot: a new menu over the same medium.
    m_settings_t loaded;
    TEST_ASSERT_TRUE(loadSettings(medium, &loaded));
    assertSettings(3, 7, 2, loaded);
    m_menu_t menu;
    m_menu_init(&menu, &loaded);
    assertSettings(3, 7, 2, menu.settings);
    // The skill row of New Game starts on the stored skill.
    TEST_ASSERT_EQUAL_UINT8(3, menu.itemOn[M_SCREEN_NEWGAME]);
}

void test_damaged_records_fall_back_to_the_defaults(void) {
    MemorySettingsStore good;
    m_settings_t settings = {3, 12, 2};
    TEST_ASSERT_TRUE(saveSettings(good, settings));
    std::uint8_t blob[M_SETTINGS_BLOB_SIZE];
    TEST_ASSERT_EQUAL_UINT32(M_SETTINGS_BLOB_SIZE, good.read(blob, sizeof blob));

    for (unsigned byte = 0; byte < M_SETTINGS_BLOB_SIZE; ++byte) {
        std::uint8_t damaged[M_SETTINGS_BLOB_SIZE];
        for (unsigned i = 0; i < sizeof damaged; ++i) {
            damaged[i] = blob[i];
        }
        damaged[byte] = static_cast<std::uint8_t>(damaged[byte] ^ 0x5Au);
        MemorySettingsStore store;
        store.poke(damaged, sizeof damaged);
        m_settings_t out = {4, 1, 3};
        TEST_ASSERT_FALSE_MESSAGE(loadSettings(store, &out), "a flipped byte was trusted");
        assertSettings(2, 8, 0, out);
    }

    // A record of the wrong size, short or long.
    const unsigned sizes[] = {1u, 7u, 9u, 16u};
    for (unsigned size : sizes) {
        MemorySettingsStore store;
        std::uint8_t padded[16] = {};
        for (unsigned i = 0; i < sizeof padded; ++i) {
            padded[i] = i < M_SETTINGS_BLOB_SIZE ? blob[i] : 0u;
        }
        store.poke(padded, size);
        m_settings_t out = {4, 1, 3};
        TEST_ASSERT_FALSE(loadSettings(store, &out));
        assertSettings(2, 8, 0, out);
    }

    // Erased flash (all 0xFF) is not a record.
    MemorySettingsStore erased;
    const std::uint8_t ff[M_SETTINGS_BLOB_SIZE] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    erased.poke(ff, sizeof ff);
    m_settings_t out = {4, 1, 3};
    TEST_ASSERT_FALSE(loadSettings(erased, &out));
    assertSettings(2, 8, 0, out);
}

void test_a_failed_write_reports_failure_and_keeps_the_old_record(void) {
    MemorySettingsStore store;
    m_settings_t first = {1, 5, 1};
    TEST_ASSERT_TRUE(saveSettings(store, first));
    store.setFailWrites(true);
    m_settings_t second = {4, 15, 4};
    TEST_ASSERT_FALSE(saveSettings(store, second));
    m_settings_t loaded;
    TEST_ASSERT_TRUE(loadSettings(store, &loaded));
    assertSettings(1, 5, 1, loaded);
}

void test_saving_clamps_a_hand_built_value_into_range(void) {
    MemorySettingsStore store;
    m_settings_t wild = {200, 200, 200};
    TEST_ASSERT_TRUE(saveSettings(store, wild));
    m_settings_t loaded;
    TEST_ASSERT_TRUE(loadSettings(store, &loaded));
    assertSettings(M_SKILL_COUNT - 1, M_VOLUME_MAX, M_GAMMA_MAX, loaded);
}

namespace {

// A recognisable PLAYPAL: entry i is (i, 255 - i, i / 2).
void buildPlaypal(std::uint8_t (&rgb)[doom::palette::kRgb888Bytes]) {
    for (int i = 0; i < doom::palette::kEntryCount; ++i) {
        rgb[i * 3 + 0] = static_cast<std::uint8_t>(i);
        rgb[i * 3 + 1] = static_cast<std::uint8_t>(255 - i);
        rgb[i * 3 + 2] = static_cast<std::uint8_t>(i / 2);
    }
}

}  // namespace

void test_gamma_changes_the_palette_in_place(void) {
    std::uint8_t rgb[doom::palette::kRgb888Bytes];
    buildPlaypal(rgb);
    doom::palette::initFromPlaypal(rgb);
    const std::uint16_t* table = doom::palette::playpal565Table();
    std::uint16_t flat[doom::palette::kEntryCount];
    for (int i = 0; i < doom::palette::kEntryCount; ++i) {
        flat[i] = table[i];
    }

    // Level 0 is the identity: the tables initFromPlaypal() built.
    doom::palette::applyGamma(rgb, 0);
    for (int i = 0; i < doom::palette::kEntryCount; ++i) {
        TEST_ASSERT_EQUAL_HEX16(flat[i], table[i]);
    }

    // Level 4 lifts the dark entries and keeps black and white.
    doom::palette::applyGamma(rgb, 4);
    TEST_ASSERT_EQUAL_PTR(table, doom::palette::playpal565Table());  // the panel's pointer
    TEST_ASSERT_TRUE(table[32] != flat[32]);
    const std::uint16_t expected32 = doom::palette::rgb888To565(
        m_gamma_correct(4, rgb[32 * 3 + 0]), m_gamma_correct(4, rgb[32 * 3 + 1]),
        m_gamma_correct(4, rgb[32 * 3 + 2]));
    TEST_ASSERT_EQUAL_HEX16(expected32, table[32]);
    TEST_ASSERT_EQUAL_HEX8(
        doom::palette::rgb888To332(m_gamma_correct(4, rgb[32 * 3 + 0]),
                                   m_gamma_correct(4, rgb[32 * 3 + 1]),
                                   m_gamma_correct(4, rgb[32 * 3 + 2])),
        doom::palette::playpalTable()[32]);
    TEST_ASSERT_TRUE(m_gamma_correct(4, 16) > 16);
    TEST_ASSERT_EQUAL_UINT8(0, m_gamma_correct(4, 0));
    TEST_ASSERT_EQUAL_UINT8(255, m_gamma_correct(4, 255));

    // Back to 0 restores the originals exactly.
    doom::palette::applyGamma(rgb, 0);
    for (int i = 0; i < doom::palette::kEntryCount; ++i) {
        TEST_ASSERT_EQUAL_HEX16(flat[i], table[i]);
    }
}

void test_gamma_above_the_top_level_clamps(void) {
    TEST_ASSERT_EQUAL_UINT8(m_gamma_correct(4, 77), m_gamma_correct(9, 77));
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;

    UNITY_BEGIN();

    RUN_TEST(test_the_defaults_are_dooms_own);
    RUN_TEST(test_the_menu_reads_the_input_maps_bits);
    RUN_TEST(test_an_empty_store_gives_the_defaults_and_says_so);
    RUN_TEST(test_a_saved_record_survives_a_reboot);
    RUN_TEST(test_damaged_records_fall_back_to_the_defaults);
    RUN_TEST(test_a_failed_write_reports_failure_and_keeps_the_old_record);
    RUN_TEST(test_saving_clamps_a_hand_built_value_into_range);
    RUN_TEST(test_gamma_changes_the_palette_in_place);
    RUN_TEST(test_gamma_above_the_top_level_clamps);

    return UNITY_END();
}
