# esp_libmbus API reference

Headers: `#include "mbus.h"` pulls in everything below. The typed meter API is in
`mbus-meter.h`, the bus handle in `mbus-protocol-aux.h`, the serial setup in
`mbus-serial-config.h`, link helpers in `mbus-link.h`. How to use it: see
[usage.md](usage.md).

Conventions: blocking calls, one bus handle per task, not thread-safe. Functions
returning `int` return `0` on success unless noted; the typed meter calls return an
`mbus_meter_err_t` (`>0` partial, `<0` failed).

## Bus handle

```text
mbus_serial_config_t cfg = MBUS_SERIAL_CONFIG_DEFAULT();
```

| Field | Default | Meaning |
|-------|---------|---------|
| `port` | `UART_NUM_1` | UART controller |
| `tx_pin`, `rx_pin` | `UART_PIN_NO_CHANGE` | set both |
| `baudrate` | 2400 | 300, 2400, 9600 are common |
| `rx_buf_size` | 1024 | driver RX ring buffer |
| `data_bits`, `parity`, `stop_bits` | 8, even, 1 | M-Bus is 8E1 |
| `rx_timeout_ms` | 0 | 0 = derived from the baud rate |

| Function | Description |
|----------|-------------|
| `mbus_handle *mbus_context_serial(const mbus_serial_config_t *cfg)` | Create a handle. `NULL` on bad config or no memory. |
| `int mbus_connect(mbus_handle *h)` | Install the UART driver and pins. `0` ok. Text in `mbus_error_str()`. |
| `int mbus_disconnect(mbus_handle *h)` | Release the UART. |
| `void mbus_context_free(mbus_handle *h)` | Free the handle. `NULL` is safe. |
| `mbus_handle *mbus_context_tcp(const char *host, uint16_t port)` | TCP gateway, only with `CONFIG_MBUS_ENABLE_TCP`. |
| `void mbus_register_send_event(h, cb)` / `mbus_register_recv_event(h, cb)` | Byte trace callbacks. `mbus_dump_send_event` / `mbus_dump_recv_event` print hex. |
| `void mbus_register_abort_scan_check(h, cb)` | `cb(h)` returning true aborts a scan or read. |
| `char *mbus_error_str(void)` | Last error text of the low-level layer. |

Order: `mbus_context_serial` -> `mbus_connect` -> use -> `mbus_disconnect` ->
`mbus_context_free`.

## Addressing

```c
typedef struct {
    char is_primary;             /* non zero = primary */
    union { int primary; char *secondary; };
} mbus_address;
```

Primary 0..250. Secondary is a 16 digit hex string: id (8 digits), manufacturer
(4), version (2), medium (2). `F` digits are wildcards.

## Meter object

| Function | Description |
|----------|-------------|
| `mbus_meter_t *mbus_meter_new(mbus_handle *h, const mbus_address *a, const mbus_meter_profile_t *p)` | Copies address and profile. `NULL` profile = generic. `NULL` on bad arguments or no memory. |
| `void mbus_meter_free(mbus_meter_t *m)` | Free. |
| `int mbus_meter_set_profile(mbus_meter_t *m, const mbus_meter_profile_t *p)` | Replace the profile, keeps last-read time and detected link mode. |
| `mbus_link_mode_t mbus_meter_link_mode(const mbus_meter_t *m)` | Mode found by auto detect, `UNKNOWN` before. |
| `int mbus_meter_probe(mbus_meter_t *m, uint32_t *baud_out)` | Try `profile.baud_list`, keep the first baud that gets an ACK. Serial only. `baud_out` may be `NULL`. |
| `int mbus_meter_read(mbus_meter_t *m, mbus_meter_readout_t *out)` | Full read cycle. Initializes `*out`, fills what was decoded even on error. Call `mbus_meter_readout_free(out)` after. |

Read cycle: optional wake-up, NKE, optional application reset, REQ_UD2 loop with
FCB handling until the meter reports no more data or a profile bound is hit.
Several telegrams are merged into one readout.

## Secondary scan

```c
typedef struct {
    const char *mask;      /* 16 hex digits, FF bytes wildcard, NULL = all */
    unsigned    max_found; /* 0 = no limit */
    int       (*on_found)(void *ctx, const char *addr); /* nonzero stops */
    void      (*on_probe)(void *ctx, const char *mask);
    void       *ctx;
} mbus_meter_scan_cfg_t;

typedef struct { unsigned found, unresolved, probes; } mbus_meter_scan_result_t;

int mbus_meter_scan_secondary(mbus_handle *h, const mbus_meter_profile_t *p,
                              const mbus_meter_scan_cfg_t *cfg,
                              mbus_meter_scan_result_t *result);
```

Walks the four id bytes (BCD 00..99) with a wildcard per byte, handles
collisions by descending. Returns the number found or a negative error. Uses the
profile for timeouts, retries, select settle, post-NKE delay and first FCB.
`NULL` profile = generic. `result` may be `NULL`.

## Readout

```c
typedef struct {
    mbus_meter_info_t        info;
    mbus_meter_record_t     *records;
    size_t                   n_records;
    uint8_t                  frames;
    mbus_meter_end_reason_t  end_reason;
    int                      last_err;
    uint8_t                  app_error;
    bool                     more_follows;
} mbus_meter_readout_t;
```

| Field | Meaning |
|-------|---------|
| `records[n_records]` | decoded records, owned by the readout |
| `frames` | telegrams decoded |
| `end_reason` | `LAST_FRAME` (normal), `MAX_FRAMES`, `MAX_RECORDS`, `REPEAT`, `ERROR`, `ABORTED` |
| `last_err` | error that ended the read, 0 if none |
| `app_error` | error byte of a CI 0x70 telegram |
| `more_follows` | last telegram ended with DIF 0x1F |

`mbus_meter_info_t` (valid when `has_header`): `ci`, `id` (number), `id_str[9]`,
`manufacturer[4]` (3 letters), `version`, `medium` (raw byte), `access_no`,
`status`, `signature`.

| Function | Description |
|----------|-------------|
| `void mbus_meter_readout_init(mbus_meter_readout_t *out)` | Needed before `mbus_meter_decode_frame()`. `mbus_meter_read()` does it itself. |
| `void mbus_meter_readout_free(mbus_meter_readout_t *out)` | Free records. Safe after any read. |
| `int mbus_meter_decode_frame(const mbus_frame *f, const mbus_meter_profile_t *p, mbus_meter_readout_t *out)` | Decode one received frame and append its records. `NULL` profile = generic. |

## Record

```c
typedef struct {
    mbus_meter_key_t   key;
    uint8_t            dif;
    mbus_meter_qty_t   qty;
    mbus_meter_unit_t  unit;
    mbus_meter_vtype_t vtype;
    int8_t             exp10;
    int8_t             add_exp;
    uint16_t           flags;
    uint8_t           *raw;       /* only with profile.keep_raw */
    uint16_t           raw_len;
    char              *unit_text; /* VIF 0x7C/0xFC plain-text unit, else NULL */
    mbus_meter_value_t value;
} mbus_meter_record_t;
```

Numeric value = mantissa (`value.i` / `value.u`) * 10^`exp10`. `add_exp` is an
additional term from VIFE 0x78..0x7B, valid with `MBUS_METER_F_HAS_ADDITIVE`; it
is not applied by `record_to_double()`.

### Key

```c
typedef struct {
    uint8_t  vif;
    uint8_t  vife[MBUS_METER_VIFE_MAX];
    uint8_t  n_vife;
    uint8_t  function;   /* mbus_meter_function_t: INSTANT, MAX, MIN, ERROR */
    uint16_t subunit;
    uint32_t tariff;
    uint64_t storage;
} mbus_meter_key_t;
```

Zero-initialized fields match zero (instantaneous, storage 0, tariff 0, no
VIFE). The key is the stable identity of a value, not the index.

### Value type

| `vtype` | Field | Note |
|---------|-------|------|
| `NONE` | - | DIF 0 / special DIF |
| `INT` | `value.i` | signed, binary or BCD |
| `UINT` | `value.u` | only with `profile.unsigned_ints` |
| `REAL` | `value.f` | float32 records |
| `TEXT` | `value.bytes` | NUL terminated, reading order |
| `BINARY` | `value.bytes` | raw bytes, also numbers wider than 8 bytes |
| `DATETIME` | `value.dt` | `year month day hour minute second kind summer_time invalid` |

### Flags (`record.flags`)

| Flag | Meaning |
|------|---------|
| `MBUS_METER_F_BCD_ERROR` | BCD nibble A..E seen, counted as 0 |
| `MBUS_METER_F_TIME_INVALID` | date/time invalid flag set |
| `MBUS_METER_F_UNKNOWN_VIF` | VIF not in the tables, record kept |
| `MBUS_METER_F_UNKNOWN_VIFE` | at least one VIFE not interpreted, kept raw |
| `MBUS_METER_F_MFR_SPECIFIC` | VIF 0x7F/0xFF or manufacturer VIFE |
| `MBUS_METER_F_HAS_ADDITIVE` | additive VIFE present, see `add_exp` |
| `MBUS_METER_F_FIXED_STRUCT` | from a fixed-structure telegram (CI 0x73) |
| `MBUS_METER_F_UNSIGNED_FORCED` | `profile.unsigned_ints` applied |
| `MBUS_METER_F_BCD_AS_HEX` | BCD under the error function read as hex digits |

### Helpers

| Function | Description |
|----------|-------------|
| `bool mbus_meter_key_equal(const key *a, const key *b)` | Exact key compare. |
| `const record *mbus_meter_readout_find(const readout *r, const key *k, size_t *from)` | Next record whose key equals `k`. `from` is the start index in and index+1 of the hit out, `NULL` = start at 0. `NULL` when no more. |
| `double mbus_meter_record_to_double(const record *r)` | Applies `exp10`. Lossy, `0.0` for non-numeric. |
| `int mbus_meter_record_to_str(const record *r, char *buf, size_t len)` | Exact decimal from mantissa and exponent, text as is, date/time ISO-like. Returns length or a negative error. Use a `MBUS_METER_VALUE_STR_MAX` buffer. |
| `const char *mbus_meter_strerror(int err)` | Text of a result code. |
| `const char *mbus_meter_end_reason_str(mbus_meter_end_reason_t)` | |
| `const char *mbus_meter_unit_str(mbus_meter_unit_t)` | `"Wh"`, `"m^3"`, ... |
| `const char *mbus_meter_qty_str(mbus_meter_qty_t)` | `"energy"`, ... |
| `const char *mbus_meter_medium_str(uint8_t medium)` | Medium byte as text. |

`mbus_meter_qty_t` lists what is measured (energy, volume, power, flow and return
temperature, date/time, fabrication number, error flags, ...). Do not rely on its
numeric values, they grow with the VIF tables.

## Result codes

| Code | Value | Meaning |
|------|-------|---------|
| `MBUS_METER_OK` | 0 | complete |
| `MBUS_METER_PARTIAL` | 1 | at least one telegram decoded, a later one failed or a cap was hit |
| `MBUS_METER_ERR_ARG` | -1 | bad argument |
| `MBUS_METER_ERR_NOMEM` | -2 | allocation failed |
| `MBUS_METER_ERR_TOO_SOON` | -3 | `min_interval_ms` not elapsed |
| `MBUS_METER_ERR_ABORTED` | -4 | abort callback returned true |
| `MBUS_METER_ERR_TIMEOUT` | -5 | no answer to the first request |
| `MBUS_METER_ERR_LINK` | -6 | UART or bus error |
| `MBUS_METER_ERR_INVALID_FRAME` | -7 | checksum, format or wrong reply address |
| `MBUS_METER_ERR_NO_ACK` | -8 | NKE, select or reset needed an ACK |
| `MBUS_METER_ERR_COLLISION` | -9 | secondary select matched more than one slave |
| `MBUS_METER_ERR_JAM` | -10 | purge cap hit, bus jammed |
| `MBUS_METER_ERR_UNSUPPORTED_CI` | -11 | telegram type not decoded, CI in `info.ci` |
| `MBUS_METER_ERR_APP_ERROR` | -12 | CI 0x70, error byte in `app_error` |

## Profile

`mbus_meter_profile_t` is a plain struct. Start from a macro (all fields are set
explicitly, the macros are const-initializable) and override.

```c
mbus_meter_profile_t p = MBUS_METER_PROFILE_GENERIC();
p.retries = 4;
```

| Field | Generic | Meaning |
|-------|---------|---------|
| `link_mode` | `DIRECT` | `DIRECT`, `WAKEUP`, `UNKNOWN` (detect once) |
| `link` | `MBUS_LINK_CONFIG_DEFAULT()` | wake-up byte 0x55, 530 bytes, pause 1500 ms, settle 50 ms, detect gap 500 ms |
| `nke_before_read` | true | SND_NKE before each read |
| `nke_ack_required` | false | missing ACK gives `ERR_NO_ACK` |
| `nke_broadcast` | false | SND_NKE to 255 (no ACK) instead of addressed |
| `post_nke_delay_ms` | 0 | pause after NKE |
| `app_reset`, `app_reset_subcode` | false, -1 | SND_UD CI 0x50 before REQ_UD2 |
| `select_settle_ms` | 100 | after secondary select ACK |
| `first_fcb` | 1 | first frame count bit |
| `fcb_policy` | `TOGGLE` | `TOGGLE`, `ALWAYS_SET`, `ALWAYS_CLEAR`, `NONE` |
| `rx_first_byte_ms` | 0 | wait for the first byte, 0 = handle default; silence ends a receive at once |
| `rx_gap_ms` | 100 | gap inside a frame |
| `retries`, `retry_delay_ms` | 2, 100 | per request |
| `inter_frame_gap_ms` | 20 | between telegrams |
| `min_interval_ms` | 0 | refuse reads closer than this (`ERR_TOO_SOON`) |
| `max_frames` | 16 | telegrams per readout, 0 = library cap 32 |
| `max_records` | 128 | records per readout |
| `stop_on_repeat` | true | stop when the same frame comes again |
| `accept_any_reply` | false | accept a reply from any address (poll 254) |
| `drain_mode` | `FLUSH` | `FLUSH` = fast input flush, `PURGE` = library purge (3 x rx timeout) |
| `baud_list` | 2400, 300, 9600 | for `mbus_meter_probe()`, 0 terminated |
| `unsigned_ints` | false | read integers as unsigned |
| `keep_raw` | false | keep original data bytes in `raw` |
| `filter`, `n_filter` | NULL, 0 | keep only records with an exactly matching key; the array must outlive the meter |

| Macro | Differences from generic |
|-------|--------------------------|
| `MBUS_METER_PROFILE_GENERIC()` | |
| `MBUS_METER_PROFILE_STRICT_FCB()` | `nke_ack_required`, `post_nke_delay_ms` 50, `inter_frame_gap_ms` 50 |
| `MBUS_METER_PROFILE_SLOW()` | `rx_first_byte_ms` 2000, `retries` 3, `select_settle_ms` 200, `post_nke_delay_ms` 50 |
| `MBUS_METER_PROFILE_OPTICAL()` | `link_mode` `WAKEUP`, `app_reset` with subcode 0x50, `rx_first_byte_ms` 1000 |

## Link helpers (`mbus-link.h`)

Low level, normally driven by the profile. `mbus_link_config_t` holds the wake-up
parameters; `mbus_link_wakeup()`, `mbus_link_ping()` and `mbus_link_detect()` send
the wake-up burst, ping a slave and detect whether a meter needs the burst.

## Frame level (inherited)

The original libmbus calls stay available: `mbus_frame_new`, `mbus_parse`,
`mbus_frame_data_parse`, `mbus_send_request_frame`, `mbus_recv_frame`,
`mbus_read_slave`, `mbus_hex2bin`, XML output and so on, see
`mbus-protocol.h` and `mbus-protocol-aux.h`. The typed API above is built on
them and is the recommended entry point.
