#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#include "test_support.h"

/* test/test-frames/EFE_Engelmann-Elster-SensoStar-2.hex */
const char TB_FRAME_BIG[] =
    "68 A1 A1 68 08 00 72 45 33 08 24 C5 14 00 04 66 27 00 00 04 78 91 7B 6F 01 "
    "04 6D 17 2E CC 13 04 15 00 00 00 00 44 15 00 00 00 00 84 01 15 00 00 00 00 "
    "04 06 00 00 00 00 44 06 00 00 00 00 84 01 06 00 00 00 00 84 10 06 00 00 00 "
    "00 C4 10 06 00 00 00 00 84 11 06 00 00 00 00 42 6C BF 1C 02 6C DF 1C 84 20 "
    "06 00 00 00 00 84 30 06 00 00 00 00 04 3B 00 00 00 00 14 3B 19 00 00 00 04 "
    "2B 00 00 00 00 14 2B 0B 00 00 00 02 5B 16 00 02 5F 15 00 04 61 09 00 00 00 "
    "02 23 0C 02 01 FD 17 00 04 90 28 0B 00 00 00 EB 16";

/* hand built vectors */
const char TB_FRAME_SMALL[]   = "68 09 09 68 08 01 78 04 13 78 56 34 12 AC 16";
const char TB_FRAME_CI7A[]    = "68 0D 0D 68 08 01 7A 05 00 00 00 04 13 78 56 34 12 B3 16";
const char TB_FRAME_HDR[]     = "68 15 15 68 08 00 72 45 33 08 24 C5 14 00 04 01 00 00 00 "
                                "04 13 78 56 34 12 27 16";
const char TB_FRAME_HDR_BAD[] = "68 15 15 68 08 00 72 45 33 08 24 C5 14 00 04 01 00 00 00 "
                                "04 13 78 56 34 12 28 16";

#define STEPS_MAX  4
#define STEP_BYTES 260

typedef struct {
    uint32_t at_ms;
    uint8_t  data[STEP_BYTES];
    size_t   len;
} step_t;

static struct {
    mbus_handle  *h;
    step_t        step[STEPS_MAX];
    int           n;
    volatile bool started;
    volatile bool done;
} s_script;

static mbus_handle *s_h;
static bool s_connected;

mbus_handle *tb_open(void)
{
    mbus_serial_config_t sc = MBUS_SERIAL_CONFIG_DEFAULT();

    sc.port = TB_UART_PORT;
    sc.tx_pin = CONFIG_TB_UART_TX_PIN;
    sc.rx_pin = CONFIG_TB_UART_RX_PIN;

    s_h = mbus_context_serial(&sc);
    TEST_ASSERT_NOT_NULL(s_h);
    TEST_ASSERT_EQUAL_INT(0, mbus_connect(s_h));
    s_connected = true;
    return s_h;
}

void tb_close(void)
{
    if (s_h == NULL)
        return;
    if (s_connected)
        mbus_disconnect(s_h);
    mbus_context_free(s_h);
    s_h = NULL;
    s_connected = false;
}

uint32_t tb_now_ms(void)
{
    return xTaskGetTickCount() * portTICK_PERIOD_MS;
}

void tb_script_reset(mbus_handle *h)
{
    memset(&s_script, 0, sizeof(s_script));
    s_script.h = h;
}

void tb_script_add(const char *hex, uint32_t at_ms)
{
    TEST_ASSERT_TRUE(s_script.n < STEPS_MAX);
    step_t *st = &s_script.step[s_script.n++];

    st->at_ms = at_ms;
    st->len = mbus_hex2bin(st->data, sizeof(st->data), (const unsigned char *)hex, strlen(hex));
    TEST_ASSERT_TRUE(st->len > 0);
}

static void script_task(void *arg)
{
    TickType_t t0 = xTaskGetTickCount();

    (void)arg;
    for (int i = 0; i < s_script.n; i++) {
        uint32_t el = (xTaskGetTickCount() - t0) * portTICK_PERIOD_MS;

        /* absolute times, a long step does not push the next ones */
        if (s_script.step[i].at_ms > el)
            vTaskDelay(pdMS_TO_TICKS(s_script.step[i].at_ms - el));
        mbus_serial_write_raw(s_script.h, s_script.step[i].data, s_script.step[i].len);
    }
    s_script.done = true;
    vTaskDelete(NULL);
}

void tb_script_start(void)
{
    s_script.done = false;
    s_script.started = true;
    TEST_ASSERT_EQUAL_INT(pdPASS, xTaskCreate(script_task, "tb_script", 4096, NULL, 5, NULL));
}

bool tb_script_wait(uint32_t timeout_ms)
{
    for (uint32_t t = 0; s_script.started && !s_script.done && t < timeout_ms; t += 10)
        vTaskDelay(pdMS_TO_TICKS(10));
    return !s_script.started || s_script.done;
}

void setUp(void)
{
}

/* runs after a failed assert too: the script task must be done before the
 * handle goes away, and the UART driver must not stay installed */
void tearDown(void)
{
    tb_script_wait(5000);
    s_script.started = false;
    tb_close();
}
