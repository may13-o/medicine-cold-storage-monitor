/*
 * mock_hal.h - simulated MCXC444 board for host tests: an SHT40 at 0x44, a DS3231 at 0x68,
 * LED, buzzer, button, UART and a millisecond clock the test controls.
 */
#ifndef MOCK_HAL_H
#define MOCK_HAL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "hal.h"

void mock_reset(void);

/* Time */
void mock_advance_ms(uint32_t ms);

/* Environment the node "measures" */
extern int16_t  g_sim_temp_x100;
extern uint16_t g_sim_rh_x100;
extern bool     g_sht_connected;

/* RTC: runs from a base epoch; invalid = oscillator-stop flag set */
void mock_rtc_set(uint32_t epoch, bool valid);
extern bool g_rtc_present;
extern bool g_rtc_valid;
uint32_t mock_rtc_now(void);

/* Inputs / outputs */
extern bool g_led;
extern bool g_buzzer;
extern bool g_button_pending;
extern bool g_display_ok;
extern unsigned g_wd_kicks;
extern node_view_t g_last_view;

/* UART: bytes the "gateway" sends to the node, and bytes the node sent out */
void   mock_uart_push(const uint8_t *data, size_t len);
extern uint8_t g_tx[16384];
extern size_t  g_tx_len;

#endif /* MOCK_HAL_H */
