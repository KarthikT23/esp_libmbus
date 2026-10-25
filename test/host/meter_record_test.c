/* host test for src/mbus-meter-record.c: tables, value decode, strings, container */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include "mbus-meter.h"
#include "mbus-meter-internal.h"

static int checks, failed;

#define CHECK(cond) do { \
    checks++; \
    if (!(cond)) { failed++; printf("FAIL line %d: %s\n", __LINE__, #cond); } \
} while (0)

static mbus_meter_vdesc_t
resolve(uint8_t vif, const uint8_t *vife, uint8_t n)
{
    mbus_meter_vdesc_t d;

    mbus_meter_vib_resolve(vif, vife, n, &d);
    return d;
}

static void
test_vib(void)
{
    mbus_meter_vdesc_t d;
    const uint8_t e1[] = { 0x17 };
    const uint8_t e2[] = { 0x48 };
    const uint8_t e3[] = { 0x59 };
    const uint8_t e4[] = { 0x01 };
    const uint8_t m1[] = { 0x75 };           /* x10^-1 */
    const uint8_t m2[] = { 0x7D };           /* x10^3 */
    const uint8_t a1[] = { 0x7A };           /* additive 10^-1 */
    const uint8_t u1[] = { 0x22 };           /* "per hour", not interpreted */
    const uint8_t u2[] = { 0x7F };
    const uint8_t fb[] = { 0x21 };

    /* primary table */
    d = resolve(0x06, NULL, 0);
    CHECK(d.qty == MBUS_METER_QTY_ENERGY && d.unit == MBUS_METER_UNIT_WH && d.exp10 == 3);
    d = resolve(0x0B, NULL, 0);
    CHECK(d.qty == MBUS_METER_QTY_ENERGY && d.unit == MBUS_METER_UNIT_J && d.exp10 == 3);
    d = resolve(0x13, NULL, 0);
    CHECK(d.qty == MBUS_METER_QTY_VOLUME && d.unit == MBUS_METER_UNIT_M3 && d.exp10 == -3);
    d = resolve(0x15, NULL, 0);
    CHECK(d.exp10 == -1);                    /* 0.1 m3 */
    d = resolve(0x1B, NULL, 0);
    CHECK(d.qty == MBUS_METER_QTY_MASS && d.unit == MBUS_METER_UNIT_KG && d.exp10 == 0);
    d = resolve(0x22, NULL, 0);
    CHECK(d.qty == MBUS_METER_QTY_ON_TIME && d.unit == MBUS_METER_UNIT_HOUR);
    d = resolve(0x27, NULL, 0);
    CHECK(d.qty == MBUS_METER_QTY_OPERATING_TIME && d.unit == MBUS_METER_UNIT_DAY);
    d = resolve(0x2B, NULL, 0);
    CHECK(d.qty == MBUS_METER_QTY_POWER && d.unit == MBUS_METER_UNIT_W && d.exp10 == 0);
    d = resolve(0x3B, NULL, 0);
    CHECK(d.qty == MBUS_METER_QTY_VOLUME_FLOW && d.unit == MBUS_METER_UNIT_M3_PER_H && d.exp10 == -3);
    d = resolve(0x5A, NULL, 0);
    CHECK(d.qty == MBUS_METER_QTY_FLOW_TEMP && d.unit == MBUS_METER_UNIT_CELSIUS && d.exp10 == -1);
    d = resolve(0x5E, NULL, 0);
    CHECK(d.qty == MBUS_METER_QTY_RETURN_TEMP && d.exp10 == -1);
    d = resolve(0x62, NULL, 0);
    CHECK(d.qty == MBUS_METER_QTY_TEMP_DIFF && d.unit == MBUS_METER_UNIT_KELVIN && d.exp10 == -1);
    d = resolve(0x6B, NULL, 0);
    CHECK(d.qty == MBUS_METER_QTY_PRESSURE && d.unit == MBUS_METER_UNIT_BAR && d.exp10 == 0);
    d = resolve(0x6C, NULL, 0);
    CHECK(d.qty == MBUS_METER_QTY_DATE && d.tp == MBUS_METER_TP_DATE_G);
    d = resolve(0x6D, NULL, 0);
    CHECK(d.qty == MBUS_METER_QTY_DATETIME && d.tp == MBUS_METER_TP_DATETIME);
    d = resolve(0x78, NULL, 0);
    CHECK(d.qty == MBUS_METER_QTY_FABRICATION_NO);
    d = resolve(0x6F, NULL, 0);
    CHECK(d.flags & MBUS_METER_F_UNKNOWN_VIF);
    d = resolve(0x7B, NULL, 0);              /* 0x7B without ext bit */
    CHECK(d.flags & MBUS_METER_F_UNKNOWN_VIF);

    /* extension tables */
    d = resolve(0xFD, e1, 1);
    CHECK(d.qty == MBUS_METER_QTY_ERROR_FLAGS);
    d = resolve(0xFD, e2, 1);
    CHECK(d.qty == MBUS_METER_QTY_VOLTAGE && d.unit == MBUS_METER_UNIT_VOLT && d.exp10 == -1);
    d = resolve(0xFD, e3, 1);
    CHECK(d.qty == MBUS_METER_QTY_CURRENT && d.unit == MBUS_METER_UNIT_AMPERE && d.exp10 == -3);
    d = resolve(0xFB, e4, 1);
    CHECK(d.qty == MBUS_METER_QTY_ENERGY && d.unit == MBUS_METER_UNIT_WH && d.exp10 == 6);  /* MWh */
    d = resolve(0xFB, fb, 1);
    CHECK(d.unit == MBUS_METER_UNIT_FT3 && d.exp10 == -1);
    d = resolve(0xFD, (const uint8_t[]){ 0x30 }, 1);
    CHECK(d.qty == MBUS_METER_QTY_TARIFF_START && d.tp == MBUS_METER_TP_DATETIME);
    d = resolve(0xFD, (const uint8_t[]){ 0x7E }, 1);   /* not in the table */
    CHECK(d.flags & MBUS_METER_F_UNKNOWN_VIFE);
    d = resolve(0xFD, NULL, 0);
    CHECK(d.flags & MBUS_METER_F_UNKNOWN_VIF);

    /* combinable VIFEs */
    d = resolve(0x93, m1, 1);                /* volume 1e-3, x1e-1 */
    CHECK(d.exp10 == -4 && !(d.flags & MBUS_METER_F_UNKNOWN_VIFE));
    d = resolve(0x93, m2, 1);
    CHECK(d.exp10 == 0);
    d = resolve(0x93, a1, 1);
    CHECK((d.flags & MBUS_METER_F_HAS_ADDITIVE) && d.add_exp == -1 && d.exp10 == -3);
    d = resolve(0x93, u1, 1);
    CHECK((d.flags & MBUS_METER_F_UNKNOWN_VIFE) && d.exp10 == -3 && d.qty == MBUS_METER_QTY_VOLUME);
    d = resolve(0x93, u2, 1);
    CHECK(d.flags & MBUS_METER_F_MFR_SPECIFIC);
    d = resolve(0x7F, NULL, 0);
    CHECK(d.qty == MBUS_METER_QTY_MANUFACTURER_SPECIFIC && (d.flags & MBUS_METER_F_MFR_SPECIFIC));
    d = resolve(0xFF, u1, 1);                /* vendor VIFE stays raw, not "unknown" */
    CHECK((d.flags & MBUS_METER_F_MFR_SPECIFIC) && !(d.flags & MBUS_METER_F_UNKNOWN_VIFE));
    d = resolve(0x7C, NULL, 0);
    CHECK(d.qty == MBUS_METER_QTY_PLAIN_TEXT);
    d = resolve(0xFC, m1, 1);                /* plain text with multiplier VIFE */
    CHECK(d.qty == MBUS_METER_QTY_PLAIN_TEXT && d.exp10 == -1);
}

static int
decode(const mbus_meter_profile_t *p, mbus_meter_record_t *r, uint8_t dif, uint8_t fn,
       mbus_meter_tp_t tp, uint8_t lvar, const uint8_t *data, size_t len)
{
    memset(r, 0, sizeof(*r));
    r->dif = dif;
    r->key.function = fn;
    return mbus_meter_value_decode(p, r, tp, lvar, data, len);
}

static void
test_values(void)
{
    mbus_meter_record_t r;
    mbus_meter_profile_t prof = MBUS_METER_PROFILE_GENERIC();
    mbus_meter_profile_t uprof = MBUS_METER_PROFILE_GENERIC();
    char s[MBUS_METER_VALUE_STR_MAX];

    uprof.unsigned_ints = true;

    /* binary integers */
    CHECK(decode(&prof, &r, 0x04, 0, MBUS_METER_TP_NONE, 0, (uint8_t[]){ 0xD2, 0x04, 0, 0 }, 4) == 0);
    CHECK(r.vtype == MBUS_METER_VT_INT && r.value.i == 1234);
    CHECK(decode(&prof, &r, 0x01, 0, MBUS_METER_TP_NONE, 0, (uint8_t[]){ 0xFF }, 1) == 0);
    CHECK(r.value.i == -1);
    CHECK(decode(&uprof, &r, 0x01, 0, MBUS_METER_TP_NONE, 0, (uint8_t[]){ 0xFF }, 1) == 0);
    CHECK(r.vtype == MBUS_METER_VT_UINT && r.value.u == 255 && (r.flags & MBUS_METER_F_UNSIGNED_FORCED));
    CHECK(decode(&prof, &r, 0x03, 0, MBUS_METER_TP_NONE, 0, (uint8_t[]){ 0x00, 0x00, 0x80 }, 3) == 0);
    CHECK(r.value.i == -8388608);
    CHECK(decode(&prof, &r, 0x04, 0, MBUS_METER_TP_NONE, 0, (uint8_t[]){ 0xFF, 0xFF, 0xFF, 0xFF }, 4) == 0);
    CHECK(r.value.i == -1);
    CHECK(decode(&uprof, &r, 0x04, 0, MBUS_METER_TP_NONE, 0, (uint8_t[]){ 0xFF, 0xFF, 0xFF, 0xFF }, 4) == 0);
    CHECK(r.value.u == 4294967295u);
    CHECK(decode(&prof, &r, 0x06, 0, MBUS_METER_TP_NONE, 0,
                 (uint8_t[]){ 1, 2, 3, 4, 5, 6 }, 6) == 0);
    CHECK(r.value.i == 0x060504030201LL);
    CHECK(decode(&prof, &r, 0x07, 0, MBUS_METER_TP_NONE, 0,
                 (uint8_t[]){ 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x7F }, 8) == 0);
    CHECK(r.value.i == INT64_MAX);           /* no precision loss above 2^53 */
    CHECK(decode(&prof, &r, 0x04, 0, MBUS_METER_TP_NONE, 0, (uint8_t[]){ 1, 2 }, 2)
          == MBUS_METER_ERR_INVALID_FRAME);  /* short data */

    /* no data / selection */
    CHECK(decode(&prof, &r, 0x00, 0, MBUS_METER_TP_NONE, 0, NULL, 0) == 0 && r.vtype == MBUS_METER_VT_NONE);
    CHECK(decode(&prof, &r, 0x08, 0, MBUS_METER_TP_NONE, 0, NULL, 0) == 0 && r.vtype == MBUS_METER_VT_NONE);

    /* BCD */
    CHECK(decode(&prof, &r, 0x0C, 0, MBUS_METER_TP_NONE, 0, (uint8_t[]){ 0x78, 0x56, 0x34, 0x12 }, 4) == 0);
    CHECK(r.vtype == MBUS_METER_VT_INT && r.value.i == 12345678 && !(r.flags & MBUS_METER_F_BCD_ERROR));
    CHECK(decode(&prof, &r, 0x0E, 0, MBUS_METER_TP_NONE, 0,
                 (uint8_t[]){ 0x99, 0x99, 0x99, 0x99, 0x99, 0x99 }, 6) == 0);
    CHECK(r.value.i == 999999999999LL);
    CHECK(decode(&prof, &r, 0x0A, 0, MBUS_METER_TP_NONE, 0, (uint8_t[]){ 0x99, 0xF0 }, 2) == 0);
    CHECK(r.value.i == -99);                 /* F in the top nibble = negative */
    CHECK(decode(&prof, &r, 0x09, 0, MBUS_METER_TP_NONE, 0, (uint8_t[]){ 0x1A }, 1) == 0);
    CHECK(r.value.i == 10 && (r.flags & MBUS_METER_F_BCD_ERROR));
    CHECK(decode(&prof, &r, 0x0A, MBUS_METER_FUNC_ERROR, MBUS_METER_TP_NONE, 0,
                 (uint8_t[]){ 0x34, 0x12 }, 2) == 0);
    CHECK(r.vtype == MBUS_METER_VT_UINT && r.value.u == 0x1234 && (r.flags & MBUS_METER_F_BCD_AS_HEX));

    /* real */
    CHECK(decode(&prof, &r, 0x05, 0, MBUS_METER_TP_NONE, 0, (uint8_t[]){ 0x00, 0x00, 0x80, 0x3F }, 4) == 0);
    CHECK(r.vtype == MBUS_METER_VT_REAL && r.value.f == 1.0);

    /* LVAR */
    CHECK(decode(&prof, &r, 0x0D, 0, MBUS_METER_TP_NONE, 5, (const uint8_t *)"olleh", 5) == 0);
    CHECK(r.vtype == MBUS_METER_VT_TEXT && r.value.bytes.len == 5 && strcmp((char *)r.value.bytes.data, "hello") == 0);
    CHECK(mbus_meter_record_to_str(&r, s, sizeof(s)) == 5 && strcmp(s, "hello") == 0);
    mbus_meter_record_clear(&r);
    CHECK(decode(&prof, &r, 0x0D, 0, MBUS_METER_TP_NONE, 0, NULL, 0) == 0);
    CHECK(r.vtype == MBUS_METER_VT_TEXT && r.value.bytes.len == 0);
    mbus_meter_record_clear(&r);
    CHECK(decode(&prof, &r, 0x0D, 0, MBUS_METER_TP_NONE, 0xC2, (uint8_t[]){ 0x34, 0x12 }, 2) == 0);
    CHECK(r.value.i == 1234);
    CHECK(decode(&prof, &r, 0x0D, 0, MBUS_METER_TP_NONE, 0xD2, (uint8_t[]){ 0x34, 0x12 }, 2) == 0);
    CHECK(r.value.i == -1234);
    CHECK(decode(&prof, &r, 0x0D, 0, MBUS_METER_TP_NONE, 0xE2, (uint8_t[]){ 0x34, 0x12 }, 2) == 0);
    CHECK(r.vtype == MBUS_METER_VT_INT && r.value.i == 0x1234);
    CHECK(decode(&prof, &r, 0x0D, 0, MBUS_METER_TP_NONE, 0xE9,
                 (uint8_t[]){ 1, 2, 3, 4, 5, 6, 7, 8, 9 }, 9) == 0);
    CHECK(r.vtype == MBUS_METER_VT_BINARY && r.value.bytes.len == 9);
    CHECK(mbus_meter_record_to_str(&r, s, sizeof(s)) == 18 && strcmp(s, "010203040506070809") == 0);
    mbus_meter_record_clear(&r);
    CHECK(decode(&prof, &r, 0x0D, 0, MBUS_METER_TP_NONE, 9, (const uint8_t *)"abc", 3)
          == MBUS_METER_ERR_INVALID_FRAME);  /* lvar longer than the data */

    /* special function, vendor data */
    CHECK(decode(&prof, &r, 0x0F, 0, MBUS_METER_TP_NONE, 0, (uint8_t[]){ 0xAA, 0xBB }, 2) == 0);
    CHECK(r.vtype == MBUS_METER_VT_BINARY && r.value.bytes.len == 2);
    mbus_meter_record_clear(&r);

    /* keep_raw */
    {
        mbus_meter_profile_t kp = MBUS_METER_PROFILE_GENERIC();

        kp.keep_raw = true;
        CHECK(decode(&kp, &r, 0x02, 0, MBUS_METER_TP_NONE, 0, (uint8_t[]){ 0x01, 0x02 }, 2) == 0);
        CHECK(r.raw != NULL && r.raw_len == 2 && r.raw[1] == 0x02);
        mbus_meter_record_clear(&r);
    }
}

static void
test_datetime(void)
{
    mbus_meter_record_t r;
    mbus_meter_profile_t prof = MBUS_METER_PROFILE_GENERIC();
    char s[MBUS_METER_VALUE_STR_MAX];
    /* 2026-10-25 12:34:56 */
    const uint8_t g[]  = { 0x59, 0x3A };
    const uint8_t f[]  = { 0x22, 0x2C, 0x59, 0x3A };
    const uint8_t i6[] = { 0x38, 0x22, 0x0C, 0x59, 0x3A, 0x00 };
    const uint8_t j[]  = { 56, 34, 12 };

    CHECK(decode(&prof, &r, 0x02, 0, MBUS_METER_TP_DATE_G, 0, g, 2) == 0);
    CHECK(r.vtype == MBUS_METER_VT_DATETIME && r.value.dt.year == 2026 && r.value.dt.month == 10 && r.value.dt.day == 25);
    CHECK(mbus_meter_record_to_str(&r, s, sizeof(s)) == 10 && strcmp(s, "2026-10-25") == 0);
    CHECK(decode(&prof, &r, 0x02, 0, MBUS_METER_TP_DATE_G, 0, (uint8_t[]){ 0xFF, 0xFF }, 2) == 0);
    CHECK(r.value.dt.invalid && (r.flags & MBUS_METER_F_TIME_INVALID));
    CHECK(mbus_meter_record_to_str(&r, s, sizeof(s)) == 0 && s[0] == 0);
    /* date VIF but not 2 bytes: plain integer */
    CHECK(decode(&prof, &r, 0x04, 0, MBUS_METER_TP_DATE_G, 0, (uint8_t[]){ 1, 0, 0, 0 }, 4) == 0);
    CHECK(r.vtype == MBUS_METER_VT_INT && r.value.i == 1);

    CHECK(decode(&prof, &r, 0x04, 0, MBUS_METER_TP_DATETIME, 0, f, 4) == 0);
    CHECK(r.value.dt.year == 2026 && r.value.dt.hour == 12 && r.value.dt.minute == 34 && !r.value.dt.invalid);
    CHECK(mbus_meter_record_to_str(&r, s, sizeof(s)) == 19 && strcmp(s, "2026-10-25T12:34:00") == 0);
    {
        uint8_t fi[4];

        memcpy(fi, f, 4);
        fi[0] |= 0x80;                       /* IV */
        CHECK(decode(&prof, &r, 0x04, 0, MBUS_METER_TP_DATETIME, 0, fi, 4) == 0);
        CHECK(r.value.dt.invalid && r.value.dt.year == 0 && (r.flags & MBUS_METER_F_TIME_INVALID));
        memcpy(fi, f, 4);
        fi[1] |= 0x80;                       /* SU */
        CHECK(decode(&prof, &r, 0x04, 0, MBUS_METER_TP_DATETIME, 0, fi, 4) == 0);
        CHECK(r.value.dt.summer_time && r.value.dt.hour == 12);
    }
    {
        uint8_t fo[4] = { 0x22, 0x0C, 0x59, 0x3A };   /* hundred-year bits 0, yy 26 -> 2000+ */

        CHECK(decode(&prof, &r, 0x04, 0, MBUS_METER_TP_DATETIME, 0, fo, 4) == 0);
        CHECK(r.value.dt.year == 2026);
        fo[1] |= 0x40;                                /* hundred-year 2 -> 2100+ */
        CHECK(decode(&prof, &r, 0x04, 0, MBUS_METER_TP_DATETIME, 0, fo, 4) == 0);
        CHECK(r.value.dt.year == 2126);
    }
    CHECK(decode(&prof, &r, 0x06, 0, MBUS_METER_TP_DATETIME, 0, i6, 6) == 0);
    CHECK(mbus_meter_record_to_str(&r, s, sizeof(s)) == 19 && strcmp(s, "2026-10-25T12:34:56") == 0);
    CHECK(decode(&prof, &r, 0x03, 0, MBUS_METER_TP_DATETIME, 0, j, 3) == 0);
    CHECK(r.value.dt.kind == MBUS_METER_DT_TIME);
    CHECK(mbus_meter_record_to_str(&r, s, sizeof(s)) == 8 && strcmp(s, "12:34:56") == 0);
    /* datetime VIF with a size that is no time type: integer */
    CHECK(decode(&prof, &r, 0x02, 0, MBUS_METER_TP_DATETIME, 0, g, 2) == 0 && r.vtype == MBUS_METER_VT_INT);
}

static void
test_str(void)
{
    mbus_meter_record_t r;
    char s[MBUS_METER_VALUE_STR_MAX];
    char small[4];

    memset(&r, 0, sizeof(r));
    r.vtype = MBUS_METER_VT_INT;

    r.value.i = 12345; r.exp10 = -3;
    CHECK(mbus_meter_record_to_str(&r, s, sizeof(s)) == 6 && strcmp(s, "12.345") == 0);
    r.value.i = 5; r.exp10 = -3;
    CHECK(mbus_meter_record_to_str(&r, s, sizeof(s)) == 5 && strcmp(s, "0.005") == 0);
    r.value.i = -5; r.exp10 = -3;
    CHECK(mbus_meter_record_to_str(&r, s, sizeof(s)) == 6 && strcmp(s, "-0.005") == 0);
    r.value.i = 123; r.exp10 = 3;
    CHECK(mbus_meter_record_to_str(&r, s, sizeof(s)) == 6 && strcmp(s, "123000") == 0);
    r.value.i = 0; r.exp10 = -2;
    CHECK(mbus_meter_record_to_str(&r, s, sizeof(s)) == 4 && strcmp(s, "0.00") == 0);
    r.value.i = 0; r.exp10 = 3;
    CHECK(mbus_meter_record_to_str(&r, s, sizeof(s)) == 1 && strcmp(s, "0") == 0);
    r.value.i = 42; r.exp10 = 0;
    CHECK(mbus_meter_record_to_str(&r, s, sizeof(s)) == 2 && strcmp(s, "42") == 0);
    r.value.i = INT64_MIN; r.exp10 = 0;
    CHECK(mbus_meter_record_to_str(&r, s, sizeof(s)) == 20 && strcmp(s, "-9223372036854775808") == 0);
    r.value.i = INT64_MAX; r.exp10 = -3;
    CHECK(mbus_meter_record_to_str(&r, s, sizeof(s)) > 0 && strcmp(s, "9223372036854775.807") == 0);
    r.value.i = 123456; r.exp10 = -3;
    CHECK(mbus_meter_record_to_str(&r, small, sizeof(small)) == MBUS_METER_ERR_ARG && small[0] == 0);
    r.value.i = 1; r.exp10 = 100;
    CHECK(mbus_meter_record_to_str(&r, s, sizeof(s)) == MBUS_METER_ERR_ARG);

    r.vtype = MBUS_METER_VT_UINT; r.value.u = 18446744073709551615ULL; r.exp10 = 0;
    CHECK(mbus_meter_record_to_str(&r, s, sizeof(s)) == 20 && strcmp(s, "18446744073709551615") == 0);

    r.vtype = MBUS_METER_VT_REAL; r.value.f = 1.5; r.exp10 = 0;
    CHECK(mbus_meter_record_to_str(&r, s, sizeof(s)) > 0 && strcmp(s, "1.5") == 0);

    /* to_double */
    r.vtype = MBUS_METER_VT_INT; r.value.i = 12345; r.exp10 = -3;
    CHECK(fabs(mbus_meter_record_to_double(&r) - 12.345) < 1e-9);   /* x87 excess precision on 32-bit MinGW */
    r.value.i = 7; r.exp10 = 3;
    CHECK(mbus_meter_record_to_double(&r) == 7000.0);
    r.vtype = MBUS_METER_VT_NONE;
    CHECK(mbus_meter_record_to_double(&r) == 0.0);
    CHECK(mbus_meter_record_to_str(&r, s, sizeof(s)) == 0);
    CHECK(mbus_meter_record_to_str(NULL, s, sizeof(s)) == MBUS_METER_ERR_ARG);
}

static void
test_names(void)
{
    CHECK(strcmp(mbus_meter_unit_str(MBUS_METER_UNIT_WH), "Wh") == 0);
    CHECK(strcmp(mbus_meter_unit_str(MBUS_METER_UNIT_M3), "m^3") == 0);
    CHECK(strcmp(mbus_meter_unit_str((mbus_meter_unit_t)999), "?") == 0);
    CHECK(strcmp(mbus_meter_qty_str(MBUS_METER_QTY_ENERGY), "energy") == 0);
    CHECK(strcmp(mbus_meter_qty_str((mbus_meter_qty_t)999), "unknown") == 0);
    CHECK(strcmp(mbus_meter_strerror(MBUS_METER_ERR_JAM), "bus jammed") == 0);
    CHECK(strcmp(mbus_meter_strerror(-99), "unknown error") == 0);
    CHECK(strcmp(mbus_meter_end_reason_str(MBUS_METER_END_REPEAT), "repeated frame") == 0);
    CHECK(strcmp(mbus_meter_medium_str(0x07), "water") == 0);
    CHECK(strcmp(mbus_meter_medium_str(0x7E), "reserved") == 0);

    /* every enumerator has a name (catches a table that lags the enum) */
    {
        int q, u;

        for (q = 0; q <= MBUS_METER_QTY_TEMP_LIMIT; q++)
            CHECK(strcmp(mbus_meter_qty_str((mbus_meter_qty_t)q), "unknown") != 0 || q == 0);
        for (u = 1; u <= MBUS_METER_UNIT_OTHER; u++)
            CHECK(mbus_meter_unit_str((mbus_meter_unit_t)u)[0] != 0);
    }
}

static void
test_container(void)
{
    mbus_meter_readout_t out;
    mbus_meter_record_t *r;
    mbus_meter_key_t k;
    size_t from = 0;
    int err = -1, i;
    mbus_meter_profile_t prof = MBUS_METER_PROFILE_GENERIC();

    mbus_meter_readout_init(&out);
    for (i = 0; i < 40; i++) {               /* crosses two growth steps */
        r = mbus_meter_readout_add(&out, 0, &err);
        CHECK(r != NULL && err == 0);
        r->key.vif = (uint8_t)(i & 1 ? 0x13 : 0x06);
        r->key.storage = (uint64_t)i;
        r->vtype = MBUS_METER_VT_INT;
        r->value.i = i;
    }
    CHECK(out.n_records == 40 && out.n_alloc >= 40);
    memset(&k, 0, sizeof(k));
    k.vif = 0x13;
    k.storage = 7;
    r = (mbus_meter_record_t *)mbus_meter_readout_find(&out, &k, NULL);
    CHECK(r != NULL && r->value.i == 7);
    k.vife[0] = 1; k.n_vife = 1;
    CHECK(mbus_meter_readout_find(&out, &k, NULL) == NULL);   /* vife part of the key */
    memset(&k, 0, sizeof(k));
    k.vif = 0x06;                            /* iterate matches by vif and storage 0 */
    k.storage = 0;
    CHECK(mbus_meter_readout_find(&out, &k, &from) != NULL && from == 1);
    CHECK(mbus_meter_readout_find(&out, &k, &from) == NULL);

    /* text record is freed with the readout */
    r = mbus_meter_readout_add(&out, 0, &err);
    CHECK(r != NULL);
    CHECK(decode(&prof, r, 0x0D, 0, MBUS_METER_TP_NONE, 3, (const uint8_t *)"cba", 3) == 0);
    r->unit_text = strdup("kWh");
    mbus_meter_readout_drop_last(&out);
    CHECK(out.n_records == 40);
    mbus_meter_readout_free(&out);
    CHECK(out.records == NULL && out.n_records == 0);

    /* cap */
    mbus_meter_readout_init(&out);
    for (i = 0; i < 3; i++)
        CHECK(mbus_meter_readout_add(&out, 3, &err) != NULL);
    CHECK(mbus_meter_readout_add(&out, 3, &err) == NULL && err == MBUS_METER_PARTIAL);
    CHECK(out.n_records == 3 && out.n_alloc == 3);
    mbus_meter_readout_free(&out);
    mbus_meter_readout_free(NULL);
    CHECK(mbus_meter_readout_add(NULL, 0, &err) == NULL && err == MBUS_METER_ERR_ARG);
}

int
main(void)
{
    test_vib();
    test_values();
    test_datetime();
    test_str();
    test_names();
    test_container();
    printf("%d checks, %d failed\n", checks, failed);
    return failed ? 1 : 0;
}
