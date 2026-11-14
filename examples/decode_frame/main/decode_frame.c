/*
 * Decode captured M-Bus telegrams without a meter or a UART.
 * Set the hex frame(s) in menuconfig (decode_frame example), build, flash,
 * read the records on the console.
 */
#include <stdio.h>
#include <string.h>
#include "mbus.h"

#define FRAME_BYTES_MAX  300
#define FRAME_TEXT_MAX   1024

static void
print_record(FILE *out, size_t i, const mbus_meter_record_t *r)
{
    char val[MBUS_METER_VALUE_STR_MAX];
    const char *unit = r->unit_text ? r->unit_text : mbus_meter_unit_str(r->unit);

    if (mbus_meter_record_to_str(r, val, sizeof(val)) < 0)
        strcpy(val, "?");

    fprintf(out, "%2u %-22s %s %s  st=%llu tf=%lu fn=%u fl=0x%04x\n",
            (unsigned)i, mbus_meter_qty_str(r->qty), val, unit,
            (unsigned long long)r->key.storage, (unsigned long)r->key.tariff,
            (unsigned)r->key.function, (unsigned)r->flags);
}

/*
 * list: telegrams in hex, ';' between telegrams. All records go into one
 * readout, like several telegrams of one read cycle.
 * Returns the number of telegrams decoded, or -1 if none could be.
 */
int
decode_frames(FILE *out, const char *list)
{
    mbus_meter_readout_t ro;
    int decoded = 0, n = 0;
    const char *p = list;

    mbus_meter_readout_init(&ro);

    while (*p) {
        char text[FRAME_TEXT_MAX];
        unsigned char bin[FRAME_BYTES_MAX];
        mbus_frame frame;
        size_t len = strcspn(p, ";"), nbin;
        int rc;

        n++;
        if (len >= sizeof(text)) {
            fprintf(out, "frame %d: too long\n", n);
        } else {
            memcpy(text, p, len);
            text[len] = 0;
            nbin = mbus_hex2bin(bin, sizeof(bin), (const unsigned char *)text, len);

            memset(&frame, 0, sizeof(frame));
            if (nbin == 0 || mbus_parse(&frame, bin, nbin) != 0) {
                fprintf(out, "frame %d: not a valid M-Bus telegram (%s)\n", n, mbus_error_str());
            } else {
                rc = mbus_meter_decode_frame(&frame, NULL, &ro);
                if (rc == MBUS_METER_OK)
                    decoded++;
                else
                    fprintf(out, "frame %d: %s (CI 0x%02x)\n", n, mbus_meter_strerror(rc),
                            (unsigned)frame.control_information);
            }
        }
        p += len;
        if (*p == ';')
            p++;
    }

    if (ro.info.has_header)
        fprintf(out, "id=%s mfr=%s medium=%s ver=%u status=0x%02x\n", ro.info.id_str,
                ro.info.manufacturer, mbus_meter_medium_str(ro.info.medium),
                (unsigned)ro.info.version, (unsigned)ro.info.status);

    for (size_t i = 0; i < ro.n_records; i++)
        print_record(out, i, &ro.records[i]);

    fprintf(out, "%d of %d telegram(s) decoded, %u record(s)\n", decoded, n,
            (unsigned)ro.n_records);
    mbus_meter_readout_free(&ro);
    return decoded ? decoded : -1;
}

#ifndef DECODE_FRAME_HOST_TEST
void
app_main(void)
{
    decode_frames(stdout, CONFIG_EXAMPLE_FRAME_HEX);
}
#endif
