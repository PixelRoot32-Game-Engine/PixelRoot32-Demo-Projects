#pragma once

/**
 * @brief The city's records and audio settings over the engine's persistence
 *        module (PersistenceService + RecordCodec).
 *
 * Everything in this file, and every include of an engine persistence header
 * anywhere in the demo, sits behind `TDC_PERSISTENCE` -- the demo's own switch,
 * set in lib/platformio.ini next to `PIXELROOT32_ENABLE_PERSISTENCE`. With it
 * off the game builds and plays exactly as before. With it on and the engine
 * module off there is nothing to link against, so that is an error.
 *
 * What is stored, and where
 *  - Settings: the `audio_settings` payload, as a settings blob under the key
 *    `kSettingsKey` ("audio"), wrapped in the engine's RecordCodec envelope
 *    under `audio_settings::kSchemaVersion`.
 *  - Records: the `records` payload in save slot `kRecordsSlot` (0), wrapped the
 *    same way under `records::kSchemaVersion`.
 *  - ESP32: NVS namespace "tdc" (settings key "audio"; slot key prefix "rec").
 *  - Native: `save_data/tdc/` (settings) and `save_data/rec/` (slots) relative to
 *    the working directory, the same convention as the DOOM demo. The folder is
 *    git-ignored in games/top_down_city/.gitignore; delete it to wipe the saves.
 *  The stores themselves are created in CityPersistenceBoot.cpp, so this class
 *  takes them by pointer and the host tests can inject the engine's memory ones.
 *
 * Failure policy: a role that is not bound answers Unavailable; any non-Ok load
 * (NotFound, Corrupt, a schema version this build does not know, a payload the
 * rules module refuses) yields the module's own defaults; a failed save returns
 * its StoreResult and changes nothing in memory. Nothing here blocks or aborts.
 *
 * Writes cost real time on a board (an NVS setBlob stalls 4-13 ms on both
 * cores), so the scenes only call `flushRecords()` at safe points. No heap is
 * touched: the record helpers run over one fixed scratch buffer owned here.
 */

#ifndef TDC_PERSISTENCE
#define TDC_PERSISTENCE 0
#endif

#if TDC_PERSISTENCE

#ifndef PIXELROOT32_ENABLE_PERSISTENCE
#define PIXELROOT32_ENABLE_PERSISTENCE 0
#endif
#if !PIXELROOT32_ENABLE_PERSISTENCE
#error "TDC_PERSISTENCE needs the engine module: add -D PIXELROOT32_ENABLE_PERSISTENCE=1"
#endif

#include <cstddef>
#include <cstdint>

#include "game/rules/AudioSettings.h"
#include "game/rules/Records.h"
#include "persistence/PersistenceService.h"
#include "persistence/RecordCodec.h"
#include "persistence/StoreTypes.h"

namespace top_down_city {

class CityPersistence {
public:
    /// Settings blob key (NVS key / host file name).
    static constexpr const char* kSettingsKey = "audio";
    /// Save slot that holds the records.
    static constexpr std::uint16_t kRecordsSlot = 0;
    /// Number of save slots a store must offer.
    static constexpr std::uint16_t kSlotCount = 1;
    /// Largest blob / record either store needs to accept (envelope included).
    static constexpr std::size_t kStoreBytes = 32;

    /// The one instance the game uses; tests construct their own.
    static CityPersistence& instance();

    /**
     * @brief Bind the two roles. Non-owning; a null pointer leaves that role
     *        unbound, so every call on it reports Unavailable.
     *
     * The caller initialises each medium first and passes the pointer only if
     * `init()` returned Ok -- a failed init therefore never reaches here bound.
     */
    void bind(pixelroot32::persistence::IKeyValueStore* settings,
              pixelroot32::persistence::ISlotStore* saves);

    /// Stored settings, or `audio_settings::defaults()` on any non-Ok result.
    audio_settings::Settings loadSettings();
    /// Stored records, or `records::clear()` on any non-Ok result.
    records::Record loadRecords();

    /// Write the settings, then one `commit()` when the write succeeded.
    pixelroot32::persistence::StoreResult saveSettings(const audio_settings::Settings& settings);
    /// Write the records. The slot store has no commit step.
    pixelroot32::persistence::StoreResult saveRecords(const records::Record& record);

    /// Load the stored records into the live book (boot). Clears the dirty flag.
    void adoptStored();
    /// The live record book (what T8 shows).
    const records::Record& records() const { return live_; }
    /// Fold a finished run or a live moment into the live book.
    /// @return true when something improved (the book is now unsaved).
    bool foldRun(const records::Record& seen);
    /// Is there an improvement the store has not seen yet?
    bool recordsDirty() const { return dirty_; }
    /// Save the live book if it improved since the last successful save. The
    /// flag stays set on failure, so the next safe point retries.
    /// @return Ok when nothing was due or the save worked, else the store's error.
    pixelroot32::persistence::StoreResult flushRecords();

    /// The underlying service (tests, and platform code that needs the stores).
    pixelroot32::persistence::PersistenceService& service() { return service_; }

private:
    static_assert(pixelroot32::persistence::RecordCodec::kHeaderSize
                          + audio_settings::kPayloadSize <= kStoreBytes
                      && pixelroot32::persistence::RecordCodec::kHeaderSize
                          + records::kPayloadSize <= kStoreBytes,
                  "scratch must hold an envelope around either payload");

    pixelroot32::persistence::PersistenceService service_;
    std::uint8_t scratch_[kStoreBytes] = {};
    records::Record live_ = records::clear();
    bool dirty_ = false;
};

}  // namespace top_down_city

#endif  // TDC_PERSISTENCE
