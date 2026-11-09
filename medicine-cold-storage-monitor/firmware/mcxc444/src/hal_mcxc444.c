/*
 * hal_mcxc444.c - FRDM-MCXC444 implementation of hal.h.
 *
 * STATUS: STUB. Every function is safe-by-default (I2C fails, so the node reports a sensor
 * fault and alarms) but does nothing real. Fill each TODO using the MCUXpresso SDK for the
 * MCX C444 after checking:
 *   - which I2C driver the SDK ships (fsl_i2c vs fsl_lpi2c) and its transfer API
 *   - which LPUART/UART instance and pins are wired to your header (docs/wiring.md, all TBD)
 *   - the GPIO and PORT API signature for your SDK version (it differs between releases)
 * Do not copy pin names from other FRDM boards.
 */
#include "hal.h"

void hal_init(void)
{
    /* TODO: board clocks, enable port clocks, pin mux (I2C SDA/SCL, UART TX/RX),
     *       LED and buzzer as outputs (initial level LOW), button as input with pull-up
     *       and falling-edge interrupt, I2C master init (100 kHz to start), UART init
     *       115200 8N1 with RX interrupt feeding a small ring buffer, SysTick 1 kHz for
     *       hal_millis(), watchdog start with a 2 to 4 s timeout. */
}

uint32_t hal_millis(void)
{
    /* TODO: return the SysTick-driven millisecond counter. */
    return 0u;
}

void hal_delay_ms(uint32_t ms)
{
    /* TODO: SDK_DelayAtLeastUs(ms * 1000u, core_clock) or a SysTick wait. */
    (void)ms;
}

bool hal_i2c_write(uint8_t addr7, const uint8_t *data, size_t len)
{
    /* TODO: blocking master write WITH a timeout. Return false on NACK/timeout/bus error. */
    (void)addr7; (void)data; (void)len;
    return false;
}

bool hal_i2c_read(uint8_t addr7, uint8_t *data, size_t len)
{
    /* TODO: blocking master read WITH a timeout. */
    (void)addr7; (void)data; (void)len;
    return false;
}

void hal_led_set(bool on)
{
    /* TODO: write the LED GPIO (active high). */
    (void)on;
}

void hal_buzzer_set(bool on)
{
    /* TODO: write the buzzer-driver GPIO (active high, drives the NPN base through 1 kOhm). */
    (void)on;
}

bool hal_button_take_press(void)
{
    /* TODO: return and clear a flag set by the button interrupt; add debounce
     *       (ignore edges within about 50 ms of the last one). */
    return false;
}

bool hal_uart_read_byte(uint8_t *byte)
{
    /* TODO: pop one byte from the UART RX ring buffer, non-blocking. */
    (void)byte;
    return false;
}

void hal_uart_write(const uint8_t *data, size_t len)
{
    /* TODO: blocking write of len bytes to the gateway UART. */
    (void)data; (void)len;
}

void hal_watchdog_kick(void)
{
    /* TODO: refresh the watchdog. */
}

bool hal_display_update(const node_view_t *view)
{
    /* TODO: SSD1306 driver. Show temperature, humidity, state, time, and the messages
     *       "SENSOR ERR" (fault & FAULT_SENSOR) and "NET?" (fault & FAULT_NO_GATEWAY).
     *       Return false if the display does not ACK on I2C. */
    (void)view;
    return true;
}
