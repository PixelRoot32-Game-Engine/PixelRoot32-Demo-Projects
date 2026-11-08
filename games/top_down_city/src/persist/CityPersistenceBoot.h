#pragma once

#include "persist/CityPersistence.h"

#if TDC_PERSISTENCE

namespace top_down_city::persist {

/**
 * @brief Platform boot step: create the platform's stores (NVS on ESP32, host
 *        files on native), bind them to `CityPersistence`, load the records
 *        into the live book and apply the stored audio settings.
 *
 * Call once, after `AudioDirector::bind` (which sets the default master volume
 * this overrides) and before the first `setScene`. Any failure leaves the
 * affected role unbound and the game on its defaults.
 */
void boot();

}  // namespace top_down_city::persist

#endif  // TDC_PERSISTENCE
