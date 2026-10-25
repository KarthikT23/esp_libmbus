/*
 * host test for src/mbus-meter-frame.c
 *   part 1: hand-built frames
 *   part 2: every .hex frame in the directories given on the command line,
 *           compared against the library parser (structure, not values)
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <dirent.h>
#include "mbus-meter.h"
#include "mbus-protocol.h"

static int checks, failed;

#define CHECK(cond) do { \
    checks++; \
    if (!(cond)) { failed++; printf("FAIL line %d: %s\n", __LINE__, #cond); } \
} while (0)

/* ------------------------------------------------------------------ */
/* helpers                                                             */
/* ------------------------------------------------------------------ */

static void
make_frame(mbus_frame *f, uint8_t ci, const uint8_t *data, size_t n)
{
    memset(f, 0, sizeof(*f));
    f->start1 = 0x68;
    f->control = 0x08;
    f->address = 1;
    f->control_information = ci;
    f->type = MBUS_FRAME_TYPE_LONG;
    memcpy(f->data, data, n);
    f->data_size = n;
}

/* 12 byte header: id 12345678, mfr ABC, ver 1, medium 7, acc 5, status 0, sig 0 */
static const uint8_t hdr72[12] = {
    0x78, 0x56, 0x34, 0x12, 0x43, 0x04, 0x01, 0x07, 0x05, 0x00, 0x00, 0x00
};

static size_t
with_hdr(uint8_t *dst, const uint8_t *rec, size_t n)
{
    memcpy(dst, hdr72, 12);
    memcpy(dst + 12, rec, n);
    return 12 + n;
}

static int
run(uint8_t ci, const uint8_t *data, size_t n, const mbus_meter_profile_t *p,
    mbus_meter_readout_t *out)
{
    mbus_frame f;

    make_frame(&f, ci, data, n);
    return mbus_meter_decode_frame(&f, p, out);
}

static int
run72(const uint8_t *rec, size_t n, const mbus_meter_profile_t *p, mbus_meter_readout_t *out)
{
    uint8_t buf[260];
    size_t t = with_hdr(buf, rec, n);

    return run(0x72, buf, t, p, out);
}

/* ------------------------------------------------------------------ */
/* part 1                                                              */
/* ------------------------------------------------------------------ */

static void
test_basic(void)
{
    mbus_meter_readout_t out;
    const mbus_meter_record_t *r;
    char s[MBUS_METER_VALUE_STR_MAX];
    const uint8_t rec[] = {
        0x04, 0x13, 0xD2, 0x04, 0x00, 0x00,        /* volume 1234 * 1e-3 m3 */
        0x2F, 0x2F,                                /* filler */
        0x02, 0x5A, 0xF5, 0x00                     /* flow temp 245 * 1e-1 */
    };
    int rc;

    mbus_meter_readout_init(&out);
    rc = run72(rec, sizeof(rec), NULL, &out);
    CHECK(rc == MBUS_METER_OK);
    CHECK(out.frames == 1 && out.n_records == 2 && !out.more_follows);
    CHECK(out.info.has_header && out.info.ci == 0x72);
    CHECK(strcmp(out.info.id_str, "12345678") == 0 && out.info.id == 12345678u);
    CHECK(strcmp(out.info.manufacturer, "ABC") == 0);
    CHECK(out.info.version == 1 && out.info.medium == 7 && out.info.access_no == 5 && out.info.status == 0);

    r = &out.records[0];
    CHECK(r->key.vif == 0x13 && r->key.n_vife == 0 && r->key.storage == 0 && r->key.function == 0);
    CHECK(r->qty == MBUS_METER_QTY_VOLUME && r->unit == MBUS_METER_UNIT_M3 && r->exp10 == -3);
    CHECK(r->vtype == MBUS_METER_VT_INT && r->value.i == 1234);
    CHECK(mbus_meter_record_to_str(r, s, sizeof(s)) == 5 && strcmp(s, "1.234") == 0);
    r = &out.records[1];
    CHECK(r->qty == MBUS_METER_QTY_FLOW_TEMP && r->value.i == 245 && r->exp10 == -1);
    CHECK(mbus_meter_record_to_str(r, s, sizeof(s)) == 4 && strcmp(s, "24.5") == 0);
    mbus_meter_readout_free(&out);
}

static void
test_key(void)
{
    mbus_meter_readout_t out;
    const mbus_meter_record_t *r;
    /* dif C4: ext, storage lsb 1, 4 bytes; dife A5: ext, tariff 2, storage nib 5;
     * dife 41: subunit 1, storage nib 1 -> storage 1 + 10 + 32 = 43, tariff 2, subunit 2 */
    const uint8_t rec[] = { 0xC4, 0xA5, 0x41, 0x13, 0x01, 0x00, 0x00, 0x00 };
    /* max function (0x10), storage 0, vif 6 = Wh e3 */
    const uint8_t rec2[] = { 0x14, 0x06, 0x0A, 0x00, 0x00, 0x00 };
    /* error function (0x30) with BCD size: digits are flags, read as hex */
    const uint8_t rec3[] = { 0x39, 0x13, 0x12 };

    mbus_meter_readout_init(&out);
    CHECK(run72(rec, sizeof(rec), NULL, &out) == MBUS_METER_OK && out.n_records == 1);
    r = &out.records[0];
    CHECK(r->key.storage == 43 && r->key.tariff == 2 && r->key.subunit == 2 && r->key.function == 0);
    mbus_meter_readout_free(&out);

    mbus_meter_readout_init(&out);
    CHECK(run72(rec2, sizeof(rec2), NULL, &out) == MBUS_METER_OK);
    r = &out.records[0];
    CHECK(r->key.function == MBUS_METER_FUNC_MAX && r->qty == MBUS_METER_QTY_ENERGY && r->exp10 == 3);
    mbus_meter_readout_free(&out);

    mbus_meter_readout_init(&out);
    CHECK(run72(rec3, sizeof(rec3), NULL, &out) == MBUS_METER_OK);
    r = &out.records[0];
    CHECK(r->key.function == MBUS_METER_FUNC_ERROR);
    CHECK(r->vtype == MBUS_METER_VT_UINT && r->value.u == 0x12 && (r->flags & MBUS_METER_F_BCD_AS_HEX));
    mbus_meter_readout_free(&out);

    /* highest storage: 10 DIFE of 0x0F ending without ext */
    {
        uint8_t d[40];
        size_t k = 0, j;

        d[k++] = 0xC1;
        for (j = 0; j < 9; j++)
            d[k++] = 0x8F;
        d[k++] = 0x0F;
        d[k++] = 0x13;
        d[k++] = 0x07;
        mbus_meter_readout_init(&out);
        CHECK(run72(d, k, NULL, &out) == MBUS_METER_OK && out.n_records == 1);
        CHECK(out.records[0].key.storage == (1ull << 41) - 1);
        mbus_meter_readout_free(&out);

        /* 11 DIFE */
        k = 0;
        d[k++] = 0xC1;
        for (j = 0; j < 10; j++)
            d[k++] = 0x8F;
        d[k++] = 0x0F;
        d[k++] = 0x13;
        d[k++] = 0x07;
        mbus_meter_readout_init(&out);
        CHECK(run72(d, k, NULL, &out) == MBUS_METER_ERR_INVALID_FRAME && out.n_records == 0);
        mbus_meter_readout_free(&out);
    }
}

static void
test_vib(void)
{
    mbus_meter_readout_t out;
    const mbus_meter_record_t *r;
    const uint8_t v1[] = { 0x02, 0xFD, 0x48, 0x78, 0x09 };          /* FD 48 voltage 2424 * 1e-1 V */
    const uint8_t v2[] = { 0x04, 0x93, 0x75, 0x01, 0x00, 0x00, 0x00 }; /* vol e-3 with vife 75 -> e-4 */
    const uint8_t v3[] = { 0x04, 0x13, 0x01, 0x00, 0x00, 0x00 };
    const uint8_t v4[] = { 0x04, 0x93, 0x22, 0x01, 0x00, 0x00, 0x00 }; /* unknown vife */
    const uint8_t v5[] = { 0x01, 0xFF, 0x01, 0x42 };                /* mfr vif */
    const uint8_t v6[] = { 0x01, 0x7F, 0x42 };
    const uint8_t v7[] = { 0x04, 0xFB, 0x01, 0x05, 0x00, 0x00, 0x00 }; /* FB 01 = MWh? */
    const uint8_t v8[] = { 0x04, 0x6F, 0x05, 0x00, 0x00, 0x00 };    /* unknown primary vif */

    mbus_meter_readout_init(&out);
    CHECK(run72(v1, sizeof(v1), NULL, &out) == MBUS_METER_OK && out.n_records == 1);
    r = &out.records[0];
    CHECK(r->key.vif == 0xFD && r->key.n_vife == 1 && r->key.vife[0] == 0x48);
    CHECK(r->qty == MBUS_METER_QTY_VOLTAGE && r->exp10 == -1 && r->value.i == 0x0978);
    mbus_meter_readout_free(&out);

    mbus_meter_readout_init(&out);
    CHECK(run72(v2, sizeof(v2), NULL, &out) == MBUS_METER_OK);
    r = &out.records[0];
    CHECK(r->key.vif == 0x93 && r->key.n_vife == 1 && r->key.vife[0] == 0x75 && r->exp10 == -4);
    mbus_meter_readout_free(&out);

    mbus_meter_readout_init(&out);
    CHECK(run72(v3, sizeof(v3), NULL, &out) == MBUS_METER_OK);
    CHECK(out.records[0].exp10 == -3 && out.records[0].key.n_vife == 0);
    mbus_meter_readout_free(&out);

    mbus_meter_readout_init(&out);
    CHECK(run72(v4, sizeof(v4), NULL, &out) == MBUS_METER_OK && out.n_records == 1);
    CHECK((out.records[0].flags & MBUS_METER_F_UNKNOWN_VIFE) && out.records[0].qty == MBUS_METER_QTY_VOLUME);
    mbus_meter_readout_free(&out);

    mbus_meter_readout_init(&out);
    CHECK(run72(v5, sizeof(v5), NULL, &out) == MBUS_METER_OK && out.n_records == 1);
    CHECK(out.records[0].flags & MBUS_METER_F_MFR_SPECIFIC);
    mbus_meter_readout_free(&out);

    mbus_meter_readout_init(&out);
    CHECK(run72(v6, sizeof(v6), NULL, &out) == MBUS_METER_OK && out.n_records == 1);
    CHECK(out.records[0].qty == MBUS_METER_QTY_MANUFACTURER_SPECIFIC && out.records[0].value.i == 0x42);
    mbus_meter_readout_free(&out);

    mbus_meter_readout_init(&out);
    CHECK(run72(v7, sizeof(v7), NULL, &out) == MBUS_METER_OK && out.n_records == 1);
    CHECK(out.records[0].qty == MBUS_METER_QTY_ENERGY && out.records[0].value.i == 0x05);
    mbus_meter_readout_free(&out);

    mbus_meter_readout_init(&out);
    CHECK(run72(v8, sizeof(v8), NULL, &out) == MBUS_METER_OK && out.n_records == 1);
    CHECK(out.records[0].flags & MBUS_METER_F_UNKNOWN_VIF);      /* kept, not dropped */
    mbus_meter_readout_free(&out);
}

static void
test_plain_text(void)
{
    mbus_meter_readout_t out;
    const mbus_meter_record_t *r;
    /* standard layout: VIF FC, VIFE 75 (no ext), len 3, "kWh" reversed, data */
    const uint8_t a[] = { 0x04, 0xFC, 0x75, 0x03, 'h', 'W', 'k', 0x0A, 0x00, 0x00, 0x00 };
    /* other layout: VIF FC, len 3, text, VIFE 75, data */
    const uint8_t b[] = { 0x04, 0xFC, 0x03, 'h', 'W', 'k', 0x75, 0x0A, 0x00, 0x00, 0x00 };
    /* no extension: VIF 7C */
    const uint8_t c[] = { 0x04, 0x7C, 0x03, 'h', 'W', 'k', 0x0A, 0x00, 0x00, 0x00 };
    /* text longer than the frame */
    const uint8_t d[] = { 0x04, 0x7C, 0x20, 'h', 'W', 'k' };

    mbus_meter_readout_init(&out);
    CHECK(run72(a, sizeof(a), NULL, &out) == MBUS_METER_OK && out.n_records == 1);
    r = &out.records[0];
    CHECK(r->qty == MBUS_METER_QTY_PLAIN_TEXT && r->unit_text && strcmp(r->unit_text, "kWh") == 0);
    CHECK(r->key.vif == 0xFC && r->key.n_vife == 1 && r->key.vife[0] == 0x75 && r->value.i == 10);
    mbus_meter_readout_free(&out);

    mbus_meter_readout_init(&out);
    CHECK(run72(b, sizeof(b), NULL, &out) == MBUS_METER_OK && out.n_records == 1);
    r = &out.records[0];
    CHECK(r->unit_text && strcmp(r->unit_text, "kWh") == 0 && r->key.n_vife == 1 && r->value.i == 10);
    mbus_meter_readout_free(&out);

    mbus_meter_readout_init(&out);
    CHECK(run72(c, sizeof(c), NULL, &out) == MBUS_METER_OK && out.n_records == 1);
    r = &out.records[0];
    CHECK(r->unit_text && strcmp(r->unit_text, "kWh") == 0 && r->key.n_vife == 0 && r->value.i == 10);
    mbus_meter_readout_free(&out);

    mbus_meter_readout_init(&out);
    CHECK(run72(d, sizeof(d), NULL, &out) == MBUS_METER_ERR_INVALID_FRAME && out.n_records == 0);
    mbus_meter_readout_free(&out);
}

static void
test_lvar_special(void)
{
    mbus_meter_readout_t out;
    const mbus_meter_record_t *r;
    const uint8_t t[] = { 0x0D, 0x78, 0x05, 'o', 'l', 'l', 'e', 'h' };
    const uint8_t t0[] = { 0x0D, 0x78, 0x00 };
    const uint8_t bad[] = { 0x0D, 0x78, 0xCA, 0x00 };              /* reserved lvar */
    const uint8_t sp[] = { 0x04, 0x13, 0x01, 0x00, 0x00, 0x00, 0x0F, 0xAA, 0xBB, 0xCC };
    const uint8_t more[] = { 0x04, 0x13, 0x01, 0x00, 0x00, 0x00, 0x1F };
    const uint8_t rsv[] = { 0x04, 0x13, 0x01, 0x00, 0x00, 0x00, 0x3F, 0x01 };

    mbus_meter_readout_init(&out);
    CHECK(run72(t, sizeof(t), NULL, &out) == MBUS_METER_OK && out.n_records == 1);
    r = &out.records[0];
    CHECK(r->vtype == MBUS_METER_VT_TEXT && strcmp((char *)r->value.bytes.data, "hello") == 0);
    CHECK(r->key.vif == 0x78 && r->qty == MBUS_METER_QTY_FABRICATION_NO);
    mbus_meter_readout_free(&out);

    mbus_meter_readout_init(&out);
    CHECK(run72(t0, sizeof(t0), NULL, &out) == MBUS_METER_OK && out.n_records == 1);
    CHECK(out.records[0].vtype == MBUS_METER_VT_TEXT && out.records[0].value.bytes.len == 0);
    mbus_meter_readout_free(&out);

    mbus_meter_readout_init(&out);
    CHECK(run72(bad, sizeof(bad), NULL, &out) == MBUS_METER_ERR_INVALID_FRAME && out.n_records == 0);
    mbus_meter_readout_free(&out);

    mbus_meter_readout_init(&out);
    CHECK(run72(sp, sizeof(sp), NULL, &out) == MBUS_METER_OK && out.n_records == 2 && !out.more_follows);
    r = &out.records[1];
    CHECK(r->dif == 0x0F && r->vtype == MBUS_METER_VT_BINARY && r->value.bytes.len == 3);
    CHECK(r->qty == MBUS_METER_QTY_MANUFACTURER_SPECIFIC && (r->flags & MBUS_METER_F_MFR_SPECIFIC));
    mbus_meter_readout_free(&out);

    mbus_meter_readout_init(&out);
    CHECK(run72(more, sizeof(more), NULL, &out) == MBUS_METER_OK && out.n_records == 1 && out.more_follows);
    CHECK(out.records[0].dif == 0x04);                  /* the bare 1F marker is not a record */
    mbus_meter_readout_free(&out);

    mbus_meter_readout_init(&out);
    CHECK(run72(rsv, sizeof(rsv), NULL, &out) == MBUS_METER_ERR_INVALID_FRAME && out.n_records == 1);
    mbus_meter_readout_free(&out);
}

static void
test_frame_kinds(void)
{
    mbus_meter_readout_t out;
    mbus_frame f;
    /* CI 78: no header */
    const uint8_t n78[] = { 0x04, 0x13, 0x0A, 0x00, 0x00, 0x00 };
    /* CI 7A: access 9, status 4, signature 0x0201 */
    const uint8_t n7a[] = { 0x09, 0x04, 0x01, 0x02, 0x04, 0x13, 0x0A, 0x00, 0x00, 0x00 };
    const uint8_t e70[] = { 0x05 };

    mbus_meter_readout_init(&out);
    CHECK(run(0x78, n78, sizeof(n78), NULL, &out) == MBUS_METER_OK);
    CHECK(!out.info.has_header && out.info.ci == 0x78 && out.n_records == 1 && out.records[0].value.i == 10);
    mbus_meter_readout_free(&out);

    mbus_meter_readout_init(&out);
    CHECK(run(0x7A, n7a, sizeof(n7a), NULL, &out) == MBUS_METER_OK);
    CHECK(!out.info.has_header && out.info.access_no == 9 && out.info.status == 4 && out.info.signature == 0x0201);
    CHECK(out.n_records == 1);
    mbus_meter_readout_free(&out);

    mbus_meter_readout_init(&out);
    CHECK(run(0x70, e70, sizeof(e70), NULL, &out) == MBUS_METER_ERR_APP_ERROR);
    CHECK(out.app_error == 5 && out.info.ci == 0x70);
    mbus_meter_readout_free(&out);

    mbus_meter_readout_init(&out);
    CHECK(run(0x76, n78, sizeof(n78), NULL, &out) == MBUS_METER_ERR_UNSUPPORTED_CI && out.info.ci == 0x76);
    CHECK(run(0x77, n78, sizeof(n78), NULL, &out) == MBUS_METER_ERR_UNSUPPORTED_CI);
    CHECK(run(0x10, n78, sizeof(n78), NULL, &out) == MBUS_METER_ERR_UNSUPPORTED_CI);
    CHECK(out.n_records == 0 && out.frames == 0);
    mbus_meter_readout_free(&out);

    /* short for the header */
    mbus_meter_readout_init(&out);
    CHECK(run(0x72, hdr72, 8, NULL, &out) == MBUS_METER_ERR_INVALID_FRAME);
    CHECK(run(0x7A, hdr72, 3, NULL, &out) == MBUS_METER_ERR_INVALID_FRAME);
    mbus_meter_readout_free(&out);

    /* wrong frame type and direction */
    mbus_meter_readout_init(&out);
    make_frame(&f, 0x78, n78, sizeof(n78));
    f.type = MBUS_FRAME_TYPE_SHORT;
    CHECK(mbus_meter_decode_frame(&f, NULL, &out) == MBUS_METER_ERR_INVALID_FRAME);
    make_frame(&f, 0x78, n78, sizeof(n78));
    f.control = 0x5B;                                /* master to slave */
    CHECK(mbus_meter_decode_frame(&f, NULL, &out) == MBUS_METER_ERR_INVALID_FRAME);
    make_frame(&f, 0x78, n78, sizeof(n78));
    f.control = 0x28;                                /* ACD set, still a reply */
    CHECK(mbus_meter_decode_frame(&f, NULL, &out) == MBUS_METER_OK);
    CHECK(mbus_meter_decode_frame(NULL, NULL, &out) == MBUS_METER_ERR_ARG);
    CHECK(mbus_meter_decode_frame(&f, NULL, NULL) == MBUS_METER_ERR_ARG);
    mbus_meter_readout_free(&out);
}

static void
test_tolerance(void)
{
    mbus_meter_readout_t out;
    /* good record, then a truncated one (data runs past the end) */
    const uint8_t trunc[] = { 0x04, 0x13, 0x01, 0x00, 0x00, 0x00, 0x04, 0x13, 0x01, 0x00 };
    /* good record, then DIF with nothing behind it */
    const uint8_t stop[] = { 0x04, 0x13, 0x01, 0x00, 0x00, 0x00, 0x04 };
    /* good record, then DIF + VIF but ext VIF without VIFE */
    const uint8_t vife[] = { 0x04, 0x13, 0x01, 0x00, 0x00, 0x00, 0x04, 0x93 };
    /* 2 frames into the same readout */
    const uint8_t one[] = { 0x04, 0x13, 0x01, 0x00, 0x00, 0x00 };
    mbus_meter_profile_t p = MBUS_METER_PROFILE_GENERIC();
    mbus_meter_key_t flt[1];
    int rc;

    mbus_meter_readout_init(&out);
    rc = run72(trunc, sizeof(trunc), NULL, &out);
    CHECK(rc == MBUS_METER_ERR_INVALID_FRAME && out.n_records == 1 && out.frames == 1);
    CHECK(out.records[0].value.i == 1);               /* record before the damage survives */
    mbus_meter_readout_free(&out);

    mbus_meter_readout_init(&out);
    CHECK(run72(stop, sizeof(stop), NULL, &out) == MBUS_METER_ERR_INVALID_FRAME && out.n_records == 1);
    mbus_meter_readout_free(&out);

    mbus_meter_readout_init(&out);
    CHECK(run72(vife, sizeof(vife), NULL, &out) == MBUS_METER_ERR_INVALID_FRAME && out.n_records == 1);
    mbus_meter_readout_free(&out);

    /* two frames, identity from the first */
    mbus_meter_readout_init(&out);
    CHECK(run72(one, sizeof(one), NULL, &out) == MBUS_METER_OK);
    {
        mbus_frame f;

        make_frame(&f, 0x78, one, sizeof(one));
        CHECK(mbus_meter_decode_frame(&f, NULL, &out) == MBUS_METER_OK);
    }
    CHECK(out.frames == 2 && out.n_records == 2 && out.info.has_header && out.info.ci == 0x72);
    mbus_meter_readout_free(&out);

    /* record cap */
    {
        const uint8_t three[] = { 0x04, 0x13, 1, 0, 0, 0, 0x04, 0x13, 2, 0, 0, 0, 0x04, 0x13, 3, 0, 0, 0 };

        p.max_records = 2;
        mbus_meter_readout_init(&out);
        CHECK(run72(three, sizeof(three), &p, &out) == MBUS_METER_PARTIAL);
        CHECK(out.n_records == 2 && out.records[1].value.i == 2);
        mbus_meter_readout_free(&out);
    }

    /* filter */
    {
        const uint8_t two[] = { 0x04, 0x13, 1, 0, 0, 0, 0x44, 0x13, 2, 0, 0, 0, 0x02, 0x5A, 9, 0 };

        p = (mbus_meter_profile_t)MBUS_METER_PROFILE_GENERIC();
        memset(flt, 0, sizeof(flt));
        flt[0].vif = 0x13;
        flt[0].storage = 1;
        p.filter = flt;
        p.n_filter = 1;
        mbus_meter_readout_init(&out);
        CHECK(run72(two, sizeof(two), &p, &out) == MBUS_METER_OK);
        CHECK(out.n_records == 1 && out.records[0].value.i == 2 && out.records[0].key.storage == 1);
        mbus_meter_readout_free(&out);
    }

    /* keep_raw and unsigned */
    {
        const uint8_t u[] = { 0x01, 0x13, 0xFF };

        p = (mbus_meter_profile_t)MBUS_METER_PROFILE_GENERIC();
        p.keep_raw = true;
        p.unsigned_ints = true;
        mbus_meter_readout_init(&out);
        CHECK(run72(u, sizeof(u), &p, &out) == MBUS_METER_OK);
        CHECK(out.records[0].value.u == 255 && out.records[0].raw_len == 1 && out.records[0].raw[0] == 0xFF);
        mbus_meter_readout_free(&out);
    }
}

static void
test_fixed(void)
{
    mbus_meter_readout_t out;
    /* id 12345678, tx 0x0A, status 0 (BCD, actual), l / l, 1 and 135 */
    const uint8_t fx[16] = {
        0x78, 0x56, 0x34, 0x12, 0x0A, 0x00, 0xE9, 0x69,
        0x01, 0x00, 0x00, 0x00, 0x35, 0x01, 0x00, 0x00
    };
    uint8_t g[16];
    char s[MBUS_METER_VALUE_STR_MAX];

    mbus_meter_readout_init(&out);
    CHECK(run(0x73, fx, 16, NULL, &out) == MBUS_METER_OK);
    CHECK(out.info.has_header && out.info.ci == 0x73 && out.info.medium == 7 && out.info.access_no == 0x0A);
    CHECK(strcmp(out.info.id_str, "12345678") == 0);
    CHECK(out.n_records == 2 && out.frames == 1);
    CHECK((out.records[0].flags & MBUS_METER_F_FIXED_STRUCT) && out.records[0].qty == MBUS_METER_QTY_VOLUME);
    CHECK(out.records[0].exp10 == -3 && out.records[0].value.i == 1 && out.records[1].value.i == 135);
    CHECK(out.records[0].key.storage == 0 && out.records[1].key.storage == 1);
    CHECK(mbus_meter_record_to_str(&out.records[1], s, sizeof(s)) == 5 && strcmp(s, "0.135") == 0);
    mbus_meter_readout_free(&out);

    /* binary counters, stored values */
    memcpy(g, fx, 16);
    g[5] = 0xC0;
    g[8] = 0xE8; g[9] = 0x03;
    mbus_meter_readout_init(&out);
    CHECK(run(0x73, g, 16, NULL, &out) == MBUS_METER_OK);
    CHECK(out.records[0].value.i == 1000 && out.records[0].key.storage == 2 && out.records[1].key.storage == 3);
    mbus_meter_readout_free(&out);

    /* wrong size */
    mbus_meter_readout_init(&out);
    CHECK(run(0x73, fx, 15, NULL, &out) == MBUS_METER_ERR_INVALID_FRAME);
    mbus_meter_readout_free(&out);
}

/* ------------------------------------------------------------------ */
/* part 2: library comparison                                          */
/* ------------------------------------------------------------------ */

static size_t
hex2bin(unsigned char *dst, size_t max, const char *s)
{
    size_t n = 0;
    unsigned v;
    int pos;

    while (n < max && sscanf(s, " %2x%n", &v, &pos) == 1) {
        dst[n++] = (unsigned char)v;
        s += pos;
    }
    return n;
}

static int g_cmp_ok, g_cmp_bad, g_cmp_skip, g_ours_only, g_both_err;

static void
compare_file(const char *dir, const char *name)
{
    static char txt[4096];
    unsigned char bin[400];
    char path[600];
    mbus_frame fr;
    mbus_frame_data d;
    mbus_meter_readout_t out;
    mbus_meter_profile_t prof = MBUS_METER_PROFILE_GENERIC();
    FILE *f;
    size_t n;
    int lib_rc, our_rc;

    snprintf(path, sizeof(path), "%s/%s", dir, name);
    f = fopen(path, "r");
    if (!f)
        return;
    n = fread(txt, 1, sizeof(txt) - 1, f);
    fclose(f);
    txt[n] = 0;
    n = hex2bin(bin, sizeof(bin), txt);

    memset(&fr, 0, sizeof(fr));
    memset(&d, 0, sizeof(d));
    if (mbus_parse(&fr, bin, n) != 0) {
        g_cmp_skip++;
        return;
    }
    lib_rc = mbus_frame_data_parse(&fr, &d);

    prof.keep_raw = true;
    prof.max_records = 0;                            /* no cap, compare everything */
    mbus_meter_readout_init(&out);
    our_rc = mbus_meter_decode_frame(&fr, &prof, &out);

    if (lib_rc != 0) {
        if (our_rc == 0)
            g_ours_only++;                           /* tolerant where the library gives up */
        else
            g_both_err++;
        goto done;
    }

    if (d.type == MBUS_DATA_TYPE_VARIABLE) {
        mbus_data_record *lr;
        size_t i = 0;
        int bad = 0;

        /* libmbus counts only CI 72/78/7A as variable; same set here */
        if (our_rc != 0) {
            printf("DIFF %s: ours rc=%d, library ok\n", name, our_rc);
            g_cmp_bad++;
            goto done;
        }
        if (out.info.has_header) {
            char id[9];

            snprintf(id, sizeof(id), "%02X%02X%02X%02X", d.data_var.header.id_bcd[3],
                     d.data_var.header.id_bcd[2], d.data_var.header.id_bcd[1],
                     d.data_var.header.id_bcd[0]);
            if (strcmp(id, out.info.id_str) != 0
                || strcmp(mbus_decode_manufacturer(d.data_var.header.manufacturer[0],
                                                   d.data_var.header.manufacturer[1]),
                          out.info.manufacturer) != 0
                || d.data_var.header.version != out.info.version
                || d.data_var.header.medium != out.info.medium
                || d.data_var.header.access_no != out.info.access_no
                || d.data_var.header.status != out.info.status) {
                printf("DIFF %s: header\n", name);
                bad++;
            }
        }
        {
            size_t lib_n = 0;

            for (lr = d.data_var.record; lr; lr = (mbus_data_record *)lr->next)
                if (!((lr->drh.dib.dif & 0x0F) == 0x0F && lr->data_len == 0))
                    lib_n++;                         /* bare 0F/1F markers are not records here */
            if (lib_n != out.n_records) {
                printf("DIFF %s: records lib=%zu ours=%zu\n", name, lib_n, out.n_records);
                bad++;
            }
        }
        for (lr = d.data_var.record; lr && i < out.n_records; lr = (mbus_data_record *)lr->next) {
            if ((lr->drh.dib.dif & 0x0F) == 0x0F && lr->data_len == 0)
                continue;
            const mbus_meter_record_t *r = &out.records[i];
            long st = mbus_data_record_storage_number(lr);
            long tf = mbus_data_record_tariff(lr);
            int dv = mbus_data_record_device(lr);
            size_t k, off = 0;
            int rb = 0;

            if (r->dif != lr->drh.dib.dif) rb = 1;
            if ((lr->drh.dib.dif & 0x0F) != 0x0F) {
                if (r->key.vif != lr->drh.vib.vif) rb = 2;
                if (r->key.n_vife != lr->drh.vib.nvife) rb = 3;
                else
                    for (k = 0; k < r->key.n_vife; k++)
                        if (r->key.vife[k] != (lr->drh.vib.vife[k] & 0x7F)) rb = 4;
                if (st >= 0 && (uint64_t)st != r->key.storage) rb = 5;
                if (tf >= 0 && (uint32_t)tf != r->key.tariff) rb = 6;
                if (dv >= 0 && (uint16_t)dv != r->key.subunit) rb = 7;
                if (r->key.function != ((lr->drh.dib.dif >> 4) & 3)) rb = 8;
                if ((lr->drh.dib.dif & 0x0F) == 0x0D)
                    off = 1;                         /* library keeps the LVAR byte in data */
            }
            if (lr->data_len < off || r->raw_len != lr->data_len - off) rb = 9;
            else if (r->raw_len && memcmp(r->raw, lr->data + off, r->raw_len) != 0
                     && (lr->drh.dib.dif & 0x0F) != 0x0D) rb = 10;
            if (rb) {
                printf("DIFF %s: record %zu (dif %02X vif %02X) reason %d\n", name, i,
                       lr->drh.dib.dif, lr->drh.vib.vif, rb);
                bad++;
            }
            i++;
        }
        if (bad) g_cmp_bad++; else g_cmp_ok++;
        mbus_data_record_free(d.data_var.record);
    } else if (d.type == MBUS_DATA_TYPE_FIXED) {
        long long c1 = mbus_data_bcd_decode(d.data_fix.cnt1_val, 4);
        long long c2 = mbus_data_bcd_decode(d.data_fix.cnt2_val, 4);
        int bad = 0;

        if (our_rc != 0 || out.n_records != 2) {
            printf("DIFF %s: fixed rc=%d n=%zu\n", name, our_rc, out.n_records);
            bad++;
        } else if (!(d.data_fix.status & 0x80)) {
            if (out.records[0].value.i != c1 && !(out.records[0].flags & MBUS_METER_F_BCD_ERROR)) bad++;
            if (out.records[1].value.i != c2 && !(out.records[1].flags & MBUS_METER_F_BCD_ERROR)) bad++;
            if (bad) printf("DIFF %s: fixed values\n", name);
        }
        if (bad) g_cmp_bad++; else g_cmp_ok++;
    } else {
        /* library reports an error frame (CI 0x70 and similar) */
        if (our_rc >= 0) {
            printf("DIFF %s: library error frame, ours rc=%d\n", name, our_rc);
            g_cmp_bad++;
        } else {
            g_cmp_ok++;
        }
    }
done:
    mbus_meter_readout_free(&out);
}

static void
compare_dir(const char *dir)
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

        if (l > 4 && strcmp(e->d_name + l - 4, ".hex") == 0)
            compare_file(dir, e->d_name);
    }
    closedir(dp);
}

int
main(int argc, char **argv)
{
    int i;

    test_basic();
    test_key();
    test_vib();
    test_plain_text();
    test_lvar_special();
    test_frame_kinds();
    test_tolerance();
    test_fixed();

    for (i = 1; i < argc; i++)
        compare_dir(argv[i]);
    if (argc > 1) {
        printf("library compare: %d same, %d differ, %d ours-only ok, %d both reject, %d unparsed\n",
               g_cmp_ok, g_cmp_bad, g_ours_only, g_both_err, g_cmp_skip);
        checks++;
        if (g_cmp_bad) failed++;
    }

    printf("%d checks, %d failed\n", checks, failed);
    return failed ? 1 : 0;
}
