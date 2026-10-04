// SPDX-License-Identifier: Apache-2.0
// Bluetooth LE radio check: advertise as DIAG_NAME (connectable, like Muse)
// and, every SCAN_PERIOD_MS, scan for SCAN_MS and log what was heard. Wi-Fi
// stays off. Results are read through `mosaico.py iris logs`.
#include <stdatomic.h>
#include <string.h>
#include "esp_iris.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "iris_ota_support.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "nvs_flash.h"
#include "services/gap/ble_svc_gap.h"

#define DIAG_NAME "MosaicoBLE-DIAG"
#define SCAN_MS 5000
#define SCAN_PERIOD_MS 15000
#define MAX_SEEN 64
#define TOP_REPORTED 5

typedef struct {
    ble_addr_t addr;
    int8_t rssi;
    char name[24];
} seen_t;

static const char *TAG = "ble_diag";

// Host task writes during a scan; the diag task reads after DISC_COMPLETE.
static seen_t s_seen[MAX_SEEN];
static int s_seen_count;
static int s_reports;
static uint8_t s_own_addr_type;
static atomic_bool s_synced = ATOMIC_VAR_INIT(false);
static atomic_bool s_scan_done = ATOMIC_VAR_INIT(true);

static int gap_event(struct ble_gap_event *event, void *arg);

static void start_advertising(void)
{
    struct ble_hs_adv_fields fields = {0};
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.name = (uint8_t *)DIAG_NAME;
    fields.name_len = strlen(DIAG_NAME);
    fields.name_is_complete = 1;
    fields.tx_pwr_lvl_is_present = 1;
    fields.tx_pwr_lvl = BLE_HS_ADV_TX_PWR_LVL_AUTO;
    int rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0) {
        ESP_LOGE(TAG, "advertising data rejected: rc=%d", rc);
        return;
    }
    struct ble_gap_adv_params params = {0};
    params.conn_mode = BLE_GAP_CONN_MODE_UND;
    params.disc_mode = BLE_GAP_DISC_MODE_GEN;
    rc = ble_gap_adv_start(s_own_addr_type, NULL, BLE_HS_FOREVER, &params, gap_event, NULL);
    ESP_LOGI(TAG, "advertising as %s: rc=%d", DIAG_NAME, rc);
}

static void remember(const struct ble_gap_disc_desc *disc)
{
    seen_t *entry = NULL;
    for (int i = 0; i < s_seen_count; ++i) {
        if (!ble_addr_cmp(&s_seen[i].addr, &disc->addr)) {
            entry = &s_seen[i];
            break;
        }
    }
    if (!entry) {
        if (s_seen_count >= MAX_SEEN) return;
        entry = &s_seen[s_seen_count++];
        memset(entry, 0, sizeof(*entry));
        entry->addr = disc->addr;
        entry->rssi = disc->rssi;
    } else if (disc->rssi > entry->rssi) {
        entry->rssi = disc->rssi;
    }
    struct ble_hs_adv_fields fields;
    if (!entry->name[0] && ble_hs_adv_parse_fields(&fields, disc->data, disc->length_data) == 0
        && fields.name_len > 0) {
        size_t len = fields.name_len < sizeof(entry->name) - 1 ? fields.name_len
                                                                : sizeof(entry->name) - 1;
        memcpy(entry->name, fields.name, len);
        entry->name[len] = '\0';
    }
}

static int gap_event(struct ble_gap_event *event, void *arg)
{
    (void)arg;
    switch (event->type) {
    case BLE_GAP_EVENT_DISC:
        remember(&event->disc);
        break;
    case BLE_GAP_EVENT_DISC_COMPLETE:
        atomic_store(&s_scan_done, true);
        break;
    case BLE_GAP_EVENT_CONNECT:
        ESP_LOGI(TAG, "central connected: status=%d", event->connect.status);
        if (event->connect.status != 0) start_advertising();
        break;
    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGI(TAG, "central disconnected: reason=%d", event->disconnect.reason);
        start_advertising();
        break;
    case BLE_GAP_EVENT_ADV_COMPLETE:
        ESP_LOGW(TAG, "advertising stopped: reason=%d", event->adv_complete.reason);
        start_advertising();
        break;
    default:
        break;
    }
    return 0;
}

static void on_sync(void)
{
    int rc = ble_hs_util_ensure_addr(0);
    if (rc == 0) rc = ble_hs_id_infer_auto(0, &s_own_addr_type);
    if (rc != 0) {
        ESP_LOGE(TAG, "no usable Bluetooth address: rc=%d", rc);
        return;
    }
    uint8_t addr[6] = {0};
    ble_hs_id_copy_addr(s_own_addr_type, addr, NULL);
    ESP_LOGI(TAG, "host synced; address %02x:%02x:%02x:%02x:%02x:%02x type %u",
             addr[5], addr[4], addr[3], addr[2], addr[1], addr[0], s_own_addr_type);
    start_advertising();
    atomic_store(&s_synced, true);
}

static void on_reset(int reason)
{
    ESP_LOGE(TAG, "host reset by the controller: reason=%d", reason);
}

static void host_task(void *arg)
{
    (void)arg;
    nimble_port_run();
    nimble_port_freertos_deinit();
}

static void report_scan(void)
{
    // Order by signal strength in place; at most MAX_SEEN entries.
    for (int i = 1; i < s_seen_count; ++i) {
        seen_t key = s_seen[i];
        int j = i - 1;
        while (j >= 0 && s_seen[j].rssi < key.rssi) {
            s_seen[j + 1] = s_seen[j];
            --j;
        }
        s_seen[j + 1] = key;
    }
    ++s_reports;
    ESP_LOGI(TAG, "scan %d: heard %d device(s); advertising %s", s_reports, s_seen_count,
             ble_gap_adv_active() ? "on" : "OFF");
    for (int i = 0; i < s_seen_count && i < TOP_REPORTED; ++i) {
        const uint8_t *a = s_seen[i].addr.val;
        ESP_LOGI(TAG, "  %4d dBm  %02x:%02x:%02x:%02x:%02x:%02x  %s", s_seen[i].rssi,
                 a[5], a[4], a[3], a[2], a[1], a[0],
                 s_seen[i].name[0] ? s_seen[i].name : "(no name)");
    }
}

static void diag_task(void *arg)
{
    (void)arg;
    while (!atomic_load(&s_synced)) vTaskDelay(pdMS_TO_TICKS(100));
    for (;;) {
        s_seen_count = 0;
        struct ble_gap_disc_params params = {0};
        params.passive = 1;
        params.filter_duplicates = 1;
        atomic_store(&s_scan_done, false);
        int rc = ble_gap_disc(s_own_addr_type, SCAN_MS, &params, gap_event, NULL);
        if (rc != 0) {
            atomic_store(&s_scan_done, true);
            ESP_LOGE(TAG, "scan did not start: rc=%d; advertising %s", rc,
                     ble_gap_adv_active() ? "on" : "OFF");
        } else {
            while (!atomic_load(&s_scan_done)) vTaskDelay(pdMS_TO_TICKS(100));
            report_scan();
        }
        vTaskDelay(pdMS_TO_TICKS(SCAN_PERIOD_MS - SCAN_MS));
    }
}

void app_main(void)
{
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_iris_boot_probe());
    // Another app's data may live in nvs; never erase it here.
    ESP_ERROR_CHECK_WITHOUT_ABORT(nvs_flash_init());
    iris_ota_support_start();

    esp_err_t err = nimble_port_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Bluetooth controller/host init failed: %s", esp_err_to_name(err));
        return;
    }
    ble_hs_cfg.sync_cb = on_sync;
    ble_hs_cfg.reset_cb = on_reset;
    ble_svc_gap_init();
    ble_svc_gap_device_name_set(DIAG_NAME);
    nimble_port_freertos_init(host_task);
    if (xTaskCreate(diag_task, "ble_diag", 4096, NULL, 4, NULL) != pdPASS) {
        ESP_LOGE(TAG, "diag task not started");
    }
    ESP_LOGI(TAG, "Bluetooth LE check started; Wi-Fi is off");
}
