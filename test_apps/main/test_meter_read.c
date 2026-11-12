#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_heap_caps.h"
#include "test_support.h"

static mbus_meter_t *new_meter(mbus_handle *h, const mbus_meter_profile_t *p)
{
    mbus_address a = { .is_primary = 1, .primary = 1 };
    mbus_meter_t *m = mbus_meter_new(h, &a, p);

    TEST_ASSERT_NOT_NULL(m);
    return m;
}

/* profile for scripted replies: one try, no NKE */
static mbus_meter_profile_t quick_profile(void)
{
    mbus_meter_profile_t p = MBUS_METER_PROFILE_GENERIC();

    p.nke_before_read = false;
    p.retries = 0;
    return p;
}

TEST_CASE("read, no slave: first-byte timeout and restore", "[meter]")
{
    mbus_handle *h = tb_open();
    mbus_meter_profile_t p = quick_profile();
    mbus_meter_readout_t out;
    mbus_meter_t *m = new_meter(h, &p);

    /* default wait */
    uint32_t t0 = tb_now_ms();
    int rc_default = mbus_meter_read(m, &out);
    uint32_t dt_default = tb_now_ms() - t0;
    mbus_meter_readout_free(&out);

    /* explicit first-byte wait ends the receive on silence */
    p.rx_first_byte_ms = 60;
    TEST_ASSERT_EQUAL_INT(MBUS_METER_OK, mbus_meter_set_profile(m, &p));
    t0 = tb_now_ms();
    int rc_short = mbus_meter_read(m, &out);
    uint32_t dt_short = tb_now_ms() - t0;
    mbus_meter_readout_free(&out);

    int f = -1, g = -1;
    mbus_serial_get_rx_timeouts(h, &f, &g);
    mbus_meter_free(m);
    printf("no slave: default %u ms, first=60 %u ms\n", (unsigned)dt_default, (unsigned)dt_short);

    TEST_ASSERT_EQUAL_INT(MBUS_METER_ERR_TIMEOUT, rc_default);
    TEST_ASSERT_EQUAL_INT(MBUS_METER_ERR_TIMEOUT, rc_short);
    TEST_ASSERT_TRUE(dt_short >= 50 && dt_short < 200);
    TEST_ASSERT_TRUE(dt_default > dt_short + 150);
    /* profile timeouts are per read, the handle goes back to its own */
    TEST_ASSERT_EQUAL_INT(0, f);
    TEST_ASSERT_EQUAL_INT(0, g);
}

TEST_CASE("read, CI 0x78 frame", "[meter]")
{
    mbus_handle *h = tb_open();
    mbus_meter_profile_t p = quick_profile();
    mbus_meter_readout_t out;
    mbus_meter_t *m = new_meter(h, &p);
    char s[MBUS_METER_VALUE_STR_MAX] = "";

    /* reply arrives 200 ms after the request */
    tb_script_reset(h);
    tb_script_add(TB_FRAME_SMALL, 200);
    tb_script_start();
    int rc = mbus_meter_read(m, &out);
    if (out.n_records > 0)
        mbus_meter_record_to_str(&out.records[0], s, sizeof(s));

    int frames = out.frames;
    size_t nrec = out.n_records;
    int end = out.end_reason;
    mbus_meter_qty_t qty = nrec ? out.records[0].qty : MBUS_METER_QTY_UNKNOWN;
    mbus_meter_readout_free(&out);
    mbus_meter_free(m);

    TEST_ASSERT_EQUAL_INT(MBUS_METER_OK, rc);
    TEST_ASSERT_EQUAL_INT(1, frames);
    TEST_ASSERT_EQUAL_UINT(1, nrec);
    TEST_ASSERT_EQUAL_INT(MBUS_METER_END_LAST_FRAME, end);
    TEST_ASSERT_EQUAL_INT(MBUS_METER_QTY_VOLUME, qty);
    TEST_ASSERT_EQUAL_STRING("305419.896", s);
}

TEST_CASE("read, NKE with required ACK", "[meter]")
{
    mbus_handle *h = tb_open();
    mbus_meter_profile_t p = quick_profile();
    mbus_meter_readout_t out;
    mbus_meter_t *m;

    p.nke_before_read = true;
    p.nke_ack_required = true;
    m = new_meter(h, &p);

    tb_script_reset(h);
    tb_script_add("E5", 150);
    tb_script_add(TB_FRAME_SMALL, 300);
    tb_script_start();
    int rc = mbus_meter_read(m, &out);
    size_t nrec = out.n_records;

    mbus_meter_readout_free(&out);
    mbus_meter_free(m);
    TEST_ASSERT_EQUAL_INT(MBUS_METER_OK, rc);
    TEST_ASSERT_EQUAL_UINT(1, nrec);
}

TEST_CASE("read, NKE without ACK stops before REQ_UD2", "[meter]")
{
    mbus_handle *h = tb_open();
    mbus_meter_profile_t p = quick_profile();
    mbus_meter_readout_t out;
    mbus_meter_t *m;

    p.nke_before_read = true;
    p.nke_ack_required = true;
    m = new_meter(h, &p);

    int rc = mbus_meter_read(m, &out);
    size_t nrec = out.n_records;

    mbus_meter_readout_free(&out);
    mbus_meter_free(m);
    TEST_ASSERT_EQUAL_INT(MBUS_METER_ERR_NO_ACK, rc);
    TEST_ASSERT_EQUAL_UINT(0, nrec);
}

TEST_CASE("rx flush drops bytes in the driver buffer", "[serial]")
{
    mbus_handle *h = tb_open();
    const uint8_t junk[8] = { 1, 2, 3, 4, 5, 6, 7, 8 };
    size_t before = 0, after = 0;

    mbus_serial_write_raw(h, junk, sizeof(junk));
    vTaskDelay(pdMS_TO_TICKS(50));
    uart_get_buffered_data_len(TB_UART_PORT, &before);
    mbus_serial_flush_rx(h);
    uart_get_buffered_data_len(TB_UART_PORT, &after);

    TEST_ASSERT_TRUE(before >= sizeof(junk));
    TEST_ASSERT_EQUAL_UINT(0, after);
}

TEST_CASE("repeated reads do not leak", "[meter]")
{
    mbus_handle *h = tb_open();
    mbus_meter_profile_t p = quick_profile();
    mbus_meter_readout_t out;
    mbus_meter_t *m = new_meter(h, &p);
    size_t base = 0;
    int bad = 0;

    for (int i = 0; i < 6; i++) {
        tb_script_reset(h);
        tb_script_add(TB_FRAME_SMALL, 100);
        tb_script_start();
        if (mbus_meter_read(m, &out) != MBUS_METER_OK)
            bad++;
        mbus_meter_readout_free(&out);
        tb_script_wait(2000);
        vTaskDelay(pdMS_TO_TICKS(50));   /* idle task frees the finished script task */
        if (i == 0)
            base = heap_caps_get_free_size(MALLOC_CAP_DEFAULT);   /* after the first, warm */
    }
    size_t now = heap_caps_get_free_size(MALLOC_CAP_DEFAULT);

    mbus_meter_free(m);
    TEST_ASSERT_EQUAL_INT(0, bad);
    TEST_ASSERT_INT_WITHIN(128, (int)base, (int)now);
}
