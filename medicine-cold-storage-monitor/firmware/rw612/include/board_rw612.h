/*
 * board_rw612.h - FRDM-RW612 board and network layer used by main.c.
 * Implement in src/board_rw612.c with the MCUXpresso SDK (UART, Wi-Fi, lwIP, SNTP, MQTT/TLS).
 */
#ifndef BOARD_RW612_H
#define BOARD_RW612_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

void     board_init(void);                 /* clocks, pins, UART to node, start Wi-Fi/SNTP/MQTT tasks */
uint32_t board_millis(void);
bool     board_uart_read_byte(uint8_t *byte);   /* non-blocking */
void     board_uart_write(const uint8_t *data, size_t len);
bool     board_net_is_up(void);            /* Wi-Fi up AND MQTT session connected */
bool     board_net_publish(const char *topic, const char *payload);  /* true once acknowledged */
uint32_t board_utc_now(void);              /* SNTP-synced UTC epoch, 0 until synced */
void     board_idle(void);                 /* short sleep or RTOS yield */

#endif /* BOARD_RW612_H */
