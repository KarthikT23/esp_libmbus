# Wiring and level converters

What sits between the ESP UART and the meters, what to check on the first read,
and how the echo is handled. Usage of the library is in [usage.md](usage.md).

## What the bus is

The M-Bus is a two-wire bus, slaves in parallel, the master controls everything
(half duplex). It is not a logic-level signal:

| Direction | Signalling | Levels |
|-----------|------------|--------|
| Master to slaves | voltage | mark about 36 V, space about 24 V (12 V lower) |
| Slave to master | current | mark up to 1.5 mA per unit load, space adds 11 to 20 mA |

Bus voltage is between 21 V and 42 V depending on the master. Baud rates are 300
to 9600. A segment carries up to 250 slaves, up to 1000 m of cable in total (the
standard assumes telephone cable such as 2 x 0.8 mm). Check your meter and
converter data sheets for the real limits, the numbers above are the spec
figures.

So the ESP never connects to the bus. A **master converter** (level converter,
bus master module) does the voltage and current conversion and gives you a
logic-level UART.

## ESP to converter

| ESP | Converter | Note |
|-----|-----------|------|
| TX pin | TXD (or TX_IN) | master to slaves |
| RX pin | RXD (or RX_OUT) | slaves to master |
| GND | GND | common ground, always |
| 3V3 or 5V | VCC | depends on the module, see below |

Notes:

- TX goes to the converter's **input**, RX to its **output**. Labels differ, a
  swapped pair looks like a dead bus (every read times out).
- ESP GPIOs are 3.3 V. If the converter's logic side is 5 V, use a module with a
  3.3 V logic option, or a level shifter on RX. Do not feed 5 V into an ESP RX pin.
- The bus side of the converter needs its own supply, often from a separate
  adapter. Follow the converter's data sheet. The bus voltage must never be
  connected to the ESP side.
- UART settings: 8 data bits, even parity, 1 stop bit (8E1). The library default is
  2400 baud, set `baudrate` in `mbus_serial_config_t` for 300 or 9600.
- Use any free UART and any GPIO pair. UART0 is normally the console, use UART1
  (ESP32-C3 has only UART0 and UART1). The example `read_meter` has the pins in
  menuconfig.

Pins used in this repository's tests, with TX jumpered to RX for loopback:

| Target | TX | RX |
|--------|----|----|
| ESP32-C6 | 6 | 7 |
| ESP32-C3 | 4 | 5 |
| ESP32-S3 | 17 | 18 |
| ESP32 | 17 | 16 |

## Echo

The converter usually shows the master's own transmission on RX again, because the
receiver sees the same bus voltage that the master drives. So a request frame comes
back before the slave answers. Some converters suppress it, some do not.

The library handles both. By default the first received frame is dropped if it is
a master-to-slave frame (the echo) and the receive is repeated, so a meter reply is
never confused with the echo. If the converter does not echo, the first frame is
already the slave reply and nothing is dropped.

You normally do nothing. Only two cases need the option:

```c
static void set_echo_mode(mbus_handle *h)
{
    /* loopback test (TX wired to RX, no converter): keep every frame */
    mbus_context_set_option(h, MBUS_OPTION_PURGE_FIRST_FRAME, MBUS_FRAME_PURGE_NONE);

    /* back to the default: drop a leading master-to-slave frame */
    mbus_context_set_option(h, MBUS_OPTION_PURGE_FIRST_FRAME, MBUS_FRAME_PURGE_M2S);
}
```

## First read does not work: checklist

Work from the bus side to the code. Most failures are in the first three.

1. **Timeout on every read** (`MBUS_METER_ERR_TIMEOUT`):
   - TX and RX swapped, or RX not connected.
   - Converter not powered on the bus side, or no common ground.
   - Wrong baud rate. Try `mbus_meter_probe()`, it walks 2400, 300, 9600.
   - Wrong address. Run the secondary scan (usage.md, section 5) and use what it
     finds. Check the meter's manual for its primary address.
   - Meter needs a longer answer window or a wake-up burst: try the SLOW and then
     the OPTICAL profile.
2. **Sometimes works, sometimes `ERR_LINK` or `ERR_INVALID_FRAME`:**
   - Bus voltage too low for the number of meters (too many unit loads for the
     converter).
   - Long or thin cable, poor connections, no common ground.
   - Two meters with the same primary address.
3. **One meter works, a second one does not:** the converter cannot drive both
   unit loads, or both have the same address.
4. **Reply arrives but the values are wrong or missing:** see the profile table in
   usage.md, section 8, and send a report with the log (the form asks for it).

Trace every byte on the bus while debugging:

```c
static void trace_on(mbus_handle *h)
{
    mbus_register_send_event(h, mbus_dump_send_event);
    mbus_register_recv_event(h, mbus_dump_recv_event);
}
```

You should see a `SEND` line for each request and a `RECV` line for the echo (if
your converter echoes) followed by a `RECV` line with the meter reply. A `SEND` with
no `RECV` at all points at the wiring or the converter, not at the library.

## Bench test without a meter

TX jumpered to RX, no converter, is enough to check the UART setup. This is what the
test app in `test_apps/` does. It proves pins, baud and parity on your board, not
the bus side.
