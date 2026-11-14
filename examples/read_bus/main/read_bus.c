#include <stdio.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "mbus.h"
#include "bus_reader.h"

static const char *TAG = "read_bus";

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

/* find the meters and create their objects, returns the slot count */
static int discover(mbus_handle *h, const mbus_meter_profile_t *prof, bus_slot_t *slots)
{
    int n;

#if CONFIG_EXAMPLE_FIND_SCAN
    mbus_meter_profile_t sp = *prof;

    sp.rx_first_byte_ms = CONFIG_EXAMPLE_SCAN_FIRST_BYTE_MS;
    ESP_LOGI(TAG, "scanning (slow on a real bus)...");
    n = bus_find_by_scan(h, &sp, slots, CONFIG_EXAMPLE_MAX_METERS);
#else
    n = bus_find_by_list(CONFIG_EXAMPLE_PRIMARY_LIST, slots, CONFIG_EXAMPLE_MAX_METERS);
#endif
    if (n < 0) {
        ESP_LOGW(TAG, "finding meters failed (%d)", n);
        return 0;
    }
    int ok = bus_open(h, prof, slots, (size_t)n);
    ESP_LOGI(TAG, "%d meter(s) found, %d object(s) created", n, ok);
    return n;
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
    bus_slot_t *slots = calloc(CONFIG_EXAMPLE_MAX_METERS, sizeof(*slots));

    if (h == NULL || slots == NULL || mbus_connect(h) != 0) {
        ESP_LOGE(TAG, "setup failed: %s", mbus_error_str());
        free(slots);
        mbus_context_free(h);
        vTaskDelete(NULL);
        return;
    }

    mbus_meter_profile_t prof;
    load_profile(&prof);

    const bus_policy_t pol = {
        .max_fails = CONFIG_EXAMPLE_MAX_FAILS,
        .park_rounds = CONFIG_EXAMPLE_PARK_ROUNDS,
    };

    int n = 0;
    unsigned round = 0;

    for (;;) {
        while (n == 0) {                    /* nothing found: wait and look again */
            n = discover(h, &prof, slots);
            if (n == 0)
                vTaskDelay(pdMS_TO_TICKS(10000));
        }

        int ok = bus_read_round(stdout, slots, (size_t)n, &pol);
        ESP_LOGI(TAG, "round %u: %d of %d meter(s) read, stack min free %u B", round, ok, n,
                 (unsigned)uxTaskGetStackHighWaterMark(NULL));
        round++;

#if CONFIG_EXAMPLE_RESCAN_ROUNDS > 0
        if (round % CONFIG_EXAMPLE_RESCAN_ROUNDS == 0) {    /* pick up new or moved meters */
            bus_close(slots, (size_t)n);
            n = 0;
            continue;
        }
#endif
        vTaskDelay(pdMS_TO_TICKS(CONFIG_EXAMPLE_INTERVAL_S * 1000));
    }
}

void app_main(void)
{
    xTaskCreate(reader_task, "read_bus", CONFIG_EXAMPLE_TASK_STACK, NULL, 5, NULL);
}
