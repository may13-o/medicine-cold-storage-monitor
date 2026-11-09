# Review of the Supplied Firmware Package

The file `complete_firmware_integration_package.md` (labelled "production-ready") was reviewed and used as the starting point for `firmware/`. This page records what was kept, what was changed and why, so the report can describe the design honestly.

## Kept

- Length-prefixed frame with SOF/EOF and CRC-8 over type, length and payload (CRC table verified correct: 256/256 entries, check value `0xF4`).
- SHT40 conversion formulas (T = −45 + 175·t/65535, RH = −6 + 125·t/65535).
- Hysteresis, a mute button, and a mute timeout (15 min) so a muted alarm cannot be forgotten.
- Store-and-forward ring buffer concept and JSON telemetry on MQTT.

## Changed

| Issue in the package | Resolution |
|---|---|
| I2C and UART never initialised; code could not run | All hardware access moved behind `hal.h`; `hal_mcxc444.c` is a documented stub to fill in |
| `{ .kPORT_PullUp, ... }` is not valid C (compile error) | Removed with the SDK-specific code; pin config belongs in the HAL |
| RH math wrapped (0 %RH printed as 649 %RH) | Clamped to 0..100 %RH; covered by `test_sensors.c` |
| SHT40 CRC bytes ignored | Both CRCs checked (`sht40.c`) |
| One reading over 8 °C alarmed instantly | 3-sample filter, 120 s hold time, plus immediate alarm beyond 0 °C / 10 °C |
| One I2C failure alarmed instantly | Alarm after 3 consecutive failures |
| WARNING state defined but never used | Implemented (LED blinks, no buzzer) |
| Failed read transmitted `0.00 °C` | Last good value held, `FAULT_SENSOR` set |
| No watchdog or I2C timeout; a hung bus meant a silent dead alarm | `hal_watchdog_kick()` in the main loop; HAL contract requires I2C timeouts |
| "Timestamp" was seconds since boot | Real UTC epoch from the DS3231 RTC; gateway sets it from SNTP |
| No RTC and no OLED code | RTC driver logic added (`ds3231.c`); OLED call is behind `hal_display_update()` |
| No sequence number, no gateway→node messages | Added `seq`, `CMD_TIME`, `CMD_HB`, `CMD_MUTE` |
| Parser could lose a good frame after a corrupted length byte | 50 ms inter-byte timeout; tested |
| `#pragma pack` structs (unaligned access on Cortex-M0+) | Explicit little-endian byte serialisation |
| Queue of 100 = under 2 minutes of outage at 1 Hz | 2048 entries with downsampling; drops oldest NORMAL first |
| Record popped before publish succeeded (data loss) | Peek, publish, pop only on acknowledged success |
| ThingsBoard topic assumed, no `ts`, so backfilled data stamped at upload time | Selectable format; ThingsBoard format sends `{"ts":<ms>,"values":{...}}` |
| `%.2f` float printf on both targets | Integer-only formatting |
| CMake files could not produce a firmware image | Removed. Use the MCUXpresso project for each board; shared code is plain C |

## Still not done (be explicit in the report)

- `hal_mcxc444.c` and `board_rw612.c` are **stubs**. Nothing has run on hardware.
- Pin assignments are unverified (see `docs/wiring.md`).
- No SSD1306 driver yet.
- Wi-Fi, SNTP and MQTT/TLS are not implemented.
- Thresholds and hold times are untuned defaults.

## What is tested

`make test` in `tests/host` runs, on a PC: CRC and framing (including corruption and resync), SHT40 and DS3231 conversions against a floating-point oracle, the alarm state machine (door-open, critical, hysteresis, mute, sensor fault), the gateway (queue, downsampling, store-and-forward, JSON, time sync), and a full node simulation with a mock board. Passing these shows the logic is correct; it does **not** show the hardware drivers work.
