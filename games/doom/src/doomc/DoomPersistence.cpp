#include "doomc/DoomPersistence.h"

#if DOOM_PERSISTENCE

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "doomc/DoomConsole.h"
#include "doomc/FrameDriver.h"

namespace doom {
namespace doomc {

namespace {

/// One `SHOW,persist`/`SHOW,save`/`SHOW,load` line; every field is a small
/// number or a short word, so 128 bytes is twice what the longest needs.
constexpr std::size_t kPersistenceLineCapacity = 128u;

constexpr std::size_t kEnvelopeBytes = 16u;  // RecordCodec::kHeaderSize

/// The store's result as the one word the serial line carries. The vocabulary is
/// the engine's `StoreResult`, which is what a maintainer reading the line has to
/// compare with `docs/api/persistence.md`.
const char* storeStatusName(pixelroot32::persistence::StoreResult result) {
    using pixelroot32::persistence::StoreResult;

    switch (result) {
        case StoreResult::Ok: return "ok";
        case StoreResult::NotFound: return "not_found";
        case StoreResult::TooLarge: return "too_large";
        case StoreResult::NoSpace: return "no_space";
        case StoreResult::Corrupt: return "corrupt";
        case StoreResult::IoError: return "io_error";
        case StoreResult::Unavailable: return "unavailable";
        case StoreResult::InvalidKey: return "invalid_key";
        case StoreResult::InvalidArgument: return "invalid_argument";
        default: return "unknown";
    }
}

/// The save layer's status as one word (SaveGame.h's enum).
const char* saveStatusName(SaveStatus status) {
    switch (status) {
        case SaveStatus::Ok: return "ok";
        case SaveStatus::TooLarge: return "too_large";
        case SaveStatus::Truncated: return "truncated";
        case SaveStatus::BadMagic: return "bad_magic";
        case SaveStatus::BadVersion: return "bad_version";
        case SaveStatus::BadField: return "bad_field";
        case SaveStatus::WrongLevel: return "wrong_level";
        case SaveStatus::WrongSkill: return "wrong_skill";
        case SaveStatus::ApplyFailed: return "apply_failed";
        default: return "unknown";
    }
}

}  // namespace

DoomPersistence::DoomPersistence()
#if defined(ESP32)
    // The settings stay in `nvs` (byte-compatible with the record T39 wrote) and
    // a 64-byte maximum is twice the eight bytes the record uses.
    : settingsMedium_("doom", 64u),
      // The slots live in the `saves` partition; 3,968 B is one NVS page's
      // largest blob (L56), envelope included.
      slotMedium_("doom", "save", kSlotCount, kRecordBytes, "saves"),
#else
    // The engine's host adapter creates `save_data/doom/` and `save_data/save/`
    // and their parents; a native persistence build has no other root.
    : settingsMedium_("save_data", "doom", 64u),
      slotMedium_("save_data", "save", kSlotCount, kRecordBytes),
#endif
      settingsAdapter_(settingsMedium_) {
}

bool DoomPersistence::init() {
    if (initialised_) {
        return ready();
    }
    initialised_ = true;

    if (settingsMedium_.init() == pixelroot32::persistence::StoreResult::Ok) {
        service_.bindSettings(&settingsMedium_);
        settingsReady_ = true;
    }
    if (slotMedium_.init() == pixelroot32::persistence::StoreResult::Ok) {
        savesReady_ = true;
    }

    if (savesReady_) {
        // One block, taken once: 3,968 B of payload then 3,968 B of scratch. A
        // static pair would overflow `dram0_0_seg`'s ~550 free bytes, and a stack
        // pair of this size would not leave the loop task enough room. If the
        // heap cannot lend it the slots answer unavailable; the boot continues.
        buffer_ = static_cast<std::uint8_t*>(std::malloc(2u * kPayloadBytes));
        if (buffer_ != nullptr) {
            service_.bindSaves(&slotMedium_);
        } else {
            savesReady_ = false;
        }
    }

    char line[kPersistenceLineCapacity];
    std::snprintf(line, sizeof line,
                  "SHOW,persist,status=%s,settings=%d,saves=%d,slots=%u,record=%u\n",
                  settingsReady_ && savesReady_ ? "ok"
                                               : (settingsReady_ || savesReady_ ? "partial"
                                                                                : "unavailable"),
                  settingsReady_ ? 1 : 0, savesReady_ ? 1 : 0,
                  static_cast<unsigned>(service_.saves().slotCount()),
                  static_cast<unsigned>(service_.saves().maxRecordSize()));
    writeLine(line);
    return ready();
}

SettingsStore& DoomPersistence::settingsStore() {
    // `settingsAdapter_` is bound to the raw medium even when init() failed: every
    // call on an uninitialised adapter answers unavailable, which loadSettings()
    // reads as "no record". The fallback exists for the case where the medium's
    // constructor could not even be reached -- a build without the platform's
    // store -- and keeps the reference lifetime obvious.
    if (settingsReady_) {
        return settingsAdapter_;
    }
    return fallbackSettings_;
}

void DoomPersistence::readSlotLabels(char labels[kSlotCount][M_SLOT_LABEL_SIZE]) {
    for (std::uint8_t slot = 0u; slot < kSlotCount; ++slot) {
        std::strcpy(labels[slot], M_SLOT_EMPTY);
    }
    if (!savesReady_ || buffer_ == nullptr) {
        return;
    }

    for (std::uint8_t slot = 0u; slot < kSlotCount; ++slot) {
        const std::uint8_t* record = nullptr;
        pixelroot32::persistence::RecordInfo info{};

        // One loadRecord() per slot into the same scratch, as the task asks: the
        // payload pointer is valid only until the next call, so the label is
        // copied out here and never kept.
        if (service_.loadRecord(slot, scratch(), kRecordBytes, record, info)
                != pixelroot32::persistence::StoreResult::Ok) {
            continue;
        }
        if (info.schemaVersion != kSlotSchema || record == nullptr) {
            continue;
        }
        SaveHeader header;
        if (saveReadHeader(record, info.payloadSize, header) != SaveStatus::Ok) {
            continue;
        }
        std::strncpy(labels[slot], header.label, M_SLOT_LABEL_SIZE - 1u);
        labels[slot][M_SLOT_LABEL_SIZE - 1u] = '\0';
    }
}

bool DoomPersistence::saveSlot(int slot) {
    char line[kPersistenceLineCapacity];

    if (!ready() || slot < 0 || slot >= static_cast<int>(kSlotCount)) {
        std::snprintf(line, sizeof line, "SHOW,save,slot=%d,status=unavailable,bytes=0\n", slot);
        writeLine(line);
        return false;
    }

    std::size_t written = 0u;
    const SaveStatus status = frameLevelSave(payload(), kPayloadBytes, written);
    if (status != SaveStatus::Ok) {
        std::snprintf(line, sizeof line, "SHOW,save,slot=%d,status=%s,bytes=0\n", slot,
                      saveStatusName(status));
        writeLine(line);
        return false;
    }
    if (written + kEnvelopeBytes > kRecordBytes) {
        // The writer's budget is the payload; the slot's is the record. Only a
        // record within 16 B of the analytic limit can land here, and the
        // measured peak is 3,393 B -- but the refusal is reported, never silent.
        std::snprintf(line, sizeof line, "SHOW,save,slot=%d,status=too_large,bytes=%u\n", slot,
                      static_cast<unsigned>(written));
        writeLine(line);
        return false;
    }

    const pixelroot32::persistence::StoreResult result = service_.saveRecord(
        static_cast<std::uint16_t>(slot), kSlotSchema, payload(), written, scratch(),
        kRecordBytes);
    std::snprintf(line, sizeof line, "SHOW,save,slot=%d,status=%s,bytes=%u\n", slot,
                  storeStatusName(result), static_cast<unsigned>(written));
    writeLine(line);
    return result == pixelroot32::persistence::StoreResult::Ok;
}

bool DoomPersistence::loadSlot(int slot) {
    char line[kPersistenceLineCapacity];

    if (!ready() || slot < 0 || slot >= static_cast<int>(kSlotCount)) {
        std::snprintf(line, sizeof line, "SHOW,load,slot=%d,status=unavailable\n", slot);
        writeLine(line);
        return false;
    }

    const std::uint8_t* record = nullptr;
    pixelroot32::persistence::RecordInfo info{};
    pixelroot32::persistence::StoreResult result =
        service_.loadRecord(static_cast<std::uint16_t>(slot), scratch(), kRecordBytes, record,
                            info);
    if (result != pixelroot32::persistence::StoreResult::Ok || record == nullptr) {
        std::snprintf(line, sizeof line, "SHOW,load,slot=%d,status=%s\n", slot,
                      storeStatusName(result));
        writeLine(line);
        return false;
    }
    if (info.schemaVersion != kSlotSchema) {
        // The envelope decoded but it is not this build's record. The engine
        // reports the version and never judges it; this is the game's judgement.
        std::snprintf(line, sizeof line, "SHOW,load,slot=%d,status=bad_schema\n", slot);
        writeLine(line);
        return false;
    }

    SaveHeader header;
    SaveStatus status = saveReadHeader(record, info.payloadSize, header);
    if (status != SaveStatus::Ok) {
        std::snprintf(line, sizeof line, "SHOW,load,slot=%d,status=%s\n", slot,
                      saveStatusName(status));
        writeLine(line);
        return false;
    }

    status = frameLevelLoad(record, info.payloadSize, header.level, header.skill);
    if (status != SaveStatus::Ok) {
        std::snprintf(line, sizeof line, "SHOW,load,slot=%d,status=%s,level=%d,skill=%d\n", slot,
                      saveStatusName(status), static_cast<int>(header.level),
                      static_cast<int>(header.skill));
        writeLine(line);
        return false;
    }

    std::snprintf(line, sizeof line,
                  "SHOW,load,slot=%d,status=ok,level=%d,skill=%d,tics=%ld\n", slot,
                  static_cast<int>(header.level), static_cast<int>(header.skill),
                  static_cast<long>(header.leveltime));
    writeLine(line);
    return true;
}

}  // namespace doomc
}  // namespace doom

#endif  // DOOM_PERSISTENCE
