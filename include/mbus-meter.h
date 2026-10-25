#ifndef MBUS_METER_H
#define MBUS_METER_H

/*
 * Typed M-Bus meter readout on top of mbus_handle.
 *
 * - blocking, one bus per task, not thread-safe
 * - records are heap allocated and owned by the readout, free with
 *   mbus_meter_readout_free()
 * - counters are kept as integer mantissa + exp10, never through double
 * - unknown VIF/VIFE never drops a record, it sets a flag
 */

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "mbus-protocol.h"
#include "mbus-protocol-aux.h"
#include "mbus-link.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MBUS_METER_VIFE_MAX       10   /* spec limit of VIFEs per record */
#define MBUS_METER_BAUD_LIST_MAX  4    /* entries incl. the 0 terminator */
#define MBUS_METER_VALUE_STR_MAX  48   /* buffer size for record_to_str */

/* ------------------------------------------------------------------ */
/* result codes: 0 = complete, >0 = partial (data kept), <0 = failed   */
/* ------------------------------------------------------------------ */

typedef enum {
    MBUS_METER_OK                 = 0,
    MBUS_METER_PARTIAL            = 1,   /* >=1 frame decoded, later frame failed or cap hit */
    MBUS_METER_ERR_ARG            = -1,
    MBUS_METER_ERR_NOMEM          = -2,
    MBUS_METER_ERR_TOO_SOON       = -3,  /* min_interval_ms not elapsed */
    MBUS_METER_ERR_ABORTED        = -4,  /* abort callback returned true */
    MBUS_METER_ERR_TIMEOUT        = -5,  /* no answer to the first request */
    MBUS_METER_ERR_LINK           = -6,  /* uart/bus error, noise */
    MBUS_METER_ERR_INVALID_FRAME  = -7,  /* checksum/format, wrong reply address */
    MBUS_METER_ERR_NO_ACK         = -8,  /* NKE/select/app reset needed an ACK */
    MBUS_METER_ERR_COLLISION      = -9,  /* secondary select matched >1 slave */
    MBUS_METER_ERR_JAM            = -10, /* purge cap hit, bus jammed */
    MBUS_METER_ERR_UNSUPPORTED_CI = -11, /* e.g. 0x76/0x77/0x10, ci in readout.info.ci */
    MBUS_METER_ERR_APP_ERROR      = -12  /* CI 0x70, error byte in readout.app_error */
} mbus_meter_err_t;

typedef enum {
    MBUS_METER_END_NONE = 0,
    MBUS_METER_END_LAST_FRAME,   /* no "more records" marker, normal */
    MBUS_METER_END_MAX_FRAMES,
    MBUS_METER_END_MAX_RECORDS,
    MBUS_METER_END_REPEAT,       /* identical frame again, stopped */
    MBUS_METER_END_ERROR,        /* see readout.last_err */
    MBUS_METER_END_ABORTED
} mbus_meter_end_reason_t;

/* ------------------------------------------------------------------ */
/* record                                                              */
/* ------------------------------------------------------------------ */

typedef enum {
    MBUS_METER_FUNC_INSTANT = 0,
    MBUS_METER_FUNC_MAX,
    MBUS_METER_FUNC_MIN,
    MBUS_METER_FUNC_ERROR
} mbus_meter_function_t;

typedef enum {
    MBUS_METER_VT_NONE = 0,   /* DIF 0, special DIFs */
    MBUS_METER_VT_INT,        /* int64, binary (signed) or BCD */
    MBUS_METER_VT_UINT,       /* uint64, only with profile.unsigned_ints */
    MBUS_METER_VT_REAL,       /* double, DIF 5 float32 only */
    MBUS_METER_VT_TEXT,       /* NUL terminated, reading order */
    MBUS_METER_VT_BINARY,     /* raw bytes, also numbers wider than 8 bytes */
    MBUS_METER_VT_DATETIME
} mbus_meter_vtype_t;

/* record.flags */
#define MBUS_METER_F_BCD_ERROR     0x0001u  /* BCD nibble A..E seen (counted as 0) */
#define MBUS_METER_F_TIME_INVALID  0x0002u  /* date/time invalid flag set */
#define MBUS_METER_F_UNKNOWN_VIF   0x0004u
#define MBUS_METER_F_UNKNOWN_VIFE  0x0008u  /* >=1 VIFE not interpreted, kept raw */
#define MBUS_METER_F_MFR_SPECIFIC  0x0010u  /* VIF 0x7F/0xFF or manufacturer VIFE */
#define MBUS_METER_F_HAS_ADDITIVE  0x0020u  /* VIFE 0x78..0x7B, see add_exp */
#define MBUS_METER_F_FIXED_STRUCT  0x0040u  /* from a fixed-structure frame (CI 0x73) */
#define MBUS_METER_F_UNSIGNED_FORCED 0x0080u /* profile.unsigned_ints applied */
#define MBUS_METER_F_BCD_AS_HEX    0x0100u  /* BCD under error function read as hex digits */

/* stable identity of a record; never use the record index */
typedef struct {
    uint8_t  vif;
    uint8_t  vife[MBUS_METER_VIFE_MAX];
    uint8_t  n_vife;
    uint8_t  function;      /* mbus_meter_function_t */
    uint16_t subunit;
    uint32_t tariff;
    uint64_t storage;
} mbus_meter_key_t;

/* what is measured; grows with the VIF tables, do not rely on numeric values */
typedef enum {
    MBUS_METER_QTY_UNKNOWN = 0,
    MBUS_METER_QTY_ENERGY,
    MBUS_METER_QTY_VOLUME,
    MBUS_METER_QTY_MASS,
    MBUS_METER_QTY_ON_TIME,
    MBUS_METER_QTY_OPERATING_TIME,
    MBUS_METER_QTY_POWER,
    MBUS_METER_QTY_VOLUME_FLOW,
    MBUS_METER_QTY_MASS_FLOW,
    MBUS_METER_QTY_FLOW_TEMP,
    MBUS_METER_QTY_RETURN_TEMP,
    MBUS_METER_QTY_TEMP_DIFF,
    MBUS_METER_QTY_EXTERNAL_TEMP,
    MBUS_METER_QTY_PRESSURE,
    MBUS_METER_QTY_DATE,
    MBUS_METER_QTY_TIME,
    MBUS_METER_QTY_DATETIME,
    MBUS_METER_QTY_HCA_UNITS,
    MBUS_METER_QTY_AVG_DURATION,
    MBUS_METER_QTY_ACTUALITY_DURATION,
    MBUS_METER_QTY_FABRICATION_NO,
    MBUS_METER_QTY_ENHANCED_ID,
    MBUS_METER_QTY_BUS_ADDRESS,
    MBUS_METER_QTY_VOLTAGE,
    MBUS_METER_QTY_CURRENT,
    MBUS_METER_QTY_FREQUENCY,
    MBUS_METER_QTY_REL_HUMIDITY,
    MBUS_METER_QTY_CREDIT,
    MBUS_METER_QTY_DEBIT,
    MBUS_METER_QTY_ACCESS_NO,
    MBUS_METER_QTY_MEDIUM,
    MBUS_METER_QTY_MANUFACTURER,
    MBUS_METER_QTY_PARAMETER_SET_ID,
    MBUS_METER_QTY_MODEL_VERSION,
    MBUS_METER_QTY_HARDWARE_VERSION,
    MBUS_METER_QTY_FIRMWARE_VERSION,
    MBUS_METER_QTY_SOFTWARE_VERSION,
    MBUS_METER_QTY_CUSTOMER_LOCATION,
    MBUS_METER_QTY_CUSTOMER,
    MBUS_METER_QTY_ERROR_FLAGS,
    MBUS_METER_QTY_ERROR_MASK,
    MBUS_METER_QTY_DIGITAL_OUTPUT,
    MBUS_METER_QTY_DIGITAL_INPUT,
    MBUS_METER_QTY_BAUDRATE,
    MBUS_METER_QTY_RESPONSE_DELAY,
    MBUS_METER_QTY_RETRY,
    MBUS_METER_QTY_TARIFF_DURATION,
    MBUS_METER_QTY_BATTERY_LIFE,
    MBUS_METER_QTY_PLAIN_TEXT,           /* VIF 0x7C/0xFC */
    MBUS_METER_QTY_MANUFACTURER_SPECIFIC, /* VIF 0x7F/0xFF */
    MBUS_METER_QTY_ANY_VIF,
    MBUS_METER_QTY_DIMENSIONLESS,
    MBUS_METER_QTY_ACCESS_CODE,
    MBUS_METER_QTY_PASSWORD,
    MBUS_METER_QTY_STORAGE_BLOCK,
    MBUS_METER_QTY_STORAGE_INTERVAL,
    MBUS_METER_QTY_READOUT_DURATION,
    MBUS_METER_QTY_TARIFF_START,
    MBUS_METER_QTY_TARIFF_PERIOD,
    MBUS_METER_QTY_RESET_COUNTER,
    MBUS_METER_QTY_CUMULATION_COUNTER,
    MBUS_METER_QTY_CONTROL_SIGNAL,
    MBUS_METER_QTY_DAY_OF_WEEK,
    MBUS_METER_QTY_WEEK_NUMBER,
    MBUS_METER_QTY_DAY_CHANGE_TIME,
    MBUS_METER_QTY_PARAM_ACTIVATION,
    MBUS_METER_QTY_SUPPLIER_INFO,
    MBUS_METER_QTY_CUMULATION_DURATION,
    MBUS_METER_QTY_BATTERY_CHANGE_DATE,
    MBUS_METER_QTY_TEMP_LIMIT
} mbus_meter_qty_t;

/* base unit, SI prefix lives in exp10 */
typedef enum {
    MBUS_METER_UNIT_NONE = 0,    /* dimensionless / counts / text */
    MBUS_METER_UNIT_WH,
    MBUS_METER_UNIT_J,
    MBUS_METER_UNIT_M3,
    MBUS_METER_UNIT_KG,
    MBUS_METER_UNIT_W,
    MBUS_METER_UNIT_J_PER_H,
    MBUS_METER_UNIT_M3_PER_H,
    MBUS_METER_UNIT_M3_PER_MIN,
    MBUS_METER_UNIT_M3_PER_S,
    MBUS_METER_UNIT_KG_PER_H,
    MBUS_METER_UNIT_CELSIUS,
    MBUS_METER_UNIT_KELVIN,
    MBUS_METER_UNIT_FAHRENHEIT,
    MBUS_METER_UNIT_BAR,
    MBUS_METER_UNIT_SECOND,
    MBUS_METER_UNIT_MINUTE,
    MBUS_METER_UNIT_HOUR,
    MBUS_METER_UNIT_DAY,
    MBUS_METER_UNIT_MONTH,
    MBUS_METER_UNIT_YEAR,
    MBUS_METER_UNIT_VOLT,
    MBUS_METER_UNIT_AMPERE,
    MBUS_METER_UNIT_HERTZ,
    MBUS_METER_UNIT_PERCENT,
    MBUS_METER_UNIT_BAUD,
    MBUS_METER_UNIT_BIT_TIME,
    MBUS_METER_UNIT_FT3,
    MBUS_METER_UNIT_US_GALLON,
    MBUS_METER_UNIT_US_GALLON_PER_MIN,
    MBUS_METER_UNIT_US_GALLON_PER_H,
    MBUS_METER_UNIT_CURRENCY,
    MBUS_METER_UNIT_OTHER        /* known but not in this list, see vif/vife */
} mbus_meter_unit_t;

typedef enum {
    MBUS_METER_DT_DATE = 0,      /* type G */
    MBUS_METER_DT_TIME,          /* type J / 24 bit */
    MBUS_METER_DT_DATETIME       /* type F / type I */
} mbus_meter_dt_kind_t;

typedef struct {
    uint16_t year;               /* full year, 0 if not present */
    uint8_t  month;              /* 1..12, 0 if not present */
    uint8_t  day;
    uint8_t  hour;
    uint8_t  minute;
    uint8_t  second;
    uint8_t  kind;               /* mbus_meter_dt_kind_t */
    bool     summer_time;
    bool     invalid;            /* meter flagged the value invalid */
} mbus_meter_datetime_t;

typedef union {
    int64_t  i;
    uint64_t u;
    double   f;
    struct {
        uint8_t *data;           /* heap, owned by the record */
        uint16_t len;
    } bytes;
    mbus_meter_datetime_t dt;
} mbus_meter_value_t;

typedef struct {
    mbus_meter_key_t   key;
    uint8_t            dif;
    mbus_meter_qty_t   qty;
    mbus_meter_unit_t  unit;
    mbus_meter_vtype_t vtype;
    int8_t             exp10;    /* value = mantissa * 10^exp10 (VIF prefix + VIFE multiplier) */
    int8_t             add_exp;  /* additive VIFE 0x78..0x7B, valid with F_HAS_ADDITIVE */
    uint16_t           flags;    /* MBUS_METER_F_* */
    uint8_t           *raw;      /* original data bytes, only with profile.keep_raw */
    uint16_t           raw_len;
    char              *unit_text; /* plain-text VIF unit (VIF 0x7C/0xFC), heap, else NULL */
    mbus_meter_value_t value;
} mbus_meter_record_t;

/* ------------------------------------------------------------------ */
/* readout                                                             */
/* ------------------------------------------------------------------ */

typedef struct {
    bool     has_header;         /* false for CI 0x78 */
    uint8_t  ci;
    uint32_t id;                 /* decoded 8 digits */
    char     id_str[9];
    char     manufacturer[4];    /* 3 letters */
    uint8_t  version;
    uint8_t  medium;             /* raw medium byte, see mbus_meter_medium_str() */
    uint8_t  access_no;
    uint8_t  status;
    uint16_t signature;
} mbus_meter_info_t;

typedef struct {
    mbus_meter_info_t        info;
    mbus_meter_record_t     *records;
    size_t                   n_records;
    size_t                   n_alloc;       /* private */
    uint8_t                  frames;        /* telegrams decoded */
    mbus_meter_end_reason_t  end_reason;
    int                      last_err;      /* mbus_meter_err_t that ended the read, 0 if none */
    uint8_t                  app_error;     /* CI 0x70 error byte */
    bool                     more_follows;  /* last decoded frame ended with DIF 0x1F */
} mbus_meter_readout_t;

/* ------------------------------------------------------------------ */
/* profile                                                             */
/* ------------------------------------------------------------------ */

typedef enum {
    MBUS_METER_FCB_TOGGLE = 0,   /* first_fcb, then toggle per frame */
    MBUS_METER_FCB_ALWAYS_SET,
    MBUS_METER_FCB_ALWAYS_CLEAR,
    MBUS_METER_FCB_NONE          /* FCV=0, no frame count check */
} mbus_meter_fcb_policy_t;

typedef enum {
    MBUS_METER_DRAIN_FLUSH = 0,  /* uart input flush + short settle */
    MBUS_METER_DRAIN_PURGE       /* library purge, costs 3 x rx timeout */
} mbus_meter_drain_t;

typedef struct {
    /* link layer; mbus_link_config_t is the existing one */
    mbus_link_mode_t        link_mode;          /* DIRECT, WAKEUP, UNKNOWN = detect once */
    mbus_link_config_t      link;

    /* slave preparation */
    bool                    nke_before_read;
    bool                    nke_ack_required;   /* missing ACK -> ERR_NO_ACK */
    bool                    nke_broadcast;      /* SND_NKE 255, no ACK, instead of addressed */
    uint16_t                post_nke_delay_ms;
    bool                    app_reset;          /* SND_UD CI 0x50 before REQ_UD2 */
    int16_t                 app_reset_subcode;  /* -1 = none */
    uint16_t                select_settle_ms;   /* after secondary select ACK */

    /* request cycle */
    uint8_t                 first_fcb;          /* 0 or 1 */
    mbus_meter_fcb_policy_t fcb_policy;
    uint16_t                rx_first_byte_ms;   /* 0 = keep handle config */
    uint16_t                rx_gap_ms;
    uint8_t                 retries;
    uint16_t                retry_delay_ms;
    uint16_t                inter_frame_gap_ms;
    uint32_t                min_interval_ms;
    uint8_t                 max_frames;         /* 0 = library cap 32 */
    uint16_t                max_records;        /* heap bound per readout */
    bool                    stop_on_repeat;
    bool                    accept_any_reply;   /* reply from any address (poll 254) */
    mbus_meter_drain_t      drain_mode;

    /* probe */
    uint32_t                baud_list[MBUS_METER_BAUD_LIST_MAX]; /* 0 terminated */

    /* decode */
    bool                    unsigned_ints;
    bool                    keep_raw;
    const mbus_meter_key_t *filter;             /* exact key match, NULL = keep all */
    uint8_t                 n_filter;
} mbus_meter_profile_t;

/* all fields explicit on purpose, each macro is const-initializable */

#define MBUS_METER_PROFILE_GENERIC() {                  \
    .link_mode = MBUS_LINK_MODE_DIRECT,                 \
    .link = MBUS_LINK_CONFIG_DEFAULT(),                 \
    .nke_before_read = true,                            \
    .nke_ack_required = false,                          \
    .nke_broadcast = false,                             \
    .post_nke_delay_ms = 0,                             \
    .app_reset = false,                                 \
    .app_reset_subcode = -1,                            \
    .select_settle_ms = 100,                            \
    .first_fcb = 1,                                     \
    .fcb_policy = MBUS_METER_FCB_TOGGLE,                \
    .rx_first_byte_ms = 0,                              \
    .rx_gap_ms = 100,                                   \
    .retries = 2,                                       \
    .retry_delay_ms = 100,                              \
    .inter_frame_gap_ms = 20,                           \
    .min_interval_ms = 0,                               \
    .max_frames = 16,                                   \
    .max_records = 128,                                 \
    .stop_on_repeat = true,                             \
    .accept_any_reply = false,                          \
    .drain_mode = MBUS_METER_DRAIN_FLUSH,               \
    .baud_list = { 2400, 300, 9600, 0 },                \
    .unsigned_ints = false,                             \
    .keep_raw = false,                                  \
    .filter = NULL,                                     \
    .n_filter = 0,                                      \
}

/* meters that only answer fully with a strict NKE + FCB sequence */
#define MBUS_METER_PROFILE_STRICT_FCB() {               \
    .link_mode = MBUS_LINK_MODE_DIRECT,                 \
    .link = MBUS_LINK_CONFIG_DEFAULT(),                 \
    .nke_before_read = true,                            \
    .nke_ack_required = true,                           \
    .nke_broadcast = false,                             \
    .post_nke_delay_ms = 50,                            \
    .app_reset = false,                                 \
    .app_reset_subcode = -1,                            \
    .select_settle_ms = 100,                            \
    .first_fcb = 1,                                     \
    .fcb_policy = MBUS_METER_FCB_TOGGLE,                \
    .rx_first_byte_ms = 0,                              \
    .rx_gap_ms = 100,                                   \
    .retries = 2,                                       \
    .retry_delay_ms = 100,                              \
    .inter_frame_gap_ms = 50,                           \
    .min_interval_ms = 0,                               \
    .max_frames = 16,                                   \
    .max_records = 128,                                 \
    .stop_on_repeat = true,                             \
    .accept_any_reply = false,                          \
    .drain_mode = MBUS_METER_DRAIN_FLUSH,               \
    .baud_list = { 2400, 300, 9600, 0 },                \
    .unsigned_ints = false,                             \
    .keep_raw = false,                                  \
    .filter = NULL,                                     \
    .n_filter = 0,                                      \
}

/* slow answer windows (300 baud, converters with latency, sleepy modules) */
#define MBUS_METER_PROFILE_SLOW() {                     \
    .link_mode = MBUS_LINK_MODE_DIRECT,                 \
    .link = MBUS_LINK_CONFIG_DEFAULT(),                 \
    .nke_before_read = true,                            \
    .nke_ack_required = false,                          \
    .nke_broadcast = false,                             \
    .post_nke_delay_ms = 50,                            \
    .app_reset = false,                                 \
    .app_reset_subcode = -1,                            \
    .select_settle_ms = 200,                            \
    .first_fcb = 1,                                     \
    .fcb_policy = MBUS_METER_FCB_TOGGLE,                \
    .rx_first_byte_ms = 2000,                           \
    .rx_gap_ms = 100,                                   \
    .retries = 3,                                       \
    .retry_delay_ms = 100,                              \
    .inter_frame_gap_ms = 20,                           \
    .min_interval_ms = 0,                               \
    .max_frames = 16,                                   \
    .max_records = 128,                                 \
    .stop_on_repeat = true,                             \
    .accept_any_reply = false,                          \
    .drain_mode = MBUS_METER_DRAIN_FLUSH,               \
    .baud_list = { 2400, 300, 9600, 0 },                \
    .unsigned_ints = false,                             \
    .keep_raw = false,                                  \
    .filter = NULL,                                     \
    .n_filter = 0,                                      \
}

/* optical head: wake-up burst, app reset with subcode 0x50 */
#define MBUS_METER_PROFILE_OPTICAL() {                  \
    .link_mode = MBUS_LINK_MODE_WAKEUP,                 \
    .link = MBUS_LINK_CONFIG_DEFAULT(),                 \
    .nke_before_read = true,                            \
    .nke_ack_required = false,                          \
    .nke_broadcast = false,                             \
    .post_nke_delay_ms = 0,                             \
    .app_reset = true,                                  \
    .app_reset_subcode = 0x50,                          \
    .select_settle_ms = 100,                            \
    .first_fcb = 1,                                     \
    .fcb_policy = MBUS_METER_FCB_TOGGLE,                \
    .rx_first_byte_ms = 1000,                           \
    .rx_gap_ms = 100,                                   \
    .retries = 2,                                       \
    .retry_delay_ms = 100,                              \
    .inter_frame_gap_ms = 20,                           \
    .min_interval_ms = 0,                               \
    .max_frames = 16,                                   \
    .max_records = 128,                                 \
    .stop_on_repeat = true,                             \
    .accept_any_reply = false,                          \
    .drain_mode = MBUS_METER_DRAIN_FLUSH,               \
    .baud_list = { 2400, 300, 9600, 0 },                \
    .unsigned_ints = false,                             \
    .keep_raw = false,                                  \
    .filter = NULL,                                     \
    .n_filter = 0,                                      \
}

/* ------------------------------------------------------------------ */
/* meter object: one per slave, shares the bus handle                  */
/* ------------------------------------------------------------------ */

typedef struct mbus_meter mbus_meter_t;

/* Copies address (incl. the secondary string) and profile. NULL profile = generic.
 * Returns NULL on bad args or no memory. handle must be connected before read. */
mbus_meter_t *mbus_meter_new(mbus_handle *handle, const mbus_address *addr,
                             const mbus_meter_profile_t *profile);
void mbus_meter_free(mbus_meter_t *meter);

/* Replace the profile (keeps last-read time and detected link mode). */
int mbus_meter_set_profile(mbus_meter_t *meter, const mbus_meter_profile_t *profile);

/* Link mode found by auto detect, UNKNOWN until detected. */
mbus_link_mode_t mbus_meter_link_mode(const mbus_meter_t *meter);

/* Walk profile.baud_list, set the first baud that gets an ACK on the handle.
 * baud_out may be NULL. Serial only. Returns 0 or a mbus_meter_err_t. */
int mbus_meter_probe(mbus_meter_t *meter, uint32_t *baud_out);

/* Full read cycle per profile. Initializes *out (do not pass a filled readout).
 * Returns OK / PARTIAL / error, *out holds whatever was decoded either way.
 * Always call mbus_meter_readout_free(out) afterwards. */
int mbus_meter_read(mbus_meter_t *meter, mbus_meter_readout_t *out);

/* ------------------------------------------------------------------ */
/* offline decode: frames that did not come from mbus_meter_read       */
/* ------------------------------------------------------------------ */

void mbus_meter_readout_init(mbus_meter_readout_t *out);
void mbus_meter_readout_free(mbus_meter_readout_t *out);

/* Parse one received frame and append its records. NULL profile = generic.
 * Returns OK or an error (UNSUPPORTED_CI, APP_ERROR, INVALID_FRAME, NOMEM). */
int mbus_meter_decode_frame(const mbus_frame *frame, const mbus_meter_profile_t *profile,
                            mbus_meter_readout_t *out);

/* ------------------------------------------------------------------ */
/* record helpers                                                      */
/* ------------------------------------------------------------------ */

bool mbus_meter_key_equal(const mbus_meter_key_t *a, const mbus_meter_key_t *b);

/* Iterate matches: *from is the start index in, index+1 of the hit out.
 * from may be NULL (search from 0). Returns NULL when no more matches. */
const mbus_meter_record_t *mbus_meter_readout_find(const mbus_meter_readout_t *readout,
                                                   const mbus_meter_key_t *key,
                                                   size_t *from);

/* Lossy on purpose. Applies exp10; additive term is not applied. 0.0 for non-numeric. */
double mbus_meter_record_to_double(const mbus_meter_record_t *rec);

/* Exact decimal from mantissa + exp10 ("12.345"), text as is, ISO-like date/time.
 * Returns length written (without NUL) or a negative mbus_meter_err_t. */
int mbus_meter_record_to_str(const mbus_meter_record_t *rec, char *buf, size_t len);

const char *mbus_meter_strerror(int err);
const char *mbus_meter_end_reason_str(mbus_meter_end_reason_t reason);
const char *mbus_meter_unit_str(mbus_meter_unit_t unit);   /* "Wh", "m^3", ... */
const char *mbus_meter_qty_str(mbus_meter_qty_t qty);
const char *mbus_meter_medium_str(uint8_t medium);

#ifdef __cplusplus
}
#endif

#endif /* MBUS_METER_H */
