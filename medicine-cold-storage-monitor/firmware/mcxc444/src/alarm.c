#include "alarm.h"

#include <string.h>

void alarm_config_default(alarm_config_t *c)
{
    c->t_low_x100 = 200;
    c->t_high_x100 = 800;
    c->warn_margin_x100 = 50;
    c->hyst_x100 = 30;
    c->crit_low_x100 = 0;
    c->crit_high_x100 = 1000;
    c->alarm_hold_s = 120u;
    c->fault_debounce = 3u;
    c->mute_timeout_s = 900u;
}

void alarm_init(alarm_t *a, const alarm_config_t *c)
{
    memset(a, 0, sizeof *a);
    a->cfg = *c;
    a->state = ALARM_STATE_NORMAL;
}

static void push_sample(alarm_t *a, int16_t t)
{
    a->samples[a->next] = t;
    a->next = (uint8_t)((a->next + 1u) % ALARM_FILTER_N);
    if (a->n_samples < ALARM_FILTER_N) {
        a->n_samples++;
    }
    int32_t sum = 0;
    for (uint8_t i = 0; i < a->n_samples; i++) {
        sum += a->samples[i];
    }
    a->filtered_x100 = (int16_t)(sum / (int32_t)a->n_samples);
}

static void enter_alarm(alarm_t *a)
{
    if (a->state != ALARM_STATE_ALARM) {
        a->state = ALARM_STATE_ALARM;
        a->muted = false;
    }
}

void alarm_update(alarm_t *a, uint32_t now_s, bool sensor_ok, int16_t temp_x100)
{
    const alarm_config_t *c = &a->cfg;

    if (!sensor_ok) {
        if (a->fail_count < 255u) {
            a->fail_count++;
        }
        if (a->fail_count >= c->fault_debounce) {
            a->sensor_fault = true;
            enter_alarm(a);
        }
        return;  /* no new data: state otherwise unchanged */
    }

    if (a->sensor_fault) {
        /* Sensor is back. Old samples are stale, so start the filter fresh. */
        a->sensor_fault = false;
        a->n_samples = 0;
        a->next = 0;
    }
    a->fail_count = 0;
    push_sample(a, temp_x100);

    const int16_t avg = a->filtered_x100;
    const bool crit = (avg < c->crit_low_x100) || (avg > c->crit_high_x100);
    const bool outside = (avg < c->t_low_x100) || (avg > c->t_high_x100);
    const bool inside_hyst = (avg >= c->t_low_x100 + c->hyst_x100) &&
                             (avg <= c->t_high_x100 - c->hyst_x100);
    const bool in_warn_zone = (avg < c->t_low_x100 + c->warn_margin_x100) ||
                              (avg > c->t_high_x100 - c->warn_margin_x100);
    const bool inside_warn_clear =
        (avg >= c->t_low_x100 + c->warn_margin_x100 + c->hyst_x100) &&
        (avg <= c->t_high_x100 - c->warn_margin_x100 - c->hyst_x100);

    /* Out-of-band timer: starts when outside, resets only after returning inside by the
     * hysteresis, so hovering at a limit still counts as "outside". */
    if (outside) {
        if (!a->out_active) {
            a->out_active = true;
            a->out_since_s = now_s;
        }
    } else if (inside_hyst) {
        a->out_active = false;
    }

    if (a->state == ALARM_STATE_ALARM) {
        if (!crit && inside_hyst) {
            a->out_active = false;
            a->muted = false;
            a->state = inside_warn_clear ? ALARM_STATE_NORMAL : ALARM_STATE_WARNING;
        }
        return;
    }

    if (crit || (a->out_active && (uint32_t)(now_s - a->out_since_s) >= c->alarm_hold_s)) {
        enter_alarm(a);
    } else if (a->out_active || in_warn_zone) {
        a->state = ALARM_STATE_WARNING;
    } else if (a->state == ALARM_STATE_WARNING && !inside_warn_clear) {
        /* inside the warning hysteresis band: stay in WARNING */
    } else {
        a->state = ALARM_STATE_NORMAL;
    }
}

void alarm_tick(alarm_t *a, uint32_t now_s)
{
    if (a->muted && (uint32_t)(now_s - a->mute_since_s) >= a->cfg.mute_timeout_s) {
        a->muted = false;
    }
}

bool alarm_mute(alarm_t *a, uint32_t now_s)
{
    if (a->state == ALARM_STATE_ALARM && !a->muted) {
        a->muted = true;
        a->mute_since_s = now_s;
        return true;
    }
    return false;
}

bool alarm_buzzer_on(const alarm_t *a)
{
    return a->state == ALARM_STATE_ALARM && !a->muted;
}

led_mode_t alarm_led_mode(const alarm_t *a)
{
    switch (a->state) {
    case ALARM_STATE_ALARM:
        return LED_SOLID;
    case ALARM_STATE_WARNING:
        return LED_BLINK;
    default:
        return LED_OFF;
    }
}

bool alarm_sensor_fault(const alarm_t *a)
{
    return a->sensor_fault;
}

alarm_state_t alarm_state(const alarm_t *a)
{
    return a->state;
}
