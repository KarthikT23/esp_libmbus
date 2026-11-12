#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "mbus.h"

static const char *TAG = "read_meter";

static void load_profile(mbus_meter_profile_t *p)
{
#if CONFIG_EXAMPLE_PROFILE_STRICT_FCB
    *p = (mbus_meter_profile_t)MBUS_METER_PROFILE_STRICT_FCB();
#elif CONFIG_EXAMPLE_PROFILE_SLOW
    *p = (mbus_meter_profile_t)MBUS_METER_PROFILE_SLOW();
#elif CONFIG_EXAMPLE_PROFILE_OPTICAL
    *p = (mbus_meter_profile_t)MBUS_METER_PROFILE_OPTICAL();
#else
    *p = (mbus_meter_profile_t)MBUS_METER_PROFILE_GENERIC();
#endif
}

#if CONFIG_EXAMPLE_SCAN_AT_START
static int on_found(void *ctx, const char *addr)
{
    (void)ctx;
    ESP_LOGI(TAG, "scan: found %s", addr);
    return 0;   /* keep scanning */
}

static void run_scan(mbus_handle *h, const mbus_meter_profile_t *base)
{
    mbus_meter_profile_t p = *base;
    mbus_meter_scan_cfg_t cfg = { .on_found = on_found };
    mbus_meter_scan_result_t res;

    p.rx_first_byte_ms = CONFIG_EXAMPLE_SCAN_FIRST_BYTE_MS;
    int rc = mbus_meter_scan_secondary(h, &p, &cfg, &res);
    if (rc < 0)
        ESP_LOGW(TAG, "scan: %s", mbus_meter_strerror(rc));
    else
        ESP_LOGI(TAG, "scan: found=%u unresolved=%u probes=%u",
                 res.found, res.unresolved, res.probes);
}
#endif

static void print_record(size_t i, const mbus_meter_record_t *r)
{
    char val[MBUS_METER_VALUE_STR_MAX];
    const char *unit = r->unit_text ? r->unit_text : mbus_meter_unit_str(r->unit);

    if (mbus_meter_record_to_str(r, val, sizeof(val)) < 0)
        strcpy(val, "?");

    ESP_LOGI(TAG, "%2u %-22s %s %s  st=%llu tf=%lu fn=%u fl=0x%04x",
             (unsigned)i, mbus_meter_qty_str(r->qty), val, unit,
             (unsigned long long)r->key.storage, (unsigned long)r->key.tariff,
             (unsigned)r->key.function, (unsigned)r->flags);
}

static void reader_task(void *arg)
{
    (void)arg;

    mbus_serial_config_t sc = MBUS_SERIAL_CONFIG_DEFAULT();
    sc.port = (uart_port_t)CONFIG_EXAMPLE_UART_PORT;
    sc.tx_pin = CONFIG_EXAMPLE_UART_TX_PIN;
    sc.rx_pin = CONFIG_EXAMPLE_UART_RX_PIN;
    sc.baudrate = CONFIG_EXAMPLE_BAUDRATE;

    mbus_handle *h = mbus_context_serial(&sc);
    if (h == NULL || mbus_connect(h) != 0) {
        ESP_LOGE(TAG, "connect failed: %s", mbus_error_str());
        mbus_context_free(h);
        vTaskDelete(NULL);
        return;
    }

    mbus_meter_profile_t prof;
    load_profile(&prof);

#if CONFIG_EXAMPLE_SCAN_AT_START
    run_scan(h, &prof);
#endif

#if CONFIG_EXAMPLE_ADDR_PRIMARY
    mbus_address addr = { .is_primary = 1, .primary = CONFIG_EXAMPLE_PRIMARY_ADDR };
#else
    mbus_address addr = { .is_primary = 0, .secondary = (char *)CONFIG_EXAMPLE_SECONDARY_ADDR };
#endif

    mbus_meter_t *m = mbus_meter_new(h, &addr, &prof);   /* copies addr + profile */
    if (m == NULL) {
        ESP_LOGE(TAG, "mbus_meter_new failed (bad address or profile)");
        mbus_disconnect(h);
        mbus_context_free(h);
        vTaskDelete(NULL);
        return;
    }

#if CONFIG_EXAMPLE_PROBE_BAUD
    uint32_t baud = 0;
    int pr = mbus_meter_probe(m, &baud);
    if (pr == 0)
        ESP_LOGI(TAG, "probe: %u baud", (unsigned)baud);
    else
        ESP_LOGW(TAG, "probe: %s", mbus_meter_strerror(pr));
#endif

    for (;;) {
        mbus_meter_readout_t out;
        int rc = mbus_meter_read(m, &out);

        ESP_LOGI(TAG, "read: rc=%d (%s) end=%s frames=%u records=%u", rc,
                 mbus_meter_strerror(rc), mbus_meter_end_reason_str(out.end_reason),
                 (unsigned)out.frames, (unsigned)out.n_records);

        if (out.info.has_header)
            ESP_LOGI(TAG, "id=%s mfr=%s medium=%s ver=%u status=0x%02x",
                     out.info.id_str, out.info.manufacturer,
                     mbus_meter_medium_str(out.info.medium),
                     (unsigned)out.info.version, (unsigned)out.info.status);

        for (size_t i = 0; i < out.n_records; i++)
            print_record(i, &out.records[i]);

        mbus_meter_readout_free(&out);

        /* bytes on esp-idf */
        ESP_LOGI(TAG, "stack min free: %u B of %d", (unsigned)uxTaskGetStackHighWaterMark(NULL),
                 CONFIG_EXAMPLE_TASK_STACK);
        vTaskDelay(pdMS_TO_TICKS(CONFIG_EXAMPLE_INTERVAL_S * 1000));
    }
}

void app_main(void)
{
    xTaskCreate(reader_task, "read_meter", CONFIG_EXAMPLE_TASK_STACK, NULL, 5, NULL);
}