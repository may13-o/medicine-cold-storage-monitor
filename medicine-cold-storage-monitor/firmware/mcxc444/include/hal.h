/*
 * hal.h - the ONLY board/SDK-specific surface the node application uses.
 *
 * Everything above this line (alarm, sht40, ds3231, node_app, protocol) is portable C and is
 * tested on a PC with a mock HAL (tests/host/mock_hal.c). Implement these functions for the
 * FRDM-MCXC444 in src/hal_mcxc444.c using the MCUXpresso SDK.
 */
#ifndef HAL_H
#define HAL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* What the display should show. Filled by node_app. */
typedef struct {
    bool     have_reading;
    int16_t  temp_x100;
    uint16_t rh_x100;
    uint8_t  state;     /* alarm_state_t */
    uint8_t  fault;     /* FAULT_* bitmask */
    bool     muted;
    uint32_t epoch;     /* 0 = unknown */
} node_view_t;

void     hal_init(void);                 /* clocks, pins, I2C, UART, GPIO, button IRQ, watchdog */
uint32_t hal_millis(void);               /* monotonic milliseconds since boot */
void     hal_delay_ms(uint32_t ms);

/* 7-bit I2C address. Must use a bus timeout so a stuck bus cannot hang the node.
 * Return false on NACK, timeout or any bus error. */
bool hal_i2c_write(uint8_t addr7, const uint8_t *data, size_t len);
bool hal_i2c_read(uint8_t addr7, uint8_t *data, size_t len);

void hal_led_set(bool on);
void hal_buzzer_set(bool on);            /* drives the NPN stage; active high */

/* Returns true once per button press (latched by an interrupt, debounced). */
bool hal_button_take_press(void);

/* Non-blocking: returns false if no byte is waiting. */
bool hal_uart_read_byte(uint8_t *byte);
void hal_uart_write(const uint8_t *data, size_t len);

void hal_watchdog_kick(void);

/* Returns false if the display did not respond. May be a no-op returning true until the
 * OLED driver exists. */
bool hal_display_update(const node_view_t *view);

#endif /* HAL_H */
