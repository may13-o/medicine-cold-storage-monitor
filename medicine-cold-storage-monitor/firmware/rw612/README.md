# RW612 Gateway Firmware

Gateway logic is portable and tested on a PC. Only `src/board_rw612.c` needs the SDK.

```
rw612/
├── include/
│   ├── gw_core.h        gateway logic: parse, sequence check, downsample, queue, heartbeat, time, JSON
│   ├── record_queue.h   store-and-forward FIFO (drops oldest NORMAL first when full)
│   └── board_rw612.h    the ONLY board/network interface
└── src/
    ├── gw_core.c  record_queue.c   portable, host-tested
    ├── main.c                      entry point
    └── board_rw612.c               STUB: UART, Wi-Fi, SNTP, MQTT/TLS via the SDK
```

## Bringing it up on the board

1. Create an MCUXpresso project for the FRDM-RW612 starting from the SDK's Wi-Fi + MQTT example, and add `../common/protocol.c` and `src/*.c`.
2. Implement `board_rw612.c`. Suggested order: UART from the node, then Wi-Fi, SNTP, MQTT.
3. `board_net_publish()` must return `true` **only after the broker acknowledges** (QoS 1), because the queue removes the record on `true`.
4. Set an MQTT Last Will of `offline` (retained) on `coldstore/<node>/status`, and publish `online` after connecting.
5. Keep credentials in a git-ignored `secrets.h`; commit only a `secrets.example.h`.
6. Choose the cloud payload in `main.c`: `GW_FMT_FLAT` (see `docs/cloud.md`) or `GW_FMT_THINGSBOARD`.

Queue size is `GW_QUEUE_CAPACITY` (2048 records, about 40 KB). Check it against the RW612 RAM budget alongside the Wi-Fi stack.
