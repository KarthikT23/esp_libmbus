/* In-memory M-Bus bus with scripted slaves. See fake_slave.h. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "fake_slave.h"

/* ---- rx queue (slave -> master) ---- */

static size_t rxq_count(const fs_bus *b)
{
    return b->rx_tail - b->rx_head;
}

static void rxq_push(fs_bus *b, const uint8_t *d, size_t n)
{
    size_t i;

    for (i = 0; i < n && rxq_count(b) < FS_RXQ_SIZE; i++)
        b->rxq[b->rx_tail++ % FS_RXQ_SIZE] = d[i];
}

/* -1 = nothing to read (read timeout) */
static int rxq_pop(fs_bus *b)
{
    if (rxq_count(b) > 0)
        return b->rxq[b->rx_head++ % FS_RXQ_SIZE];
    if (b->jam_left > 0) {
        b->jam_left--;
        return 0xFF;
    }
    return -1;
}

/* ---- helpers ---- */

size_t fs_hex(uint8_t *dst, size_t max, const char *s)
{
    size_t n = 0;
    int hi = -1;

    for (; *s && n < max; s++) {
        int v;

        if (isspace((unsigned char)*s))
            continue;
        if (!isxdigit((unsigned char)*s))
            break;
        v = isdigit((unsigned char)*s) ? *s - '0' : (tolower((unsigned char)*s) - 'a' + 10);
        if (hi < 0) {
            hi = v;
        } else {
            dst[n++] = (uint8_t)((hi << 4) | v);
            hi = -1;
        }
    }
    return n;
}

size_t fs_build_long(uint8_t *out, uint8_t addr, uint8_t ci, const uint8_t *ud, size_t n)
{
    size_t i, p = 0;
    uint8_t cs;

    out[p++] = 0x68;
    out[p++] = (uint8_t)(3 + n);
    out[p++] = (uint8_t)(3 + n);
    out[p++] = 0x68;
    out[p++] = 0x08;
    out[p++] = addr;
    out[p++] = ci;
    for (i = 0; i < n; i++)
        out[p++] = ud[i];
    for (cs = 0, i = 4; i < p; i++)
        cs = (uint8_t)(cs + out[i]);
    out[p++] = cs;
    out[p++] = 0x16;
    return p;
}

/* id/mfr/version/medium bytes exactly as the select frame packs them */
static int sec_bytes(const char *sec16, uint8_t out[8])
{
    mbus_frame t;
    char tmp[17];

    memset(&t, 0, sizeof(t));
    snprintf(tmp, sizeof(tmp), "%s", sec16);
    if (mbus_frame_select_secondary_pack(&t, tmp) != 0)
        return -1;
    memcpy(out, t.data, 8);
    return 0;
}

size_t fs_build_var(uint8_t *out, uint8_t addr, uint8_t ci, const char *sec16,
                    uint8_t access, const uint8_t *rec, size_t rec_n, int more)
{
    uint8_t ud[FS_TG_MAX];
    size_t n = 0;

    if (ci == 0x72) {
        if (sec16 == NULL || sec_bytes(sec16, ud) != 0)
            return 0;
        n = 8;
        ud[n++] = access;
        ud[n++] = 0x00;   /* status */
        ud[n++] = 0x00;   /* signature */
        ud[n++] = 0x00;
    } else if (ci == 0x7A) {
        ud[n++] = access;
        ud[n++] = 0x00;
        ud[n++] = 0x00;
        ud[n++] = 0x00;
    }
    if (n + rec_n + 1 > sizeof(ud))
        return 0;
    memcpy(&ud[n], rec, rec_n);
    n += rec_n;
    if (more)
        ud[n++] = 0x1F;
    return fs_build_long(out, addr, ci, ud, n);
}

/* ---- bus setup ---- */

void fs_init(fs_bus *b)
{
    memset(b, 0, sizeof(*b));
}

int fs_add_slave(fs_bus *b, int primary, const char *sec16, fs_fcb_mode mode)
{
    fs_slave *s;

    if (b->n_slaves >= FS_MAX_SLAVES)
        return -1;
    s = &b->slave[b->n_slaves];
    memset(s, 0, sizeof(*s));
    s->primary = primary;
    if (sec16)
        snprintf(s->sec, sizeof(s->sec), "%s", sec16);
    s->fcb_mode = mode;
    s->select_resets_fcb = 1;
    s->last_sent = -1;
    return b->n_slaves++;
}

void fs_add_tg(fs_bus *b, int slave, const uint8_t *tg, size_t len)
{
    fs_slave *s = &b->slave[slave];

    if (s->n_tg >= FS_MAX_TG || len > FS_TG_MAX)
        return;
    memcpy(s->tg[s->n_tg], tg, len);
    s->tg_len[s->n_tg++] = len;
}

void fs_set_reduced(fs_bus *b, int slave, const uint8_t *tg, size_t len)
{
    fs_slave *s = &b->slave[slave];

    if (len > FS_TG_MAX)
        return;
    memcpy(s->reduced, tg, len);
    s->reduced_len = len;
}

/* ---- slave behaviour ---- */

static void slave_reset(fs_slave *s)
{
    s->idx = 0;
    s->last_fcb = 0;
    s->sent_any = 0;
    s->last_sent = -1;
}

static void emit(fs_bus *b, const uint8_t *tg, size_t len)
{
    uint8_t tmp[FS_TG_MAX];

    memcpy(tmp, tg, len);
    if (b->corrupt_next == 1 && len > 2) {
        tmp[len - 2] ^= 0xFF;            /* checksum */
    } else if (b->corrupt_next == 2) {
        len /= 2;                        /* cut in the middle */
    }
    b->corrupt_next = 0;
    rxq_push(b, tmp, len);
}

static void answer(fs_bus *b, fs_slave *s, int fcb, int fcv)
{
    int new_frame, i;

    s->requests++;
    if (s->silent_requests > 0) {
        s->silent_requests--;
        return;
    }
    if (s->n_tg == 0)
        return;

    if (!fcv) {
        new_frame = 1;
        s->idx = 0;
    } else if (s->fcb_mode == FS_FCB_LENIENT) {
        new_frame = 1;
    } else {
        new_frame = (fcb != s->last_fcb);
    }

    if (new_frame) {
        if (fcv)
            s->last_fcb = fcb;
        i = s->idx;
        if (i >= s->n_tg)
            i = s->repeat_last ? s->n_tg - 1 : 0;
        emit(b, s->tg[i], s->tg_len[i]);
        s->last_sent = i;
        s->sent_any = 1;
        s->idx = i + 1;
    } else if (s->sent_any && s->last_sent >= 0) {
        emit(b, s->tg[s->last_sent], s->tg_len[s->last_sent]);
    } else if (s->reduced_len) {
        emit(b, s->reduced, s->reduced_len);
        s->sent_any = 1;
        s->last_sent = -1;
    } else {
        emit(b, s->tg[0], s->tg_len[0]);
    }
}

static int match_primary(fs_bus *b, int addr, int *out, int max)
{
    int i, n = 0;

    for (i = 0; i < b->n_slaves && n < max; i++) {
        fs_slave *s = &b->slave[i];

        if (addr == 253) {
            if (s->selected)
                out[n++] = i;
        } else if (addr == 254) {
            out[n++] = i;
        } else if (s->primary == addr) {
            out[n++] = i;
        }
    }
    return n;
}

/* mask nibble F / byte FF match anything */
static int sec_match(const uint8_t *mask, const uint8_t *own)
{
    int i;

    for (i = 0; i < 4; i++) {
        uint8_t m = mask[i], o = own[i];

        if ((m & 0xF0) != 0xF0 && (m & 0xF0) != (o & 0xF0))
            return 0;
        if ((m & 0x0F) != 0x0F && (m & 0x0F) != (o & 0x0F))
            return 0;
    }
    for (i = 4; i < 8; i++)
        if (mask[i] != 0xFF && mask[i] != own[i])
            return 0;
    return 1;
}

static void handle_select(fs_bus *b, const mbus_frame *f)
{
    int i, hits = 0;
    static const uint8_t ack = 0xE5;

    for (i = 0; i < b->n_slaves; i++) {
        fs_slave *s = &b->slave[i];
        uint8_t own[8];

        s->selected = 0;
        if (s->sec[0] == '\0' || sec_bytes(s->sec, own) != 0)
            continue;
        if (sec_match(f->data, own)) {
            s->selected = 1;
            if (s->select_resets_fcb)
                slave_reset(s);
            hits++;
        }
    }
    for (i = 0; i < hits; i++)       /* overlapping ACKs read as extra frames */
        rxq_push(b, &ack, 1);
}

static int fs_send(mbus_handle *h, mbus_frame *f)
{
    fs_bus *b = (fs_bus *)h->auxdata;
    uint8_t pk[600];
    static const uint8_t ack = 0xE5;
    int n, fn, t[FS_MAX_SLAVES], nt, i;

    if ((n = mbus_frame_pack(f, pk, sizeof(pk))) < 0)
        return -1;

    /* real driver drops stale rx bytes before it writes */
    b->rx_head = b->rx_tail;

    if (b->n_log < FS_LOG_MAX) {
        fs_log_entry *e = &b->log[b->n_log++];

        e->type = (uint8_t)f->type;
        e->control = f->control;
        e->address = f->address;
        e->ci = f->control_information;
    }

    if (b->echo)
        rxq_push(b, pk, (size_t)n);

    fn = f->control & 0x0F;

    if (fn == 0x00 && f->type == MBUS_FRAME_TYPE_SHORT) {            /* SND_NKE */
        if (f->address == 255) {
            for (i = 0; i < b->n_slaves; i++)
                slave_reset(&b->slave[i]);
        } else if (f->address == 253) {
            for (i = 0; i < b->n_slaves; i++)
                b->slave[i].selected = 0;
        } else {
            nt = match_primary(b, f->address, t, FS_MAX_SLAVES);
            for (i = 0; i < nt; i++)
                slave_reset(&b->slave[t[i]]);
            if (nt > 0)
                rxq_push(b, &ack, 1);
        }
    } else if (fn == 0x0B && f->type == MBUS_FRAME_TYPE_SHORT) {     /* REQ_UD2 */
        nt = match_primary(b, f->address, t, FS_MAX_SLAVES);
        if (nt == 1) {
            answer(b, &b->slave[t[0]], (f->control & 0x20) != 0, (f->control & 0x10) != 0);
        } else if (nt > 1) {
            static const uint8_t junk[] = { 0x68, 0x05, 0x05, 0x68, 0x08 };

            rxq_push(b, junk, sizeof(junk));
        }
    } else if (fn == 0x03) {                                         /* SND_UD */
        if (f->address == 253 && f->control_information == MBUS_CONTROL_INFO_SELECT_SLAVE
            && f->data_size == 8) {
            handle_select(b, f);
        } else {
            nt = match_primary(b, f->address, t, FS_MAX_SLAVES);
            if (f->control_information == MBUS_CONTROL_INFO_APPLICATION_RESET)
                for (i = 0; i < nt; i++)
                    slave_reset(&b->slave[t[i]]);
            if (nt > 0 && f->address != 254)
                rxq_push(b, &ack, 1);
        }
    }
    return 0;
}

static int fs_recv(mbus_handle *h, mbus_frame *frame)
{
    fs_bus *b = (fs_bus *)h->auxdata;
    unsigned char buff[2048];
    int remaining = 1;
    size_t len = 0;

    b->recv_calls++;

    /* same loop shape as the serial driver: ask the parser how much it still needs */
    do {
        int got = 0;

        while (got < remaining && len < sizeof(buff)) {
            int c = rxq_pop(b);

            if (c < 0)
                break;
            buff[len++] = (unsigned char)c;
            got++;
        }
        if (got == 0)
            break;
    } while ((remaining = mbus_parse(frame, buff, len)) > 0);

    if (len == 0)
        return MBUS_RECV_RESULT_TIMEOUT;
    if (remaining != 0)
        return MBUS_RECV_RESULT_INVALID;
    return MBUS_RECV_RESULT_OK;
}

static int fs_nop(mbus_handle *h)
{
    (void)h;
    return 0;
}

mbus_handle *fs_handle(fs_bus *b)
{
    mbus_handle *h = (mbus_handle *)calloc(1, sizeof(*h));

    if (h == NULL)
        return NULL;
    h->max_data_retry = 3;
    h->max_search_retry = 1;
    h->fd = -1;
    h->purge_first_frame = MBUS_FRAME_PURGE_M2S;
    h->open = fs_nop;
    h->close = fs_nop;
    h->send = fs_send;
    h->recv = fs_recv;
    h->auxdata = b;
    return h;
}

void fs_free_handle(mbus_handle *h)
{
    free(h);
}

int fs_count_frames(mbus_frame *f)
{
    int n = 0;

    for (; f; f = (mbus_frame *)f->next)
        n++;
    return n;
}

int fs_req_control(const fs_bus *b, int n)
{
    int i, k = 0;

    for (i = 0; i < b->n_log; i++) {
        if (b->log[i].type == MBUS_FRAME_TYPE_SHORT && (b->log[i].control & 0x0F) == 0x0B) {
            if (k++ == n)
                return b->log[i].control;
        }
    }
    return -1;
}

int fs_count_req(const fs_bus *b)
{
    int i, k = 0;

    for (i = 0; i < b->n_log; i++)
        if (b->log[i].type == MBUS_FRAME_TYPE_SHORT && (b->log[i].control & 0x0F) == 0x0B)
            k++;
    return k;
}
