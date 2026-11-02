#pragma once

/*
 * D7.1 (T53, S18): the save slots and the settings record on the engine's
 * persistence layer.
 *
 * WHAT THIS OWNS
 *
 * One `PersistenceService` with its two roles, the two media behind them, the
 * buffers a save record needs, and the six slot labels the menu draws. The
 * scene calls `init()` once, binds nothing itself, and afterwards asks for three
 * things: the `SettingsStore` the existing settings codec already knows how to
 * drive, the labels, and a save/load by slot number.
 *
 * THE MEDIA, AND WHY THEY DIFFER PER PLATFORM
 *
 *   board   Esp32NvsKeyValueStore("doom", 64)   the `nvs` partition, byte-
 *           compatible with the `Preferences` record the port wrote before this
 *           task (namespace `doom`, key `cfg`)
 *           Esp32NvsSlotStore("doom", "save", 6, 3968, "saves")
 *           the 32 KB `saves` partition T53 carved out of `wad`, keys save0..save5
 *
 *   host    HostFileKeyValueStore("save_data", "doom", 64)
 *           HostFileSlotStore("save_data", "save", 6, 3968)
 *           files under `save_data/` in the working directory, which the engine's
 *           adapter creates (parents included); the desktop mirror has no
 *           persistence flag today, so this branch is written for the first
 *           native build that does.
 *
 * The two records are stored differently on purpose. The settings record keeps
 * its exact eight bytes (see SettingsStore.h): the engine's envelope would change
 * what a board already holds. A save slot is a new record, so it goes through
 * `PersistenceService::saveRecord()`, which wraps it in the engine's 16-byte
 * self-checking envelope -- the CRC is the second guard beside the record's own
 * checksum, and a torn NVS write cannot be mistaken for a save.
 *
 * WHY THE BUFFERS ARE ON THE HEAP
 *
 * A save payload is 3,968 B and the envelope needs a 3,968 B scratch beside it.
 * `dram0_0_seg` has about 550 B free in the shipped image (the census prints
 * `seg_free`), so a static pair cannot link. They are one 7,936 B block taken
 * once in `init()` and held for the session: nothing is allocated on the frame
 * path, and if the block cannot be had the slots report `Unavailable` and the
 * game keeps running -- the same contract as a missing partition.
 *
 * A FAILURE IS NOT FATAL
 *
 * `init()` prints one `SHOW,persist` line and answers whether the two roles are
 * bound. Settings may work while saves do not (a board flashed with a table that
 * predates the `saves` row), so the line names them separately and the status is
 * `ok`, `partial` or `unavailable`. The menu is still usable; a save reports
 * `status=unavailable` instead of pretending.
 */
#if DOOM_PERSISTENCE

#include <cstddef>
#include <cstdint>

#include "doomc/SaveGame.h"
#include "doomc/SettingsStore.h"
#include "m_menu.h"
#include "persistence/PersistenceService.h"

#if defined(ESP32)
#include "drivers/esp32/Esp32NvsStore.h"
#else
#include "drivers/native/HostFileStore.h"
#endif

namespace doom {
namespace doomc {

/**
 * @class DoomPersistence
 * @brief The board's (or the host's) save slots and settings record (T53).
 *
 * Construct once, call `init()` once from the scene's `init()` before the menu
 * can be opened, then use `settingsStore()`, `readSlotLabels()`, `saveSlot()`
 * and `loadSlot()`. Every method is safe before `init()`: the calls answer
 * "unavailable" and write no line. Single-threaded, like the engine's layer.
 */
class DoomPersistence {
public:
    /// Vanilla's slot count and this partition's honest maximum (S18/L56).
    static constexpr std::uint8_t kSlotCount = M_SLOT_COUNT;

    /// The spec's two numbers, pinned: a table that says otherwise is a build
    /// failure, not a slot that silently shrinks.
    static_assert(kSlotCount == 6, "S18/L56: six slots");
    static_assert(M_SLOT_LABEL_SIZE == 16, "a label is 15 characters plus NUL");

    /**
     * @brief One slot's record budget, the engine envelope included.
     *
     * L56's arithmetic: one NVS page is 126 entries of 32 B and the largest blob
     * that stays inside it is (126 - 2) x 32 = 3,968 B. The envelope's 16 bytes
     * live inside that number, which is why the payload guard below subtracts
     * them; the measured record is 3,393 B, so the gap is never reached by a
     * real world.
     */
    static constexpr std::size_t kRecordBytes = 3968;

    /// The payload budget the record writer enforces (SaveGame.h).
    static constexpr std::size_t kPayloadBytes = kSavePayloadMax;

    /// The envelope's schema field: the save record's own format version.
    static constexpr std::uint16_t kSlotSchema = 1u;

    /// The map this build can save: the session opens E1M1 and only E1M1.
    static constexpr std::uint8_t kLevelNumber = 1u;

    DoomPersistence();

    /**
     * @brief Initialises the media, binds the roles and prints `SHOW,persist`.
     *
     * Idempotent. A store that cannot initialise is left unbound, which is what
     * makes it answer `Unavailable` later instead of failing the boot.
     *
     * @return true when both roles are bound.
     */
    bool init();

    /// Whether settings can be read and written.
    bool settingsAvailable() const { return settingsReady_; }

    /// Whether the six slots can be read and written.
    bool savesAvailable() const { return savesReady_; }

    /**
     * @brief The settings seam: the engine store, or RAM defaults when the
     *        medium could not be initialised.
     *
     * The fallback is a `MemorySettingsStore` that is never written, so the
     * existing `loadSettings()` sees "no record" and the game runs on defaults,
     * exactly as it would on a first boot. A write through it is accepted and
     * lost, which the settings line reports as `default`.
     */
    SettingsStore& settingsStore();

    /**
     * @brief Fills six labels for the menu's LOAD/SAVE screens.
     *
     * One `loadRecord()` per slot into the shared scratch; a slot that answers
     * `NotFound`, `Corrupt` or an unreadable header becomes "EMPTY". A label is
     * the record's own header text ("E1M1 HURT 03:12"), copied while the payload
     * is still valid -- the next `loadRecord()` overwrites the scratch.
     *
     * @param labels Six buffers of `M_SLOT_LABEL_SIZE` bytes.
     */
    void readSlotLabels(char labels[kSlotCount][M_SLOT_LABEL_SIZE]);

    /**
     * @brief Writes the live world into @p slot and prints `SHOW,save`.
     *
     * @param slot 0..kSlotCount-1 (the menu's row).
     * @return true when the record reached the medium.
     */
    bool saveSlot(int slot);

    /**
     * @brief Reads @p slot, reloads its level at its skill and applies it.
     *
     * Prints `SHOW,load` with the record's level, skill and tics on success, or
     * the failing status. On a failed apply the world is left freshly loaded (the
     * driver's contract), never half-restored.
     *
     * @param slot 0..kSlotCount-1 (the menu's row).
     * @return true when the world holds the record.
     */
    bool loadSlot(int slot);

private:
    bool ready() const { return settingsReady_ && savesReady_ && buffer_ != nullptr; }
    std::uint8_t* payload() { return buffer_; }
    std::uint8_t* scratch() { return buffer_ + kPayloadBytes; }

#if defined(ESP32)
    pixelroot32::persistence::Esp32NvsKeyValueStore settingsMedium_;
    pixelroot32::persistence::Esp32NvsSlotStore slotMedium_;
#else
    pixelroot32::persistence::HostFileKeyValueStore settingsMedium_;
    pixelroot32::persistence::HostFileSlotStore slotMedium_;
#endif
    /// The settings seam over `settingsMedium_`; constructed after it.
    KeyValueSettingsStore settingsAdapter_;
    /// The RAM fallback `settingsStore()` answers through when the medium failed.
    MemorySettingsStore fallbackSettings_;
    /// One block: `kPayloadBytes` of payload, then `kPayloadBytes` of scratch.
    std::uint8_t* buffer_ = nullptr;
    pixelroot32::persistence::PersistenceService service_;
    bool initialised_ = false;
    bool settingsReady_ = false;
    bool savesReady_ = false;
};

}  // namespace doomc
}  // namespace doom

#endif  // DOOM_PERSISTENCE
