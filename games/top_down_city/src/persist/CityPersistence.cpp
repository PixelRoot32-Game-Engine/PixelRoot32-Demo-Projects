#include "persist/CityPersistence.h"

#if TDC_PERSISTENCE

namespace top_down_city {

namespace pp = pixelroot32::persistence;

CityPersistence& CityPersistence::instance() {
    static CityPersistence persistence;
    return persistence;
}

void CityPersistence::bind(pp::IKeyValueStore* settings, pp::ISlotStore* saves) {
    service_.bindSettings(settings);
    service_.bindSaves(saves);
}

audio_settings::Settings CityPersistence::loadSettings() {
    const std::uint8_t* payload = nullptr;
    pp::RecordInfo info{};
    audio_settings::Settings settings = audio_settings::defaults();
    if (service_.loadSettingRecord(kSettingsKey, scratch_, sizeof scratch_, payload, info)
            == pp::StoreResult::Ok
        && info.schemaVersion == audio_settings::kSchemaVersion
        && audio_settings::decode(payload, info.payloadSize, settings)) {
        return settings;
    }
    return audio_settings::defaults();
}

records::Record CityPersistence::loadRecords() {
    const std::uint8_t* payload = nullptr;
    pp::RecordInfo info{};
    records::Record record = records::clear();
    if (service_.loadRecord(kRecordsSlot, scratch_, sizeof scratch_, payload, info)
            == pp::StoreResult::Ok
        && info.schemaVersion == records::kSchemaVersion
        && records::decode(payload, info.payloadSize, record)) {
        return record;
    }
    return records::clear();
}

pp::StoreResult CityPersistence::saveSettings(const audio_settings::Settings& settings) {
    std::uint8_t payload[audio_settings::kPayloadSize];
    const std::size_t size = audio_settings::encode(settings, payload, sizeof payload);
    if (size == 0) {
        return pp::StoreResult::InvalidArgument;
    }
    const pp::StoreResult written = service_.saveSettingRecord(
        kSettingsKey, audio_settings::kSchemaVersion, payload, size, scratch_,
        sizeof scratch_);
    if (written != pp::StoreResult::Ok) {
        return written;
    }
    return service_.settings().commit();
}

pp::StoreResult CityPersistence::saveRecords(const records::Record& record) {
    std::uint8_t payload[records::kPayloadSize];
    const std::size_t size = records::encode(record, payload, sizeof payload);
    if (size == 0) {
        return pp::StoreResult::InvalidArgument;
    }
    return service_.saveRecord(kRecordsSlot, records::kSchemaVersion, payload, size,
                               scratch_, sizeof scratch_);
}

void CityPersistence::adoptStored() {
    live_ = loadRecords();
    dirty_ = false;
}

bool CityPersistence::foldRun(const records::Record& seen) {
    const bool improved = records::merge(live_, seen);
    if (improved) {
        dirty_ = true;
    }
    return improved;
}

pp::StoreResult CityPersistence::flushRecords() {
    if (!dirty_) {
        return pp::StoreResult::Ok;
    }
    const pp::StoreResult result = saveRecords(live_);
    if (result == pp::StoreResult::Ok) {
        dirty_ = false;
    }
    return result;
}

}  // namespace top_down_city

#endif  // TDC_PERSISTENCE
