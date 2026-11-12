#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "test_support.h"

/* stack use of one mbus_meter_read() with the largest frame */
#define STACK_TEST_BYTES 6144
#define STACK_MIN_FREE   1536   /* regression bound, documented minimum is 3072 */

typedef struct {
    mbus_meter_t  *m;
    int            rc;
    unsigned       n;
    UBaseType_t    free_b;
    volatile bool  done;
} stk_arg_t;

static void stk_task(void *p)
{
    stk_arg_t *a = (stk_arg_t *)p;
    mbus_meter_readout_t out;

    a->rc = mbus_meter_read(a->m, &out);
    a->n = (unsigned)out.n_records;
    mbus_meter_readout_free(&out);
    a->free_b = uxTaskGetStackHighWaterMark(NULL);   /* bytes on esp-idf */
    a->done = true;
    vTaskDelete(NULL);
}

TEST_CASE("read, 25-record frame in a 6144 byte task", "[meter][stack]")
{
    mbus_handle *h = tb_open();
    mbus_address addr = { .is_primary = 1, .primary = 1 };
    mbus_meter_profile_t p = MBUS_METER_PROFILE_GENERIC();

    p.nke_before_read = true;
    p.nke_ack_required = true;
    p.retries = 0;
    p.accept_any_reply = true;   /* frame carries address 0 */
    p.rx_first_byte_ms = 1000;   /* keep the first-byte window out of the measurement */

    stk_arg_t a = { .m = mbus_meter_new(h, &addr, &p) };

    TEST_ASSERT_NOT_NULL(a.m);
    tb_script_reset(h);
    tb_script_add("E5", 150);
    tb_script_add(TB_FRAME_BIG, 300);
    tb_script_start();
    TEST_ASSERT_EQUAL_INT(pdPASS, xTaskCreate(stk_task, "stk", STACK_TEST_BYTES, &a, 5, NULL));

    for (int i = 0; i < 300 && !a.done; i++)
        vTaskDelay(pdMS_TO_TICKS(10));
    tb_script_wait(3000);
    vTaskDelay(pdMS_TO_TICKS(200));

    printf("stack: size=%d free=%u used=%u rc=%d records=%u\n", STACK_TEST_BYTES,
           (unsigned)a.free_b, (unsigned)(STACK_TEST_BYTES - a.free_b), a.rc, a.n);
    mbus_meter_free(a.m);

    TEST_ASSERT_TRUE(a.done);
    TEST_ASSERT_EQUAL_INT(MBUS_METER_OK, a.rc);
    TEST_ASSERT_EQUAL_UINT(TB_BIG_RECORDS, a.n);
    TEST_ASSERT_TRUE(a.free_b >= STACK_MIN_FREE);
}
