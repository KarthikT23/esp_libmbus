#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "test_support.h"

static mbus_link_config_t link_cfg(void)
{
    mbus_link_config_t lc = MBUS_LINK_CONFIG_DEFAULT();

    lc.wakeup_count = 50;        /* short burst, test only */
    lc.wakeup_pause_ms = 100;
    return lc;
}

TEST_CASE("loopback, frame round trip after raw burst", "[loopback]")
{
    mbus_handle *h = tb_open();
    uint8_t wk[20];
    mbus_frame rx;

    /* loopback gives one frame (our own), so don't wait for a second */
    mbus_context_set_option(h, MBUS_OPTION_PURGE_FIRST_FRAME, MBUS_FRAME_PURGE_NONE);

    /* raw 8N1 burst, then back to 8E1; send_frame flushes the echo */
    memset(wk, 0x55, sizeof(wk));
    mbus_serial_set_parity(h, UART_PARITY_DISABLE);
    mbus_serial_write_raw(h, wk, sizeof(wk));
    vTaskDelay(pdMS_TO_TICKS(100));
    mbus_serial_set_parity(h, UART_PARITY_EVEN);

    memset(&rx, 0, sizeof(rx));
    TEST_ASSERT_EQUAL_INT(0, mbus_send_request_frame(h, 1));
    TEST_ASSERT_EQUAL_INT(MBUS_RECV_RESULT_OK, mbus_recv_frame(h, &rx));
    TEST_ASSERT_EQUAL_INT(MBUS_FRAME_TYPE_SHORT, mbus_frame_type(&rx));
    TEST_ASSERT_EQUAL_INT(1, rx.address);
}

TEST_CASE("loopback, FCB variants differ by the FCB bit only", "[loopback]")
{
    mbus_handle *h = tb_open();
    mbus_frame rx0, rx1;

    mbus_context_set_option(h, MBUS_OPTION_PURGE_FIRST_FRAME, MBUS_FRAME_PURGE_NONE);
    memset(&rx0, 0, sizeof(rx0));
    memset(&rx1, 0, sizeof(rx1));

    mbus_send_request_frame_fcb(h, 1, 0);
    TEST_ASSERT_EQUAL_INT(MBUS_RECV_RESULT_OK, mbus_recv_frame(h, &rx0));
    mbus_send_request_frame_fcb(h, 1, 1);
    TEST_ASSERT_EQUAL_INT(MBUS_RECV_RESULT_OK, mbus_recv_frame(h, &rx1));

    TEST_ASSERT_EQUAL_HEX8(0x5B, rx0.control);
    TEST_ASSERT_EQUAL_HEX8(0x7B, rx1.control);
}

TEST_CASE("link ping, direct, no slave", "[link]")
{
    mbus_handle *h = tb_open();
    mbus_link_config_t lc = link_cfg();

    TEST_ASSERT_TRUE(mbus_link_ping(h, 1, &lc, MBUS_LINK_MODE_DIRECT) != 0);
}

TEST_CASE("link ping, direct, slave answers", "[link]")
{
    mbus_handle *h = tb_open();
    mbus_link_config_t lc = link_cfg();

    tb_script_reset(h);
    tb_script_add("E5", 150);
    tb_script_start();
    TEST_ASSERT_EQUAL_INT(0, mbus_link_ping(h, 1, &lc, MBUS_LINK_MODE_DIRECT));
}

TEST_CASE("link ping, wakeup, slow slave answers", "[link]")
{
    mbus_handle *h = tb_open();
    mbus_link_config_t lc = link_cfg();

    tb_script_reset(h);
    tb_script_add("E5", 500);
    tb_script_start();
    TEST_ASSERT_EQUAL_INT(0, mbus_link_ping(h, 1, &lc, MBUS_LINK_MODE_WAKEUP));
}

TEST_CASE("link detect, no slave", "[link]")
{
    mbus_handle *h = tb_open();
    mbus_link_config_t lc = link_cfg();

    TEST_ASSERT_EQUAL_INT(MBUS_LINK_MODE_UNKNOWN, mbus_link_detect(h, 1, &lc));
}

TEST_CASE("link detect, direct slave", "[link]")
{
    mbus_handle *h = tb_open();
    mbus_link_config_t lc = link_cfg();

    tb_script_reset(h);
    tb_script_add("E5", 150);
    tb_script_start();
    TEST_ASSERT_EQUAL_INT(MBUS_LINK_MODE_DIRECT, mbus_link_detect(h, 1, &lc));
}
