# Test Plan

Record the result of every test (pass/fail, date, notes, photo or log). Passing tests are the evidence for the report.

| ID | Test | Procedure | Pass criteria |
|---|---|---|---|
| T01 | I2C scan | Run a scan with all three devices wired | 0x44, 0x3C and 0x68 all respond |
| T02 | Sensor sanity | Compare SHT40 to a reference thermometer at room temperature and in an ice-water slush (about 0 °C) | Error within the SHT40 datasheet tolerance; record actual offset |
| T03 | Alarm, cold side | Chill the sensor below 2 °C | Warning, then alarm after hold time; critical limit (0 °C) alarms immediately |
| T04 | Alarm, warm side | Warm the sensor above 8 °C (hold gently in a hand or warm air) | Alarm after hold time |
| T05 | Door-open tolerance | Exceed 8 °C briefly, shorter than `ALARM_HOLD` | No alarm, warning at most |
| T06 | Hysteresis | Hover around a limit | No rapid LED/buzzer flapping |
| T07 | Mute | Press the button during an alarm | Buzzer silent, LED stays on, state stays ALARM |
| T08 | Sensor fault | Unplug the SHT40 during run | `SENSOR ERR`, alarm, `fault` bit 0 set |
| T09 | RTC persistence | Power-cycle the node | Time stays correct (coin cell present) |
| T10 | RTC invalid | Remove coin cell and power off | RTC fault set, `epoch = 0`; gateway resync clears it |
| T11 | UART integrity | Corrupt bytes on the link, or unplug TX briefly | Gateway counts CRC errors and sequence gaps; no bad data published |
| T12 | Host unit tests | `make test` in `tests/host` | All five suites pass: protocol (CRC `0xF4`, corruption, resync), sensors, alarm, gateway, node simulation |
| T13 | Cloud upload | Run normally for 10 minutes | Dashboard shows continuous data with correct timestamps |
| T14 | Excursion alert | Trigger an alarm | Email arrives within the target delay (record the actual delay) |
| T15 | Offline alert | Pull gateway power or Wi-Fi | No-data alert fires; local alarm still works |
| T16 | Store and forward | Drop Wi-Fi for 5 minutes, restore | Buffered readings appear in order with `buffered: true` |
| T17 | Local alarm independence | Disconnect the gateway entirely and heat the sensor | Buzzer and LED still alarm |

## Evidence to keep

- Photos of the ice-bath and warm tests with the OLED reading visible.
- Serial logs of the UART frames, including an injected error.
- Screenshots of the dashboard, the alert email, and the offline alert.
- A table of final threshold values and measured alert latency.
