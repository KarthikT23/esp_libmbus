# Changelog

## 0.1.0

First release.

- ESP-IDF component derived from rscada libmbus 0.10.4
- UART master (IDF driver), primary and secondary addressing
- Typed meter API (`mbus-meter.h`): exact values, units, flags, key based lookup
- Read profiles (generic, strict FCB, slow, optical), baud probe
- Byte-level secondary address scan
- Link layer: wake-up burst, ping, auto detection
- Offline frame decode
- Example `read_meter`, Unity test app, host tests, CI
- Not verified on a real meter yet