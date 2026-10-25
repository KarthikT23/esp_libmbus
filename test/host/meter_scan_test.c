/* mbus_meter_scan_secondary() against the fake bus, byte-wise id matching */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "fake_slave.h"
#include "mbus-meter.h"

static int n_pass, n_fail;

#define CHECK(cond, ...) do { \
    if (cond) { n_pass++; } \
    else { n_fail++; printf("  FAIL line %d: ", __LINE__); printf(__VA_ARGS__); printf("\n"); } \
} while (0)

#define MAXLOG 4096

typedef struct {
    char     found[32][17];
    unsigned n_found;
    char     masks[MAXLOG][17];
    unsigned n_masks;
    unsigned stop_after;        /* on_found returns 1 at this count, 0 = never */
    unsigned abort_at;          /* abort flag set at this probe, 0 = never */
} rec_t;

static rec_t g_rec;
static bool g_abort;

static bool abort_cb(mbus_handle *h) { (void)h; return g_abort; }

static int
on_found(void *ctx, const char *addr)
{
    rec_t *r = (rec_t *)ctx;

    if (r->n_found < 32)
        snprintf(r->found[r->n_found], 17, "%s", addr);
    r->n_found++;
    return r->stop_after && r->n_found >= r->stop_after;
}

static void
on_probe(void *ctx, const char *mask)
{
    rec_t *r = (rec_t *)ctx;

    if (r->n_masks < MAXLOG)
        snprintf(r->masks[r->n_masks], 17, "%s", mask);
    r->n_masks++;
    if (r->abort_at && r->n_masks == r->abort_at)
        g_abort = true;
}

static mbus_meter_scan_cfg_t
cfg_new(const char *mask)
{
    mbus_meter_scan_cfg_t c;

    memset(&g_rec, 0, sizeof(g_rec));
    g_abort = false;
    memset(&c, 0, sizeof(c));
    c.mask = mask;
    c.on_found = on_found;
    c.on_probe = on_probe;
    c.ctx = &g_rec;
    return c;
}

/* one telegram, answers REQ_UD2 to 253 once selected */
static void
add_dev(fs_bus *b, const char *sec)
{
    uint8_t rec[8] = { 0x04, 0x13, 1, 0, 0, 0 }, tg[FS_TG_MAX];
    int s = fs_add_slave(b, -1, sec, FS_FCB_LENIENT);
    size_t l = fs_build_var(tg, 253, 0x72, sec, 1, rec, 6, 0);

    fs_add_tg(b, s, tg, l);
}

static bool
was_found(const char *sec)
{
    unsigned i;

    for (i = 0; i < g_rec.n_found && i < 32; i++)
        if (strcmp(g_rec.found[i], sec) == 0)
            return true;
    return false;
}

static bool
nke253(const fs_log_entry *e)
{
    return e->type == MBUS_FRAME_TYPE_SHORT && e->control == 0x40 && e->address == 253;
}

#define D1 "12345678A7320107"
#define D2 "12345699A7320107"
#define D3 "12340000A7320107"
#define D4 "55555555A7320107"
#define D5 "55550001A7320107"

int
main(void)
{
    fs_bus b;
    mbus_handle *h;
    mbus_meter_scan_cfg_t c;
    mbus_meter_scan_result_t res;
    int rc;

    printf("arguments\n");
    fs_init(&b);
    h = fs_handle(&b);
    c = cfg_new(NULL);
    CHECK(mbus_meter_scan_secondary(NULL, NULL, &c, &res) == MBUS_METER_ERR_ARG, "null handle");
    CHECK(mbus_meter_scan_secondary(h, NULL, NULL, &res) == MBUS_METER_ERR_ARG, "null cfg");
    c.mask = "FFFF";
    CHECK(mbus_meter_scan_secondary(h, NULL, &c, &res) == MBUS_METER_ERR_ARG, "short mask");
    c.mask = "GGFFFFFFFFFFFFFF";
    CHECK(mbus_meter_scan_secondary(h, NULL, &c, &res) == MBUS_METER_ERR_ARG, "non hex mask");
    CHECK(b.n_log == 0, "nothing sent on bad arguments");
    fs_free_handle(h);

    printf("empty bus\n");
    fs_init(&b);
    b.sec_bytewise = 1;
    h = fs_handle(&b);
    c = cfg_new(NULL);
    rc = mbus_meter_scan_secondary(h, NULL, &c, &res);
    CHECK(rc == 0 && res.found == 0 && res.probes == 1, "rc=%d found=%u probes=%u", rc, res.found, res.probes);
    CHECK(b.n_log >= 3 && nke253(&b.log[0]) && nke253(&b.log[b.n_log - 1]), "NKE 253 before and after");
    fs_free_handle(h);

    printf("one device\n");
    fs_init(&b);
    b.sec_bytewise = 1;
    add_dev(&b, D1);
    h = fs_handle(&b);
    c = cfg_new(NULL);
    rc = mbus_meter_scan_secondary(h, NULL, &c, &res);
    CHECK(rc == 1 && res.found == 1 && res.probes == 1 && was_found(D1), "rc=%d probes=%u", rc, res.probes);
    CHECK(fs_req_control(&b, 0) == 0x7B, "first REQ_UD2 after select 0x%02X", fs_req_control(&b, 0));
    fs_free_handle(h);

    printf("five devices, shared prefixes\n");
    fs_init(&b);
    b.sec_bytewise = 1;
    add_dev(&b, D1); add_dev(&b, D2); add_dev(&b, D3); add_dev(&b, D4); add_dev(&b, D5);
    h = fs_handle(&b);
    c = cfg_new(NULL);
    rc = mbus_meter_scan_secondary(h, NULL, &c, &res);
    CHECK(rc == 5 && res.found == 5 && res.unresolved == 0, "rc=%d unresolved=%u", rc, res.unresolved);
    CHECK(was_found(D1) && was_found(D2) && was_found(D3) && was_found(D4) && was_found(D5), "all found");
    CHECK(g_rec.n_found == 5, "each reported once, %u", g_rec.n_found);
    CHECK(res.probes == g_rec.n_masks, "probe count %u / %u", res.probes, g_rec.n_masks);
    printf("  probes used: %u\n", res.probes);
    CHECK(res.probes < 700, "probes %u", res.probes);
    CHECK(strcmp(g_rec.masks[0], "FFFFFFFFFFFFFFFF") == 0, "root probe first");
    CHECK(strcmp(g_rec.masks[1], "00FFFFFFFFFFFFFF") == 0, "first level walks the top id byte, got %s", g_rec.masks[1]);
    fs_free_handle(h);

    printf("same id, different medium\n");
    fs_init(&b);
    b.sec_bytewise = 1;
    add_dev(&b, "11111111A7320107");
    add_dev(&b, "11111111A732010B");
    add_dev(&b, D4);
    h = fs_handle(&b);
    c = cfg_new(NULL);
    rc = mbus_meter_scan_secondary(h, NULL, &c, &res);
    CHECK(rc == 1 && res.found == 1 && res.unresolved == 1 && was_found(D4), "rc=%d found=%u unresolved=%u",
          rc, res.found, res.unresolved);
    fs_free_handle(h);

    printf("fixed prefix\n");
    fs_init(&b);
    b.sec_bytewise = 1;
    add_dev(&b, D1); add_dev(&b, D2); add_dev(&b, D3); add_dev(&b, D4); add_dev(&b, D5);
    h = fs_handle(&b);
    c = cfg_new("12FFFFFFFFFFFFFF");
    rc = mbus_meter_scan_secondary(h, NULL, &c, &res);
    CHECK(rc == 3 && was_found(D1) && was_found(D2) && was_found(D3), "rc=%d", rc);
    {
        unsigned i;
        int bad = 0;

        for (i = 0; i < g_rec.n_masks && i < MAXLOG; i++)
            if (strncmp(g_rec.masks[i], "12", 2) != 0)
                bad++;
        CHECK(bad == 0, "%d probes outside the prefix", bad);
    }
    fs_free_handle(h);

    printf("stop conditions\n");
    fs_init(&b);
    b.sec_bytewise = 1;
    add_dev(&b, D1); add_dev(&b, D2); add_dev(&b, D3); add_dev(&b, D4);
    h = fs_handle(&b);
    c = cfg_new(NULL);
    g_rec.stop_after = 1;
    rc = mbus_meter_scan_secondary(h, NULL, &c, &res);
    CHECK(rc == 1 && g_rec.n_found == 1, "callback stop rc=%d found=%u", rc, g_rec.n_found);
    c = cfg_new(NULL);
    c.max_found = 2;
    rc = mbus_meter_scan_secondary(h, NULL, &c, &res);
    CHECK(rc == 2 && res.found == 2, "max_found rc=%d", rc);
    CHECK(nke253(&b.log[b.n_log - 1]), "bus deselected after an early stop");
    h->abort_scan_check = abort_cb;
    c = cfg_new(NULL);
    g_rec.abort_at = 5;
    rc = mbus_meter_scan_secondary(h, NULL, &c, &res);
    CHECK(rc == MBUS_METER_ERR_ABORTED && res.probes == 5, "abort rc=%d probes=%u", rc, res.probes);
    CHECK(nke253(&b.log[b.n_log - 1]), "bus deselected after an abort");
    h->abort_scan_check = NULL;
    fs_free_handle(h);

    printf("profile first_fcb\n");
    fs_init(&b);
    b.sec_bytewise = 1;
    add_dev(&b, D1);
    h = fs_handle(&b);
    {
        mbus_meter_profile_t p = MBUS_METER_PROFILE_GENERIC();

        p.first_fcb = 0;
        c = cfg_new(NULL);
        rc = mbus_meter_scan_secondary(h, &p, &c, &res);
        CHECK(rc == 1 && fs_req_control(&b, 0) == 0x5B, "first_fcb=0 -> 0x%02X", fs_req_control(&b, 0));
    }
    fs_free_handle(h);

    printf("\n%d checks passed, %d failed\n", n_pass, n_fail);
    return n_fail ? 1 : 0;
}
