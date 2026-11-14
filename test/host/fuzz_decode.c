/*
 * Randomised decode test, fixed seed, no external fuzzer needed.
 *   usage: fuzz_decode [-n iterations_per_frame] [-s seed] [-l] dir...
 *
 * Every .hex frame in the directories is mutated and fed to
 *   - mbus_parse()                      (raw bytes, structure usually broken)
 *   - mbus_meter_decode_frame()         (structure and checksum repaired, so the
 *                                         mutated body reaches the record decoder)
 * and a pure random body per CI value. -l also runs the upstream
 * mbus_frame_data_parse() on repaired frames.
 * Passes when nothing crashes, leaks or trips a sanitizer and the readout
 * invariants hold. Run it with SAN=1 (ASan + UBSan) to get the real value.
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <dirent.h>
#include "mbus-meter.h"
#include "mbus-protocol.h"

static int checks, failed;

#define CHECK(cond) do { \
    checks++; \
    if (!(cond)) { failed++; printf("FAIL line %d: %s\n", __LINE__, #cond); } \
} while (0)

static uint64_t rng_state;
static unsigned long g_frames, g_ok, g_partial, g_err, g_recs;

static uint32_t
rnd(void)
{
    /* xorshift64*, deterministic */
    rng_state ^= rng_state >> 12;
    rng_state ^= rng_state << 25;
    rng_state ^= rng_state >> 27;
    return (uint32_t)((rng_state * 0x2545F4914F6CDD1DULL) >> 32);
}

static size_t
hex2bin(uint8_t *dst, size_t max, const char *s)
{
    size_t n = 0;
    int hi = -1;

    for (; *s; s++) {
        int v;

        if (*s >= '0' && *s <= '9')
            v = *s - '0';
        else if (*s >= 'a' && *s <= 'f')
            v = *s - 'a' + 10;
        else if (*s >= 'A' && *s <= 'F')
            v = *s - 'A' + 10;
        else
            continue;
        if (hi < 0) {
            hi = v;
        } else if (n < max) {
            dst[n++] = (uint8_t)(hi << 4 | v);
            hi = -1;
        }
    }
    return n;
}

/* ------------------------------------------------------------------ */
/* invariants on a decoded readout                                      */
/* ------------------------------------------------------------------ */

static void
check_readout(const mbus_meter_readout_t *out, int rc, const mbus_meter_profile_t *p)
{
    size_t i;
    char s[MBUS_METER_VALUE_STR_MAX];

    CHECK(rc >= MBUS_METER_ERR_APP_ERROR && rc <= MBUS_METER_PARTIAL);
    if (rc == MBUS_METER_OK)
        g_ok++;
    else if (rc == MBUS_METER_PARTIAL)
        g_partial++;
    else
        g_err++;
    g_recs += out->n_records;
    CHECK(mbus_meter_strerror(rc) != NULL);
    if (p->max_records)
        CHECK(out->n_records <= p->max_records);
    CHECK(out->n_records == 0 || out->records != NULL);

    for (i = 0; i < out->n_records; i++) {
        const mbus_meter_record_t *r = &out->records[i];
        int n;

        CHECK(r->key.n_vife <= MBUS_METER_VIFE_MAX);
        CHECK(mbus_meter_unit_str(r->unit) != NULL);
        CHECK(mbus_meter_qty_str(r->qty) != NULL);
        if (r->vtype == MBUS_METER_VT_TEXT || r->vtype == MBUS_METER_VT_BINARY)
            CHECK(r->value.bytes.len == 0 || r->value.bytes.data != NULL);
        if (r->unit_text)
            CHECK(strlen(r->unit_text) < 256);
        (void)mbus_meter_record_to_double(r);
        n = mbus_meter_record_to_str(r, s, sizeof(s));
        CHECK(n < (int)sizeof(s));
        /* a tiny buffer must be refused, never overrun */
        n = mbus_meter_record_to_str(r, s, 2);
        CHECK(n < 2);
        CHECK(mbus_meter_readout_find(out, &r->key, NULL) != NULL);
    }
}

static void
decode_both_profiles(const mbus_frame *f)
{
    mbus_meter_profile_t pg = MBUS_METER_PROFILE_GENERIC();
    mbus_meter_profile_t px = MBUS_METER_PROFILE_GENERIC();
    mbus_meter_readout_t out;
    int rc;

    px.unsigned_ints = true;
    px.keep_raw = true;
    px.max_records = 3;

    mbus_meter_readout_init(&out);
    rc = mbus_meter_decode_frame(f, &pg, &out);
    check_readout(&out, rc, &pg);
    mbus_meter_readout_free(&out);

    mbus_meter_readout_init(&out);
    rc = mbus_meter_decode_frame(f, &px, &out);
    check_readout(&out, rc, &px);
    mbus_meter_readout_free(&out);
}

/* ------------------------------------------------------------------ */
/* mutation                                                             */
/* ------------------------------------------------------------------ */

static void
mutate(uint8_t *b, size_t *n, size_t cap)
{
    int k = 1 + (int)(rnd() % 4);

    while (k-- > 0 && *n > 0) {
        size_t at = rnd() % *n;

        switch (rnd() % 6) {
        case 0: b[at] ^= (uint8_t)(1u << (rnd() % 8)); break;   /* bit flip */
        case 1: b[at] = (uint8_t)rnd(); break;                  /* random byte */
        case 2: b[at] = (uint8_t[]){ 0x00, 0xFF, 0x7F, 0x80, 0x0F, 0x1F, 0x2F, 0x7C, 0xFC, 0x0D, 0x8D }[rnd() % 11]; break;
        case 3: if (*n > 1) { memmove(b + at, b + at + 1, *n - at - 1); (*n)--; } break;   /* delete */
        case 4: if (*n < cap) { memmove(b + at + 1, b + at, *n - at); b[at] = (uint8_t)rnd(); (*n)++; } break;   /* insert */
        default: *n = at + 1; break;                            /* truncate */
        }
    }
}

/* long frame bytes -> mbus_frame with a valid structure; body = C A CI data */
static int
build_long(mbus_frame *f, const uint8_t *body, size_t n)
{
    if (n < 3 || n - 3 > MBUS_FRAME_DATA_LENGTH)
        return -1;
    memset(f, 0, sizeof(*f));
    f->start1 = 0x68;
    f->control = body[0];
    f->address = body[1];
    f->control_information = body[2];
    f->type = MBUS_FRAME_TYPE_LONG;
    memcpy(f->data, body + 3, n - 3);
    f->data_size = n - 3;
    return 0;
}

/* raw bytes with L, L, checksum recomputed so mbus_parse() accepts them */
static size_t
wrap_long(uint8_t *dst, const uint8_t *body, size_t n)
{
    size_t i;
    uint8_t cs = 0;

    if (n < 3 || n > 255)
        return 0;
    dst[0] = 0x68; dst[1] = (uint8_t)n; dst[2] = (uint8_t)n; dst[3] = 0x68;
    memcpy(dst + 4, body, n);
    for (i = 0; i < n; i++)
        cs = (uint8_t)(cs + body[i]);
    dst[4 + n] = cs;
    dst[5 + n] = 0x16;
    return n + 6;
}

static int g_lib;

static void
run_one(const uint8_t *body, size_t n)
{
    mbus_frame f;
    mbus_frame pf;
    uint8_t wire[300];
    size_t wn = wrap_long(wire, body, n);

    g_frames++;
    if (build_long(&f, body, n) == 0)
        decode_both_profiles(&f);

    if (wn) {
        memset(&pf, 0, sizeof(pf));
        if (mbus_parse(&pf, wire, wn) == 0) {
            decode_both_profiles(&pf);
            if (g_lib) {
                mbus_frame_data d;

                memset(&d, 0, sizeof(d));
                if (mbus_frame_data_parse(&pf, &d) == 0 && d.type == MBUS_DATA_TYPE_VARIABLE
                    && d.data_var.record)
                    mbus_data_record_free(d.data_var.record);
            }
        }
    }
}

static void
fuzz_frame(const uint8_t *orig, size_t on, int iters)
{
    uint8_t body[300];
    int i;

    /* the frame without its 4 byte start and checksum/stop: C A CI data */
    if (on < 10 || orig[0] != 0x68)
        return;
    {
        size_t l = orig[1];

        if (l + 6 != on || l > sizeof(body))
            return;
        memcpy(body, orig + 4, l);
        run_one(body, l);                       /* unmodified */
        for (i = 0; i < iters; i++) {
            uint8_t b[300];
            size_t n = l;

            memcpy(b, body, l);
            mutate(b, &n, sizeof(b));
            run_one(b, n);
        }
    }
}

static void
fuzz_random(int iters)
{
    static const uint8_t ci[] = { 0x72, 0x73, 0x78, 0x7A, 0x70, 0x76, 0x77, 0x10 };
    int i;

    for (i = 0; i < iters; i++) {
        uint8_t b[300];
        size_t n = 3 + rnd() % 200, j;

        b[0] = 0x08; b[1] = 0x01; b[2] = ci[rnd() % sizeof(ci)];
        for (j = 3; j < n; j++)
            b[j] = (uint8_t)rnd();
        run_one(b, n);
    }
}

static void
fuzz_dir(const char *dir, int iters)
{
    DIR *dp = opendir(dir);
    struct dirent *e;

    if (!dp) {
        printf("cannot open %s\n", dir);
        failed++;
        return;
    }
    while ((e = readdir(dp))) {
        size_t l = strlen(e->d_name);
        char path[512], txt[4096];
        uint8_t bin[600];
        FILE *fp;
        size_t n;

        if (l <= 4 || strcmp(e->d_name + l - 4, ".hex") != 0)
            continue;
        snprintf(path, sizeof(path), "%s/%s", dir, e->d_name);
        fp = fopen(path, "r");
        if (!fp)
            continue;
        n = fread(txt, 1, sizeof(txt) - 1, fp);
        fclose(fp);
        txt[n] = 0;
        n = hex2bin(bin, sizeof(bin), txt);
        fuzz_frame(bin, n, iters);
    }
    closedir(dp);
}

int
main(int argc, char **argv)
{
    int iters = 300, i;
    uint64_t seed = 1;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-n") == 0 && i + 1 < argc)
            iters = atoi(argv[++i]);
        else if (strcmp(argv[i], "-s") == 0 && i + 1 < argc)
            seed = strtoull(argv[++i], NULL, 10);
        else if (strcmp(argv[i], "-l") == 0)
            g_lib = 1;
    }
    rng_state = 0x9E3779B97F4A7C15ULL ^ seed;

    for (i = 1; i < argc; i++) {
        if (argv[i][0] == '-') {
            if (strcmp(argv[i], "-n") == 0 || strcmp(argv[i], "-s") == 0)
                i++;
            continue;
        }
        fuzz_dir(argv[i], iters);
    }
    fuzz_random(iters * 10);

    printf("%lu frames (decoded: %lu ok, %lu partial, %lu rejected, %lu records), "
           "%d checks passed, %d failed (seed %llu)\n",
           g_frames, g_ok, g_partial, g_err, g_recs, checks - failed, failed,
           (unsigned long long)seed);
    return failed ? 1 : 0;
}
