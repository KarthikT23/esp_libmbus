/* mbus_meter_read() / probe against the fake bus (fake_slave.c) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "fake_slave.h"
#include "mbus-serial.h"
#include "mbus-meter.h"

extern uint32_t g_now_ms;
extern unsigned g_to_sets;
extern int g_to_first_hist[16], g_to_gap_hist[16];
mbus_handle *fs_serial_handle(fs_bus *b, mbus_serial_data *sd, long baud);

static int n_pass, n_fail;

#define CHECK(cond, ...) do { \
    if (cond) { n_pass++; } \
    else { n_fail++; printf("  FAIL line %d: ", __LINE__); printf(__VA_ARGS__); printf("\n"); } \
} while (0)

#define SEC_A "12345678A7320107"
#define SEC_B "12345678A732010B"

static size_t
vol_rec(uint8_t *r, uint32_t v)
{
    r[0] = 0x04; r[1] = 0x13;
    r[2] = (uint8_t)v; r[3] = (uint8_t)(v >> 8); r[4] = (uint8_t)(v >> 16); r[5] = (uint8_t)(v >> 24);
    return 6;
}

/* n telegrams with nrec volume records each; all but the last end with 1F */
static void
add_multi(fs_bus *b, int s, uint8_t addr, const char *sec, int n_tg, int nrec)
{
    uint8_t rec[64], tg[FS_TG_MAX];
    int t, k;

    for (t = 0; t < n_tg; t++) {
        size_t n = 0, l;

        for (k = 0; k < nrec; k++)
            n += vol_rec(rec + n, 1000u * (unsigned)(t + 1) + (unsigned)k);
        l = fs_build_var(tg, addr, 0x72, sec, (uint8_t)(t + 1), rec, n, t < n_tg - 1);
        fs_add_tg(b, s, tg, l);
    }
}

static void
add_reduced(fs_bus *b, int s, uint8_t addr, const char *sec)
{
    uint8_t rec[16], tg[FS_TG_MAX];
    size_t l = fs_build_var(tg, addr, 0x72, sec, 9, rec, vol_rec(rec, 7), 0);

    fs_set_reduced(b, s, tg, l);
}

static mbus_address
prim(int a)
{
    mbus_address x;

    memset(&x, 0, sizeof(x));
    x.is_primary = 1;
    x.primary = a;
    return x;
}

static mbus_address
sec(const char *s)
{
    mbus_address x;

    memset(&x, 0, sizeof(x));
    x.is_primary = 0;
    x.secondary = (char *)s;
    return x;
}

/* count frames of one kind in the bus log */
static int
count_log(const fs_bus *b, int type, int fn)
{
    int i, n = 0;

    for (i = 0; i < b->n_log; i++)
        if (b->log[i].type == type && (b->log[i].control & 0x0F) == fn)
            n++;
    return n;
}

static int
count_nke(const fs_bus *b, int addr)
{
    int i, n = 0;

    for (i = 0; i < b->n_log; i++)
        if (b->log[i].type == MBUS_FRAME_TYPE_SHORT && (b->log[i].control & 0x0F) == 0x00
            && b->log[i].address == addr)
            n++;
    return n;
}

static bool g_abort;
static bool abort_cb(mbus_handle *h) { (void)h; return g_abort; }

/* ------------------------------------------------------------------ */

static void
t_args(void)
{
    fs_bus b; mbus_handle *h; mbus_meter_t *m; mbus_address a = prim(5), bad = prim(300), s = sec("short");
    mbus_meter_profile_t p = MBUS_METER_PROFILE_GENERIC();
    mbus_meter_readout_t out;

    printf("object and argument checks\n");
    fs_init(&b);
    h = fs_handle(&b);
    CHECK(mbus_meter_new(NULL, &a, NULL) == NULL, "null handle");
    CHECK(mbus_meter_new(h, NULL, NULL) == NULL, "null addr");
    CHECK(mbus_meter_new(h, &bad, NULL) == NULL, "bad primary");
    CHECK(mbus_meter_new(h, &s, NULL) == NULL, "bad secondary");
    p.first_fcb = 2;
    CHECK(mbus_meter_new(h, &a, &p) == NULL, "bad first_fcb");
    p = (mbus_meter_profile_t)MBUS_METER_PROFILE_GENERIC();
    p.baud_list[0] = p.baud_list[1] = p.baud_list[2] = p.baud_list[3] = 2400;
    CHECK(mbus_meter_new(h, &a, &p) == NULL, "baud list without terminator");
    p = (mbus_meter_profile_t)MBUS_METER_PROFILE_GENERIC();
    p.n_filter = 1;
    CHECK(mbus_meter_new(h, &a, &p) == NULL, "filter count without filter");
    m = mbus_meter_new(h, &a, NULL);
    CHECK(m != NULL, "generic");
    CHECK(mbus_meter_link_mode(m) == MBUS_LINK_MODE_UNKNOWN, "mode unknown");
    CHECK(mbus_meter_set_profile(m, &p) == MBUS_METER_ERR_ARG, "set_profile validates");
    CHECK(mbus_meter_read(NULL, &out) == MBUS_METER_ERR_ARG, "read null meter");
    CHECK(mbus_meter_read(m, NULL) == MBUS_METER_ERR_ARG, "read null out");
    CHECK(mbus_meter_probe(m, NULL) == MBUS_METER_ERR_ARG, "probe needs serial");
    mbus_meter_free(m);
    mbus_meter_free(NULL);
    fs_free_handle(h);
}

static void
t_single(void)
{
    fs_bus b; mbus_handle *h; mbus_meter_t *m; mbus_address a = prim(5);
    mbus_meter_readout_t out; uint8_t rec[16], tg[FS_TG_MAX]; size_t l; int s, rc;
    char str[MBUS_METER_VALUE_STR_MAX];

    printf("single telegram, primary\n");
    fs_init(&b);
    s = fs_add_slave(&b, 5, NULL, FS_FCB_STRICT);
    l = fs_build_var(tg, 5, 0x72, SEC_A, 1, rec, vol_rec(rec, 4242), 0);
    fs_add_tg(&b, s, tg, l);
    h = fs_handle(&b);
    m = mbus_meter_new(h, &a, NULL);
    rc = mbus_meter_read(m, &out);
    CHECK(rc == MBUS_METER_OK, "rc=%d", rc);
    CHECK(out.frames == 1 && out.n_records == 1 && out.end_reason == MBUS_METER_END_LAST_FRAME
          && out.last_err == 0, "frames=%d recs=%zu end=%d", out.frames, out.n_records, out.end_reason);
    CHECK(mbus_meter_record_to_str(&out.records[0], str, sizeof(str)) == 5 && strcmp(str, "4.242") == 0, "value");
    CHECK(strcmp(out.info.id_str, "12345678") == 0, "id %s", out.info.id_str);
    CHECK(b.n_log >= 2 && b.log[0].control == 0x40 && b.log[0].address == 5, "NKE first, ctrl=%02X", b.log[0].control);
    CHECK(fs_req_control(&b, 0) == 0x7B, "first REQ_UD2 0x%02X", fs_req_control(&b, 0));
    mbus_meter_readout_free(&out);
    mbus_meter_free(m);
    fs_free_handle(h);
}

static void
t_multi_fcb(void)
{
    fs_bus b; mbus_handle *h; mbus_meter_t *m; mbus_address a = prim(5);
    mbus_meter_readout_t out; int s, rc;
    mbus_meter_profile_t p = MBUS_METER_PROFILE_GENERIC();

    printf("multi telegram, FCB policies\n");
    fs_init(&b);
    s = fs_add_slave(&b, 5, NULL, FS_FCB_STRICT);
    add_multi(&b, s, 5, SEC_A, 3, 2);
    h = fs_handle(&b);
    m = mbus_meter_new(h, &a, NULL);
    rc = mbus_meter_read(m, &out);
    CHECK(rc == MBUS_METER_OK && out.frames == 3 && out.n_records == 6, "rc=%d frames=%d recs=%zu", rc, out.frames, out.n_records);
    CHECK(fs_req_control(&b, 0) == 0x7B && fs_req_control(&b, 1) == 0x5B && fs_req_control(&b, 2) == 0x7B,
          "ctrl %02X %02X %02X", fs_req_control(&b, 0), fs_req_control(&b, 1), fs_req_control(&b, 2));
    CHECK(out.records[5].value.i == 3001, "last value %lld", (long long)out.records[5].value.i);
    CHECK(!out.more_follows, "last frame has no 1F");
    mbus_meter_readout_free(&out);

    /* first_fcb = 0 starts with 0x5B */
    p.first_fcb = 0;
    mbus_meter_set_profile(m, &p);
    b.n_log = 0;
    rc = mbus_meter_read(m, &out);
    /* strict slave after NKE expects FCB=1 first; FCB=0 gets the first telegram twice */
    CHECK(rc == MBUS_METER_OK && out.end_reason == MBUS_METER_END_REPEAT && out.frames == 1 && out.n_records == 2,
          "first_fcb=0: rc=%d end=%d frames=%d recs=%zu", rc, out.end_reason, out.frames, out.n_records);
    CHECK(fs_req_control(&b, 0) == 0x5B && fs_req_control(&b, 1) == 0x7B, "first_fcb=0: %02X %02X",
          fs_req_control(&b, 0), fs_req_control(&b, 1));
    mbus_meter_readout_free(&out);

    /* fixed FCB values (lenient slave, just look at what goes out) */
    fs_init(&b);
    s = fs_add_slave(&b, 5, NULL, FS_FCB_LENIENT);
    add_multi(&b, s, 5, SEC_A, 3, 1);
    p = (mbus_meter_profile_t)MBUS_METER_PROFILE_GENERIC();
    p.fcb_policy = MBUS_METER_FCB_ALWAYS_SET;
    mbus_meter_set_profile(m, &p);
    rc = mbus_meter_read(m, &out);
    CHECK(rc == MBUS_METER_OK && out.frames == 3, "always-set rc=%d", rc);
    CHECK(fs_req_control(&b, 0) == 0x7B && fs_req_control(&b, 1) == 0x7B && fs_req_control(&b, 2) == 0x7B, "always set");
    mbus_meter_readout_free(&out);

    fs_init(&b);
    s = fs_add_slave(&b, 5, NULL, FS_FCB_LENIENT);
    add_multi(&b, s, 5, SEC_A, 3, 1);
    p.fcb_policy = MBUS_METER_FCB_ALWAYS_CLEAR;
    mbus_meter_set_profile(m, &p);
    rc = mbus_meter_read(m, &out);
    CHECK(rc == MBUS_METER_OK && fs_req_control(&b, 0) == 0x5B && fs_req_control(&b, 2) == 0x5B, "always clear");
    mbus_meter_readout_free(&out);

    fs_init(&b);
    s = fs_add_slave(&b, 5, NULL, FS_FCB_STRICT);
    add_multi(&b, s, 5, SEC_A, 1, 1);
    p.fcb_policy = MBUS_METER_FCB_NONE;
    mbus_meter_set_profile(m, &p);
    rc = mbus_meter_read(m, &out);
    CHECK(rc == MBUS_METER_OK && fs_req_control(&b, 0) == 0x4B, "FCV off: 0x%02X", fs_req_control(&b, 0));
    mbus_meter_readout_free(&out);

    mbus_meter_free(m);
    fs_free_handle(h);
}

static void
t_strict_fcb(void)
{
    fs_bus b; mbus_handle *h; mbus_meter_t *m; mbus_address a = prim(5);
    mbus_meter_readout_t out; int s, rc;
    mbus_meter_profile_t p = MBUS_METER_PROFILE_GENERIC();

    printf("strict FCB: reduced set without NKE, abandoned cycle\n");
    fs_init(&b);
    s = fs_add_slave(&b, 5, NULL, FS_FCB_STRICT);
    add_multi(&b, s, 5, SEC_A, 3, 2);
    add_reduced(&b, s, 5, SEC_A);
    h = fs_handle(&b);

    /* without NKE the very first request with FCB=0 hits the repeat branch: reduced set */
    p.nke_before_read = false;
    p.first_fcb = 0;
    m = mbus_meter_new(h, &a, &p);
    rc = mbus_meter_read(m, &out);
    CHECK(rc == MBUS_METER_OK && out.n_records == 1 && out.records[0].value.i == 7,
          "reduced: rc=%d recs=%zu", rc, out.n_records);
    CHECK(count_nke(&b, 5) == 0, "no NKE sent");
    mbus_meter_readout_free(&out);

    /* default profile sends NKE first: full set, 3 telegrams */
    mbus_meter_set_profile(m, NULL);
    rc = mbus_meter_read(m, &out);
    CHECK(rc == MBUS_METER_OK && out.frames == 3 && out.n_records == 6, "full: rc=%d frames=%d recs=%zu", rc, out.frames, out.n_records);
    mbus_meter_readout_free(&out);

    /* abandon after 1 telegram, then read again with NKE: starts over */
    p = (mbus_meter_profile_t)MBUS_METER_PROFILE_GENERIC();
    p.max_frames = 1;
    mbus_meter_set_profile(m, &p);
    rc = mbus_meter_read(m, &out);
    CHECK(rc == MBUS_METER_PARTIAL && out.end_reason == MBUS_METER_END_MAX_FRAMES && out.frames == 1
          && out.more_follows && out.n_records == 2, "abandon: rc=%d end=%d frames=%d", rc, out.end_reason, out.frames);
    CHECK(out.last_err == 0, "cap is not an error");
    mbus_meter_readout_free(&out);

    p.max_frames = 0;
    mbus_meter_set_profile(m, &p);
    rc = mbus_meter_read(m, &out);
    CHECK(rc == MBUS_METER_OK && out.frames == 3 && out.records[0].value.i == 1000, "restart after NKE: rc=%d frames=%d", rc, out.frames);
    mbus_meter_readout_free(&out);

    mbus_meter_free(m);
    fs_free_handle(h);
}

static void
t_repeat(void)
{
    fs_bus b; mbus_handle *h; mbus_meter_t *m; mbus_address a = prim(5);
    mbus_meter_readout_t out; uint8_t rec[16], tg[FS_TG_MAX]; size_t l; int s, rc;
    mbus_meter_profile_t p = MBUS_METER_PROFILE_GENERIC();

    printf("endless 1F loop, repeat detection\n");
    fs_init(&b);
    s = fs_add_slave(&b, 5, NULL, FS_FCB_LENIENT);
    b.slave[s].repeat_last = 1;
    l = fs_build_var(tg, 5, 0x72, SEC_A, 1, rec, vol_rec(rec, 5), 1);   /* always ends with 1F */
    fs_add_tg(&b, s, tg, l);
    h = fs_handle(&b);
    m = mbus_meter_new(h, &a, NULL);
    rc = mbus_meter_read(m, &out);
    CHECK(rc == MBUS_METER_OK && out.end_reason == MBUS_METER_END_REPEAT && out.frames == 1 && out.n_records == 1,
          "rc=%d end=%d frames=%d recs=%zu", rc, out.end_reason, out.frames, out.n_records);
    CHECK(fs_count_req(&b) == 2, "two requests, got %d", fs_count_req(&b));
    mbus_meter_readout_free(&out);

    /* without repeat detection the frame cap ends it */
    p.stop_on_repeat = false;
    p.max_frames = 4;
    mbus_meter_set_profile(m, &p);
    rc = mbus_meter_read(m, &out);
    CHECK(rc == MBUS_METER_PARTIAL && out.end_reason == MBUS_METER_END_MAX_FRAMES && out.frames == 4,
          "rc=%d end=%d frames=%d", rc, out.end_reason, out.frames);
    mbus_meter_readout_free(&out);

    /* default cap is 32 */
    p.max_frames = 0;
    mbus_meter_set_profile(m, &p);
    rc = mbus_meter_read(m, &out);
    CHECK(rc == MBUS_METER_PARTIAL && out.frames == 32, "default cap: frames=%d", out.frames);
    mbus_meter_readout_free(&out);

    mbus_meter_free(m);
    fs_free_handle(h);
}

static void
t_retry(void)
{
    fs_bus b; mbus_handle *h; mbus_meter_t *m; mbus_address a = prim(5);
    mbus_meter_readout_t out; int s, rc;
    mbus_meter_profile_t p = MBUS_METER_PROFILE_GENERIC();
    uint32_t t0;

    printf("timeouts, corrupt frames, retry with the same FCB\n");
    fs_init(&b);
    s = fs_add_slave(&b, 5, NULL, FS_FCB_STRICT);
    add_multi(&b, s, 5, SEC_A, 2, 1);
    h = fs_handle(&b);
    m = mbus_meter_new(h, &a, NULL);

    b.slave[s].silent_requests = 1;                    /* one lost request */
    rc = mbus_meter_read(m, &out);
    CHECK(rc == MBUS_METER_OK && out.frames == 2, "recovered rc=%d frames=%d", rc, out.frames);
    CHECK(fs_req_control(&b, 0) == 0x7B && fs_req_control(&b, 1) == 0x7B && fs_req_control(&b, 2) == 0x5B,
          "retry keeps FCB: %02X %02X %02X", fs_req_control(&b, 0), fs_req_control(&b, 1), fs_req_control(&b, 2));
    mbus_meter_readout_free(&out);

    fs_init(&b);
    s = fs_add_slave(&b, 5, NULL, FS_FCB_STRICT);
    add_multi(&b, s, 5, SEC_A, 2, 1);
    b.corrupt_next = 1;                                /* bad checksum on the NKE ack? no: first data frame */
    b.slave[s].silent_requests = 0;
    rc = mbus_meter_read(m, &out);
    CHECK(rc == MBUS_METER_OK && out.frames == 2, "corrupt/ack mix rc=%d", rc);
    mbus_meter_readout_free(&out);

    fs_init(&b);
    s = fs_add_slave(&b, 5, NULL, FS_FCB_STRICT);
    add_multi(&b, s, 5, SEC_A, 2, 1);
    b.slave[s].silent_requests = 1000;
    t0 = g_now_ms;
    rc = mbus_meter_read(m, &out);
    CHECK(rc == MBUS_METER_ERR_TIMEOUT && out.n_records == 0 && out.last_err == MBUS_METER_ERR_TIMEOUT
          && out.end_reason == MBUS_METER_END_ERROR, "silent rc=%d", rc);
    CHECK(fs_count_req(&b) == 3, "1 + 2 retries, got %d", fs_count_req(&b));
    CHECK(g_now_ms - t0 >= 200, "retry delays counted, %u ms", (unsigned)(g_now_ms - t0));
    mbus_meter_readout_free(&out);

    p.retries = 0;
    mbus_meter_set_profile(m, &p);
    b.n_log = 0;
    rc = mbus_meter_read(m, &out);
    CHECK(rc == MBUS_METER_ERR_TIMEOUT && fs_count_req(&b) == 1, "no retries: reqs=%d", fs_count_req(&b));
    mbus_meter_readout_free(&out);

    /* slave goes quiet after the first telegram: PARTIAL with the data so far */
    fs_init(&b);
    s = fs_add_slave(&b, 5, NULL, FS_FCB_STRICT);
    add_multi(&b, s, 5, SEC_A, 3, 2);
    b.slave[s].silent_after = 1;
    p = (mbus_meter_profile_t)MBUS_METER_PROFILE_GENERIC();
    mbus_meter_set_profile(m, &p);
    rc = mbus_meter_read(m, &out);
    CHECK(rc == MBUS_METER_PARTIAL && out.frames == 1 && out.n_records == 2 && out.last_err == MBUS_METER_ERR_TIMEOUT
          && out.end_reason == MBUS_METER_END_ERROR, "partial rc=%d frames=%d err=%d", rc, out.frames, out.last_err);
    mbus_meter_readout_free(&out);

    /* truncated frame, then good */
    fs_init(&b);
    s = fs_add_slave(&b, 5, NULL, FS_FCB_LENIENT);
    add_multi(&b, s, 5, SEC_A, 1, 1);
    b.corrupt_next = 0;
    {
        mbus_meter_profile_t q = MBUS_METER_PROFILE_GENERIC();

        q.nke_before_read = false;                    /* corrupt_next must hit the data frame */
        mbus_meter_set_profile(m, &q);
    }
    b.corrupt_next = 2;
    /* corrupt_next is consumed by the first emitted frame; the fake only emits on REQ_UD2 here */
    rc = mbus_meter_read(m, &out);
    CHECK(rc == MBUS_METER_OK && out.n_records == 1, "truncated then ok rc=%d", rc);
    CHECK(fs_count_req(&b) == 2, "one retry, reqs=%d", fs_count_req(&b));
    mbus_meter_readout_free(&out);

    mbus_meter_free(m);
    fs_free_handle(h);
}

static void
t_jam(void)
{
    fs_bus b; mbus_handle *h; mbus_meter_t *m; mbus_address a = prim(5);
    mbus_meter_readout_t out; int s, rc;
    mbus_meter_profile_t p = MBUS_METER_PROFILE_GENERIC();

    printf("jammed bus\n");
    fs_init(&b);
    s = fs_add_slave(&b, 5, NULL, FS_FCB_LENIENT);
    add_multi(&b, s, 5, SEC_A, 1, 1);
    p.drain_mode = MBUS_METER_DRAIN_PURGE;
    p.nke_before_read = false;
    h = fs_handle(&b);
    m = mbus_meter_new(h, &a, &p);
    b.jam_left = 100000;
    rc = mbus_meter_read(m, &out);
    /* the frame itself is fine, the drain behind it never ends */
    CHECK(rc == MBUS_METER_PARTIAL && out.last_err == MBUS_METER_ERR_JAM && out.n_records == 1,
          "jam after data: rc=%d err=%d recs=%zu", rc, out.last_err, out.n_records);
    mbus_meter_readout_free(&out);

    /* nothing but noise: error */
    fs_init(&b);
    s = fs_add_slave(&b, 5, NULL, FS_FCB_LENIENT);
    add_multi(&b, s, 5, SEC_A, 1, 1);
    b.slave[s].silent_requests = 1000;
    b.jam_left = 100000;
    rc = mbus_meter_read(m, &out);
    CHECK(rc == MBUS_METER_ERR_JAM || rc == MBUS_METER_ERR_INVALID_FRAME, "noise only rc=%d", rc);
    mbus_meter_readout_free(&out);

    mbus_meter_free(m);
    fs_free_handle(h);
}

static void
t_secondary(void)
{
    fs_bus b; mbus_handle *h; mbus_meter_t *m; mbus_address a = sec(SEC_A);
    mbus_meter_readout_t out; int s, rc;
    mbus_meter_profile_t p = MBUS_METER_PROFILE_GENERIC();

    printf("secondary addressing\n");
    fs_init(&b);
    s = fs_add_slave(&b, -1, SEC_A, FS_FCB_STRICT);
    add_multi(&b, s, 253, SEC_A, 2, 2);
    h = fs_handle(&b);
    m = mbus_meter_new(h, &a, NULL);
    rc = mbus_meter_read(m, &out);
    CHECK(rc == MBUS_METER_OK && out.frames == 2 && out.n_records == 4, "rc=%d frames=%d", rc, out.frames);
    CHECK(count_nke(&b, 253) == 1 && count_log(&b, MBUS_FRAME_TYPE_LONG, 0x03) == 1, "NKE 253 + select");
    CHECK(b.log[0].address == 253 && b.log[0].control == 0x40, "NKE 253 first");
    CHECK(fs_req_control(&b, 0) == 0x7B, "first REQ_UD2 0x%02X", fs_req_control(&b, 0));
    CHECK(mbus_meter_link_mode(m) == MBUS_LINK_MODE_UNKNOWN, "fixed profile mode is not stored");
    mbus_meter_readout_free(&out);

    /* application reset before the read */
    p.app_reset = true;
    p.app_reset_subcode = 0x50;
    mbus_meter_set_profile(m, &p);
    b.n_log = 0;
    rc = mbus_meter_read(m, &out);
    CHECK(rc == MBUS_METER_OK && out.frames == 2, "app reset rc=%d", rc);
    CHECK(count_log(&b, MBUS_FRAME_TYPE_LONG, 0x03) == 2, "select + app reset, %d", count_log(&b, MBUS_FRAME_TYPE_LONG, 0x03));
    {
        int i, seen = 0;

        for (i = 0; i < b.n_log; i++)
            if (b.log[i].ci == MBUS_CONTROL_INFO_APPLICATION_RESET) seen++;
        CHECK(seen == 1, "CI 0x50 sent once");
    }
    mbus_meter_readout_free(&out);

    /* two devices with the same secondary address */
    fs_init(&b);
    s = fs_add_slave(&b, -1, SEC_A, FS_FCB_LENIENT);
    add_multi(&b, s, 253, SEC_A, 1, 1);
    s = fs_add_slave(&b, -1, SEC_A, FS_FCB_LENIENT);
    add_multi(&b, s, 253, SEC_A, 1, 1);
    mbus_meter_set_profile(m, NULL);
    rc = mbus_meter_read(m, &out);
    CHECK(rc == MBUS_METER_ERR_COLLISION && out.n_records == 0, "collision rc=%d", rc);
    mbus_meter_readout_free(&out);

    /* nobody there */
    fs_init(&b);
    s = fs_add_slave(&b, -1, SEC_B, FS_FCB_LENIENT);
    add_multi(&b, s, 253, SEC_B, 1, 1);
    rc = mbus_meter_read(m, &out);
    CHECK(rc == MBUS_METER_ERR_NO_ACK, "not found rc=%d", rc);
    CHECK(count_log(&b, MBUS_FRAME_TYPE_LONG, 0x03) >= 2, "select retried");
    mbus_meter_readout_free(&out);

    mbus_meter_free(m);
    fs_free_handle(h);
}

static void
t_nke_ack(void)
{
    fs_bus b; mbus_handle *h; mbus_meter_t *m; mbus_address a = prim(5);
    mbus_meter_readout_t out; int s, rc;
    mbus_meter_profile_t p = MBUS_METER_PROFILE_STRICT_FCB();

    printf("NKE ack, broadcast NKE, post-NKE delay\n");
    fs_init(&b);
    s = fs_add_slave(&b, 6, NULL, FS_FCB_LENIENT);       /* address 5 does not exist */
    add_multi(&b, s, 6, SEC_A, 1, 1);
    h = fs_handle(&b);
    m = mbus_meter_new(h, &a, &p);
    rc = mbus_meter_read(m, &out);
    CHECK(rc == MBUS_METER_ERR_NO_ACK && out.n_records == 0, "strict NKE needs ack, rc=%d", rc);
    CHECK(fs_count_req(&b) == 0, "no REQ_UD2 after failed NKE");
    mbus_meter_readout_free(&out);

    p = (mbus_meter_profile_t)MBUS_METER_PROFILE_GENERIC();
    mbus_meter_set_profile(m, &p);
    rc = mbus_meter_read(m, &out);
    CHECK(rc == MBUS_METER_ERR_TIMEOUT, "generic ignores a missing NKE ack, then times out: rc=%d", rc);
    mbus_meter_readout_free(&out);

    fs_init(&b);
    s = fs_add_slave(&b, 5, NULL, FS_FCB_STRICT);
    add_multi(&b, s, 5, SEC_A, 1, 1);
    p.nke_broadcast = true;
    p.post_nke_delay_ms = 250;
    mbus_meter_set_profile(m, &p);
    {
        uint32_t t0 = g_now_ms;

        rc = mbus_meter_read(m, &out);
        CHECK(rc == MBUS_METER_OK, "broadcast NKE rc=%d", rc);
        CHECK(count_nke(&b, 255) == 1 && count_nke(&b, 5) == 0, "NKE 255 only");
        CHECK(g_now_ms - t0 >= 250, "post-NKE delay waited");
    }
    mbus_meter_readout_free(&out);

    /* NKE off */
    fs_init(&b);
    s = fs_add_slave(&b, 5, NULL, FS_FCB_LENIENT);
    add_multi(&b, s, 5, SEC_A, 1, 1);
    p = (mbus_meter_profile_t)MBUS_METER_PROFILE_GENERIC();
    p.nke_before_read = false;
    mbus_meter_set_profile(m, &p);
    rc = mbus_meter_read(m, &out);
    CHECK(rc == MBUS_METER_OK && b.log[0].control != 0x40, "no NKE: first frame is REQ_UD2");
    mbus_meter_readout_free(&out);

    mbus_meter_free(m);
    fs_free_handle(h);
}

static void
t_ci(void)
{
    fs_bus b; mbus_handle *h; mbus_meter_t *m; mbus_address a = prim(5);
    mbus_meter_readout_t out; int s, rc;
    uint8_t tg[FS_TG_MAX], rec[16]; size_t l;
    const uint8_t err[1] = { 0x05 };

    printf("application error and unsupported CI\n");
    fs_init(&b);
    s = fs_add_slave(&b, 5, NULL, FS_FCB_LENIENT);
    l = fs_build_long(tg, 5, 0x70, err, 1);
    fs_add_tg(&b, s, tg, l);
    h = fs_handle(&b);
    m = mbus_meter_new(h, &a, NULL);
    rc = mbus_meter_read(m, &out);
    CHECK(rc == MBUS_METER_ERR_APP_ERROR && out.app_error == 5 && out.info.ci == 0x70, "0x70 rc=%d err=%d", rc, out.app_error);
    mbus_meter_readout_free(&out);

    fs_init(&b);
    s = fs_add_slave(&b, 5, NULL, FS_FCB_LENIENT);
    rec[0] = 0x01;
    l = fs_build_long(tg, 5, 0x76, rec, 1);
    fs_add_tg(&b, s, tg, l);
    rc = mbus_meter_read(m, &out);
    CHECK(rc == MBUS_METER_ERR_UNSUPPORTED_CI && out.info.ci == 0x76, "0x76 rc=%d", rc);
    mbus_meter_readout_free(&out);

    /* good telegram with 1F, then an error telegram: partial */
    fs_init(&b);
    s = fs_add_slave(&b, 5, NULL, FS_FCB_STRICT);
    l = fs_build_var(tg, 5, 0x72, SEC_A, 1, rec, vol_rec(rec, 1), 1);
    fs_add_tg(&b, s, tg, l);
    l = fs_build_long(tg, 5, 0x70, err, 1);
    fs_add_tg(&b, s, tg, l);
    rc = mbus_meter_read(m, &out);
    CHECK(rc == MBUS_METER_PARTIAL && out.last_err == MBUS_METER_ERR_APP_ERROR && out.frames == 1 && out.n_records == 1,
          "error after data rc=%d err=%d", rc, out.last_err);
    mbus_meter_readout_free(&out);

    mbus_meter_free(m);
    fs_free_handle(h);
}

static void
t_profile_misc(void)
{
    fs_bus b; mbus_handle *h; mbus_meter_t *m; mbus_address a = prim(5);
    mbus_meter_readout_t out; int s, rc;
    mbus_meter_profile_t p = MBUS_METER_PROFILE_GENERIC();
    mbus_meter_key_t flt[1];
    uint32_t t0;

    printf("min interval, abort, record cap, filter, address check, delays\n");
    fs_init(&b);
    s = fs_add_slave(&b, 5, NULL, FS_FCB_STRICT);
    add_multi(&b, s, 5, SEC_A, 3, 2);
    h = fs_handle(&b);

    p.min_interval_ms = 5000;
    m = mbus_meter_new(h, &a, &p);
    rc = mbus_meter_read(m, &out);
    CHECK(rc == MBUS_METER_OK, "first read rc=%d", rc);
    mbus_meter_readout_free(&out);
    b.n_log = 0;
    rc = mbus_meter_read(m, &out);
    CHECK(rc == MBUS_METER_ERR_TOO_SOON && b.n_log == 0 && out.n_records == 0 && out.last_err == MBUS_METER_ERR_TOO_SOON,
          "too soon rc=%d, bus untouched (%d frames)", rc, b.n_log);
    mbus_meter_readout_free(&out);
    g_now_ms += 5000;
    rc = mbus_meter_read(m, &out);
    CHECK(rc == MBUS_METER_OK, "after the interval rc=%d", rc);
    mbus_meter_readout_free(&out);

    /* abort callback */
    mbus_meter_set_profile(m, NULL);
    h->abort_scan_check = abort_cb;
    g_abort = true;
    b.n_log = 0;
    rc = mbus_meter_read(m, &out);
    CHECK(rc == MBUS_METER_ERR_ABORTED && out.end_reason == MBUS_METER_END_ABORTED && b.n_log == 0, "abort first rc=%d", rc);
    mbus_meter_readout_free(&out);
    g_abort = false;
    h->abort_scan_check = NULL;

    /* record cap */
    p = (mbus_meter_profile_t)MBUS_METER_PROFILE_GENERIC();
    p.max_records = 3;
    mbus_meter_set_profile(m, &p);
    rc = mbus_meter_read(m, &out);
    CHECK(rc == MBUS_METER_PARTIAL && out.n_records == 3 && out.end_reason == MBUS_METER_END_MAX_RECORDS && out.last_err == 0,
          "cap rc=%d recs=%zu end=%d", rc, out.n_records, out.end_reason);
    mbus_meter_readout_free(&out);

    /* filter: storage 0 volume only, 3 telegrams -> 3 hits */
    memset(flt, 0, sizeof(flt));
    flt[0].vif = 0x13;
    p = (mbus_meter_profile_t)MBUS_METER_PROFILE_GENERIC();
    p.filter = flt;
    p.n_filter = 1;
    mbus_meter_set_profile(m, &p);
    rc = mbus_meter_read(m, &out);
    CHECK(rc == MBUS_METER_OK && out.n_records == 6, "filter keeps matching records rc=%d recs=%zu", rc, out.n_records);
    mbus_meter_readout_free(&out);
    flt[0].vif = 0x06;
    rc = mbus_meter_read(m, &out);
    CHECK(rc == MBUS_METER_OK && out.n_records == 0 && out.frames == 3, "filter drops all rc=%d", rc);
    mbus_meter_readout_free(&out);

    /* inter-frame gap counted: 3 telegrams -> 2 gaps */
    p = (mbus_meter_profile_t)MBUS_METER_PROFILE_GENERIC();
    p.inter_frame_gap_ms = 300;
    mbus_meter_set_profile(m, &p);
    t0 = g_now_ms;
    rc = mbus_meter_read(m, &out);
    CHECK(rc == MBUS_METER_OK && g_now_ms - t0 >= 600, "gap %u ms", (unsigned)(g_now_ms - t0));
    mbus_meter_readout_free(&out);
    mbus_meter_free(m);

    /* reply from a different address */
    fs_init(&b);
    s = fs_add_slave(&b, 5, NULL, FS_FCB_LENIENT);
    add_multi(&b, s, 9, SEC_A, 1, 1);                  /* frame says address 9 */
    p = (mbus_meter_profile_t)MBUS_METER_PROFILE_GENERIC();
    m = mbus_meter_new(h, &a, &p);
    rc = mbus_meter_read(m, &out);
    CHECK(rc == MBUS_METER_ERR_INVALID_FRAME && out.n_records == 0, "wrong address rejected rc=%d", rc);
    mbus_meter_readout_free(&out);
    p.accept_any_reply = true;
    mbus_meter_set_profile(m, &p);
    rc = mbus_meter_read(m, &out);
    CHECK(rc == MBUS_METER_OK && out.n_records == 1, "accept_any_reply rc=%d", rc);
    mbus_meter_readout_free(&out);
    mbus_meter_free(m);
    fs_free_handle(h);
}

static void
t_decode_retry(void)
{
    fs_bus b; mbus_handle *h; mbus_meter_t *m; mbus_address a = prim(5);
    mbus_meter_readout_t out; int s, rc;
    /* records: one good, then a record whose data runs past the end */
    const uint8_t bad[] = { 0x04, 0x13, 1, 0, 0, 0, 0x04, 0x13, 2, 0 };
    uint8_t tg[FS_TG_MAX]; size_t l;

    printf("undecodable telegram: retried, then partial data kept\n");
    fs_init(&b);
    s = fs_add_slave(&b, 5, NULL, FS_FCB_STRICT);
    l = fs_build_var(tg, 5, 0x72, SEC_A, 1, bad, sizeof(bad), 0);
    fs_add_tg(&b, s, tg, l);
    h = fs_handle(&b);
    m = mbus_meter_new(h, &a, NULL);
    rc = mbus_meter_read(m, &out);
    CHECK(rc == MBUS_METER_ERR_INVALID_FRAME && out.n_records == 1 && out.last_err == MBUS_METER_ERR_INVALID_FRAME,
          "rc=%d recs=%zu", rc, out.n_records);
    CHECK(fs_count_req(&b) == 3, "1 + 2 retries, got %d", fs_count_req(&b));
    CHECK(out.frames == 1, "frames=%d", out.frames);
    mbus_meter_readout_free(&out);
    mbus_meter_free(m);
    fs_free_handle(h);
}

static void
t_serial_features(void)
{
    fs_bus b; mbus_serial_data sd; mbus_handle *h; mbus_meter_t *m; mbus_address a = prim(5);
    mbus_meter_readout_t out; int s, rc;
    mbus_meter_profile_t p = MBUS_METER_PROFILE_SLOW();

    printf("serial handle: rx timeouts applied and restored, flush, wake-up, probe\n");
    fs_init(&b);
    s = fs_add_slave(&b, 5, NULL, FS_FCB_STRICT);
    add_multi(&b, s, 5, SEC_A, 2, 1);
    h = fs_serial_handle(&b, &sd, 2400);
    sd.rx_first_ms = 111;
    sd.rx_gap_ms = 222;
    g_to_sets = 0;
    m = mbus_meter_new(h, &a, &p);
    rc = mbus_meter_read(m, &out);
    CHECK(rc == MBUS_METER_OK && out.frames == 2, "slow profile rc=%d", rc);
    CHECK(g_to_sets == 2, "timeouts set twice (apply + restore): %u", g_to_sets);
    CHECK(g_to_first_hist[0] == 2000 && g_to_gap_hist[0] == 100, "applied %d/%d", g_to_first_hist[0], g_to_gap_hist[0]);
    CHECK(sd.rx_first_ms == 111 && sd.rx_gap_ms == 222, "restored %d/%d", sd.rx_first_ms, sd.rx_gap_ms);
    CHECK(b.flushes >= 2, "flush drain used: %u", b.flushes);
    mbus_meter_readout_free(&out);

    /* generic profile: first byte 0 = keep the handle's value, gap 100 */
    g_to_sets = 0;
    mbus_meter_set_profile(m, NULL);
    rc = mbus_meter_read(m, &out);
    CHECK(rc == MBUS_METER_OK, "rc=%d", rc);
    CHECK(g_to_sets == 2 && g_to_first_hist[0] == 111 && g_to_gap_hist[0] == 100, "keeps first byte value: %d", g_to_first_hist[0]);
    mbus_meter_readout_free(&out);
    mbus_meter_free(m);
    fs_free_handle(h);

    /* slave that needs the wake-up burst */
    fs_init(&b);
    s = fs_add_slave(&b, 5, NULL, FS_FCB_STRICT);
    b.slave[s].needs_wakeup = 1;
    add_multi(&b, s, 5, SEC_A, 1, 1);
    h = fs_serial_handle(&b, &sd, 2400);

    p = (mbus_meter_profile_t)MBUS_METER_PROFILE_GENERIC();       /* DIRECT */
    m = mbus_meter_new(h, &a, &p);
    rc = mbus_meter_read(m, &out);
    CHECK(rc == MBUS_METER_ERR_TIMEOUT && b.wakeups == 0, "direct mode cannot reach it rc=%d", rc);
    mbus_meter_readout_free(&out);
    mbus_meter_free(m);

    p = (mbus_meter_profile_t)MBUS_METER_PROFILE_OPTICAL();       /* WAKEUP */
    m = mbus_meter_new(h, &a, &p);
    rc = mbus_meter_read(m, &out);
    CHECK(rc == MBUS_METER_OK && b.wakeups == 1, "wake-up mode rc=%d bursts=%u", rc, b.wakeups);
    mbus_meter_readout_free(&out);
    mbus_meter_free(m);

    fs_sleep(&b);
    p = (mbus_meter_profile_t)MBUS_METER_PROFILE_GENERIC();
    p.link_mode = MBUS_LINK_MODE_UNKNOWN;                          /* detect once */
    m = mbus_meter_new(h, &a, &p);
    b.wakeups = 0;
    rc = mbus_meter_read(m, &out);
    CHECK(rc == MBUS_METER_OK && mbus_meter_link_mode(m) == MBUS_LINK_MODE_WAKEUP, "detected rc=%d mode=%d", rc, mbus_meter_link_mode(m));
    mbus_meter_readout_free(&out);
    fs_sleep(&b);
    b.wakeups = 0;
    rc = mbus_meter_read(m, &out);                                 /* mode is remembered: no second detect */
    CHECK(rc == MBUS_METER_OK && b.wakeups == 1, "second read uses the stored mode, bursts=%u", b.wakeups);
    mbus_meter_readout_free(&out);
    mbus_meter_free(m);
    fs_free_handle(h);

    /* secondary + wake-up */
    fs_init(&b);
    s = fs_add_slave(&b, -1, SEC_A, FS_FCB_STRICT);
    b.slave[s].needs_wakeup = 1;
    add_multi(&b, s, 253, SEC_A, 1, 1);
    h = fs_serial_handle(&b, &sd, 2400);
    {
        mbus_address sa = sec(SEC_A);

        p = (mbus_meter_profile_t)MBUS_METER_PROFILE_GENERIC();
        p.link_mode = MBUS_LINK_MODE_UNKNOWN;
        m = mbus_meter_new(h, &sa, &p);
        rc = mbus_meter_read(m, &out);
        CHECK(rc == MBUS_METER_OK && mbus_meter_link_mode(m) == MBUS_LINK_MODE_WAKEUP, "secondary detect rc=%d mode=%d", rc, mbus_meter_link_mode(m));
        mbus_meter_readout_free(&out);
        mbus_meter_free(m);
    }
    fs_free_handle(h);
}

static void
t_probe(void)
{
    fs_bus b; mbus_serial_data sd; mbus_handle *h; mbus_meter_t *m; mbus_address a = prim(5);
    int s, rc;
    uint32_t baud = 0;
    mbus_meter_profile_t p = MBUS_METER_PROFILE_GENERIC();

    printf("baud probe\n");
    fs_init(&b);
    s = fs_add_slave(&b, 5, NULL, FS_FCB_STRICT);
    add_multi(&b, s, 5, SEC_A, 1, 1);
    b.slave_baud = 300;                               /* last-but-one entry of the list */
    h = fs_serial_handle(&b, &sd, 9600);
    p.link_mode = MBUS_LINK_MODE_UNKNOWN;
    m = mbus_meter_new(h, &a, &p);
    rc = mbus_meter_probe(m, &baud);
    CHECK(rc == MBUS_METER_OK && baud == 300 && sd.cfg.baudrate == 300 && mbus_meter_link_mode(m) == MBUS_LINK_MODE_DIRECT,
          "found rc=%d baud=%u uart=%d", rc, (unsigned)baud, sd.cfg.baudrate);
    mbus_meter_free(m);

    b.slave_baud = 19200;                              /* not in the list */
    b.baud = 4800;
    sd.cfg.baudrate = 4800;
    m = mbus_meter_new(h, &a, &p);
    rc = mbus_meter_probe(m, &baud);
    CHECK(rc == MBUS_METER_ERR_TIMEOUT && sd.cfg.baudrate == 4800, "not found rc=%d, baud restored to %d", rc, sd.cfg.baudrate);
    mbus_meter_free(m);

    /* secondary */
    {
        mbus_address sa = sec(SEC_A);

        fs_init(&b);
        s = fs_add_slave(&b, -1, SEC_A, FS_FCB_STRICT);
        add_multi(&b, s, 253, SEC_A, 1, 1);
        b.slave_baud = 2400;
        fs_free_handle(h);
        h = fs_serial_handle(&b, &sd, 300);
        m = mbus_meter_new(h, &sa, &p);
        rc = mbus_meter_probe(m, &baud);
        CHECK(rc == MBUS_METER_OK && baud == 2400, "secondary probe rc=%d baud=%u", rc, (unsigned)baud);
        mbus_meter_free(m);
    }
    fs_free_handle(h);
}

static void
t_strings(void)
{
    printf("strings\n");
    CHECK(strcmp(mbus_meter_strerror(MBUS_METER_ERR_TOO_SOON), "") != 0, "strerror");
    CHECK(strcmp(mbus_meter_end_reason_str(MBUS_METER_END_MAX_FRAMES), "?") != 0, "end reason");
}

int
main(void)
{
    t_args();
    t_single();
    t_multi_fcb();
    t_strict_fcb();
    t_repeat();
    t_retry();
    t_jam();
    t_secondary();
    t_nke_ack();
    t_ci();
    t_profile_misc();
    t_decode_retry();
    t_serial_features();
    t_probe();
    t_strings();
    printf("\n%d checks passed, %d failed\n", n_pass, n_fail);
    return n_fail ? 1 : 0;
}
