#include "mock_hal.h"

#include <string.h>

#include "ds3231.h"
#include "sht40.h"

static uint32_t now_ms;

int16_t  g_sim_temp_x100 = 500;
uint16_t g_sim_rh_x100 = 5000;
bool     g_sht_connected = true;

bool g_rtc_present = true;
bool g_rtc_valid = true;
static uint32_t rtc_base_epoch;
static uint32_t rtc_base_ms;

bool g_led;
bool g_buzzer;
bool g_button_pending;
bool g_display_ok = true;
unsigned g_wd_kicks;
node_view_t g_last_view;

uint8_t g_tx[16384];
size_t  g_tx_len;
static uint8_t rx_q[1024];
static size_t rx_head, rx_tail;

static uint8_t rtc_ptr;

void mock_reset(void)
{
    now_ms = 0u;
    g_sim_temp_x100 = 500;
    g_sim_rh_x100 = 5000;
    g_sht_connected = true;
    g_rtc_present = true;
    g_rtc_valid = true;
    rtc_base_epoch = 1794096000u;
    rtc_base_ms = 0u;
    g_led = false;
    g_buzzer = false;
    g_button_pending = false;
    g_display_ok = true;
    g_wd_kicks = 0u;
    g_tx_len = 0u;
    rx_head = rx_tail = 0u;
    rtc_ptr = 0u;
    memset(&g_last_view, 0, sizeof g_last_view);
}

void mock_advance_ms(uint32_t ms) { now_ms += ms; }

uint32_t mock_rtc_now(void)
{
    return rtc_base_epoch + (now_ms - rtc_base_ms) / 1000u;
}

void mock_rtc_set(uint32_t epoch, bool valid)
{
    rtc_base_epoch = epoch;
    rtc_base_ms = now_ms;
    g_rtc_valid = valid;
}

void mock_uart_push(const uint8_t *data, size_t len)
{
    for (size_t i = 0; i < len && rx_head < sizeof rx_q; i++) {
        rx_q[rx_head++] = data[i];
    }
}

/* ------------------------------------------------------------------ HAL */

void hal_init(void) {}
uint32_t hal_millis(void) { return now_ms; }
void hal_delay_ms(uint32_t ms) { now_ms += ms; }

bool hal_i2c_write(uint8_t addr7, const uint8_t *data, size_t len)
{
    if (addr7 == SHT40_I2C_ADDR) {
        return g_sht_connected && len == 1u && data[0] == SHT40_CMD_MEASURE_HIGH;
    }
    if (addr7 == DS3231_I2C_ADDR) {
        if (!g_rtc_present || len < 1u) {
            return false;
        }
        rtc_ptr = data[0];
        if (rtc_ptr == DS3231_REG_TIME && len == 8u) {
            uint32_t ep;
            if (!ds3231_regs_to_epoch(&data[1], &ep)) {
                return false;
            }
            rtc_base_epoch = ep;
            rtc_base_ms = now_ms;  /* writing time does NOT clear OSF, like the real chip */
        } else if (rtc_ptr == DS3231_REG_STATUS && len == 2u) {
            if ((data[1] & DS3231_STATUS_OSF) == 0u) {
                g_rtc_valid = true;
            }
        }
        return true;
    }
    return false;
}

bool hal_i2c_read(uint8_t addr7, uint8_t *data, size_t len)
{
    if (addr7 == SHT40_I2C_ADDR) {
        if (!g_sht_connected || len != 6u) {
            return false;
        }
        int32_t t_ticks = ((int32_t)g_sim_temp_x100 + 4500) * 65535 / 17500;
        int32_t rh_ticks = ((int32_t)g_sim_rh_x100 + 600) * 65535 / 12500;
        data[0] = (uint8_t)(t_ticks >> 8);
        data[1] = (uint8_t)(t_ticks & 0xFF);
        data[2] = sht40_crc8(&data[0], 2);
        data[3] = (uint8_t)(rh_ticks >> 8);
        data[4] = (uint8_t)(rh_ticks & 0xFF);
        data[5] = sht40_crc8(&data[3], 2);
        return true;
    }
    if (addr7 == DS3231_I2C_ADDR) {
        if (!g_rtc_present) {
            return false;
        }
        if (rtc_ptr == DS3231_REG_STATUS && len == 1u) {
            data[0] = g_rtc_valid ? 0x00 : DS3231_STATUS_OSF;
            return true;
        }
        if (rtc_ptr == DS3231_REG_TIME && len == 7u) {
            ds3231_epoch_to_regs(mock_rtc_now(), data);
            return true;
        }
    }
    return false;
}

void hal_led_set(bool on) { g_led = on; }
void hal_buzzer_set(bool on) { g_buzzer = on; }

bool hal_button_take_press(void)
{
    bool p = g_button_pending;
    g_button_pending = false;
    return p;
}

bool hal_uart_read_byte(uint8_t *byte)
{
    if (rx_tail >= rx_head) {
        return false;
    }
    *byte = rx_q[rx_tail++];
    return true;
}

void hal_uart_write(const uint8_t *data, size_t len)
{
    if (g_tx_len + len <= sizeof g_tx) {
        memcpy(g_tx + g_tx_len, data, len);
        g_tx_len += len;
    }
}

void hal_watchdog_kick(void) { g_wd_kicks++; }

bool hal_display_update(const node_view_t *view)
{
    g_last_view = *view;
    return g_display_ok;
}
