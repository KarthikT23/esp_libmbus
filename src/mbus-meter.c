#include <stdio.h>
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
to_apply_hp(mbus_handle *h, const mbus_meter_profile_t *p, to_save_t *s)
{
    int f, g;

    s->applied = false;
    if (!h->is_serial || (p->rx_first_byte_ms == 0 && p->rx_gap_ms == 0))
        return;
    if (mbus_serial_get_rx_timeouts(h, &s->first, &s->gap) != 0)
        return;
    f = p->rx_first_byte_ms ? (int)p->rx_first_byte_ms : s->first;
    g = p->rx_gap_ms ? (int)p->rx_gap_ms : s->gap;
    if (mbus_serial_set_rx_timeouts(h, f, g) == 0)
        s->applied = true;
}

static void
to_restore_h(mbus_handle *h, const to_save_t *s)
{
    if (s->applied)
        mbus_serial_set_rx_timeouts(h, s->first, s->gap);
}

static void
to_apply(const mbus_meter_t *m, to_save_t *s)
{
    to_apply_hp(m->h, &m->prof, s);
}

static void
to_restore(const mbus_meter_t *m, const to_save_t *s)
{
    to_restore_h(m->h, s);
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

/* FCB / FCV of the first REQ_UD2 after NKE or select */
static void
first_fcb(const mbus_meter_profile_t *p, int *fcb, int *fcv)
{
    *fcv = 1;
    switch (p->fcb_policy) {
    case MBUS_METER_FCB_ALWAYS_SET:   *fcb = 1; break;
    case MBUS_METER_FCB_ALWAYS_CLEAR: *fcb = 0; break;
    case MBUS_METER_FCB_NONE:         *fcb = 0; *fcv = 0; break;
    default:                          *fcb = p->first_fcb; break;
    }
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
    int fcb, fcv, good = 0, rc, dr;
    int max_frames = p->max_frames ? p->max_frames : MAX_FRAMES_DEFAULT;

    first_fcb(p, &fcb, &fcv);

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

/* ------------------------------------------------------------------ */
/* secondary scan, wildcards per byte                                  */
/* ------------------------------------------------------------------ */

/* Most meters compare the id byte by byte, so a single F nibble is no
 * wildcard. Each id byte is walked 00..99 with the rest left at FF. */

typedef struct {
    mbus_handle                 *h;
    const mbus_meter_profile_t  *p;
    const mbus_meter_scan_cfg_t *cfg;
    mbus_meter_scan_result_t     res;
    int                          err;       /* hard error, 0 = none */
    bool                         stop;
} scan_t;

static bool
scan_aborted(const scan_t *s)
{
    return s->h->abort_scan_check != NULL && s->h->abort_scan_check(s->h);
}

/* select by mask, then read the selected slave to get its full address */
static int
scan_probe(scan_t *s, const char *mask, char *addr_out)
{
    const mbus_meter_profile_t *p = s->p;
    unsigned a;

    s->res.probes++;
    if (s->cfg->on_probe)
        s->cfg->on_probe(s->cfg->ctx, mask);

    for (a = 0; a <= p->retries; a++) {
        mbus_frame reply;
        char buf[32], *addr;
        int r, fcb, fcv;

        r = mbus_select_secondary_address(s->h, mask);
        if (r == MBUS_PROBE_ERROR || r == MBUS_PROBE_COLLISION)
            return r;
        if (r == MBUS_PROBE_SINGLE) {
            mbus_meter_port_delay_ms(p->select_settle_ms);
            first_fcb(p, &fcb, &fcv);
            if (send_req_ud2(s->h, ADDR_NETWORK, fcb, fcv) != 0)
                return MBUS_PROBE_ERROR;

            memset(&reply, 0, sizeof(reply));
            r = mbus_recv_frame(s->h, &reply);
            if (r == MBUS_RECV_RESULT_OK) {
                if (mbus_purge_frames(s->h))
                    return MBUS_PROBE_COLLISION;            /* more than one answered */
                addr = mbus_frame_get_secondary_address_to_buffer(&reply, buf, sizeof(buf));
                if (reply.type == MBUS_FRAME_TYPE_LONG && addr != NULL) {
                    snprintf(addr_out, 17, "%s", addr);
                    return MBUS_PROBE_SINGLE;
                }
            } else if (r == MBUS_RECV_RESULT_INVALID) {
                mbus_purge_frames(s->h);
                return MBUS_PROBE_COLLISION;                /* garbled = overlapping answers */
            } else if (r != MBUS_RECV_RESULT_TIMEOUT) {
                return MBUS_PROBE_ERROR;
            }
        }
        if (a < p->retries)
            mbus_meter_port_delay_ms(p->retry_delay_ms);
    }
    return MBUS_PROBE_NOTHING;
}

static void
scan_report(scan_t *s, const char *addr)
{
    s->res.found++;
    if (s->cfg->on_found && s->cfg->on_found(s->cfg->ctx, addr))
        s->stop = true;
    if (s->cfg->max_found && s->res.found >= s->cfg->max_found)
        s->stop = true;
}

static bool
is_ff(const char *b)
{
    return (b[0] == 'F' || b[0] == 'f') && (b[1] == 'F' || b[1] == 'f');
}

/* id byte pos (0 = most significant) of a mask that matched more than one slave */
static void
scan_rec(scan_t *s, char *mask, int pos)
{
    char *b = &mask[2 * pos];
    int v;

    if (!is_ff(b)) {                          /* fixed by the caller */
        if (pos < 3)
            scan_rec(s, mask, pos + 1);
        else
            s->res.unresolved++;
        return;
    }

    for (v = 0; v <= 99 && !s->stop && !s->err; v++) {
        char addr[32];
        int r;

        b[0] = (char)('0' + v / 10);
        b[1] = (char)('0' + v % 10);
        if (scan_aborted(s)) {
            s->err = MBUS_METER_ERR_ABORTED;
            break;
        }
        r = scan_probe(s, mask, addr);
        if (r == MBUS_PROBE_SINGLE) {
            scan_report(s, addr);
        } else if (r == MBUS_PROBE_COLLISION) {
            if (pos < 3)
                scan_rec(s, mask, pos + 1);
            else
                s->res.unresolved++;          /* same id, differs in mfr/version/medium */
        } else if (r == MBUS_PROBE_ERROR) {
            s->err = MBUS_METER_ERR_LINK;
        }
    }
    b[0] = b[1] = 'F';
}

static bool
scan_mask_ok(const char *m)
{
    int i;

    if (strlen(m) != 16)
        return false;
    for (i = 0; i < 16; i++)
        if (!((m[i] >= '0' && m[i] <= '9') || (m[i] >= 'A' && m[i] <= 'F') || (m[i] >= 'a' && m[i] <= 'f')))
            return false;
    return true;
}

int
mbus_meter_scan_secondary(mbus_handle *handle, const mbus_meter_profile_t *profile,
                          const mbus_meter_scan_cfg_t *cfg, mbus_meter_scan_result_t *result)
{
    mbus_meter_profile_t def = MBUS_METER_PROFILE_GENERIC();
    scan_t s;
    to_save_t ts;
    char mask[17], addr[32];
    int r;

    if (handle == NULL || cfg == NULL || (cfg->mask != NULL && !scan_mask_ok(cfg->mask)))
        return MBUS_METER_ERR_ARG;
    if (profile == NULL)
        profile = &def;
    else if (profile_check(profile) != MBUS_METER_OK)
        return MBUS_METER_ERR_ARG;

    memset(&s, 0, sizeof(s));
    s.h = handle;
    s.p = profile;
    s.cfg = cfg;
    snprintf(mask, sizeof(mask), "%s", cfg->mask ? cfg->mask : "FFFFFFFFFFFFFFFF");
    for (r = 0; r < 16; r++)
        if (mask[r] == 'f')
            mask[r] = 'F';

    to_apply_hp(handle, profile, &ts);

    /* deselect everything first, a slave left selected would answer every probe */
    if (mbus_send_ping_frame(handle, ADDR_NETWORK, 0) != 0) {
        s.err = MBUS_METER_ERR_LINK;
    } else {
        mbus_meter_port_delay_ms(profile->post_nke_delay_ms);
        if (scan_aborted(&s)) {
            s.err = MBUS_METER_ERR_ABORTED;
        } else {
            r = scan_probe(&s, mask, addr);
            if (r == MBUS_PROBE_SINGLE)
                scan_report(&s, addr);
            else if (r == MBUS_PROBE_COLLISION)
                scan_rec(&s, mask, 0);
            else if (r == MBUS_PROBE_ERROR)
                s.err = MBUS_METER_ERR_LINK;
        }
        if (mbus_send_ping_frame(handle, ADDR_NETWORK, 0) == 0)
            mbus_meter_port_delay_ms(profile->post_nke_delay_ms);
    }

    to_restore_h(handle, &ts);
    if (result)
        *result = s.res;
    return s.err ? s.err : (int)s.res.found;
}
