// SPDX-License-Identifier: Apache-2.0
#include "esp_err.h"
#include "esp_iris.h"
#include "esp_log.h"
#include "iris_ota_support.h"

/* Strong definition of the hook Muse's app_main calls before Wi-Fi, BLE and
 * the UI start. ESP-Iris keeps its own state in the retained sysmeta NVS
 * partition, so Muse's nvs partition and its initialisation stay untouched.
 * iris_ota_support_start() registers the enter-Vibe-Mode RPC, starts Iris on
 * High-Speed USB (and TCP once Wi-Fi is up) and accepts this image. */
void muse_gadget_platform_start(void)
{
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_iris_boot_probe());
    iris_ota_support_start();
    ESP_LOGI("mosaico", "Vibe Mode services started");
}
