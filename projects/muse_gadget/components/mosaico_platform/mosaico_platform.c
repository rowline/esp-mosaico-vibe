// SPDX-License-Identifier: Apache-2.0
#include "camera.h"
#include "camera_mosaico.h"
#include "esp_err.h"
#include "esp_iris.h"
#include "esp_log.h"
#include "iris_ota_support.h"
#include "muse_tts.h"
#include "tts_bench.h"
#include "tts_mosaico.h"
#include "mosaico_presence.h"

/* Strong definition of the hook Muse's app_main calls before Wi-Fi, BLE and
 * the UI start. ESP-Iris keeps its own state in the retained sysmeta NVS
 * partition, so Muse's nvs partition and its initialisation stay untouched.
 * iris_ota_support_start() registers the enter-Vibe-Mode RPC, starts Iris on
 * High-Speed USB and accepts this image. */
void muse_gadget_platform_start(void)
{
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_iris_boot_probe());
    iris_ota_support_start();
    ESP_LOGI("mosaico", "Vibe Mode services started");
    /* Before Muse's first control session, so camera.capture is advertised.
     * Registering touches no hardware; each capture looks for the module. */
    camera_register(camera_mosaico());
    /* esp-sr's voice for replies: mapped here, on the main task's internal
     * stack; it loads with the first reply it says. */
    muse_tts_register(tts_mosaico_start());
    tts_bench_register();
    /* Waits for Muse's UI and an Interaction module in its own task. */
    ESP_ERROR_CHECK_WITHOUT_ABORT(mosaico_presence_start());
}
