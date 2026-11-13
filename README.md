# esp_libmbus

M-Bus (Meter-Bus, EN 13757-2/-3) **master** for ESP-IDF. Reads heat, water, gas,
electricity and other meters over a UART with a level converter and returns typed,
exact values. Based on [rscada libmbus](https://github.com/rscada/libmbus)
0.10.4, ported to ESP-IDF and extended.

- UART master, 300 / 2400 / 9600 baud, 8E1, primary and secondary addressing
- Typed readout: quantity, unit, exact integer value with exponent, date/time,
  text, flags for anything not understood (no record is dropped)
- Secondary address scan with byte-level wildcards and collision handling
- Read profiles for meters with quirks (strict FCB, slow answers, optical head
  with wake-up burst), baud rate probe
- Frame decode without hardware, for development and log analysis
- Original libmbus frame-level API still available
- ESP-IDF 5.x, any target with `esp_driver_uart`; optional TCP gateway transport

## Status: read this first

**No real meter was available during development.** What has been tested:

- host tests (gcc, also with ASan/UBSan) against the rscada test frame corpus,
  a fake bus with scripted slaves and the byte-level scan logic
- on target with UART loopback and a scripted slave on ESP32, ESP32-C3,
  ESP32-C6 and ESP32-S3: framing, echo removal, timeouts, read cycle, scan,
  stack and heap use (23 tests, `test_apps/`)
- CI builds the example and the test app for those four targets

What this means: the library speaks the same protocol as rscada libmbus, so
meters that work with rscada libmbus should work here too, but that is **not
verified on real hardware** (no meter, no optical head). Treat the profiles as a
starting point. If it works for your meter, please say so, see
[Meter reports](#meter-reports).

## Quick start

```bash
idf.py add-dependency "karthikt23/esp_libmbus"
```

```c
#include "mbus.h"

void read_meter(void)
{
    mbus_serial_config_t sc = MBUS_SERIAL_CONFIG_DEFAULT();
    sc.port = UART_NUM_1;
    sc.tx_pin = 17;
    sc.rx_pin = 16;

    mbus_handle *h = mbus_context_serial(&sc);
    if (h == NULL || mbus_connect(h) != 0) {
        printf("connect: %s\n", mbus_error_str());
        mbus_context_free(h);
        return;
    }

    mbus_address addr = { .is_primary = 1, .primary = 1 };
    mbus_meter_t *m = mbus_meter_new(h, &addr, NULL);    /* NULL = generic profile */

    if (m != NULL) {
        mbus_meter_readout_t out;
        int rc = mbus_meter_read(m, &out);

        printf("%s, %u records\n", mbus_meter_strerror(rc), (unsigned)out.n_records);
        for (size_t i = 0; i < out.n_records; i++) {
            char val[MBUS_METER_VALUE_STR_MAX];

            if (mbus_meter_record_to_str(&out.records[i], val, sizeof(val)) >= 0)
                printf("%s = %s %s\n", mbus_meter_qty_str(out.records[i].qty), val,
                       mbus_meter_unit_str(out.records[i].unit));
        }
        mbus_meter_readout_free(&out);
        mbus_meter_free(m);
    }

    mbus_disconnect(h);
    mbus_context_free(h);
}
```

Run it from a task with at least 4096 bytes of stack. A complete project with
menuconfig pins, scan and baud probe is in
[examples/read_meter](https://github.com/KarthikT23/esp_libmbus/tree/main/examples/read_meter).

## Documentation

| | |
|---|---|
| [Usage guide](https://github.com/KarthikT23/esp_libmbus/blob/main/docs/usage.md) | wiring, addressing, scan, reading values, profiles, errors, offline decode |
| [API reference](https://github.com/KarthikT23/esp_libmbus/blob/main/docs/api.md) | every function, struct, flag and profile field |
| [Tested meters](https://github.com/KarthikT23/esp_libmbus/blob/main/docs/tested-meters.md) | what was reported to work, by whom, with which profile |

## Hardware

ESP UART TX/RX go to an M-Bus master level converter (24-36 V bus), not to the
bus directly. Common ground with the converter. Echo of the transmitted bytes on
RX, which many converters produce, is removed by the library.

## Profiles in one line

`MBUS_METER_PROFILE_GENERIC()` first. If the read times out or repeats frames, try
`_SLOW()`, `_STRICT_FCB()`, then `_OPTICAL()`. Every field can be overridden. See
the usage guide, section 8.

## Meter reports

Tried it with a real meter? Open an issue with the *Meter report* form, also when
it did **not** work, that is just as useful. Working reports are added to the
tested-meters table.

## Differences from rscada libmbus

- ESP-IDF component: UART through the IDF driver (`mbus_context_serial()` takes a
  config struct), no POSIX serial, no command line tools, no autotools
- New typed API (`mbus-meter.h`) on top of the frame API, own tolerant frame
  decoder, exact integer values instead of doubles
- Secondary scan rewritten: byte-level wildcards, per-nibble search only for the id
- Serial layer: baud-derived timeouts, quick silence detection, input flush
  instead of long purges, bounded buffers
- Link layer: wake-up burst, ping and auto detection (`mbus-link.h`)
- Several upstream defects fixed (unbounded loops and caps, uninitialised TCP
  data, signed counters, precision loss above 2^53, unknown VIF handling)
- XML output and the frame-level calls are kept as they were

## Credits and licence

Derived from libmbus by Raditex Control AB (rSCADA), BSD 3-Clause. The original
copyright notices are kept in `LICENSE` and the source files. Changes in this
component are released under the same licence. This component is not endorsed by
or affiliated with the original authors.
