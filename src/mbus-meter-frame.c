#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "mbus-meter.h"
#include "mbus-meter-internal.h"

/* Frame -> typed records. Own parser, tolerant: a bad record ends the frame
 * but everything decoded before it stays in the readout. */

#define CI_RESP_VAR_LSB   0x72
#define CI_RESP_FIXED_LSB 0x73
#define CI_APP_ERROR      0x70
#define CI_RESP_VAR_NOHDR 0x78
#define CI_RESP_VAR_SHORT 0x7A

#define VAR_HDR_FULL   12
#define VAR_HDR_SHORT  4

/* data field length by DIF low nibble, 0xD (LVAR) and 0xF (special) handled apart */
static const uint8_t dif_len[16] = {
    0, 1, 2, 3, 4, 4, 6, 8,
    0, 1, 2, 3, 4, 0, 6, 0
};

/* ------------------------------------------------------------------ */
/* header helpers                                                      */
/* ------------------------------------------------------------------ */

static bool
bcd_valid(const uint8_t *b, size_t n)
{
    size_t i;

    for (i = 0; i < n; i++)
        if ((b[i] & 0x0F) > 9 || (b[i] >> 4) > 9)
            return false;
    return true;
}

/* id_bcd is LSB first; id_str reads like the printed number */
static void
fill_id(mbus_meter_info_t *info, const uint8_t *bcd)
{
    uint32_t v = 0;
    int i;

    snprintf(info->id_str, sizeof(info->id_str), "%02X%02X%02X%02X",
             bcd[3], bcd[2], bcd[1], bcd[0]);
    if (bcd_valid(bcd, 4)) {
        for (i = 3; i >= 0; i--)
            v = v * 100u + (uint32_t)((bcd[i] >> 4) * 10 + (bcd[i] & 0x0F));
    }
    info->id = v;                                /* 0 when the id holds A..F nibbles */
}

static void
fill_manufacturer(mbus_meter_info_t *info, const uint8_t *m)
{
    unsigned v = (unsigned)m[0] | ((unsigned)m[1] << 8);

    info->manufacturer[0] = (char)(((v >> 10) & 0x1F) + 64);
    info->manufacturer[1] = (char)(((v >> 5) & 0x1F) + 64);
    info->manufacturer[2] = (char)((v & 0x1F) + 64);
    info->manufacturer[3] = 0;
}

/* ------------------------------------------------------------------ */
/* record header scan                                                  */
/* ------------------------------------------------------------------ */

typedef struct {
    uint8_t  dif;
    uint8_t  dife[MBUS_METER_VIFE_MAX];
    uint8_t  n_dife;
    uint8_t  vif;
    uint8_t  vife[MBUS_METER_VIFE_MAX];
    uint8_t  n_vife;
    char    *unit_text;                          /* heap, moved into the record */
    uint8_t  lvar;
    const uint8_t *data;
    size_t   data_len;
} rec_hdr_t;

static bool
printable_run(const uint8_t *p, size_t n)
{
    size_t i;

    for (i = 0; i < n; i++)
        if (p[i] < 0x20 || p[i] == 0x7F)
            return false;
    return true;
}

/* VIFE chain: bytes while bit 7 set, the last one has it clear. */
static bool
scan_vife_chain(const uint8_t *p, size_t n, size_t *pi, uint8_t *vife, uint8_t *nv)
{
    uint8_t b;

    do {
        if (*pi >= n || *nv >= MBUS_METER_VIFE_MAX)
            return false;
        b = p[(*pi)++];
        vife[(*nv)++] = b;
    } while (b & 0x80);
    return true;
}

/* length byte + ASCII unit, stored reversed like all text fields */
static bool
scan_plain_text(const uint8_t *p, size_t n, size_t *pi, char **text)
{
    size_t len, i;
    char *s;

    if (*pi >= n)
        return false;
    len = p[*pi];
    if (*pi + 1 + len > n || !printable_run(&p[*pi + 1], len))
        return false;
    s = (char *)malloc(len + 1);
    if (s == NULL)
        return false;
    for (i = 0; i < len; i++)
        s[i] = (char)p[*pi + len - i];           /* last byte first */
    s[len] = 0;
    *text = s;
    *pi += 1 + len;
    return true;
}

/*
 * Plain-text VIF (0x7C/0xFC). Layout per the standard is VIF, VIFE chain,
 * length, text. Some meters send VIF, length, text, VIFE chain. Standard
 * layout is tried first; the text must be printable, otherwise the other
 * layout is used. Without the extension bit both layouts are the same.
 * Return: 0 ok, -1 not parseable (-2 no memory).
 */
static int
scan_plain_vif(const uint8_t *p, size_t n, size_t *pi, rec_hdr_t *h)
{
    size_t i = *pi;
    uint8_t nv = 0;
    char *text = NULL;

    if (h->vif & 0x80) {
        if (scan_vife_chain(p, n, &i, h->vife, &nv) && scan_plain_text(p, n, &i, &text)) {
            h->n_vife = nv;
            h->unit_text = text;
            *pi = i;
            return 0;
        }
        i = *pi;
        nv = 0;
        if (scan_plain_text(p, n, &i, &text) && scan_vife_chain(p, n, &i, h->vife, &nv)) {
            h->n_vife = nv;
            h->unit_text = text;
            *pi = i;
            return 0;
        }
        free(text);
        return -1;
    }
    if (!scan_plain_text(p, n, &i, &text))
        return -1;
    h->unit_text = text;
    *pi = i;
    return 0;
}

/* LVAR byte -> data length, -1 for reserved codes */
static int
lvar_len(uint8_t l)
{
    if (l <= 0xBF) return l;
    if (l >= 0xC0 && l <= 0xC9) return l - 0xC0;
    if (l >= 0xD0 && l <= 0xD9) return l - 0xD0;
    if (l >= 0xE0 && l <= 0xEF) return l - 0xE0;
    if (l >= 0xF0 && l <= 0xF4) return (l - 0xEC) * 4;
    if (l == 0xF5) return 48;
    if (l == 0xF6) return 64;
    return -1;
}

/* DIF/DIFE -> key fields */
static void
key_from_dib(mbus_meter_key_t *k, const rec_hdr_t *h)
{
    unsigned j;

    k->function = (uint8_t)((h->dif >> 4) & 0x03);
    k->storage = (uint64_t)((h->dif >> 6) & 1);
    k->tariff = 0;
    k->subunit = 0;
    for (j = 0; j < h->n_dife; j++) {
        k->storage |= (uint64_t)(h->dife[j] & 0x0F) << (1 + 4 * j);
        k->tariff |= (uint32_t)((h->dife[j] >> 4) & 0x03) << (2 * j);
        k->subunit |= (uint16_t)(((h->dife[j] >> 6) & 1) << j);
    }
}

static bool
key_wanted(const mbus_meter_profile_t *p, const mbus_meter_key_t *k)
{
    unsigned i;

    if (p->filter == NULL || p->n_filter == 0)
        return true;
    for (i = 0; i < p->n_filter; i++)
        if (mbus_meter_key_equal(k, &p->filter[i]))
            return true;
    return false;
}

static void
rec_hdr_free(rec_hdr_t *h)
{
    free(h->unit_text);
    h->unit_text = NULL;
}

/* ------------------------------------------------------------------ */
/* variable data                                                       */
/* ------------------------------------------------------------------ */

/* Returns OK, PARTIAL (record cap) or an error; *more set for DIF 0x1F. */
static int
parse_records(const uint8_t *p, size_t n, size_t i, const mbus_meter_profile_t *prof,
              mbus_meter_readout_t *out, bool *more)
{
    int err = 0, rc;

    while (i < n) {
        rec_hdr_t h;
        mbus_meter_vdesc_t d;
        mbus_meter_record_t *rec;
        mbus_meter_key_t key;
        mbus_meter_tp_t tp = MBUS_METER_TP_NONE;
        uint8_t ext;
        uint8_t k;

        if (p[i] == 0x2F) {                      /* idle filler */
            i++;
            continue;
        }

        memset(&h, 0, sizeof(h));
        memset(&key, 0, sizeof(key));
        h.dif = p[i++];

        /* special DIFs: rest of the frame is manufacturer data */
        if ((h.dif & 0x0F) == 0x0F) {
            if (h.dif != 0x0F && h.dif != 0x1F)
                return MBUS_METER_ERR_INVALID_FRAME;     /* reserved special function */
            if (h.dif == 0x1F)
                *more = true;
            key.vif = 0;
            if (!key_wanted(prof, &key)) {
                i = n;
                continue;
            }
            rec = mbus_meter_readout_add(out, prof->max_records, &err);
            if (rec == NULL)
                return err;
            rec->dif = h.dif;
            rec->key = key;
            rec->qty = MBUS_METER_QTY_MANUFACTURER_SPECIFIC;
            rec->unit = MBUS_METER_UNIT_NONE;
            rec->flags = MBUS_METER_F_MFR_SPECIFIC;
            rc = mbus_meter_value_decode(prof, rec, MBUS_METER_TP_NONE, 0, &p[i], n - i);
            if (rc < 0) {
                mbus_meter_readout_drop_last(out);
                return rc;
            }
            i = n;
            continue;
        }

        /* DIFE chain */
        ext = h.dif & 0x80;
        while (ext) {
            if (i >= n || h.n_dife >= MBUS_METER_VIFE_MAX)
                return MBUS_METER_ERR_INVALID_FRAME;
            h.dife[h.n_dife] = p[i++];
            ext = h.dife[h.n_dife] & 0x80;
            h.n_dife++;
        }

        /* VIF + VIFE (or plain text unit) */
        if (i >= n)
            return MBUS_METER_ERR_INVALID_FRAME;
        h.vif = p[i++];
        if ((h.vif & 0x7F) == 0x7C) {
            rc = scan_plain_vif(p, n, &i, &h);
            if (rc < 0)
                return rc == -2 ? MBUS_METER_ERR_NOMEM : MBUS_METER_ERR_INVALID_FRAME;
        } else if (h.vif & 0x80) {
            if (!scan_vife_chain(p, n, &i, h.vife, &h.n_vife))
                return MBUS_METER_ERR_INVALID_FRAME;
        }

        /* data length */
        if ((h.dif & 0x0F) == 0x0D) {
            int l;

            if (i >= n) {
                rec_hdr_free(&h);
                return MBUS_METER_ERR_INVALID_FRAME;
            }
            h.lvar = p[i++];
            l = lvar_len(h.lvar);
            if (l < 0) {
                rec_hdr_free(&h);
                return MBUS_METER_ERR_INVALID_FRAME;
            }
            h.data_len = (size_t)l;
        } else {
            h.data_len = dif_len[h.dif & 0x0F];
        }
        if (i + h.data_len > n) {
            rec_hdr_free(&h);
            return MBUS_METER_ERR_INVALID_FRAME;
        }
        h.data = &p[i];
        i += h.data_len;

        /* key */
        key_from_dib(&key, &h);
        key.vif = h.vif;
        key.n_vife = h.n_vife;
        for (k = 0; k < h.n_vife; k++)
            key.vife[k] = h.vife[k] & 0x7F;

        if (!key_wanted(prof, &key)) {
            rec_hdr_free(&h);
            continue;
        }

        mbus_meter_vib_resolve(h.vif, h.vife, h.n_vife, &d);
        tp = d.tp;

        rec = mbus_meter_readout_add(out, prof->max_records, &err);
        if (rec == NULL) {
            rec_hdr_free(&h);
            return err;
        }
        rec->key = key;
        rec->dif = h.dif;
        rec->qty = d.qty;
        rec->unit = d.unit;
        rec->exp10 = d.exp10;
        rec->add_exp = d.add_exp;
        rec->flags = d.flags;
        rec->unit_text = h.unit_text;            /* ownership moves to the record */
        h.unit_text = NULL;

        rc = mbus_meter_value_decode(prof, rec, tp, h.lvar, h.data, h.data_len);
        if (rc < 0) {
            mbus_meter_readout_drop_last(out);
            return rc;
        }
    }
    return MBUS_METER_OK;
}

/* ------------------------------------------------------------------ */
/* fixed structure (CI 0x73)                                           */
/* ------------------------------------------------------------------ */

#define FIXED_LEN        16
#define FIXED_ST_INT     0x80     /* counters binary instead of BCD */
#define FIXED_ST_STORED  0x40     /* stored values instead of actual */

static void
fixed_unit(uint8_t code, mbus_meter_vdesc_t *d)
{
    memset(d, 0, sizeof(*d));
    d->qty = MBUS_METER_QTY_UNKNOWN;
    d->unit = MBUS_METER_UNIT_OTHER;
    d->tp = MBUS_METER_TP_NONE;

    if (code == 0x00) {
        d->qty = MBUS_METER_QTY_TIME;            /* hhmmss */
        d->unit = MBUS_METER_UNIT_NONE;
    } else if (code == 0x01) {
        d->qty = MBUS_METER_QTY_DATE;            /* DDMMYY */
        d->unit = MBUS_METER_UNIT_NONE;
    } else if (code <= 0x0A) {
        d->qty = MBUS_METER_QTY_ENERGY;
        d->unit = MBUS_METER_UNIT_WH;
        d->exp10 = (int8_t)(code - 0x02);
    } else if (code <= 0x13) {
        d->qty = MBUS_METER_QTY_ENERGY;
        d->unit = MBUS_METER_UNIT_J;
        d->exp10 = (int8_t)(3 + code - 0x0B);
    } else if (code <= 0x1C) {
        d->qty = MBUS_METER_QTY_POWER;
        d->unit = MBUS_METER_UNIT_W;
        d->exp10 = (int8_t)(code - 0x14);
    } else if (code <= 0x25) {
        d->qty = MBUS_METER_QTY_POWER;
        d->unit = MBUS_METER_UNIT_J_PER_H;
        d->exp10 = (int8_t)(3 + code - 0x1D);
    } else if (code <= 0x2E) {
        d->qty = MBUS_METER_QTY_VOLUME;
        d->unit = MBUS_METER_UNIT_M3;
        d->exp10 = (int8_t)(-6 + code - 0x26);
    } else if (code <= 0x37) {
        d->qty = MBUS_METER_QTY_VOLUME_FLOW;
        d->unit = MBUS_METER_UNIT_M3_PER_H;
        d->exp10 = (int8_t)(-6 + code - 0x2F);
    } else if (code == 0x38) {
        d->qty = MBUS_METER_QTY_FLOW_TEMP;       /* 1e-3 degC steps, which one is not coded */
        d->unit = MBUS_METER_UNIT_CELSIUS;
        d->exp10 = -3;
    } else if (code == 0x39) {
        d->qty = MBUS_METER_QTY_HCA_UNITS;
        d->unit = MBUS_METER_UNIT_NONE;
    } else {
        d->flags = MBUS_METER_F_UNKNOWN_VIF;
    }
}

static int
parse_fixed(const mbus_frame *fr, const mbus_meter_profile_t *prof, mbus_meter_readout_t *out)
{
    const uint8_t *p = fr->data;
    uint8_t status = p[5];
    int idx, err = 0, rc;

    if (fr->data_size != FIXED_LEN)
        return MBUS_METER_ERR_INVALID_FRAME;

    if (out->frames == 0) {
        mbus_meter_info_t *in = &out->info;

        in->has_header = true;
        in->ci = CI_RESP_FIXED_LSB;
        fill_id(in, p);
        in->manufacturer[0] = 0;                 /* not part of this format */
        in->version = 0;
        in->medium = (uint8_t)(((p[6] & 0xC0) >> 6) | ((p[7] & 0xC0) >> 4));
        in->access_no = p[4];
        in->status = status;
        in->signature = 0;
    }
    out->frames++;

    for (idx = 0; idx < 2; idx++) {
        mbus_meter_record_t *rec;
        mbus_meter_vdesc_t d;
        mbus_meter_key_t key;
        uint8_t type = p[6 + idx];

        fixed_unit(type & 0x3F, &d);
        memset(&key, 0, sizeof(key));
        key.vif = type & 0x3F;                   /* unit code */
        key.storage = (uint64_t)idx + ((status & FIXED_ST_STORED) ? 2u : 0u);
        if (!key_wanted(prof, &key))
            continue;

        rec = mbus_meter_readout_add(out, prof->max_records, &err);
        if (rec == NULL)
            return err;
        rec->key = key;
        rec->dif = (status & FIXED_ST_INT) ? 0x04 : 0x0C;
        rec->qty = d.qty;
        rec->unit = d.unit;
        rec->exp10 = d.exp10;
        rec->flags = d.flags | MBUS_METER_F_FIXED_STRUCT;
        rc = mbus_meter_value_decode(prof, rec, MBUS_METER_TP_NONE, 0, &p[8 + 4 * idx], 4);
        if (rc < 0) {
            mbus_meter_readout_drop_last(out);
            return rc;
        }
    }
    return MBUS_METER_OK;
}

/* ------------------------------------------------------------------ */
/* public                                                              */
/* ------------------------------------------------------------------ */

int
mbus_meter_decode_frame(const mbus_frame *frame, const mbus_meter_profile_t *profile,
                        mbus_meter_readout_t *out)
{
    mbus_meter_profile_t generic = MBUS_METER_PROFILE_GENERIC();
    const uint8_t *p;
    size_t n, hdr;
    bool more = false;
    int rc;

    if (frame == NULL || out == NULL)
        return MBUS_METER_ERR_ARG;
    if (profile == NULL)
        profile = &generic;

    if (frame->type != MBUS_FRAME_TYPE_LONG || (frame->control & MBUS_CONTROL_MASK_DIR_M2S)
        || frame->data_size > MBUS_FRAME_DATA_LENGTH)
        return MBUS_METER_ERR_INVALID_FRAME;

    p = frame->data;
    n = frame->data_size;
    out->more_follows = false;

    switch (frame->control_information) {
    case CI_APP_ERROR:
        out->info.ci = CI_APP_ERROR;
        out->app_error = n > 0 ? p[0] : 0;
        return MBUS_METER_ERR_APP_ERROR;

    case CI_RESP_FIXED_LSB:
        return parse_fixed(frame, profile, out);

    case CI_RESP_VAR_LSB:
        hdr = VAR_HDR_FULL;
        break;
    case CI_RESP_VAR_SHORT:
        hdr = VAR_HDR_SHORT;
        break;
    case CI_RESP_VAR_NOHDR:
        hdr = 0;
        break;

    default:                                     /* 0x76/0x77 (MSB first), 0x10 dialects, ... */
        out->info.ci = frame->control_information;
        return MBUS_METER_ERR_UNSUPPORTED_CI;
    }

    if (n < hdr)
        return MBUS_METER_ERR_INVALID_FRAME;

    if (out->frames == 0) {                      /* identity comes from the first telegram */
        mbus_meter_info_t *in = &out->info;

        memset(in, 0, sizeof(*in));
        in->ci = frame->control_information;
        if (hdr == VAR_HDR_FULL) {
            in->has_header = true;
            fill_id(in, p);
            fill_manufacturer(in, p + 4);
            in->version = p[6];
            in->medium = p[7];
            in->access_no = p[8];
            in->status = p[9];
            in->signature = (uint16_t)(p[10] | (p[11] << 8));
        } else if (hdr == VAR_HDR_SHORT) {
            in->access_no = p[0];
            in->status = p[1];
            in->signature = (uint16_t)(p[2] | (p[3] << 8));
        }
    }
    out->frames++;                               /* counts as soon as records are kept */

    rc = parse_records(p, n, hdr, profile, out, &more);
    out->more_follows = (rc == MBUS_METER_OK) ? more : false;
    return rc;
}
