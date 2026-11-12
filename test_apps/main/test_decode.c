#include <stdio.h>
#include <string.h>
#include "test_support.h"

static size_t to_bin(const char *hex, unsigned char *dst, size_t max)
{
    return mbus_hex2bin(dst, max, (const unsigned char *)hex, strlen(hex));
}

TEST_CASE("library decode, 167-byte frame", "[decode]")
{
    unsigned char bin[300];
    mbus_frame frame;
    mbus_frame_data data;
    size_t n = to_bin(TB_FRAME_BIG, bin, sizeof(bin));

    memset(&frame, 0, sizeof(frame));
    memset(&data, 0, sizeof(data));
    TEST_ASSERT_EQUAL_UINT(167, n);
    TEST_ASSERT_EQUAL_INT(0, mbus_parse(&frame, bin, n));
    TEST_ASSERT_EQUAL_INT(0, mbus_frame_data_parse(&frame, &data));

    int type = data.type;
    int nrec = data.data_var.nrecords;

    if (data.data_var.record)
        mbus_data_record_free(data.data_var.record);
    TEST_ASSERT_EQUAL_INT(MBUS_DATA_TYPE_VARIABLE, type);
    TEST_ASSERT_EQUAL_INT(TB_BIG_RECORDS, nrec);
}

TEST_CASE("library decode, CI 0x78 and 0x7A", "[decode]")
{
    const char *f[] = { TB_FRAME_SMALL, TB_FRAME_CI7A };
    const unsigned char ci[] = { 0x78, 0x7A };

    for (int k = 0; k < 2; k++) {
        unsigned char bin[64];
        mbus_frame fr;
        mbus_frame_data d;
        size_t n = to_bin(f[k], bin, sizeof(bin));

        memset(&fr, 0, sizeof(fr));
        memset(&d, 0, sizeof(d));
        TEST_ASSERT_EQUAL_INT(0, mbus_parse(&fr, bin, n));
        TEST_ASSERT_EQUAL_HEX8(ci[k], fr.control_information);
        TEST_ASSERT_EQUAL_INT(0, mbus_frame_data_parse(&fr, &d));

        int type = d.type;
        int nrec = d.data_var.nrecords;

        if (d.data_var.record)
            mbus_data_record_free(d.data_var.record);
        TEST_ASSERT_EQUAL_INT(MBUS_DATA_TYPE_VARIABLE, type);
        TEST_ASSERT_EQUAL_INT(1, nrec);
    }
}

TEST_CASE("typed decode, 167-byte frame", "[decode]")
{
    unsigned char bin[300];
    mbus_frame frame;
    mbus_meter_readout_t out;
    char s[MBUS_METER_VALUE_STR_MAX];
    size_t n = to_bin(TB_FRAME_BIG, bin, sizeof(bin));

    memset(&frame, 0, sizeof(frame));
    TEST_ASSERT_EQUAL_INT(0, mbus_parse(&frame, bin, n));

    mbus_meter_readout_init(&out);
    int rc = mbus_meter_decode_frame(&frame, NULL, &out);

    bool hdr = out.info.has_header;
    size_t nrec = out.n_records;
    char id[9], mfr[4];

    snprintf(id, sizeof(id), "%s", out.info.id_str);
    snprintf(mfr, sizeof(mfr), "%s", out.info.manufacturer);

    /* record 0 is the fabrication number, record 2 the first volume */
    mbus_meter_qty_t q0 = nrec > 0 ? out.records[0].qty : MBUS_METER_QTY_UNKNOWN;
    mbus_meter_qty_t q1 = nrec > 1 ? out.records[1].qty : MBUS_METER_QTY_UNKNOWN;
    mbus_meter_qty_t q2 = nrec > 2 ? out.records[2].qty : MBUS_METER_QTY_UNKNOWN;
    s[0] = '\0';
    if (nrec > 1)
        mbus_meter_record_to_str(&out.records[1], s, sizeof(s));
    mbus_meter_readout_free(&out);

    TEST_ASSERT_EQUAL_INT(MBUS_METER_OK, rc);
    TEST_ASSERT_TRUE(hdr);
    TEST_ASSERT_EQUAL_UINT(TB_BIG_RECORDS, nrec);
    TEST_ASSERT_EQUAL_STRING("24083345", id);
    TEST_ASSERT_EQUAL_STRING("EFE", mfr);
    TEST_ASSERT_EQUAL_INT(MBUS_METER_QTY_FABRICATION_NO, q0);
    TEST_ASSERT_EQUAL_INT(MBUS_METER_QTY_DATETIME, q1);
    TEST_ASSERT_EQUAL_INT(MBUS_METER_QTY_VOLUME, q2);
    TEST_ASSERT_EQUAL_STRING("2014-03-12T14:23:00", s);
}

TEST_CASE("typed decode, header frame and bad checksum", "[decode]")
{
    unsigned char bin[64];
    mbus_frame frame;
    mbus_meter_readout_t out;
    char s[MBUS_METER_VALUE_STR_MAX];

    memset(&frame, 0, sizeof(frame));
    TEST_ASSERT_EQUAL_INT(0, mbus_parse(&frame, bin, to_bin(TB_FRAME_HDR, bin, sizeof(bin))));
    mbus_meter_readout_init(&out);
    TEST_ASSERT_EQUAL_INT(MBUS_METER_OK, mbus_meter_decode_frame(&frame, NULL, &out));
    TEST_ASSERT_EQUAL_UINT(1, out.n_records);
    mbus_meter_record_to_str(&out.records[0], s, sizeof(s));
    TEST_ASSERT_EQUAL_STRING("305419.896", s);
    TEST_ASSERT_EQUAL_INT(MBUS_METER_QTY_VOLUME, out.records[0].qty);
    TEST_ASSERT_EQUAL_INT(MBUS_METER_UNIT_M3, out.records[0].unit);
    mbus_meter_readout_free(&out);

    /* checksum is checked by the library parser */
    memset(&frame, 0, sizeof(frame));
    TEST_ASSERT_TRUE(mbus_parse(&frame, bin, to_bin(TB_FRAME_HDR_BAD, bin, sizeof(bin))) < 0);
}
