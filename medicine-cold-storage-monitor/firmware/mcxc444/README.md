# MCXC444 Node Firmware

Portable logic is written and tested on a PC. Only `src/hal_mcxc444.c` needs the SDK.

```
mcxc444/
├── include/
│   ├── alarm.h       alarm state machine (filter, hold time, hysteresis, mute, sensor fault)
│   ├── sht40.h       CRC and conversion for the SHT40
│   ├── ds3231.h      RTC register <-> UTC epoch (assumes a DS3231; change here if not)
│   ├── node_app.h    application: sample, report, handle gateway commands
│   └── hal.h         the ONLY board-specific interface
└── src/
    ├── alarm.c  sht40.c  ds3231.c  node_app.c   portable, host-tested
    ├── main.c                                   entry point
    └── hal_mcxc444.c                            STUB: implement with the MCUXpresso SDK
```

## Bringing it up on the board

1. In MCUXpresso, create a project for the FRDM-MCXC444 from the SDK. Add `../common` and `include` to the include path, and add `src/*.c` plus `../common/protocol.c`.
2. Fix the pin table in `docs/wiring.md` from the board user guide and the SDK pin tool.
3. Fill in each `TODO` in `hal_mcxc444.c`. Order that works well: millis timer, LED/buzzer, I2C scan, SHT40, UART, button, RTC, watchdog, OLED last.
4. Requirements the HAL must meet:
   - I2C transfers must have a **timeout** and return `false` on any error.
   - `hal_millis()` must be monotonic and not stop in sleep.
   - Start the watchdog in `hal_init()` with a 2–4 s timeout.
   - Keep the buzzer and LED low during reset.

## Behaviour defaults (edit `alarm_config_default()`)

| Setting | Default |
|---|---|
| Band | 2.0–8.0 °C |
| Warning margin / hysteresis | 0.5 °C / 0.3 °C |
| Critical limits (immediate alarm) | below 0 °C, above 10 °C |
| Hold time before alarm | 120 s |
| Sensor failures before fault | 3 in a row |
| Mute timeout | 900 s |
| Sample period | 10 s (`NODE_SAMPLE_PERIOD_MS`) |
