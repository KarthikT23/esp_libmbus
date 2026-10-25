#include <stdlib.h>
#include <string.h>
#include "mbus-meter.h"
#include "mbus-meter-internal.h"
#include "mbus-serial.h"

/* Read cycle on top of the library: link wake-up, NKE / secondary select,
 * optional application reset, REQ_UD2 loop with FCB handling, retries and
 * repeat detection. Blocking, one bus per task. */

#define ADDR_NETWORK   253            /* secondary addressing */
#define ADDR_BROADCAST 255
#define MAX_FRAMES_DEFAULT 32
#define PURGE_CAP      64             /* frames drained before the bus counts as jammed */

struct mbus_meter {
    mbus_handle         *h;
    bool                 primary;
    int                  paddr;
    char                 sec[17];
    mbus_meter_profile_t prof;
    mbus_link_mode_t     mode;        /* found by detect, UNKNOWN until then */
    bool                 has_read;
    uint32_t             last_read_ms;
};

/* receive side scratch, heap to keep the task stack small */
typedef struct {
    mbus_frame fr;
    uint8_t    prev[1 + MBUS_FRAME_DATA_LENGTH];
    size_t     prev_len;
    bool       have_prev;
} rx_ctx_t;

/* ------------------------------------------------------------------ */
/* object                                                              */
/* ------------------------------------------------------------------ */

static int
profile_check(const mbus_meter_profile_t *p)
{
    int i;
    bool term = false;

    if (p->first_fcb > 1 || (int)p->fcb_policy < 0 || p->fcb_policy > MBUS_METER_FCB_NONE
        || (int)p->drain_mode < 0 || p->drain_mode > MBUS_METER_DRAIN_PURGE
        || (int)p->link_mode < 0 || p->link_mode > MBUS_LINK_MODE_WAKEUP
        || p->app_reset_subcode < -1 || p->app_reset_subcode > 0xFF
        || (p->n_filter > 0 && p->filter == NULL))
        return MBUS_METER_ERR_ARG;
    for (i = 0; i < MBUS_METER_BAUD_LIST_MAX; i++)
        if (p->baud_list[i] == 0) {
            term = true;
            break;
        }
    return term ? MBUS_METER_OK : MBUS_METER_ERR_ARG;
}

mbus_meter_t *
mbus_meter_new(mbus_handle *handle, const mbus_address *addr, const mbus_meter_profile_t *profile)
{
    mbus_meter_profile_t generic = MBUS_METER_PROFILE_GENERIC();
    mbus_meter_t *m;

    if (handle == NULL || addr == NULL)
        return NULL;
    if (profile == NULL)
        profile = &generic;
    if (profile_check(profile) != MBUS_METER_OK)
        return NULL;
    if (addr->is_primary) {
        if (!mbus_is_primary_address(addr->primary))
            return NULL;
    } else if (!mbus_is_secondary_address(addr->secondary)) {
        return NULL;
    }

    m = (mbus_meter_t *)calloc(1, sizeof(*m));
    if (m == NULL)
        return NULL;
    m->h = handle;
    m->primary = addr->is_primary != 0;
    if (m->primary)
        m->paddr = addr->primary;
    else
        memcpy(m->sec, addr->secondary, 16);
    m->prof = *profile;
    m->mode = MBUS_LINK_MODE_UNKNOWN;
    return m;
}

void
mbus_meter_free(mbus_meter_t *meter)
{
    free(meter);
}

int
mbus_meter_set_profile(mbus_meter_t *meter, const mbus_meter_profile_t *profile)
{
    mbus_meter_profile_t generic = MBUS_METER_PROFILE_GENERIC();

    if (meter == NULL)
        return MBUS_METER_ERR_ARG;
    if (profile == NULL)
        profile = &generic;
    if (profile_check(profile) != MBUS_METER_OK)
        return MBUS_METER_ERR_ARG;
    meter->prof = *profile;
    return MBUS_METER_OK;
}

mbus_link_mode_t
mbus_meter_link_mode(const mbus_meter_t *meter)
{
    return meter ? meter->mode : MBUS_LINK_MODE_UNKNOWN;
}

/* ------------------------------------------------------------------ */
/* helpers                                                             */
/* ------------------------------------------------------------------ */

static bool
aborted(const mbus_meter_t *m)
{
    return m->h->abort_scan_check != NULL && m->h->abort_scan_check(m->h);
}

static mbus_link_mode_t
eff_mode(const mbus_meter_t *m)
{
    return m->prof.link_mode != MBUS_LINK_MODE_UNKNOWN ? m->prof.link_mode : m->mode;
}

/* rx timeouts from the profile for the length of one operation, serial only */
typedef struct {
    bool applied;
    int  first, gap;
} to_save_t;

static void
to_apply(const mbus_meter_t *m, to_save_t *s)
{
    int f, g;

    s->applied = false;
    if (!m->h->is_serial || (m->prof.rx_first_byte_ms == 0 && m->prof.rx_gap_ms == 0))
        return;
    if (mbus_serial_get_rx_timeouts(m->h, &s->first, &s->gap) != 0)
        return;
    f = m->prof.rx_first_byte_ms ? (int)m->prof.rx_first_byte_ms : s->first;
    g = m->prof.rx_gap_ms ? (int)m->prof.rx_gap_ms : s->gap;
    if (mbus_serial_set_rx_timeouts(m->h, f, g) == 0)
        s->applied = true;
}

static void
to_restore(const mbus_meter_t *m, const to_save_t *s)
{
    if (s->applied)
        mbus_serial_set_rx_timeouts(m->h, s->first, s->gap);
}

/* Read and drop frames until the bus is quiet. Returns 1 if the cap was hit. */
static int
purge_jam(mbus_handle *h)
{
    mbus_frame junk;
    int guard, r;

    for (guard = PURGE_CAP; guard > 0; guard--) {
        memset(&junk, 0, sizeof(junk));
        r = mbus_recv_frame(h, &junk);
        if (r != MBUS_RECV_RESULT_OK && r != MBUS_RECV_RESULT_INVALID)
            return 0;
    }
    return 1;
}

/* after a good frame, per profile */
static int
drain(const mbus_meter_t *m)
{
    if (m->prof.drain_mode == MBUS_METER_DRAIN_PURGE)
        return purge_jam(m->h) ? MBUS_METER_ERR_JAM : 0;
    if (m->h->is_serial)
        mbus_serial_flush_rx(m->h);
    return 0;
}

/* REQ_UD2 with explicit FCV, the library helper always sets it */
static int
send_req_ud2(mbus_handle *h, int addr, int fcb, int fcv)
{
    mbus_frame *f = mbus_frame_new(MBUS_FRAME_TYPE_SHORT);
    int rc;

    if (f == NULL)
        return -1;
    f->control = MBUS_CONTROL_MASK_REQ_UD2 | MBUS_CONTROL_MASK_DIR_M2S;
    f->control &= (unsigned char)~MBUS_CONTROL_MASK_FCV;
    if (fcv) {
        f->control |= MBUS_CONTROL_MASK_FCV;
        if (fcb)
            f->control |= MBUS_CONTROL_MASK_FCB;
    }
    f->address = (unsigned char)addr;
    rc = mbus_send_frame(h, f);
    mbus_frame_free(f);
    return rc == 0 ? 0 : -1;
}

static int
fail(mbus_meter_readout_t *out, int err, mbus_meter_end_reason_t why)
{
    out->last_err = err;
    out->end_reason = why;
    return err;
}

/* An error after >= 1 good frame leaves a usable readout: PARTIAL. */
static int
fail_or_partial(mbus_meter_readout_t *out, int good, int err, mbus_meter_end_reason_t why)
{
    out->last_err = err;
    out->end_reason = why;
    return good > 0 ? MBUS_METER_PARTIAL : err;
}

/* ------------------------------------------------------------------ */
/* slave preparation                                                   */
/* ------------------------------------------------------------------ */

/* 1 = single slave selected, 0 = nobody answered, <0 = error code */
static int
select_once(mbus_meter_t *m)
{
    int r = mbus_select_secondary_address(m->h, m->sec);

    if (r == MBUS_PROBE_SINGLE)
        return 1;
    if (r == MBUS_PROBE_COLLISION)
        return MBUS_METER_ERR_COLLISION;
    if (r == MBUS_PROBE_ERROR)
        return MBUS_METER_ERR_LINK;
    return 0;
}

/* Select with the known link mode, or find it: direct first, then wake-up. */
static int
select_secondary(mbus_meter_t *m, uint8_t retries)
{
    const mbus_meter_profile_t *p = &m->prof;
    mbus_link_mode_t mode = eff_mode(m);
    int r, a;

    if (mode == MBUS_LINK_MODE_UNKNOWN) {
        r = select_once(m);
        if (r == 1) {
            m->mode = MBUS_LINK_MODE_DIRECT;
            return 1;
        }
        if (r < 0)
            return r;
        mbus_meter_port_delay_ms(p->link.detect_gap_ms);
        if (m->h->is_serial && mbus_link_wakeup(m->h, &p->link) != 0)
            return MBUS_METER_ERR_LINK;
        r = select_once(m);
        if (r == 1)
            m->mode = MBUS_LINK_MODE_WAKEUP;
        return r;
    }

    for (a = 0; a <= retries; a++) {
        if (aborted(m))
            return MBUS_METER_ERR_ABORTED;
        if (mode == MBUS_LINK_MODE_WAKEUP && m->h->is_serial
            && mbus_link_wakeup(m->h, &p->link) != 0)
            return MBUS_METER_ERR_LINK;
        r = select_once(m);
        if (r != 0)
            return r;
        if (a < retries)
            mbus_meter_port_delay_ms(p->retry_delay_ms);
    }
    return 0;
}

/* Brings the slave to a known state. *q_addr is the address for REQ_UD2. */
static int
prepare_slave(mbus_meter_t *m, int *q_addr)
{
    const mbus_meter_profile_t *p = &m->prof;
    mbus_handle *h = m->h;
    mbus_link_mode_t mode = eff_mode(m);
    bool nke_done = false;
    int r;

    if (m->primary) {
        *q_addr = m->paddr;

        if (mode == MBUS_LINK_MODE_UNKNOWN) {
            /* detect is a ping, so it also does the NKE */
            mode = mbus_link_detect(h, m->paddr, &p->link);
            if (mode == MBUS_LINK_MODE_UNKNOWN)
                return MBUS_METER_ERR_TIMEOUT;
            m->mode = mode;
            nke_done = true;
        } else if (p->nke_before_read && !p->nke_broadcast) {
            r = mbus_link_ping(h, m->paddr, &p->link, mode);
            if (r != 0 && p->nke_ack_required)
                return MBUS_METER_ERR_NO_ACK;
            nke_done = true;
        } else if (mode == MBUS_LINK_MODE_WAKEUP && h->is_serial) {
            if (mbus_link_wakeup(h, &p->link) != 0)
                return MBUS_METER_ERR_LINK;
        }

        if (p->nke_before_read && p->nke_broadcast) {
            if (mbus_send_ping_frame(h, ADDR_BROADCAST, 0) != 0)
                return MBUS_METER_ERR_LINK;
            nke_done = true;
        }
        if (nke_done)
            mbus_meter_port_delay_ms(p->post_nke_delay_ms);
    } else {
        *q_addr = ADDR_NETWORK;

        if (mode == MBUS_LINK_MODE_WAKEUP && h->is_serial
            && mbus_link_wakeup(h, &p->link) != 0)
            return MBUS_METER_ERR_LINK;
        if (p->nke_before_read) {
            /* network layer NKE: deselects, slaves do not answer */
            if (mbus_send_ping_frame(h, ADDR_NETWORK, 0) != 0)
                return MBUS_METER_ERR_LINK;
            mbus_meter_port_delay_ms(p->post_nke_delay_ms);
        }
        /* a link mode that still has to be found is probed inside select */
        r = select_secondary(m, p->retries);
        if (r < 0)
            return r;
        if (r == 0)
            return MBUS_METER_ERR_NO_ACK;
        mbus_meter_port_delay_ms(p->select_settle_ms);
    }

    if (p->app_reset) {
        mbus_frame ack;

        if (mbus_send_application_reset_frame(h, *q_addr, p->app_reset_subcode) != 0)
            return MBUS_METER_ERR_LINK;
        memset(&ack, 0, sizeof(ack));
        r = mbus_recv_frame(h, &ack);
        if ((r != MBUS_RECV_RESULT_OK || ack.type != MBUS_FRAME_TYPE_ACK) && p->nke_ack_required)
            return MBUS_METER_ERR_NO_ACK;
        mbus_meter_port_delay_ms(p->post_nke_delay_ms);
    }
    return MBUS_METER_OK;
}

/* ------------------------------------------------------------------ */
/* request loop                                                        */
/* ------------------------------------------------------------------ */

/* One telegram: send REQ_UD2, receive, retry with the same FCB. 0 or error code. */
static int
fetch_frame(mbus_meter_t *m, rx_ctx_t *c, int q_addr, int fcb, int fcv)
{
    const mbus_meter_profile_t *p = &m->prof;
    int last = MBUS_METER_ERR_TIMEOUT;
    unsigned a;

    for (a = 0; a <= p->retries; a++) {
        int r;

        if (aborted(m))
            return MBUS_METER_ERR_ABORTED;
        if (send_req_ud2(m->h, q_addr, fcb, fcv) != 0)
            return MBUS_METER_ERR_LINK;

        memset(&c->fr, 0, sizeof(c->fr));
        r = mbus_recv_frame(m->h, &c->fr);

        if (r == MBUS_RECV_RESULT_OK) {
            bool addr_ok = !m->primary || p->accept_any_reply || m->paddr >= 254
                           || c->fr.address == m->paddr;

            if ((c->fr.type == MBUS_FRAME_TYPE_LONG || c->fr.type == MBUS_FRAME_TYPE_CONTROL) && addr_ok)
                return 0;
            last = MBUS_METER_ERR_INVALID_FRAME;   /* ACK, short frame or wrong slave */
            if (purge_jam(m->h))
                return MBUS_METER_ERR_JAM;
        } else if (r == MBUS_RECV_RESULT_TIMEOUT) {
            last = MBUS_METER_ERR_TIMEOUT;
        } else if (r == MBUS_RECV_RESULT_INVALID) {
            last = MBUS_METER_ERR_INVALID_FRAME;
            if (purge_jam(m->h))
                return MBUS_METER_ERR_JAM;
        } else {
            return MBUS_METER_ERR_LINK;
        }
        if (a < p->retries)
            mbus_meter_port_delay_ms(p->retry_delay_ms);
    }
    return last;
}

static void
rollback(mbus_meter_readout_t *out, size_t n_records, uint8_t frames)
{
    while (out->n_records > n_records)
        mbus_meter_readout_drop_last(out);
    out->frames = frames;
    out->more_follows = false;
}

static bool
same_as_prev(const rx_ctx_t *c)
{
    size_t n = 1 + c->fr.data_size;

    return c->have_prev && c->prev_len == n && c->prev[0] == c->fr.control_information
           && memcmp(&c->prev[1], c->fr.data, c->fr.data_size) == 0;
}

static void
remember(rx_ctx_t *c)
{
    c->prev[0] = c->fr.control_information;
    memcpy(&c->prev[1], c->fr.data, c->fr.data_size);
    c->prev_len = 1 + c->fr.data_size;
    c->have_prev = true;
}

static int
request_loop(mbus_meter_t *m, rx_ctx_t *c, int q_addr, mbus_meter_readout_t *out)
{
    const mbus_meter_profile_t *p = &m->prof;
    int fcb, fcv = 1, good = 0, rc, dr;
    int max_frames = p->max_frames ? p->max_frames : MAX_FRAMES_DEFAULT;

    switch (p->fcb_policy) {
    case MBUS_METER_FCB_ALWAYS_SET:   fcb = 1; break;
    case MBUS_METER_FCB_ALWAYS_CLEAR: fcb = 0; break;
    case MBUS_METER_FCB_NONE:         fcb = 0; fcv = 0; break;
    default:                          fcb = p->first_fcb; break;
    }

    for (;;) {
        unsigned t;
        size_t n_before = out->n_records;
        uint8_t fr_before = out->frames;

        if (aborted(m))
            return fail(out, MBUS_METER_ERR_ABORTED, MBUS_METER_END_ABORTED);

        /* decode errors get the same retry budget as receive errors */
        for (t = 0;; t++) {
            rc = fetch_frame(m, c, q_addr, fcb, fcv);
            if (rc == MBUS_METER_ERR_ABORTED)
                return fail(out, rc, MBUS_METER_END_ABORTED);
            if (rc != 0)
                return fail_or_partial(out, good, rc, MBUS_METER_END_ERROR);

            if (p->stop_on_repeat && same_as_prev(c)) {
                dr = drain(m);
                out->end_reason = MBUS_METER_END_REPEAT;
                out->more_follows = false;
                if (dr != 0)
                    return fail_or_partial(out, good, dr, MBUS_METER_END_ERROR);
                return MBUS_METER_OK;
            }

            rc = mbus_meter_decode_frame(&c->fr, p, out);
            if (rc != MBUS_METER_ERR_INVALID_FRAME || t >= p->retries)
                break;
            rollback(out, n_before, fr_before);
            mbus_meter_port_delay_ms(p->retry_delay_ms);
        }

        dr = drain(m);

        if (rc == MBUS_METER_PARTIAL) {            /* record cap */
            out->end_reason = MBUS_METER_END_MAX_RECORDS;
            return MBUS_METER_PARTIAL;
        }
        if (rc != MBUS_METER_OK)
            return fail_or_partial(out, good, rc, MBUS_METER_END_ERROR);

        good++;
        remember(c);
        if (dr != 0)
            return fail_or_partial(out, good, dr, MBUS_METER_END_ERROR);

        if (!out->more_follows) {
            out->end_reason = MBUS_METER_END_LAST_FRAME;
            return MBUS_METER_OK;
        }
        if (good >= max_frames) {
            out->end_reason = MBUS_METER_END_MAX_FRAMES;
            return MBUS_METER_PARTIAL;
        }
        if (p->fcb_policy == MBUS_METER_FCB_TOGGLE)
            fcb ^= 1;
        mbus_meter_port_delay_ms(p->inter_frame_gap_ms);
    }
}

/* ------------------------------------------------------------------ */
/* public                                                              */
/* ------------------------------------------------------------------ */

int
mbus_meter_read(mbus_meter_t *meter, mbus_meter_readout_t *out)
{
    const mbus_meter_profile_t *p;
    rx_ctx_t *c;
    to_save_t ts;
    uint32_t now;
    int q_addr = 0, rc;

    if (meter == NULL || out == NULL)
        return MBUS_METER_ERR_ARG;
    p = &meter->prof;
    mbus_meter_readout_init(out);

    now = mbus_meter_port_now_ms();
    if (meter->has_read && p->min_interval_ms
        && (uint32_t)(now - meter->last_read_ms) < p->min_interval_ms)
        return fail(out, MBUS_METER_ERR_TOO_SOON, MBUS_METER_END_NONE);

    c = (rx_ctx_t *)calloc(1, sizeof(*c));
    if (c == NULL)
        return fail(out, MBUS_METER_ERR_NOMEM, MBUS_METER_END_ERROR);

    to_apply(meter, &ts);

    if (aborted(meter)) {
        rc = fail(out, MBUS_METER_ERR_ABORTED, MBUS_METER_END_ABORTED);
    } else {
        rc = prepare_slave(meter, &q_addr);
        if (rc == MBUS_METER_ERR_ABORTED)
            rc = fail(out, rc, MBUS_METER_END_ABORTED);
        else if (rc != MBUS_METER_OK)
            rc = fail(out, rc, MBUS_METER_END_ERROR);
        else
            rc = request_loop(meter, c, q_addr, out);
    }

    to_restore(meter, &ts);
    free(c);

    meter->has_read = true;
    meter->last_read_ms = mbus_meter_port_now_ms();
    return rc;
}

int
mbus_meter_probe(mbus_meter_t *meter, uint32_t *baud_out)
{
    const mbus_meter_profile_t *p;
    mbus_serial_data *sd;
    to_save_t ts;
    long orig;
    int i, found = 0;

    if (meter == NULL || !meter->h->is_serial || meter->h->auxdata == NULL)
        return MBUS_METER_ERR_ARG;
    p = &meter->prof;
    sd = (mbus_serial_data *)meter->h->auxdata;
    orig = sd->cfg.baudrate;

    to_apply(meter, &ts);
    for (i = 0; i < MBUS_METER_BAUD_LIST_MAX && p->baud_list[i] != 0 && !found; i++) {
        if (aborted(meter))
            break;
        if (mbus_serial_set_baudrate(meter->h, (long)p->baud_list[i]) != 0)
            continue;
        if (meter->primary) {
            mbus_link_mode_t mode = eff_mode(meter);

            if (mode == MBUS_LINK_MODE_UNKNOWN) {
                mode = mbus_link_detect(meter->h, meter->paddr, &p->link);
                if (mode != MBUS_LINK_MODE_UNKNOWN) {
                    meter->mode = mode;
                    found = 1;
                }
            } else if (mbus_link_ping(meter->h, meter->paddr, &p->link, mode) == 0) {
                found = 1;
            }
        } else {
            mbus_meter_port_delay_ms(p->post_nke_delay_ms);
            if (mbus_send_ping_frame(meter->h, ADDR_NETWORK, 0) == 0
                && select_secondary(meter, 0) == 1)
                found = 1;
        }
        if (found && baud_out)
            *baud_out = p->baud_list[i];
    }
    to_restore(meter, &ts);

    if (!found) {
        mbus_serial_set_baudrate(meter->h, orig);
        return aborted(meter) ? MBUS_METER_ERR_ABORTED : MBUS_METER_ERR_TIMEOUT;
    }
    return MBUS_METER_OK;
}
