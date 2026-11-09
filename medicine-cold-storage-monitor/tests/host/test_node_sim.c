/*
 * End-to-end simulation of the node application against the mock board:
 * sensor + RTC + button + UART + outputs, driven through node_step() with a simulated clock.
 */
#include <string.h>

#include "check.h"
#include "mock_hal.h"
#include "node_app.h"

static node_t node;

static void run_s(unsigned seconds)
{
    for (unsigned ms = 0; ms < seconds * 1000u; ms += 10u) {
        node_step(&node);
        mock_advance_ms(10u);
    }
}

/* Parse the node's UART output into telemetry records; clears the capture buffer. */
static int drain_tx(telemetry_t *out, int max)
{
    frame_parser_t p;
    int n = 0;
    frame_parser_init(&p);
    for (size_t i = 0; i < g_tx_len; i++) {
        if (frame_parser_feed(&p, g_tx[i], 0u) && p.frame.msg_type == MSG_TELEMETRY && n < max) {
            if (telemetry_unpack(p.frame.payload, p.frame.len, &out[n])) {
                n++;
            }
        }
    }
    g_tx_len = 0u;
    return n;
}

static void gateway_sends(uint8_t type, const uint8_t *payload, uint8_t len)
{
    uint8_t buf[FRAME_MAX_LEN];
    int n = frame_encode(type, payload, len, buf, sizeof buf);
    mock_uart_push(buf, (size_t)n);
}

static void gateway_sends_time(uint32_t epoch)
{
    uint8_t p[4] = { (uint8_t)epoch, (uint8_t)(epoch >> 8), (uint8_t)(epoch >> 16), (uint8_t)(epoch >> 24) };
    gateway_sends(MSG_CMD_TIME, p, 4u);
}

int main(void)
{
    telemetry_t t[64];
    int n;

    mock_reset();
    node_init(&node);

    /* ---- Normal operation: one frame per 10 s, sequence counting up, RTC time present. */
    run_s(35);
    n = drain_tx(t, 64);
    CHECK_EQ(n, 4);  /* t = 0, 10, 20, 30 */
    CHECK_EQ(t[0].seq, 0);
    CHECK_EQ(t[3].seq, 3);
    CHECK(t[0].temp_x100 >= 499 && t[0].temp_x100 <= 501);
    CHECK(t[0].rh_x100 >= 4990 && t[0].rh_x100 <= 5010);
    CHECK_EQ(t[0].state, ALARM_STATE_NORMAL);
    CHECK_EQ(t[0].fault & FAULT_RTC, 0);
    CHECK(t[0].epoch >= 1794096000u && t[0].epoch <= 1794096002u);
    CHECK(t[3].epoch >= 1794096030u && t[3].epoch <= 1794096032u);
    CHECK(!g_buzzer && !g_led);
    CHECK(g_wd_kicks > 100u);
    CHECK(g_last_view.have_reading);

    /* ---- Gateway heartbeat keeps FAULT_NO_GATEWAY away; its absence sets it after 60 s. */
    gateway_sends(MSG_CMD_HB, NULL, 0u);
    run_s(30);
    n = drain_tx(t, 64);
    CHECK(n >= 3);
    CHECK_EQ(t[n - 1].fault & FAULT_NO_GATEWAY, 0);
    run_s(70);
    n = drain_tx(t, 64);
    CHECK((t[n - 1].fault & FAULT_NO_GATEWAY) != 0);
    gateway_sends(MSG_CMD_HB, NULL, 0u);
    run_s(11);
    n = drain_tx(t, 64);
    CHECK_EQ(t[n - 1].fault & FAULT_NO_GATEWAY, 0);

    /* ---- Door opened briefly: warm for 40 s then back. No alarm. */
    g_sim_temp_x100 = 900;
    run_s(40);
    g_sim_temp_x100 = 500;
    for (int i = 0; i < 4; i++) {
        gateway_sends(MSG_CMD_HB, NULL, 0u);
        run_s(15);
    }
    n = drain_tx(t, 64);
    for (int i = 0; i < n; i++) {
        CHECK(t[i].state != ALARM_STATE_ALARM);
    }
    CHECK(!g_buzzer);

    /* ---- Sustained excursion: alarm after the hold time, buzzer and LED on. */
    g_sim_temp_x100 = 900;
    for (int i = 0; i < 12; i++) {
        gateway_sends(MSG_CMD_HB, NULL, 0u);
        run_s(15);
    }
    CHECK(g_buzzer);
    CHECK(g_led);
    n = drain_tx(t, 64);
    CHECK_EQ(t[n - 1].state, ALARM_STATE_ALARM);
    CHECK(t[n - 1].temp_x100 >= 899 && t[n - 1].temp_x100 <= 901);
    CHECK_EQ(g_last_view.state, ALARM_STATE_ALARM);

    /* ---- Local button mutes: buzzer off, LED stays on, an extra frame reports muted. */
    g_button_pending = true;
    run_s(1);
    CHECK(!g_buzzer);
    CHECK(g_led);
    n = drain_tx(t, 64);
    CHECK(n >= 1);
    CHECK_EQ(t[n - 1].muted, 1);
    CHECK_EQ(t[n - 1].state, ALARM_STATE_ALARM);

    /* ---- Recovery clears alarm and mute. */
    g_sim_temp_x100 = 500;
    for (int i = 0; i < 4; i++) {
        gateway_sends(MSG_CMD_HB, NULL, 0u);
        run_s(15);
    }
    CHECK(!g_buzzer && !g_led);
    n = drain_tx(t, 64);
    CHECK_EQ(t[n - 1].state, ALARM_STATE_NORMAL);
    CHECK_EQ(t[n - 1].muted, 0);

    /* ---- Remote mute command works the same way. */
    g_sim_temp_x100 = 1100;
    for (int i = 0; i < 4; i++) {
        gateway_sends(MSG_CMD_HB, NULL, 0u);
        run_s(15);
    }
    CHECK(g_buzzer);
    gateway_sends(MSG_CMD_MUTE, NULL, 0u);
    run_s(1);
    CHECK(!g_buzzer);
    g_sim_temp_x100 = 500;
    for (int i = 0; i < 4; i++) {
        gateway_sends(MSG_CMD_HB, NULL, 0u);
        run_s(15);
    }
    drain_tx(t, 64);

    /* ---- Sensor unplugged: fault + alarm after 3 failed reads, last good value is held. */
    g_sht_connected = false;
    for (int i = 0; i < 4; i++) {
        gateway_sends(MSG_CMD_HB, NULL, 0u);
        run_s(10);
    }
    CHECK(g_buzzer);
    n = drain_tx(t, 64);
    CHECK((t[n - 1].fault & FAULT_SENSOR) != 0);
    CHECK_EQ(t[n - 1].state, ALARM_STATE_ALARM);
    CHECK(t[n - 1].temp_x100 >= 499 && t[n - 1].temp_x100 <= 501);  /* not zero */

    g_sht_connected = true;
    for (int i = 0; i < 3; i++) {
        gateway_sends(MSG_CMD_HB, NULL, 0u);
        run_s(10);
    }
    n = drain_tx(t, 64);
    CHECK_EQ(t[n - 1].fault & FAULT_SENSOR, 0);
    CHECK_EQ(t[n - 1].state, ALARM_STATE_NORMAL);
    CHECK(!g_buzzer);

    /* ---- RTC lost power: fault + epoch 0, then the gateway's TIME command repairs it. */
    mock_rtc_set(946684800u, false);
    gateway_sends(MSG_CMD_HB, NULL, 0u);
    run_s(11);
    n = drain_tx(t, 64);
    CHECK((t[n - 1].fault & FAULT_RTC) != 0);
    CHECK_EQ(t[n - 1].epoch, 0);

    gateway_sends_time(1794100000u);
    run_s(11);
    n = drain_tx(t, 64);
    CHECK_EQ(t[n - 1].fault & FAULT_RTC, 0);
    CHECK(t[n - 1].epoch >= 1794100000u && t[n - 1].epoch <= 1794100015u);

    /* ---- Corrupted command frame is ignored. */
    {
        uint8_t buf[FRAME_MAX_LEN];
        uint8_t p[4] = { 0x00, 0x00, 0x00, 0x00 };
        int len = frame_encode(MSG_CMD_TIME, p, 4u, buf, sizeof buf);
        buf[4] ^= 0xFF;
        mock_uart_push(buf, (size_t)len);
        run_s(11);
        n = drain_tx(t, 64);
        CHECK(t[n - 1].epoch >= 1794100000u);  /* RTC not overwritten with 1970 */
    }

    /* ---- Display failure is reported but is non-critical. */
    g_display_ok = false;
    gateway_sends(MSG_CMD_HB, NULL, 0u);
    run_s(21);
    n = drain_tx(t, 64);
    CHECK((t[n - 1].fault & FAULT_OLED) != 0);
    CHECK_EQ(t[n - 1].state, ALARM_STATE_NORMAL);

    FINISH("test_node_sim");
}
