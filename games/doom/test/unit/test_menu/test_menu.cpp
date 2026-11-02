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
 *   - the mute flag (T49) rides in the record's reserved byte: a record written
 *     before the flag existed still decodes as "not muted", the flag round-trips,
 *     and a damaged flag byte is refused like any other damage
 *   - the volume and mute become DOOM's 0..127 level exactly once (`audioLevel`):
 *     silence at 0, S_MAX_VOLUME at M_VOLUME_MAX, and mute silences without
 *     touching the stored volume
 */
#include <cstdint>

#include <unity.h>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#include "doomc/AudioLevels.h"
#include "doomc/InputMap.h"
#include "doomc/SettingsStore.h"
#include "m_menu.h"
#pragma GCC diagnostic pop
#include "palette/DoomPalette.h"

#include "../../test_config.h"

using doom::doomc::audioLevel;
using doom::doomc::audioMasterVolume;
using doom::doomc::loadSettings;
using doom::doomc::MemorySettingsStore;
using doom::doomc::saveSettings;

void setUp(void) { test_setup(); }
void tearDown() { test_teardown(); }

namespace {

void assertSettings(int skill, int volume, int gamma, const m_settings_t& settings,
                    int muted = 0) {
    TEST_ASSERT_EQUAL_INT(skill, settings.skill);
    TEST_ASSERT_EQUAL_INT(volume, settings.volume);
    TEST_ASSERT_EQUAL_INT(gamma, settings.gamma);
    TEST_ASSERT_EQUAL_INT(muted, settings.muted);
}

/// The eight bytes a board wrote before the mute flag existed: the reserved byte
/// (6) is zero, and that is the only thing that says "not muted".
constexpr std::uint8_t kOldRecord[M_SETTINGS_BLOB_SIZE] = {0x44, 0x4D, 1, 3, 12, 2, 0, 0x48};

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
    // Start is the menu's alone: a bit outside the six game bits.
    TEST_ASSERT_EQUAL_UINT8(0x40u, M_BTN_START);
    TEST_ASSERT_EQUAL_UINT8(0u, M_BTN_START & 0x3Fu);
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
    m_settings_t settings = {3, 12, 2, 0};
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
        m_settings_t out = {4, 1, 3, 0};
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
        m_settings_t out = {4, 1, 3, 0};
        TEST_ASSERT_FALSE(loadSettings(store, &out));
        assertSettings(2, 8, 0, out);
    }

    // Erased flash (all 0xFF) is not a record.
    MemorySettingsStore erased;
    const std::uint8_t ff[M_SETTINGS_BLOB_SIZE] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    erased.poke(ff, sizeof ff);
    m_settings_t out = {4, 1, 3, 0};
    TEST_ASSERT_FALSE(loadSettings(erased, &out));
    assertSettings(2, 8, 0, out);
}

void test_a_failed_write_reports_failure_and_keeps_the_old_record(void) {
    MemorySettingsStore store;
    m_settings_t first = {1, 5, 1, 0};
    TEST_ASSERT_TRUE(saveSettings(store, first));
    store.setFailWrites(true);
    m_settings_t second = {4, 15, 4, 0};
    TEST_ASSERT_FALSE(saveSettings(store, second));
    m_settings_t loaded;
    TEST_ASSERT_TRUE(loadSettings(store, &loaded));
    assertSettings(1, 5, 1, loaded);
}

void test_saving_clamps_a_hand_built_value_into_range(void) {
    MemorySettingsStore store;
    m_settings_t wild = {200, 200, 200, 200};
    TEST_ASSERT_TRUE(saveSettings(store, wild));
    m_settings_t loaded;
    TEST_ASSERT_TRUE(loadSettings(store, &loaded));
    assertSettings(M_SKILL_COUNT - 1, M_VOLUME_MAX, M_GAMMA_MAX, loaded, M_MUTE_MAX);
}

// --- Volume and mute (T49, D6.6) ---------------------------------------------

void test_a_record_written_before_the_mute_flag_still_decodes(void) {
    // The board's stored record, byte for byte: 'D' 'M' v1 skill 3 volume 12
    // gamma 2, reserved byte 0, checksum. Nothing about it changed, so the flag
    // had to live in a byte whose old value already means "not muted".
    MemorySettingsStore store;
    store.poke(kOldRecord, M_SETTINGS_BLOB_SIZE);
    m_settings_t loaded = {0, 0, 0, 1};
    TEST_ASSERT_TRUE(loadSettings(store, &loaded));
    assertSettings(3, 12, 2, loaded, 0);
}

void test_the_mute_flag_round_trips_through_the_store(void) {
    MemorySettingsStore medium;
    m_settings_t settings = {3, 5, 2, 1};
    TEST_ASSERT_TRUE(saveSettings(medium, settings));

    std::uint8_t blob[M_SETTINGS_BLOB_SIZE];
    TEST_ASSERT_EQUAL_UINT32(M_SETTINGS_BLOB_SIZE, medium.read(blob, sizeof blob));
    // The reserved byte becomes the flag; skill, volume and gamma do not move.
    TEST_ASSERT_EQUAL_HEX8(1u, blob[6]);
    TEST_ASSERT_EQUAL_HEX8(static_cast<std::uint8_t>('D'), blob[0]);
    TEST_ASSERT_EQUAL_HEX8(3u, blob[3]);
    TEST_ASSERT_EQUAL_HEX8(5u, blob[4]);
    TEST_ASSERT_EQUAL_HEX8(2u, blob[5]);

    m_settings_t loaded;
    TEST_ASSERT_TRUE(loadSettings(medium, &loaded));
    assertSettings(3, 5, 2, loaded, 1);

    // And back off again: the flag is a flag, not a one-way latch.
    settings.muted = 0;
    TEST_ASSERT_TRUE(saveSettings(medium, settings));
    TEST_ASSERT_TRUE(loadSettings(medium, &loaded));
    assertSettings(3, 5, 2, loaded, 0);
}

void test_a_flag_byte_that_is_not_a_flag_is_not_trusted(void) {
    // 0 and 1 are the flag's two values. Anything else is damage, and the record
    // falls back to the defaults rather than half-trusting it -- even when the
    // checksum was recomputed for it, so this is the range test failing and not
    // the checksum.
    std::uint8_t damaged[M_SETTINGS_BLOB_SIZE];
    for (unsigned index = 0; index < sizeof damaged; ++index) {
        damaged[index] = kOldRecord[index];
    }
    damaged[6] = 2;
    damaged[7] = static_cast<std::uint8_t>(
        (0xA5u + 0x44u + 0x4Du + 1u + 3u + 12u + 2u + 2u) & 0xFFu);
    MemorySettingsStore store;
    store.poke(damaged, sizeof damaged);
    m_settings_t out = {4, 1, 3, 1};
    TEST_ASSERT_FALSE(loadSettings(store, &out));
    assertSettings(2, 8, 0, out, 0);
}

void test_volume_maps_onto_dooms_0_127_scale(void) {
    // The menu's 0..15 onto S_MAX_VOLUME's 0..127, integer, one division: the
    // exact level the scene hands the audio engine.
    TEST_ASSERT_EQUAL_INT(0, audioLevel(0, false));
    TEST_ASSERT_EQUAL_INT(127, audioLevel(M_VOLUME_MAX, false));
    TEST_ASSERT_EQUAL_INT(67, audioLevel(8, false));  // 8 * 127 / 15 = 67.7
    TEST_ASSERT_EQUAL_INT(8, audioLevel(1, false));   // 127 / 15 = 8.47
    // Out of range is clamped, never wrapped: a hand-built setting is still a
    // legal level.
    TEST_ASSERT_EQUAL_INT(127, audioLevel(200, false));

    // The same number as the ratio AudioEngine::setMasterVolume takes.
    TEST_ASSERT_EQUAL_FLOAT(0.0f, audioMasterVolume(0, false));
    TEST_ASSERT_EQUAL_FLOAT(1.0f, audioMasterVolume(M_VOLUME_MAX, false));
    TEST_ASSERT_FLOAT_WITHIN(1.0e-6f, 67.0f / 127.0f, audioMasterVolume(8, false));
}

void test_mute_silences_without_touching_the_stored_volume(void) {
    m_settings_t settings = {2, 9, 1, 1};
    TEST_ASSERT_EQUAL_INT(0, audioLevel(settings.volume, settings.muted != 0));
    TEST_ASSERT_EQUAL_FLOAT(0.0f, audioMasterVolume(settings.volume, settings.muted != 0));
    // Unmuting restores the level the slider still holds: mute is a gate, not a
    // volume write.
    TEST_ASSERT_EQUAL_INT(9, settings.volume);
    TEST_ASSERT_EQUAL_INT(76, audioLevel(settings.volume, false));  // 9 * 127 / 15
}

void test_the_mute_row_is_the_fourth_options_row(void) {
    m_menu_t menu;
    m_menu_init(&menu, nullptr);
    m_menu_open(&menu);
    (void)m_menu_key(&menu, M_KEY_DOWN);
    (void)m_menu_key(&menu, M_KEY_ENTER);  // OPTIONS
    // Rows: skill, volume, gamma, mute. DOWN three times lands on the mute row.
    (void)m_menu_key(&menu, M_KEY_DOWN);
    (void)m_menu_key(&menu, M_KEY_DOWN);
    (void)m_menu_key(&menu, M_KEY_DOWN);
    TEST_ASSERT_EQUAL_UINT8(3, menu.itemOn[M_SCREEN_OPTIONS]);
    (void)m_menu_key(&menu, M_KEY_RIGHT);  // OFF -> ON
    TEST_ASSERT_EQUAL_UINT8(1, menu.settings.muted);
    (void)m_menu_key(&menu, M_KEY_RIGHT);  // already ON: clamps, no change
    TEST_ASSERT_EQUAL_UINT8(1, menu.settings.muted);
    (void)m_menu_key(&menu, M_KEY_LEFT);   // ON -> OFF
    TEST_ASSERT_EQUAL_UINT8(0, menu.settings.muted);
    // ENTER steps a slider up, upstream's routine(1), and it is the same row type.
    (void)m_menu_key(&menu, M_KEY_ENTER);
    TEST_ASSERT_EQUAL_UINT8(1, menu.settings.muted);
    // The mute row never disturbs the volume, which is what unmuting restores.
    TEST_ASSERT_EQUAL_UINT8(M_VOLUME_DEFAULT, menu.settings.volume);
    // The change was reported, so the owner persists it.
    TEST_ASSERT_TRUE(m_menu_take_settings_changed(&menu) != 0);
    TEST_ASSERT_TRUE(m_menu_take_settings_changed(&menu) == 0);
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
    RUN_TEST(test_a_record_written_before_the_mute_flag_still_decodes);
    RUN_TEST(test_the_mute_flag_round_trips_through_the_store);
    RUN_TEST(test_a_flag_byte_that_is_not_a_flag_is_not_trusted);
    RUN_TEST(test_volume_maps_onto_dooms_0_127_scale);
    RUN_TEST(test_mute_silences_without_touching_the_stored_volume);
    RUN_TEST(test_the_mute_row_is_the_fourth_options_row);

    return UNITY_END();
}
