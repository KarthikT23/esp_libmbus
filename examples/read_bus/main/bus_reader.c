#include <stdlib.h>
#include <string.h>
#include "bus_reader.h"

typedef struct {
    bus_slot_t *slots;
    size_t      max, n;
} scan_ctx_t;

static int
on_found(void *ctx, const char *addr)
{
    scan_ctx_t *c = (scan_ctx_t *)ctx;

    if (c->n >= c->max)
        return 1;                       /* full, stop the scan */
    memset(&c->slots[c->n], 0, sizeof(c->slots[0]));
    c->slots[c->n].primary = -1;
    snprintf(c->slots[c->n].sec, sizeof(c->slots[0].sec), "%s", addr);
    c->n++;
    return c->n >= c->max;
}

int
bus_find_by_scan(mbus_handle *h, const mbus_meter_profile_t *scan_profile,
                 bus_slot_t *slots, size_t max)
{
    scan_ctx_t c = { .slots = slots, .max = max };
    mbus_meter_scan_cfg_t cfg = { .max_found = (unsigned)max, .on_found = on_found, .ctx = &c };
    mbus_meter_scan_result_t res;
    int rc = mbus_meter_scan_secondary(h, scan_profile, &cfg, &res);

    return rc < 0 ? rc : (int)c.n;
}

int
bus_find_by_list(const char *list, bus_slot_t *slots, size_t max)
{
    size_t n = 0;
    const char *p = list;

    while (*p) {
        char *end;
        long v = strtol(p, &end, 10);

        if (end == p || v < 0 || v > 250)
            return -1;
        if (n >= max)
            break;
        memset(&slots[n], 0, sizeof(slots[0]));
        slots[n].primary = (int)v;
        n++;
        p = end;
        while (*p == ' ')
            p++;
        if (*p == ',')
            p++;
        else if (*p)
            return -1;
        while (*p == ' ')
            p++;
    }
    return (int)n;
}

int
bus_open(mbus_handle *h, const mbus_meter_profile_t *p, bus_slot_t *slots, size_t n)
{
    int ok = 0;

    for (size_t i = 0; i < n; i++) {
        mbus_address a;

        memset(&a, 0, sizeof(a));
        if (slots[i].primary >= 0) {
            a.is_primary = 1;
            a.primary = slots[i].primary;
        } else {
            a.is_primary = 0;
            a.secondary = slots[i].sec;
        }
        slots[i].meter = mbus_meter_new(h, &a, p);      /* copies address and profile */
        if (slots[i].meter)
            ok++;
    }
    return ok;
}

static void
print_label(FILE *out, const bus_slot_t *s)
{
    if (s->primary >= 0)
        fprintf(out, "primary %d", s->primary);
    else
        fprintf(out, "%s", s->sec);
}

static void
print_records(FILE *out, const mbus_meter_readout_t *ro)
{
    for (size_t i = 0; i < ro->n_records; i++) {
        const mbus_meter_record_t *r = &ro->records[i];
        char val[MBUS_METER_VALUE_STR_MAX];

        if (mbus_meter_record_to_str(r, val, sizeof(val)) < 0)
            strcpy(val, "?");
        fprintf(out, "    %-22s %s %s\n", mbus_meter_qty_str(r->qty), val,
                r->unit_text ? r->unit_text : mbus_meter_unit_str(r->unit));
    }
}

int
bus_read_round(FILE *out, bus_slot_t *slots, size_t n, const bus_policy_t *pol)
{
    int ok = 0;

    for (size_t i = 0; i < n; i++) {
        bus_slot_t *s = &slots[i];
        mbus_meter_readout_t ro;
        int rc;

        if (s->meter == NULL)
            continue;

        if (s->parked) {
            s->parked--;
            fprintf(out, "[");
            print_label(out, s);
            fprintf(out, "] parked, %u round(s) left\n", s->parked);
            continue;
        }

        rc = mbus_meter_read(s->meter, &ro);
        fprintf(out, "[");
        print_label(out, s);
        fprintf(out, "] %s, %u record(s)\n", mbus_meter_strerror(rc), (unsigned)ro.n_records);

        if (rc >= 0) {                      /* complete or partial: data is usable */
            ok++;
            s->fails = 0;
            s->reads_ok++;
            print_records(out, &ro);
        } else if (++s->fails >= pol->max_fails) {
            s->fails = 0;
            s->parked = pol->park_rounds;
            fprintf(out, "    %u failures in a row, skipping it for %u round(s)\n",
                    pol->max_fails, pol->park_rounds);
        }
        mbus_meter_readout_free(&ro);       /* also after a failed read */
    }
    return ok;
}

void
bus_close(bus_slot_t *slots, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        mbus_meter_free(slots[i].meter);
        slots[i].meter = NULL;
    }
}
