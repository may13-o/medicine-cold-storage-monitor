# Cloud Layer

## MQTT topics

| Topic | Direction | Content |
|---|---|---|
| `coldstore/<node_id>/telemetry` | gateway → cloud | One reading (JSON below) |
| `coldstore/<node_id>/event` | gateway → cloud | State changes, faults, power events |
| `coldstore/<node_id>/status` | gateway → cloud | `online` / `offline`, retained. `offline` is set as the MQTT **Last Will** so the broker announces a dead gateway |
| `coldstore/<node_id>/cmd` | cloud → gateway | Optional: remote mute, threshold changes |

Use TLS and per-device credentials. Do not commit credentials; use a local config file that is in `.gitignore`.

## Telemetry payload

```json
{
  "node": "node1",
  "ts": 1794096000,
  "ts_src": "rtc",
  "temp": 4.45,
  "rh": 61.2,
  "state": "NORMAL",
  "fault": 0,
  "seq": 1042,
  "buffered": false
}
```

- `ts_src` is `rtc` when the node's RTC time was valid, otherwise `gateway`.
- `buffered` is `true` for readings uploaded after an outage, so the dashboard can show backfilled data.
- Send every 30–60 s in normal operation, and immediately on a state change.

## Alert rules

| Rule | Trigger | Why |
|---|---|---|
| Excursion | `state` is `ALARM` | Product may be spoiling |
| Cold excursion | `temp` below 2.0 °C sustained | Freezing damages many medicines |
| No data | No telemetry for 3 minutes, or `status` goes `offline` | Silence must alert |
| Sensor / RTC fault | `fault` has bit 0 or 1 set | Data is not trustworthy |
| Sequence gaps | Many missed `seq` values | Link is degraded |

Send a "back to normal" notification when an alarm clears, and rate-limit repeats (for example one reminder every 15 minutes).

## Platform options (decision pending)

| Option | Pros | Cons |
|---|---|---|
| ThingsBoard Cloud | Dashboard, rule engine and email notifications with little setup | SMS usually needs an external provider |
| AWS IoT Core + SNS (+ Grafana / CloudWatch) | Closest to industry practice; SMS available through SNS | More setup, and costs to watch |
| Adafruit IO or similar | Fastest to a working demo | Limited rule logic and notification options |

Email is the safe first target. **SMS to Indian numbers generally needs sender/template registration (DLT)**, so treat SMS as a stretch goal and verify the current rules for your chosen provider before promising it in the report.
