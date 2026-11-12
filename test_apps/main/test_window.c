#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "test_support.h"

/* first-byte window: one variable per case */
typedef struct {
    const char *name;
    bool        nke;       /* NKE + ACK first, ACK scripted at 150 ms */
    const char *hex;
    uint32_t    at_ms;     /* data start, from script start */
    uint32_t    first_ms;  /* profile rx_first_byte_ms, 0 = default */
    size_t      n_rec;
} wcase_t;

TEST_CASE("first-byte window cases", "[meter]")
{
    const wcase_t cases[] = {
        { "small nke  300 first=0  ", true,  TB_FRAME_SMALL, 300, 0,   1 },
        { "big   nke  300 first=0  ", true,  TB_FRAME_BIG,   300, 0,   TB_BIG_RECORDS },
        { "big   nke  300 first=300", true,  TB_FRAME_BIG,   300, 300, TB_BIG_RECORDS },
        { "small plain 200 first=0 ", false, TB_FRAME_SMALL, 200, 0,   1 },
        { "big   plain 200 first=0 ", false, TB_FRAME_BIG,   200, 0,   TB_BIG_RECORDS },
        { "big   plain 100 first=0 ", false, TB_FRAME_BIG,   100, 0,   TB_BIG_RECORDS },
    };

    mbus_handle *h = tb_open();
    mbus_address addr = { .is_primary = 1, .primary = 1 };
    mbus_meter_profile_t p = MBUS_METER_PROFILE_GENERIC();
    mbus_meter_t *m = mbus_meter_new(h, &addr, &p);

    TEST_ASSERT_NOT_NULL(m);

    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        const wcase_t *c = &cases[i];
        mbus_meter_readout_t out;

        p = (mbus_meter_profile_t)MBUS_METER_PROFILE_GENERIC();
        p.nke_before_read = c->nke;
        p.nke_ack_required = c->nke;
        p.retries = 0;
        p.accept_any_reply = true;       /* the big frame carries address 0 */
        p.rx_first_byte_ms = c->first_ms;
        mbus_meter_set_profile(m, &p);

        tb_script_reset(h);
        if (c->nke)
            tb_script_add("E5", 150);
        tb_script_add(c->hex, c->at_ms);
        tb_script_start();

        uint32_t t0 = tb_now_ms();
        int rc = mbus_meter_read(m, &out);
        uint32_t dt = tb_now_ms() - t0;
        size_t nrec = out.n_records;

        mbus_meter_readout_free(&out);
        printf("window %s rc=%d records=%u %u ms\n", c->name, rc, (unsigned)nrec, (unsigned)dt);

        /* let the script finish and the line settle before the next case */
        tb_script_wait(3000);
        vTaskDelay(pdMS_TO_TICKS(300));

        if (rc != MBUS_METER_OK || nrec != c->n_rec) {
            mbus_meter_free(m);
            TEST_FAIL_MESSAGE(c->name);
        }
    }
    mbus_meter_free(m);
}
