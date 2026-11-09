#include "node_app.h"

#include <string.h>

#include "ds3231.h"
#include "hal.h"
#include "sht40.h"

#ifndef NODE_SAMPLE_PERIOD_MS
#define NODE_SAMPLE_PERIOD_MS 10000u
#endif
#ifndef NODE_NET_TIMEOUT_MS
#define NODE_NET_TIMEOUT_MS 60000u
#endif
#define NODE_RX_BYTES_PER_STEP 64u
#define LED_BLINK_HALF_PERIOD_MS 500u

/* ---------------------------------------------------------------- sensor */

static bool read_sht40(int16_t *temp_x100, uint16_t *rh_x100)
{
    uint8_t cmd = (uint8_t)SHT40_CMD_MEASURE_HIGH;
    uint8_t raw[6];

    if (!hal_i2c_write(SHT40_I2C_ADDR, &cmd, 1u)) {
        return false;
    }
    hal_delay_ms(SHT40_MEASURE_WAIT_MS);
    if (!hal_i2c_read(SHT40_I2C_ADDR, raw, sizeof raw)) {
        return false;
    }
    return sht40_parse(raw, temp_x100, rh_x100);
}

/* ------------------------------------------------------------------- RTC */

static bool rtc_set_pointer_and_read(uint8_t reg, uint8_t *buf, size_t len)
{
    return hal_i2c_write(DS3231_I2C_ADDR, &reg, 1u) && hal_i2c_read(DS3231_I2C_ADDR, buf, len);
}

static bool read_rtc(uint32_t *epoch)
{
    uint8_t status;
    uint8_t regs[7];

    if (!rtc_set_pointer_and_read((uint8_t)DS3231_REG_STATUS, &status, 1u)) {
        return false;
    }
    if ((status & DS3231_STATUS_OSF) != 0u) {
        return false;  /* oscillator stopped: stored time is not trustworthy */
    }
    if (!rtc_set_pointer_and_read((uint8_t)DS3231_REG_TIME, regs, sizeof regs)) {
        return false;
    }
    return ds3231_regs_to_epoch(regs, epoch);
}

static bool write_rtc(uint32_t epoch)
{
    uint8_t buf[8];
    uint8_t status;

    buf[0] = (uint8_t)DS3231_REG_TIME;
    ds3231_epoch_to_regs(epoch, &buf[1]);
    if (!hal_i2c_write(DS3231_I2C_ADDR, buf, sizeof buf)) {
        return false;
    }

    /* Clear the oscillator-stop flag now that the time is valid again. */
    if (!rtc_set_pointer_and_read((uint8_t)DS3231_REG_STATUS, &status, 1u)) {
        return false;
    }
    buf[0] = (uint8_t)DS3231_REG_STATUS;
    buf[1] = (uint8_t)(status & (uint8_t)~DS3231_STATUS_OSF);
    return hal_i2c_write(DS3231_I2C_ADDR, buf, 2u);
}

/* ------------------------------------------------------------- reporting */

static uint8_t current_fault(const node_t *n, uint32_t now_ms)
{
    uint8_t f = FAULT_NONE;
    if (alarm_sensor_fault(&n->alarm)) {
        f |= FAULT_SENSOR;
    }
    if (!n->rtc_ok) {
        f |= FAULT_RTC;
    }
    if (!n->oled_ok) {
        f |= FAULT_OLED;
    }
    if ((uint32_t)(now_ms - n->last_hb_ms) > NODE_NET_TIMEOUT_MS) {
        f |= FAULT_NO_GATEWAY;
    }
    return f;
}

static void send_telemetry(node_t *n, uint32_t now_ms)
{
    telemetry_t t;
    uint8_t payload[TELEMETRY_WIRE_LEN];
    uint8_t frame[FRAME_MAX_LEN];

    t.seq = n->seq++;
    t.epoch = n->rtc_ok ? n->epoch : 0u;
    t.temp_x100 = n->last_temp_x100;
    t.rh_x100 = n->last_rh_x100;
    t.state = (uint8_t)alarm_state(&n->alarm);
    t.fault = current_fault(n, now_ms);
    t.muted = n->alarm.muted ? 1u : 0u;

    telemetry_pack(&t, payload);
    int len = frame_encode((uint8_t)MSG_TELEMETRY, payload, (uint8_t)sizeof payload, frame, sizeof frame);
    if (len > 0) {
        hal_uart_write(frame, (size_t)len);
    }
}

static void update_display(node_t *n, uint32_t now_ms)
{
    node_view_t v;
    v.have_reading = n->have_reading;
    v.temp_x100 = n->last_temp_x100;
    v.rh_x100 = n->last_rh_x100;
    v.state = (uint8_t)alarm_state(&n->alarm);
    v.fault = current_fault(n, now_ms);
    v.muted = n->alarm.muted;
    v.epoch = n->rtc_ok ? n->epoch : 0u;
    n->oled_ok = hal_display_update(&v);
}

static void update_outputs(const node_t *n, uint32_t now_ms)
{
    hal_buzzer_set(alarm_buzzer_on(&n->alarm));

    switch (alarm_led_mode(&n->alarm)) {
    case LED_SOLID:
        hal_led_set(true);
        break;
    case LED_BLINK:
        hal_led_set(((now_ms / LED_BLINK_HALF_PERIOD_MS) & 1u) == 0u);
        break;
    default:
        hal_led_set(false);
        break;
    }
}

static void sample_and_report(node_t *n, uint32_t now_ms)
{
    int16_t t = 0;
    uint16_t rh = 0;
    const uint32_t now_s = now_ms / 1000u;

    bool ok = read_sht40(&t, &rh);
    if (ok) {
        n->last_temp_x100 = t;
        n->last_rh_x100 = rh;
        n->have_reading = true;
    }
    alarm_update(&n->alarm, now_s, ok, t);

    uint32_t ep = 0u;
    n->rtc_ok = read_rtc(&ep);
    n->epoch = n->rtc_ok ? ep : 0u;

    send_telemetry(n, now_ms);
    update_display(n, now_ms);
}

/* -------------------------------------------------------------- commands */

static void handle_frame(node_t *n, const frame_t *f, uint32_t now_ms)
{
    switch (f->msg_type) {
    case MSG_CMD_TIME:
        if (f->len == 4u) {
            uint32_t ep = (uint32_t)f->payload[0] | ((uint32_t)f->payload[1] << 8) |
                          ((uint32_t)f->payload[2] << 16) | ((uint32_t)f->payload[3] << 24);
            if (write_rtc(ep)) {
                n->rtc_ok = true;
                n->epoch = ep;
            }
        }
        n->last_hb_ms = now_ms;  /* any valid frame proves the gateway is alive */
        break;
    case MSG_CMD_MUTE:
        if (alarm_mute(&n->alarm, now_ms / 1000u)) {
            send_telemetry(n, now_ms);
        }
        n->last_hb_ms = now_ms;
        break;
    case MSG_CMD_HB:
        n->last_hb_ms = now_ms;
        break;
    default:
        break;
    }
}

/* ------------------------------------------------------------ public API */

void node_init(node_t *n)
{
    alarm_config_t cfg;
    uint32_t now = hal_millis();

    memset(n, 0, sizeof *n);
    alarm_config_default(&cfg);
    alarm_init(&n->alarm, &cfg);
    frame_parser_init(&n->parser);
    n->next_sample_ms = now;  /* take the first sample immediately */
    n->last_hb_ms = now;      /* grace period before FAULT_NO_GATEWAY */
    n->oled_ok = true;
}

void node_step(node_t *n)
{
    uint32_t now_ms = hal_millis();

    hal_watchdog_kick();

    uint8_t b;
    for (unsigned i = 0; i < NODE_RX_BYTES_PER_STEP && hal_uart_read_byte(&b); i++) {
        if (frame_parser_feed(&n->parser, b, now_ms)) {
            handle_frame(n, &n->parser.frame, now_ms);
        }
    }

    if (hal_button_take_press()) {
        if (alarm_mute(&n->alarm, now_ms / 1000u)) {
            send_telemetry(n, now_ms);
        }
    }

    if ((int32_t)(now_ms - n->next_sample_ms) >= 0) {
        n->next_sample_ms += NODE_SAMPLE_PERIOD_MS;
        if ((int32_t)(now_ms - n->next_sample_ms) >= 0) {
            n->next_sample_ms = now_ms + NODE_SAMPLE_PERIOD_MS;  /* fell far behind: resync */
        }
        sample_and_report(n, now_ms);
        hal_watchdog_kick();
    }

    now_ms = hal_millis();
    alarm_tick(&n->alarm, now_ms / 1000u);
    update_outputs(n, now_ms);
}
