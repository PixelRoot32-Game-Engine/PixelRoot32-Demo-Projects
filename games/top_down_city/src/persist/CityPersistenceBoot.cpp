#include "persist/CityPersistenceBoot.h"

#if TDC_PERSISTENCE

#include "audio/AudioDirector.h"
#include "persist/CityPersistence.h"

#if defined(PLATFORM_NATIVE)
#include <drivers/native/HostFileStore.h>
#elif defined(ESP32)
#include <drivers/esp32/Esp32NvsStore.h>
#endif

namespace top_down_city::persist {

namespace pp = pixelroot32::persistence;

namespace {

#if defined(PLATFORM_NATIVE)
// Relative to the working directory, like the DOOM demo; git-ignored.
pp::HostFileKeyValueStore settingsMedium("save_data", "tdc",
                                         CityPersistence::kStoreBytes);
pp::HostFileSlotStore slotMedium("save_data", "rec", CityPersistence::kSlotCount,
                                 CityPersistence::kStoreBytes);
#elif defined(ESP32)
// Default Arduino partition table: both live in the stock "nvs" partition.
pp::Esp32NvsKeyValueStore settingsMedium("tdc", CityPersistence::kStoreBytes);
pp::Esp32NvsSlotStore slotMedium("tdc", "rec", CityPersistence::kSlotCount,
                                 CityPersistence::kStoreBytes);
#else
#error "TDC_PERSISTENCE has no store for this platform"
#endif

}  // namespace

void boot() {
    CityPersistence& persistence = CityPersistence::instance();

    // A medium that fails init() stays unbound: its role answers Unavailable
    // and the game plays on with defaults.
    pp::IKeyValueStore* settings =
        settingsMedium.init() == pp::StoreResult::Ok ? &settingsMedium : nullptr;
    pp::ISlotStore* saves =
        slotMedium.init() == pp::StoreResult::Ok ? &slotMedium : nullptr;
    persistence.bind(settings, saves);

    persistence.adoptStored();
    AudioDirector::instance().applySettings(persistence.loadSettings());
}

}  // namespace top_down_city::persist

#endif  // TDC_PERSISTENCE
