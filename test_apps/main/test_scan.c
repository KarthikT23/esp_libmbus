#include <stdio.h>
#include <string.h>
#include "test_support.h"

typedef struct {
    unsigned n;
    char     first[17];
    unsigned n_probe;
    char     mask[3][17];       /* first masks sent */
} scan_ctx_t;

static int on_found(void *ctx, const char *addr)
{
    scan_ctx_t *c = (scan_ctx_t *)ctx;

    if (c->n == 0)
        snprintf(c->first, sizeof(c->first), "%s", addr);
    c->n++;
    return 0;
}

static void on_probe(void *ctx, const char *mask)
{
    scan_ctx_t *c = (scan_ctx_t *)ctx;

    if (c->n_probe < 3)
        snprintf(c->mask[c->n_probe], sizeof(c->mask[0]), "%s", mask);
    c->n_probe++;
}

/* explicit first-byte wait: silence ends a receive at once */
static mbus_meter_profile_t scan_profile(void)
{
    mbus_meter_profile_t p = MBUS_METER_PROFILE_GENERIC();

    p.retries = 0;
    p.rx_first_byte_ms = 300;
    p.select_settle_ms = 20;
    return p;
}

TEST_CASE("scan, one slave answers the wildcard select", "[scan]")
{
    mbus_handle *h = tb_open();
    mbus_meter_profile_t p = scan_profile();
    scan_ctx_t ctx;
    mbus_meter_scan_result_t res;
    mbus_meter_scan_cfg_t cfg = { .max_found = 1, .on_found = on_found, .on_probe = on_probe, .ctx = &ctx };

    memset(&ctx, 0, sizeof(ctx));

    /* ACK to the select, data frame to the REQ_UD2 that follows the settle time */
    tb_script_reset(h);
    tb_script_add("E5", 150);
    tb_script_add(TB_FRAME_HDR, 650);
    tb_script_start();
    int rc = mbus_meter_scan_secondary(h, &p, &cfg, &res);

    TEST_ASSERT_EQUAL_INT(1, rc);
    TEST_ASSERT_EQUAL_UINT(1, res.found);
    TEST_ASSERT_EQUAL_UINT(1, res.probes);
    TEST_ASSERT_EQUAL_UINT(0, res.unresolved);
    TEST_ASSERT_EQUAL_UINT(1, ctx.n);
    TEST_ASSERT_EQUAL_STRING(TB_HDR_ADDR, ctx.first);
    TEST_ASSERT_EQUAL_STRING("FFFFFFFFFFFFFFFF", ctx.mask[0]);
}

TEST_CASE("scan, collision narrows the first id byte", "[scan]")
{
    mbus_handle *h = tb_open();
    mbus_meter_profile_t p = scan_profile();
    scan_ctx_t ctx;
    mbus_meter_scan_result_t res;
    mbus_meter_scan_cfg_t cfg = { .max_found = 1, .on_found = on_found, .on_probe = on_probe, .ctx = &ctx };

    memset(&ctx, 0, sizeof(ctx));

    /* probe 1: garbled reply = collision. probe 2 (id byte 00): ACK, then data */
    tb_script_reset(h);
    tb_script_add(TB_FRAME_HDR_BAD, 150);
    tb_script_add("E5", 750);
    tb_script_add(TB_FRAME_HDR, 1250);
    tb_script_start();
    int rc = mbus_meter_scan_secondary(h, &p, &cfg, &res);

    TEST_ASSERT_EQUAL_INT(1, rc);
    TEST_ASSERT_EQUAL_UINT(1, res.found);
    TEST_ASSERT_EQUAL_UINT(2, res.probes);
    TEST_ASSERT_EQUAL_UINT(0, res.unresolved);
    TEST_ASSERT_EQUAL_STRING(TB_HDR_ADDR, ctx.first);
    TEST_ASSERT_EQUAL_STRING("FFFFFFFFFFFFFFFF", ctx.mask[0]);
    TEST_ASSERT_EQUAL_STRING("00FFFFFFFFFFFFFF", ctx.mask[1]);
}

TEST_CASE("scan, empty bus finds nothing", "[scan]")
{
    mbus_handle *h = tb_open();
    mbus_meter_profile_t p = scan_profile();
    scan_ctx_t ctx;
    mbus_meter_scan_result_t res;
    mbus_meter_scan_cfg_t cfg = { .on_found = on_found, .on_probe = on_probe, .ctx = &ctx };

    memset(&ctx, 0, sizeof(ctx));
    int rc = mbus_meter_scan_secondary(h, &p, &cfg, &res);

    TEST_ASSERT_EQUAL_INT(0, rc);
    TEST_ASSERT_EQUAL_UINT(0, res.found);
    TEST_ASSERT_EQUAL_UINT(1, res.probes);   /* silence on the wildcard select ends the scan */
}

TEST_CASE("scan, bad mask is rejected", "[scan]")
{
    mbus_handle *h = tb_open();
    mbus_meter_scan_cfg_t cfg = { .mask = "12345" };

    TEST_ASSERT_EQUAL_INT(MBUS_METER_ERR_ARG, mbus_meter_scan_secondary(h, NULL, &cfg, NULL));
}
