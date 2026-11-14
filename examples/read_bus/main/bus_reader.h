/*
 * Several meters on one bus: one handle, one meter object per address,
 * reads one after the other. A meter that keeps failing is parked for a few
 * rounds so it does not slow the others down, then tried again.
 */
#ifndef BUS_READER_H
#define BUS_READER_H

#include <stdio.h>
#include <stddef.h>
#include "mbus.h"

typedef struct {
    int           primary;      /* 0..250, or -1 when addressed by secondary */
    char          sec[17];      /* 16 hex digits when primary == -1 */
    mbus_meter_t *meter;
    unsigned      fails;        /* consecutive failed reads */
    unsigned      parked;       /* rounds still to skip */
    unsigned      reads_ok;
} bus_slot_t;

typedef struct {
    unsigned max_fails;         /* consecutive failures before a meter is parked */
    unsigned park_rounds;       /* rounds a parked meter is skipped */
} bus_policy_t;

/* Secondary scan, fills up to max slots. Returns the count, <0 on error. */
int bus_find_by_scan(mbus_handle *h, const mbus_meter_profile_t *scan_profile,
                     bus_slot_t *slots, size_t max);

/* "1,2,17" -> primary slots. Returns the count, <0 on a bad list. */
int bus_find_by_list(const char *list, bus_slot_t *slots, size_t max);

/* Creates the meter objects. Returns how many could be created. */
int bus_open(mbus_handle *h, const mbus_meter_profile_t *p, bus_slot_t *slots, size_t n);

/* One pass over all meters. Returns how many were read (complete or partial). */
int bus_read_round(FILE *out, bus_slot_t *slots, size_t n, const bus_policy_t *pol);

void bus_close(bus_slot_t *slots, size_t n);

#endif
