# esp_libmbus usage guide

Step by step use of the component in your own project. Function details are in
[api.md](api.md). Every `c` block here is compiled in CI; blocks marked with an
output block are also run and compared.

All snippets assume:

```c
#include "mbus.h"
```

## 1. Add the component

From the registry (replace the namespace once published):

```bash
idf.py add-dependency "NAMESPACE/esp_libmbus"
```

Or as a local copy, in the project's top level `CMakeLists.txt` before
`project()`:

```cmake
set(EXTRA_COMPONENT_DIRS "path/to/esp_libmbus")
```

The directory must be called `esp_libmbus`. Other components that include
`mbus.h` need `REQUIRES esp_libmbus` in their `idf_component_register()`.
The `main` component gets it from `idf_component.yml`.

Optional: `idf.py menuconfig` -> `esp_libmbus` -> *M-Bus over TCP gateway*
(off by default, needs lwIP).

## 2. Hardware

The ESP UART is 3.3 V logic. The M-Bus is a 24-36 V current loop, so a level
converter (master) is needed between them.

| Signal | Wiring |
|--------|--------|
| ESP TX | converter TXD (master to slaves) |
| ESP RX | converter RXD (slaves to master) |
| GND    | common ground with the converter |

- Default line settings are 2400 baud, 8E1. Set `baudrate` for 300 or 9600
  meters.
- Many converters echo the transmitted bytes back on RX. The library removes the
  echo of its own request frames by default, nothing to configure.
- Any free UART and any GPIO pair works. Do not use the console UART.
- Wake-up burst (optical heads and some battery meters) is a profile setting,
  see section 8.

## 3. Minimal read

Connect, create a meter object, read, print, free.

```c
static void read_once(void)
{
    mbus_serial_config_t sc = MBUS_SERIAL_CONFIG_DEFAULT();
    sc.port = UART_NUM_1;
    sc.tx_pin = 17;
    sc.rx_pin = 16;

    mbus_handle *h = mbus_context_serial(&sc);
    if (h == NULL || mbus_connect(h) != 0) {
        printf("connect: %s\n", mbus_error_str());
        mbus_context_free(h);          /* NULL is fine */
        return;
    }

    mbus_address addr = { .is_primary = 1, .primary = 1 };
    mbus_meter_t *m = mbus_meter_new(h, &addr, NULL);   /* NULL = generic profile */

    if (m != NULL) {
        mbus_meter_readout_t out;
        int rc = mbus_meter_read(m, &out);

        printf("rc=%d (%s), %u records\n", rc, mbus_meter_strerror(rc),
               (unsigned)out.n_records);

        for (size_t i = 0; i < out.n_records; i++) {
            char val[MBUS_METER_VALUE_STR_MAX];
            const mbus_meter_record_t *r = &out.records[i];

            if (mbus_meter_record_to_str(r, val, sizeof(val)) < 0)
                continue;
            printf("%-20s %s %s\n", mbus_meter_qty_str(r->qty), val,
                   r->unit_text ? r->unit_text : mbus_meter_unit_str(r->unit));
        }
        mbus_meter_readout_free(&out);    /* always, also after an error */
        mbus_meter_free(m);
    }

    mbus_disconnect(h);
    mbus_context_free(h);
}
```

Points that matter:

- `mbus_meter_read()` fills `out` even when it fails. `rc` is `0` complete,
  `> 0` partial (records kept), `< 0` failed (see section 9).
- `mbus_meter_readout_free()` is required after every read.
- The address is copied by `mbus_meter_new()`, a string literal is fine.
- One bus handle per UART, used from one task at a time (not thread-safe).
- Run it in a task with at least 4096 bytes of stack.

## 4. Addressing

Primary address (0..250, set on the meter, often 1 or 0):

```c
static mbus_meter_t *new_primary(mbus_handle *h, int primary)
{
    mbus_address a = { .is_primary = 1, .primary = primary };
    return mbus_meter_new(h, &a, NULL);
}
```

Secondary address, 16 hex digits = 8 digit id + 2 byte manufacturer + version +
medium, as printed by the scan or by the meter:

```c
static mbus_meter_t *new_secondary(mbus_handle *h)
{
    char sec[] = "24083345C5140004";   /* id 24083345, mfr C514, ver 00, medium 04 */
    mbus_address a = { .is_primary = 0, .secondary = sec };
    return mbus_meter_new(h, &a, NULL);
}
```

Wildcards are `F` digits (`FFFFFFFFFFFFFFFF` matches any meter, only safe with
one meter on the bus; a second match gives `MBUS_METER_ERR_COLLISION`).

## 5. Find the meters on the bus

`mbus_meter_scan_secondary()` walks the id bytes and reports every secondary
address. It sends many frames, expect seconds to minutes depending on the number
of meters. Run it once, store the addresses.

```c
static int on_found(void *ctx, const char *addr)
{
    (*(unsigned *)ctx)++;
    printf("found %s\n", addr);
    return 0;                      /* nonzero stops the scan */
}

static void scan_bus(mbus_handle *h)
{
    unsigned n = 0;
    mbus_meter_scan_cfg_t cfg = { .on_found = on_found, .ctx = &n };
    mbus_meter_scan_result_t res;
    mbus_meter_profile_t p = MBUS_METER_PROFILE_GENERIC();

    p.rx_first_byte_ms = 300;      /* silence this long = nobody answered */

    int rc = mbus_meter_scan_secondary(h, &p, &cfg, &res);
    if (rc < 0)
        printf("scan: %s\n", mbus_meter_strerror(rc));
    else
        printf("found=%u unresolved=%u probes=%u\n", res.found, res.unresolved, res.probes);
}
```

- `cfg.mask` limits the scan, e.g. `"24FFFFFFFFFFFFFF"` only ids starting `24`.
- `cfg.max_found` stops early.
- A scan can be cancelled from another task with
  `mbus_register_abort_scan_check()` (callback returns true to abort).
- `unresolved > 0`: the same id answered for more than one meter, differing in
  manufacturer, version or medium, and could not be told apart.

## 6. Use the values

A record is identified by its **key** (VIF, VIFE, function, subunit, tariff,
storage), never by its index: the order and count differ between meters and
firmware versions.

Find a value by key and read it as number or as exact text:

```c
static void print_total_volume(const mbus_meter_readout_t *out)
{
    mbus_meter_key_t k = { .vif = 0x13, .function = MBUS_METER_FUNC_INSTANT };
    const mbus_meter_record_t *r = mbus_meter_readout_find(out, &k, NULL);

    if (r == NULL) {
        printf("no volume record\n");
        return;
    }

    char s[MBUS_METER_VALUE_STR_MAX];
    mbus_meter_record_to_str(r, s, sizeof(s));        /* exact decimal */
    printf("volume %s %s (%.3f as double)\n", s, mbus_meter_unit_str(r->unit),
           mbus_meter_record_to_double(r));            /* lossy */
}
```

Value types (`record.vtype`) and where the value lives:

| vtype | field | notes |
|-------|-------|-------|
| `MBUS_METER_VT_INT` | `value.i` | int64, binary or BCD |
| `MBUS_METER_VT_UINT` | `value.u` | only with `profile.unsigned_ints` |
| `MBUS_METER_VT_REAL` | `value.f` | 32 bit float records |
| `MBUS_METER_VT_TEXT` | `value.bytes` | NUL terminated |
| `MBUS_METER_VT_BINARY` | `value.bytes` | raw bytes, wide numbers |
| `MBUS_METER_VT_DATETIME` | `value.dt` | decoded fields |

Integer values are `mantissa * 10^exp10`. Counters stay exact in `value.i` and
`exp10`; `record_to_double()` is only for display and charts. Records the library
cannot fully interpret are kept, with `MBUS_METER_F_UNKNOWN_VIF` /
`MBUS_METER_F_UNKNOWN_VIFE` in `flags` (use `profile.keep_raw` to also get the
original bytes in `raw`).

Several records with the same key (for example historic values) are visited
with the `from` argument:

```c
static void list_all_energy(const mbus_meter_readout_t *out)
{
    size_t from = 0;
    mbus_meter_key_t k = { .vif = 0x06, .function = MBUS_METER_FUNC_INSTANT };   /* energy kWh */
    const mbus_meter_record_t *r;

    while ((r = mbus_meter_readout_find(out, &k, &from)) != NULL) {
        char s[MBUS_METER_VALUE_STR_MAX];
        mbus_meter_record_to_str(r, s, sizeof(s));
        printf("storage %llu: %s\n", (unsigned long long)r->key.storage, s);
    }
}
```

`key.storage` and `key.tariff` are part of the match, set them when you want a
specific historic or tariff value.

Keep only some records (saves heap) with a profile filter. Matching is exact on
the whole key:

```c
static void read_filtered(mbus_handle *h, const mbus_address *a)
{
    static const mbus_meter_key_t keep[] = {
        { .vif = 0x06, .function = MBUS_METER_FUNC_INSTANT },   /* energy */
        { .vif = 0x13, .function = MBUS_METER_FUNC_INSTANT },   /* volume */
    };
    mbus_meter_profile_t p = MBUS_METER_PROFILE_GENERIC();

    p.filter = keep;               /* must stay valid while the meter lives */
    p.n_filter = 2;

    mbus_meter_t *m = mbus_meter_new(h, a, &p);
    if (m == NULL)
        return;

    mbus_meter_readout_t out;
    mbus_meter_read(m, &out);
    printf("%u records kept\n", (unsigned)out.n_records);
    mbus_meter_readout_free(&out);
    mbus_meter_free(m);
}
```

Meter identity of the last read is in `out.info` (`id_str`, `manufacturer`,
`version`, `medium`, `status`) when `info.has_header` is set.

## 7. Poll in a task

Create the handle and meter once, read in a loop, free the readout every time.
Keep the poll interval above what the meter allows (many battery meters limit
requests per day, use `profile.min_interval_ms` to refuse early reads).

```c
static void meter_task(void *arg)
{
    (void)arg;

    mbus_serial_config_t sc = MBUS_SERIAL_CONFIG_DEFAULT();
    sc.tx_pin = 17;
    sc.rx_pin = 16;

    mbus_handle *h = mbus_context_serial(&sc);
    if (h == NULL || mbus_connect(h) != 0) {
        mbus_context_free(h);
        vTaskDelete(NULL);
        return;
    }

    mbus_address a = { .is_primary = 1, .primary = 1 };
    mbus_meter_profile_t p = MBUS_METER_PROFILE_GENERIC();
    p.min_interval_ms = 60 * 1000;
    mbus_meter_t *m = mbus_meter_new(h, &a, &p);

    if (m == NULL) {
        vTaskDelete(NULL);
        return;
    }

    for (;;) {
        mbus_meter_readout_t out;
        int rc = mbus_meter_read(m, &out);

        if (rc >= 0)
            printf("ok, %u records\n", (unsigned)out.n_records);
        else
            printf("read failed: %s\n", mbus_meter_strerror(rc));

        mbus_meter_readout_free(&out);
        vTaskDelay(pdMS_TO_TICKS(60 * 1000));
    }
}

void start_meter_task(void)
{
    xTaskCreate(meter_task, "meter", 4096, NULL, 5, NULL);
}
```

Several meters on one bus: create one `mbus_meter_t` per meter on the same
handle and read them one after the other.

## 8. Profiles: when the generic read does not work

A profile is a plain struct of timing and sequence settings. Start from a macro,
change fields, pass it to `mbus_meter_new()` (or `mbus_meter_set_profile()`).

| Macro | Use when |
|-------|----------|
| `MBUS_METER_PROFILE_GENERIC()` | default, try this first |
| `MBUS_METER_PROFILE_STRICT_FCB()` | meter repeats the same frame or only answers after a clean NKE with ACK |
| `MBUS_METER_PROFILE_SLOW()` | answers late: 300 baud, converters with latency, sleepy modules |
| `MBUS_METER_PROFILE_OPTICAL()` | optical head or meter that needs the wake-up burst and an application reset |

```c
static mbus_meter_t *new_slow_meter(mbus_handle *h, const mbus_address *a)
{
    mbus_meter_profile_t p = MBUS_METER_PROFILE_SLOW();

    p.rx_first_byte_ms = 3000;     /* wait longer for the first byte */
    p.retries = 4;
    return mbus_meter_new(h, a, &p);
}
```

Fields worth knowing (all in [api.md](api.md#profile)):

- `rx_first_byte_ms`: how long to wait for the answer to start. `0` = derived
  from the baud rate.
- `nke_before_read`, `nke_ack_required`: slave reset before each read.
- `fcb_policy`, `first_fcb`: frame count bit handling, for meters that toggle
  (default), never toggle, or ignore it.
- `max_frames`, `max_records`, `stop_on_repeat`: bounds for multi-telegram
  readouts and heap use.
- `link_mode`: `DIRECT`, `WAKEUP`, or `UNKNOWN` to detect once.

Rule of thumb when a read times out: GENERIC, then SLOW, then STRICT_FCB, then
OPTICAL. Meters that need a quirk none of these cover: copy a macro and change
the fields; report it, see section 11.

Unknown baud rate of the meter:

```c
static void find_baud(mbus_meter_t *m)
{
    uint32_t baud = 0;
    int rc = mbus_meter_probe(m, &baud);       /* tries profile.baud_list */

    if (rc == 0)
        printf("meter answers at %u baud\n", (unsigned)baud);
    else
        printf("probe: %s\n", mbus_meter_strerror(rc));
}
```

The first baud in the list that gets an ACK is kept on the UART. Default list is
2400, 300, 9600.

## 9. Errors

`mbus_meter_strerror(rc)` gives the text. Typical causes:

| Result | Meaning | First thing to check |
|--------|---------|----------------------|
| `MBUS_METER_OK` | complete | |
| `MBUS_METER_PARTIAL` | some telegrams decoded, a later one failed | `out.last_err`, usable data is in `out.records` |
| `MBUS_METER_ERR_TIMEOUT` | no answer | wiring (TX/RX swapped?), baud, address, converter power, SLOW profile |
| `MBUS_METER_ERR_LINK` | uart or bus noise | grounding, bus voltage, cable length |
| `MBUS_METER_ERR_INVALID_FRAME` | checksum, format or reply from another address | duplicate primary addresses on the bus |
| `MBUS_METER_ERR_NO_ACK` | NKE or select not acknowledged | `nke_ack_required`, secondary address digits |
| `MBUS_METER_ERR_COLLISION` | secondary select matched several meters | use full address, not wildcards |
| `MBUS_METER_ERR_JAM` | bus does not go quiet | a slave transmitting continuously, short on the bus |
| `MBUS_METER_ERR_UNSUPPORTED_CI` | frame type not decoded | `out.info.ci`, please report |
| `MBUS_METER_ERR_APP_ERROR` | meter answered with an error telegram | `out.app_error` |
| `MBUS_METER_ERR_TOO_SOON` | `min_interval_ms` not elapsed | wait |
| `MBUS_METER_ERR_ABORTED` | abort callback returned true | |

How the read ended is in `out.end_reason`
(`mbus_meter_end_reason_str()`), `out.frames` counts decoded telegrams.

Raw frame trace: register the trace callbacks on the handle to see every byte
on the bus.

```c
static void trace_on(mbus_handle *h)
{
    mbus_register_send_event(h, mbus_dump_send_event);
    mbus_register_recv_event(h, mbus_dump_recv_event);
}
```

## 10. Decode frames without a meter

`mbus_meter_decode_frame()` takes a frame you already have (from a log, a
gateway, a test) and gives the same typed records. Useful to develop the
application side without hardware and to check what a captured telegram
contains.

```c
/* docs:run */
void offline_example(void)
{
    const char *hex = "68 15 15 68 08 00 72 45 33 08 24 C5 14 00 04 01 00 00 00 "
                      "04 13 78 56 34 12 27 16";
    unsigned char bin[64];
    size_t n = mbus_hex2bin(bin, sizeof(bin), (const unsigned char *)hex, strlen(hex));

    mbus_frame frame;
    memset(&frame, 0, sizeof(frame));
    if (mbus_parse(&frame, bin, n) != 0) {
        printf("parse: %s\n", mbus_error_str());
        return;
    }

    mbus_meter_readout_t out;
    mbus_meter_readout_init(&out);          /* required before decode */

    int rc = mbus_meter_decode_frame(&frame, NULL, &out);
    printf("rc=%d id=%s mfr=%s medium=%s\n", rc, out.info.id_str,
           out.info.manufacturer, mbus_meter_medium_str(out.info.medium));

    for (size_t i = 0; i < out.n_records; i++) {
        char s[MBUS_METER_VALUE_STR_MAX];
        mbus_meter_record_to_str(&out.records[i], s, sizeof(s));
        printf("%s = %s %s\n", mbus_meter_qty_str(out.records[i].qty), s,
               mbus_meter_unit_str(out.records[i].unit));
    }
    mbus_meter_readout_free(&out);
}
```

```text
rc=0 id=24083345 mfr=EFE medium=heat (outlet)
volume = 305419.896 m^3
```

Several frames of one readout can be decoded into the same `out`, call
`mbus_meter_decode_frame()` for each.

## 11. Sizes and limits

- Stack: one `mbus_meter_read()` peaked at 1712 bytes on ESP32-C6 (IDF 5.5.5,
  25 record frame). Use at least 3072 bytes for the reading task, 4096 leaves
  margin. Other targets were checked with the test app only for pass/fail.
- Heap: records are heap allocated and freed by `mbus_meter_readout_free()`.
  `profile.max_records` (default 128) and `profile.filter` bound it.
- Time: the call blocks until the read ends. Timeouts follow the baud rate and the
  profile; a missing meter costs the first-byte wait times `retries + 1`.
- Thread safety: none. One task per bus handle.
- Targets: UART transport on any ESP32 series with `esp_driver_uart`. The TCP
  gateway transport is optional (Kconfig).

## 12. Tell us it worked (or did not)

No real meter was available while writing this component, so reports from real
installations are the most useful thing you can give back. Open a *Meter
report* issue on the repository (the form asks for meter model, profile used
and the log), the result is added to [tested-meters.md](tested-meters.md).
