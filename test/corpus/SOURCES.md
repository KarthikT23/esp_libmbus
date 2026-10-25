# Frame sources

Frames are data only. Hex bytes, no code copied.

| file | meter | medium | source | licence |
|---|---|---|---|---|
| gombus_acw_v20_19004636_a64.hex, _a157.hex | Itron Cyble-class | water | yottabytesolutions/gombus (tests) | Apache-2.0 |
| gombus_gav_v199_90072114_a37.hex, _a77.hex | Gavazzi / Garo GNM3x-EM340 family | electricity | yottabytesolutions/gombus (tests) | Apache-2.0 |
| pymb_elv_v20_64000392_a4.hex | Elvaco comm controller (medium 0x31) | gateway | ganehag/pyMeterBus (tests) | BSD-3-Clause |
| pymb_elv_v50_54000834_a242.hex | Elvaco temp/humidity | sensor | ganehag/pyMeterBus (tests) | BSD-3-Clause |
| wep_indoor.hex | WEP room sensor | room sensor | ganehag/pyMeterBus (tests) | BSD-3-Clause |
| libmbus_pr229_plain_text.hex | synthetic, VIF FC with VIFE before the text | water | rscada/libmbus PR 229 (open, test frame) | BSD-3-Clause |

The gombus frames are labelled synthesized/hand-validated upstream, not live captures.
Upstream libmbus frames live in test/test-frames (BSD-3-Clause).