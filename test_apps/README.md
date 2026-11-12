# esp_libmbus test app

Unity tests that run on the target. No meter and no M-Bus transceiver needed:
the UART TX pin is jumpered to the RX pin and a scripted "slave" writes
replies onto the same wire at fixed times.

Default pins (change with `idf.py menuconfig`, "esp_libmbus test app"):

| Target  | TX -> RX        |
|---------|-----------------|
| esp32c6 | GPIO6 -> GPIO7  |
| esp32c3 | GPIO4 -> GPIO5  |
| esp32s3 | GPIO17 -> GPIO18|
| esp32   | GPIO17 -> GPIO16|

Run all tests:

    idf.py set-target esp32c6
    idf.py -p COMx flash monitor

Timing-based: a test that fails on a loaded or very slow host may pass on a
rerun. A full run takes well under a minute.
