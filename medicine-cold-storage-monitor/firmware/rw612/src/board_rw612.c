/*
 * board_rw612.c - FRDM-RW612 implementation of board_rw612.h.
 *
 * STATUS: STUB. The network is reported as down, so the gateway only buffers.
 * Fill each TODO from the SDK's own Wi-Fi, lwIP, SNTP and MQTT examples rather than writing
 * the stack from scratch. Keep Wi-Fi and MQTT credentials in a git-ignored secrets.h.
 *
 * MQTT notes:
 *   - Use TLS and per-device credentials.
 *   - Set a Last Will on coldstore/<node>/status = "offline" (retained) and publish
 *     "online" after connecting, so the cloud notices a dead gateway immediately.
 *   - board_net_publish() should return true only after the broker acknowledged (QoS 1),
 *     because gw_core removes the record from its queue on true.
 */
#include "board_rw612.h"

void board_init(void)
{
    /* TODO: clocks, pins, UART to the node (115200 8N1, RX interrupt into a ring buffer),
     *       Wi-Fi connect with backoff, SNTP client, MQTT client with TLS. */
}

uint32_t board_millis(void)
{
    /* TODO: monotonic millisecond counter. */
    return 0u;
}

bool board_uart_read_byte(uint8_t *byte)
{
    /* TODO: pop one byte from the UART RX ring buffer. */
    (void)byte;
    return false;
}

void board_uart_write(const uint8_t *data, size_t len)
{
    /* TODO: blocking write to the node UART. */
    (void)data; (void)len;
}

bool board_net_is_up(void)
{
    /* TODO: Wi-Fi associated, IP acquired and MQTT connected. */
    return false;
}

bool board_net_publish(const char *topic, const char *payload)
{
    /* TODO: MQTT publish QoS 1; return true only when acknowledged. */
    (void)topic; (void)payload;
    return false;
}

uint32_t board_utc_now(void)
{
    /* TODO: SNTP-synced UTC epoch in seconds; return 0 until the first successful sync. */
    return 0u;
}

void board_idle(void)
{
    /* TODO: yield to the RTOS or sleep a few milliseconds. */
}
