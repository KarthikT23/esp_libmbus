/* examples/read_bus logic on the fake bus: scan, open, rounds, parking */
#include "../../examples/read_bus/main/bus_reader.c"
#include "fake_slave.h"

static int checks, failed;

#define CHECK(cond, ...) do { \
    checks++; \
    if (!(cond)) { failed++; printf("FAIL line %d: ", __LINE__); printf(__VA_ARGS__); printf("\n"); } \
} while (0)

#define D1 "12345678A7320107"
#define D2 "12345699A7320107"
#define D3 "12340000A7320107"

static int
add_dev(fs_bus *b, const char *sec)
{
    uint8_t rec[8] = { 0x04, 0x13, 1, 0, 0, 0 }, tg[FS_TG_MAX];
    int s = fs_add_slave(b, -1, sec, FS_FCB_LENIENT);
    size_t l = fs_build_var(tg, 253, 0x72, sec, 1, rec, 6, 0);

    fs_add_tg(b, s, tg, l);
    return s;
}

static char *
round_text(bus_slot_t *sl, size_t n, const bus_policy_t *pol, int *ok)
{
    char *buf = NULL;
    size_t sz = 0;
    FILE *f = open_memstream(&buf, &sz);

    *ok = bus_read_round(f, sl, n, pol);
    fclose(f);
    return buf;
}

int
main(void)
{
    fs_bus b;
    mbus_handle *h;
    bus_slot_t slots[4];
    mbus_meter_profile_t p = MBUS_METER_PROFILE_GENERIC();
    bus_policy_t pol = { .max_fails = 2, .park_rounds = 2 };
    unsigned req;
    char *t;
    int n, ok;

    p.rx_first_byte_ms = 100;
    p.retries = 0;

    /* primary list parsing */
    CHECK(bus_find_by_list("1,2,17", slots, 4) == 3 && slots[2].primary == 17, "list");
    CHECK(bus_find_by_list(" 1 , 2 ", slots, 4) == 2, "list with spaces");
    CHECK(bus_find_by_list("", slots, 4) == 0, "empty list");
    CHECK(bus_find_by_list("1,2,3,4,5", slots, 4) == 4, "capped at max");
    CHECK(bus_find_by_list("1,x", slots, 4) < 0, "bad entry");
    CHECK(bus_find_by_list("1,251", slots, 4) < 0, "out of range");
    CHECK(bus_find_by_list("1;2", slots, 4) < 0, "bad separator");

    /* three meters, scan, open */
    fs_init(&b);
    b.sec_bytewise = 1;
    add_dev(&b, D1);
    int s2 = add_dev(&b, D2);
    add_dev(&b, D3);
    h = fs_handle(&b);

    memset(slots, 0, sizeof(slots));
    n = bus_find_by_scan(h, &p, slots, 4);
    CHECK(n == 3, "scan found %d", n);
    CHECK(bus_open(h, &p, slots, (size_t)n) == 3, "open");

    /* one meter goes silent */
    b.slave[s2].silent_requests = 1000;
    t = round_text(slots, (size_t)n, &pol, &ok);
    CHECK(ok == 2, "round 1 ok=%d", ok);
    CHECK(strstr(t, "] ok, 1 record(s)") != NULL, "ok line: %s", t);
    free(t);
    int bad = -1;
    for (int i = 0; i < n; i++)
        if (strcmp(slots[i].sec, D2) == 0)
            bad = i;
    CHECK(bad >= 0 && slots[bad].fails == 1 && slots[bad].parked == 0, "failure counted");

    t = round_text(slots, (size_t)n, &pol, &ok);
    CHECK(ok == 2, "round 2 ok=%d", ok);
    CHECK(strstr(t, "skipping it for 2 round(s)") != NULL, "parking announced");
    free(t);
    CHECK(slots[bad].parked == 2 && slots[bad].fails == 0, "parked");

    /* parked: the silent meter is not asked at all */
    req = b.slave[s2].requests;
    t = round_text(slots, (size_t)n, &pol, &ok);
    CHECK(ok == 2 && slots[bad].parked == 1, "round 3");
    CHECK(strstr(t, "parked, 1 round(s) left") != NULL, "parked text");
    free(t);
    t = round_text(slots, (size_t)n, &pol, &ok);
    CHECK(ok == 2 && slots[bad].parked == 0, "round 4");
    free(t);
    CHECK(b.slave[s2].requests == req, "no request to a parked meter (%u vs %u)",
          b.slave[s2].requests, req);

    /* back to life: tried again, counters reset */
    b.slave[s2].silent_requests = 0;
    t = round_text(slots, (size_t)n, &pol, &ok);
    CHECK(ok == 3, "round 5 ok=%d", ok);
    CHECK(strstr(t, "volume") != NULL, "records printed");
    free(t);
    CHECK(slots[bad].fails == 0 && slots[bad].reads_ok == 1, "recovered");

    bus_close(slots, (size_t)n);
    CHECK(slots[0].meter == NULL, "closed");
    bus_close(slots, (size_t)n);                 /* twice is fine */
    fs_free_handle(h);

    /* scan limited by the slot count */
    fs_init(&b);
    b.sec_bytewise = 1;
    add_dev(&b, D1);
    add_dev(&b, D2);
    add_dev(&b, D3);
    h = fs_handle(&b);
    memset(slots, 0, sizeof(slots));
    n = bus_find_by_scan(h, &p, slots, 2);
    CHECK(n == 2, "capped scan %d", n);
    fs_free_handle(h);

    printf("%d checks passed, %d failed\n", checks - failed, failed);
    return failed ? 1 : 0;
}
