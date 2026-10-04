// SPDX-License-Identifier: Apache-2.0
#include "presence_clock.h"

#include <stdlib.h>
#include <time.h>

#include "esp_log.h"
#include "esp_netif_sntp.h"
#include "muse_wifi.h"

static const char *TAG = "presence";

#define CLOCK_SET_YEAR 2025     /* an unset clock starts in 1970 */

static bool s_started;

void presence_clock_poll(void)
{
    if (s_started || !muse_wifi_connected()) {
        return;
    }
    s_started = true;   /* once: a failure here won't fix itself */
    setenv("TZ", CONFIG_MOSAICO_PRESENCE_TZ, 1);
    tzset();
    esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG(CONFIG_MOSAICO_PRESENCE_NTP_SERVER);
    esp_err_t err = esp_netif_sntp_init(&config);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "SNTP didn't start (%s): greetings leave out the time of day", esp_err_to_name(err));
        return;
    }
    ESP_LOGI(TAG, "SNTP from %s, time zone %s", CONFIG_MOSAICO_PRESENCE_NTP_SERVER, CONFIG_MOSAICO_PRESENCE_TZ);
}

bool presence_clock_hour(int *hour)
{
    time_t now = time(NULL);
    struct tm local;
    if (!localtime_r(&now, &local) || local.tm_year + 1900 < CLOCK_SET_YEAR) {
        return false;
    }
    *hour = local.tm_hour;
    return true;
}
