/**
 * @brief CityPersistence over the engine's in-memory stores.
 *
 * Pinned here: records and settings survive a "restart" (a fresh instance over
 * the same stores), every non-Ok load falls back to the module's defaults, an
 * unbound role never crashes and reports Unavailable, and a failed write
 * returns its error while the live values stay as they were.
 */
#include <unity.h>

#include <cstdint>

#include "game/rules/AudioSettings.h"
#include "game/rules/Records.h"
#include "persist/CityPersistence.h"
#include "persistence/MemoryStores.h"
#include "persistence/RecordCodec.h"

namespace pp = pixelroot32::persistence;
namespace tdc = top_down_city;
namespace as = top_down_city::audio_settings;
namespace rc = top_down_city::records;

using KV = pp::MemoryKeyValueStore<4, 32>;
using Slots = pp::MemorySlotStore<1, 32>;

static KV kv;
static Slots slots;

void setUp() {
    kv = KV();
    slots = Slots();
}
void tearDown() {}

static int code(pp::StoreResult r) { return static_cast<int>(r); }

static rc::Record makeRecord(std::uint8_t streak, std::uint8_t chapters,
                             std::uint16_t cash) {
    rc::Record r = rc::clear();
    r.bestStreak = streak;
    r.chapters = chapters;
    r.bestCash = cash;
    return r;
}

static bool sameRecord(const rc::Record& a, const rc::Record& b) {
    return a.bestStreak == b.bestStreak && a.chapters == b.chapters &&
           a.bestCash == b.bestCash;
}

static void bound(tdc::CityPersistence& p) { p.bind(&kv, &slots); }

void test_settings_round_trip_over_new_instance() {
    {
        tdc::CityPersistence p;
        bound(p);
        TEST_ASSERT_EQUAL(code(pp::StoreResult::Ok),
                          code(p.saveSettings(as::Settings{false, 1})));
    }
    tdc::CityPersistence q;
    bound(q);
    const as::Settings got = q.loadSettings();
    TEST_ASSERT_FALSE(got.audioOn);
    TEST_ASSERT_EQUAL_UINT8(1, got.level);
}

void test_records_round_trip_over_new_instance() {
    const rc::Record r = makeRecord(7, 0x03, 1234);
    {
        tdc::CityPersistence p;
        bound(p);
        TEST_ASSERT_EQUAL(code(pp::StoreResult::Ok), code(p.saveRecords(r)));
    }
    tdc::CityPersistence q;
    bound(q);
    TEST_ASSERT_TRUE(sameRecord(r, q.loadRecords()));
}

void test_save_settings_commits_exactly_once() {
    tdc::CityPersistence p;
    bound(p);
    p.saveSettings(as::defaults());
    TEST_ASSERT_EQUAL_UINT32(1, kv.commits());
}

void test_nothing_stored_gives_defaults() {
    tdc::CityPersistence p;
    bound(p);
    const as::Settings s = p.loadSettings();
    TEST_ASSERT_EQUAL(as::defaults().audioOn, s.audioOn);
    TEST_ASSERT_EQUAL_UINT8(as::defaults().level, s.level);
    TEST_ASSERT_TRUE(sameRecord(rc::clear(), p.loadRecords()));
}

void test_corrupt_envelope_gives_defaults() {
    tdc::CityPersistence p;
    bound(p);
    p.saveSettings(as::Settings{false, 0});
    p.saveRecords(makeRecord(9, 0x07, 500));
    TEST_ASSERT_EQUAL(code(pp::StoreResult::Ok), code(kv.corruptByte("audio", 17)));
    TEST_ASSERT_EQUAL(code(pp::StoreResult::Ok), code(slots.corruptByte(0, 17)));
    const as::Settings s = p.loadSettings();
    TEST_ASSERT_TRUE(s.audioOn);
    TEST_ASSERT_EQUAL_UINT8(as::defaults().level, s.level);
    TEST_ASSERT_TRUE(sameRecord(rc::clear(), p.loadRecords()));
}

void test_other_schema_version_gives_defaults() {
    // A well-formed envelope with valid payload bytes but a schema this build
    // does not know: migration is the game's job and this game has none yet.
    const std::uint8_t payload[2] = {0, 1};
    std::uint8_t scratch[32];
    tdc::CityPersistence p;
    bound(p);
    TEST_ASSERT_EQUAL(
        code(pp::StoreResult::Ok),
        code(p.service().saveSettingRecord(
            "audio", static_cast<std::uint16_t>(as::kSchemaVersion + 1), payload,
            sizeof payload, scratch, sizeof scratch)));
    const std::uint8_t rpayload[4] = {5, 1, 0x10, 0x00};
    TEST_ASSERT_EQUAL(
        code(pp::StoreResult::Ok),
        code(p.service().saveRecord(
            0, static_cast<std::uint16_t>(rc::kSchemaVersion + 1), rpayload,
            sizeof rpayload, scratch, sizeof scratch)));
    const as::Settings s = p.loadSettings();
    TEST_ASSERT_TRUE(s.audioOn);
    TEST_ASSERT_EQUAL_UINT8(as::defaults().level, s.level);
    TEST_ASSERT_TRUE(sameRecord(rc::clear(), p.loadRecords()));
}

void test_unbound_roles_give_defaults_and_unavailable() {
    tdc::CityPersistence p;  // never bound: what an init failure leaves behind
    TEST_ASSERT_TRUE(p.loadSettings().audioOn);
    TEST_ASSERT_TRUE(sameRecord(rc::clear(), p.loadRecords()));
    TEST_ASSERT_EQUAL(code(pp::StoreResult::Unavailable),
                      code(p.saveSettings(as::defaults())));
    TEST_ASSERT_EQUAL(code(pp::StoreResult::Unavailable),
                      code(p.saveRecords(makeRecord(1, 0, 1))));
}

void test_failed_write_returns_error_and_keeps_live_values() {
    tdc::CityPersistence p;
    bound(p);
    p.adoptStored();
    TEST_ASSERT_TRUE(p.foldRun(makeRecord(4, 0x01, 300)));
    kv.setFailWrites(true);
    slots.setFailWrites(true);
    TEST_ASSERT_EQUAL(code(pp::StoreResult::IoError), code(p.flushRecords()));
    TEST_ASSERT_TRUE(sameRecord(makeRecord(4, 0x01, 300), p.records()));
    TEST_ASSERT_TRUE(p.recordsDirty());
    TEST_ASSERT_EQUAL(code(pp::StoreResult::IoError),
                      code(p.saveSettings(as::Settings{false, 0})));
    // The next safe point retries once the medium is back, and then clears it.
    kv.setFailWrites(false);
    slots.setFailWrites(false);
    TEST_ASSERT_EQUAL(code(pp::StoreResult::Ok), code(p.flushRecords()));
    TEST_ASSERT_FALSE(p.recordsDirty());
}

void test_flush_only_writes_after_an_improvement() {
    tdc::CityPersistence p;
    bound(p);
    p.adoptStored();
    p.flushRecords();
    TEST_ASSERT_EQUAL_UINT32(0, slots.writes());
    TEST_ASSERT_TRUE(p.foldRun(makeRecord(2, 0, 50)));
    p.flushRecords();
    TEST_ASSERT_EQUAL_UINT32(1, slots.writes());
    TEST_ASSERT_FALSE(p.foldRun(makeRecord(1, 0, 10)));  // lower: no improvement
    p.flushRecords();
    TEST_ASSERT_EQUAL_UINT32(1, slots.writes());
}

void test_adopt_stored_loads_saved_records() {
    {
        tdc::CityPersistence p;
        bound(p);
        p.saveRecords(makeRecord(6, 0x05, 777));
    }
    tdc::CityPersistence q;
    bound(q);
    q.adoptStored();
    TEST_ASSERT_TRUE(sameRecord(makeRecord(6, 0x05, 777), q.records()));
    TEST_ASSERT_FALSE(q.recordsDirty());
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_settings_round_trip_over_new_instance);
    RUN_TEST(test_records_round_trip_over_new_instance);
    RUN_TEST(test_save_settings_commits_exactly_once);
    RUN_TEST(test_nothing_stored_gives_defaults);
    RUN_TEST(test_corrupt_envelope_gives_defaults);
    RUN_TEST(test_other_schema_version_gives_defaults);
    RUN_TEST(test_unbound_roles_give_defaults_and_unavailable);
    RUN_TEST(test_failed_write_returns_error_and_keeps_live_values);
    RUN_TEST(test_flush_only_writes_after_an_improvement);
    RUN_TEST(test_adopt_stored_loads_saved_records);
    return UNITY_END();
}
