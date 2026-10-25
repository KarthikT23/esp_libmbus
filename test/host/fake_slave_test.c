/* Master-side tests against the in-memory bus (fake_slave.c). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "fake_slave.h"

static int n_fail, n_pass;

#define CHECK(cond, ...) do { \
    if (cond) { n_pass++; } \
    else { n_fail++; printf("  FAIL line %d: ", __LINE__); printf(__VA_ARGS__); printf("\n"); } \
} while (0)

#define SEC_A "12345678A7320107"
#define SEC_B "12345678A732010B"
#define SEC_C "12349999A7320107"

/* volume record, DIF 04 (32 bit), VIF 13 (m^3 * 1e-3) */
static size_t vol_rec(uint8_t *r, uint32_t v)
{
    r[0] = 0x04; r[1] = 0x13;
    r[2] = (uint8_t)v; r[3] = (uint8_t)(v >> 8); r[4] = (uint8_t)(v >> 16); r[5] = (uint8_t)(v >> 24);
    return 6;
}

/* 3 telegrams, first two end with 1F. rec_count records in each. */
static void add_multi(fs_bus *b, int s, const char *sec, int nrec_each)
{
    uint8_t rec[64], tg[FS_TG_MAX];
    int t, k;

    for (t = 0; t < 3; t++) {
        size_t n = 0, l;

        for (k = 0; k < nrec_each; k++)
            n += vol_rec(rec + n, 1000u * (t + 1) + k);
        l = fs_build_var(tg, (uint8_t)(b->slave[s].primary >= 0 ? b->slave[s].primary : 1),
                         0x72, sec, (uint8_t)(t + 1), rec, n, t < 2);
        fs_add_tg(b, s, tg, l);
    }
}

static void add_reduced(fs_bus *b, int s, const char *sec)
{
    uint8_t rec[16], tg[FS_TG_MAX];
    size_t n = vol_rec(rec, 7), l;

    l = fs_build_var(tg, 1, 0x72, sec, 9, rec, n, 0);
    fs_set_reduced(b, s, tg, l);
}

static size_t ud_size(const fs_slave *s, int i)
{
    return s->tg_len[i] - 9;
}

static int chain_records(mbus_frame *reply)
{
    int total = 0;
    mbus_frame *f;

    for (f = reply; f; f = (mbus_frame *)f->next) {
        mbus_frame_data d;
        mbus_data_record *r;

        memset(&d, 0, sizeof(d));
        if (mbus_frame_data_parse(f, &d) != 0)
            return -1;
        for (r = d.data_var.record; r; r = r->next)
            if (r->drh.dib.dif != 0x1F && r->drh.dib.dif != 0x0F)
                total++;
        if (d.data_var.record)
            mbus_data_record_free(d.data_var.record);
    }
    return total;
}

static void free_chain(mbus_frame *reply)
{
    if (reply->next)
        mbus_frame_free((mbus_frame *)reply->next);
}

/* ---- tests ---- */

static void t_single(void)
{
    fs_bus b; mbus_handle *h; mbus_frame reply; uint8_t rec[16], tg[FS_TG_MAX];
    size_t l;
    int s, r;

    printf("single frame, primary\n");
    fs_init(&b);
    s = fs_add_slave(&b, 5, NULL, FS_FCB_LENIENT);
    l = fs_build_var(tg, 5, 0x72, "12345678A7320107", 1, rec, vol_rec(rec, 4242), 0);
    fs_add_tg(&b, s, tg, l);
    h = fs_handle(&b);
    memset(&reply, 0, sizeof(reply));
    r = mbus_sendrecv_request(h, 5, &reply, 16);
    CHECK(r == 0, "rc=%d", r);
    CHECK(fs_count_frames(&reply) == 1, "frames=%d", fs_count_frames(&reply));
    CHECK(chain_records(&reply) == 1, "records=%d", chain_records(&reply));
    CHECK(fs_req_control(&b, 0) == 0x7B, "first REQ_UD2 = 0x%02X", fs_req_control(&b, 0));
    free_chain(&reply);
    fs_free_handle(h);
}

static void t_multi(void)
{
    fs_bus b; mbus_handle *h; mbus_frame reply; int s, r;

    printf("multi telegram, FCB toggles\n");
    fs_init(&b);
    s = fs_add_slave(&b, 5, NULL, FS_FCB_STRICT);
    add_multi(&b, s, SEC_A, 2);
    h = fs_handle(&b);
    memset(&reply, 0, sizeof(reply));
    r = mbus_sendrecv_request(h, 5, &reply, 16);
    CHECK(r == 0, "rc=%d", r);
    CHECK(fs_count_frames(&reply) == 3, "frames=%d", fs_count_frames(&reply));
    CHECK(chain_records(&reply) == 6, "records=%d", chain_records(&reply));
    CHECK(fs_req_control(&b, 0) == 0x7B && fs_req_control(&b, 1) == 0x5B && fs_req_control(&b, 2) == 0x7B,
          "ctrl %02X %02X %02X", fs_req_control(&b, 0), fs_req_control(&b, 1), fs_req_control(&b, 2));
    free_chain(&reply);
    fs_free_handle(h);
}

static void t_fcb_first_request(void)
{
    fs_bus b; mbus_handle *h; mbus_frame fr; int s, r;

    printf("strict meter: first request FCB=0 vs FCB=1 (needs _fcb)\n");
    fs_init(&b);
    s = fs_add_slave(&b, 5, NULL, FS_FCB_STRICT);
    add_multi(&b, s, SEC_A, 2);
    add_reduced(&b, s, SEC_A);
    h = fs_handle(&b);

    mbus_send_ping_frame(h, 5, 1);
    mbus_send_request_frame(h, 5);                      /* 0x5B */
    memset(&fr, 0, sizeof(fr));
    r = mbus_recv_frame(h, &fr);
    CHECK(r == MBUS_RECV_RESULT_OK, "rc=%d", r);
    CHECK(fr.data_size == b.slave[s].reduced_len - 9, "0x5B got %zu bytes (reduced)", fr.data_size);

    mbus_send_ping_frame(h, 5, 1);
    mbus_send_request_frame_fcb(h, 5, 1);               /* 0x7B */
    memset(&fr, 0, sizeof(fr));
    r = mbus_recv_frame(h, &fr);
    CHECK(r == MBUS_RECV_RESULT_OK, "rc=%d", r);
    CHECK(fr.data_size == ud_size(&b.slave[s], 0), "0x7B got %zu bytes, want %zu",
          fr.data_size, ud_size(&b.slave[s], 0));
    fs_free_handle(h);
}

static void t_nke_first(void)
{
    fs_bus b; mbus_handle *h; mbus_frame reply; int s, r;

    printf("abandoned cycle: new read continues unless SND_NKE first\n");
    fs_init(&b);
    s = fs_add_slave(&b, 5, NULL, FS_FCB_STRICT);
    add_multi(&b, s, SEC_A, 2);
    h = fs_handle(&b);

    /* previous cycle stopped after frame 2 (FCB now 0) */
    mbus_send_request_frame_fcb(h, 5, 1); mbus_purge_frames(h);
    mbus_send_request_frame_fcb(h, 5, 0); mbus_purge_frames(h);

    memset(&reply, 0, sizeof(reply));
    r = mbus_sendrecv_request(h, 5, &reply, 16);
    CHECK(r == 0, "rc=%d", r);
    CHECK(reply.data_size == ud_size(&b.slave[s], 2), "no NKE: first frame is #3 (%zu bytes)", reply.data_size);
    free_chain(&reply);

    mbus_send_ping_frame(h, 5, 1);
    memset(&reply, 0, sizeof(reply));
    r = mbus_sendrecv_request(h, 5, &reply, 16);
    CHECK(r == 0 && fs_count_frames(&reply) == 3, "with NKE: frames=%d", fs_count_frames(&reply));
    CHECK(reply.data_size == ud_size(&b.slave[s], 0), "with NKE: first frame is #1");
    free_chain(&reply);
    fs_free_handle(h);
}

static void t_silent(void)
{
    fs_bus b; mbus_handle *h; mbus_frame reply; int s, r, i, same = 1;

    printf("silent slave: bounded retries, FCB unchanged\n");
    fs_init(&b);
    s = fs_add_slave(&b, 5, NULL, FS_FCB_STRICT);
    add_multi(&b, s, SEC_A, 1);
    b.slave[s].silent_requests = 100;
    h = fs_handle(&b);
    memset(&reply, 0, sizeof(reply));
    r = mbus_sendrecv_request(h, 5, &reply, 16);
    CHECK(r != 0, "rc=%d", r);
    CHECK(fs_count_req(&b) == h->max_data_retry + 1, "requests=%d", fs_count_req(&b));
    for (i = 1; i < fs_count_req(&b); i++)
        if (fs_req_control(&b, i) != fs_req_control(&b, 0))
            same = 0;
    CHECK(same, "retry changed FCB");
    free_chain(&reply);
    fs_free_handle(h);
}

static void t_recover(int mode)
{
    fs_bus b; mbus_handle *h; mbus_frame reply; int s, r;
    const char *name[] = { "", "lost reply", "bad checksum", "truncated" };

    printf("recover after %s\n", name[mode]);
    fs_init(&b);
    s = fs_add_slave(&b, 5, NULL, FS_FCB_STRICT);
    add_multi(&b, s, SEC_A, 1);
    if (mode == 1) b.slave[s].silent_requests = 1;
    if (mode == 2) b.corrupt_next = 1;
    if (mode == 3) b.corrupt_next = 2;
    h = fs_handle(&b);
    memset(&reply, 0, sizeof(reply));
    r = mbus_sendrecv_request(h, 5, &reply, 16);
    CHECK(r == 0, "rc=%d", r);
    CHECK(fs_count_frames(&reply) == 3, "frames=%d", fs_count_frames(&reply));
    CHECK(fs_req_control(&b, 0) == 0x7B && fs_req_control(&b, 1) == 0x7B,
          "retry ctrl %02X %02X", fs_req_control(&b, 0), fs_req_control(&b, 1));
    CHECK(reply.data_size == ud_size(&b.slave[s], 0), "first frame is #1 after retry");
    free_chain(&reply);
    fs_free_handle(h);
}

static void t_echo(void)
{
    fs_bus b; mbus_handle *h; mbus_frame reply; int s, r;

    printf("converter echo\n");
    fs_init(&b);
    b.echo = 1;
    s = fs_add_slave(&b, 5, NULL, FS_FCB_STRICT);
    add_multi(&b, s, SEC_A, 1);
    h = fs_handle(&b);
    memset(&reply, 0, sizeof(reply));
    r = mbus_sendrecv_request(h, 5, &reply, 16);
    CHECK(r == 0 && fs_count_frames(&reply) == 3, "rc=%d frames=%d", r, fs_count_frames(&reply));
    free_chain(&reply);
    fs_free_handle(h);
}

static void t_jam(void)
{
    fs_bus b; mbus_handle *h; long used;

    printf("jammed bus: purge must give up\n");
    fs_init(&b);
    b.jam_left = 100000;
    h = fs_handle(&b);
    mbus_purge_frames(h);
    used = 100000 - b.jam_left;
    CHECK(used <= 256, "purge consumed %ld garbage bytes", used);
    fs_free_handle(h);
}

static void t_max_frames(void)
{
    fs_bus b; mbus_handle *h; mbus_frame reply; uint8_t rec[16], tg[FS_TG_MAX];
    size_t l; int s, r, n;

    printf("max_frames: limit, and 0 = capped default\n");
    fs_init(&b);
    s = fs_add_slave(&b, 5, NULL, FS_FCB_LENIENT);
    l = fs_build_var(tg, 5, 0x72, SEC_A, 1, rec, vol_rec(rec, 1), 1);   /* always "more follows" */
    fs_add_tg(&b, s, tg, l);
    b.slave[s].repeat_last = 1;
    h = fs_handle(&b);

    memset(&reply, 0, sizeof(reply));
    r = mbus_sendrecv_request(h, 5, &reply, 5);
    CHECK(r == 0 && fs_count_frames(&reply) == 5, "max 5: rc=%d frames=%d", r, fs_count_frames(&reply));
    free_chain(&reply);

    memset(&reply, 0, sizeof(reply));
    b.n_log = 0;
    r = mbus_sendrecv_request(h, 5, &reply, 0);
    n = fs_count_frames(&reply);
    CHECK(r == 0 && n >= 2 && n <= 64, "max 0: rc=%d frames=%d", r, n);
    free_chain(&reply);
    fs_free_handle(h);
}

static char found_addr[8][17];
static int  found_n;

static void on_found(mbus_handle *h, mbus_frame *f, const char *addr)
{
    (void)h; (void)f;
    if (found_n < 8)
        snprintf(found_addr[found_n++], 17, "%s", addr);
}

static int was_found(const char *a)
{
    int i;

    for (i = 0; i < found_n; i++)
        if (strcmp(found_addr[i], a) == 0)
            return 1;
    return 0;
}

static void t_scan(void)
{
    fs_bus b; mbus_handle *h; int sa, sc;

    printf("secondary scan, two different ids\n");
    fs_init(&b);
    sa = fs_add_slave(&b, -1, SEC_A, FS_FCB_STRICT);
    sc = fs_add_slave(&b, -1, SEC_C, FS_FCB_STRICT);
    add_multi(&b, sa, SEC_A, 1);
    add_multi(&b, sc, SEC_C, 1);
    h = fs_handle(&b);
    found_n = 0;
    mbus_register_found_event(h, on_found);
    mbus_scan_2nd_address_range(h, 0, "FFFFFFFFFFFFFFFF");
    CHECK(found_n == 2, "found %d", found_n);
    CHECK(was_found(SEC_A), "A missing");
    CHECK(was_found(SEC_C), "C missing");
    fs_free_handle(h);
}

/* Same 8 digit id, only the medium byte differs. Wildcards are per nibble in the id
   and per byte (FF) in mfr/version/medium, so the nibble walk cannot separate them. */
static void t_scan_same_id(void)
{
    fs_bus b; mbus_handle *h; int sa, sb;

    printf("secondary scan, same id, different medium (known limitation)\n");
    fs_init(&b);
    sa = fs_add_slave(&b, -1, SEC_A, FS_FCB_STRICT);
    sb = fs_add_slave(&b, -1, SEC_B, FS_FCB_STRICT);
    add_multi(&b, sa, SEC_A, 1);
    add_multi(&b, sb, SEC_B, 1);
    h = fs_handle(&b);
    found_n = 0;
    mbus_register_found_event(h, on_found);
    mbus_scan_2nd_address_range(h, 0, "FFFFFFFFFFFFFFFF");
    printf("  note: found %d of 2 (library walks hex fields by nibble)\n", found_n);
    fs_free_handle(h);
}

static void t_secondary_read(void)
{
    fs_bus b; mbus_handle *h; mbus_frame reply, fr; int sa, sb, r;

    printf("secondary select + read\n");
    fs_init(&b);
    sa = fs_add_slave(&b, -1, SEC_A, FS_FCB_STRICT);
    sb = fs_add_slave(&b, -1, SEC_B, FS_FCB_STRICT);
    add_multi(&b, sa, SEC_A, 1);
    add_multi(&b, sb, SEC_B, 1);
    add_reduced(&b, sa, SEC_A);
    h = fs_handle(&b);

    r = mbus_select_secondary_address(h, "12345678FFFFFFFF");
    CHECK(r == MBUS_PROBE_COLLISION, "wildcard mask rc=%d", r);
    r = mbus_select_secondary_address(h, "00000000FFFFFFFF");
    CHECK(r == MBUS_PROBE_NOTHING, "no match rc=%d", r);

    r = mbus_select_secondary_address(h, SEC_A);
    CHECK(r == MBUS_PROBE_SINGLE, "exact rc=%d", r);
    memset(&reply, 0, sizeof(reply));
    r = mbus_sendrecv_request(h, 253, &reply, 16);
    CHECK(r == 0 && fs_count_frames(&reply) == 3, "rc=%d frames=%d", r, fs_count_frames(&reply));
    free_chain(&reply);

    /* default single request after select asks with FCB=0 */
    r = mbus_select_secondary_address(h, SEC_A);
    CHECK(r == MBUS_PROBE_SINGLE, "exact rc=%d", r);
    mbus_send_request_frame(h, 253);
    memset(&fr, 0, sizeof(fr));
    mbus_recv_frame(h, &fr);
    CHECK(fr.data_size == b.slave[sa].reduced_len - 9, "0x5B after select -> reduced (%zu)", fr.data_size);
    fs_free_handle(h);
}

#ifdef MBUS_CONTROL_INFO_RESP_VARIABLE_NOHDR
static void t_ci(void)
{
    fs_bus b; mbus_handle *h; mbus_frame reply; uint8_t rec[16], tg[FS_TG_MAX];
    size_t l; int s, r, k;
    const uint8_t cis[2] = { 0x78, 0x7A };

    printf("CI 0x78 / 0x7A through the bus\n");
    for (k = 0; k < 2; k++) {
        fs_init(&b);
        s = fs_add_slave(&b, 5, NULL, FS_FCB_LENIENT);
        l = fs_build_var(tg, 5, cis[k], NULL, 3, rec, vol_rec(rec, 77), 0);
        fs_add_tg(&b, s, tg, l);
        h = fs_handle(&b);
        memset(&reply, 0, sizeof(reply));
        r = mbus_sendrecv_request(h, 5, &reply, 16);
        CHECK(r == 0, "ci 0x%02X rc=%d", cis[k], r);
        CHECK(chain_records(&reply) == 1, "ci 0x%02X records=%d", cis[k], chain_records(&reply));
        free_chain(&reply);
        fs_free_handle(h);
    }
}
#endif

int main(void)
{
    t_single();
    t_multi();
    t_fcb_first_request();
    t_nke_first();
    t_silent();
    t_recover(1);
    t_recover(2);
    t_recover(3);
    t_echo();
    t_jam();
    t_max_frames();
    t_scan();
    t_scan_same_id();
    t_secondary_read();
#ifdef MBUS_CONTROL_INFO_RESP_VARIABLE_NOHDR
    t_ci();
#endif
    printf("\n%d checks passed, %d failed\n", n_pass, n_fail);
    return n_fail ? 1 : 0;
}
