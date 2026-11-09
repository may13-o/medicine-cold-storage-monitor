/*
 * alarm.h - alarm state machine for the cold-storage node (portable C, no hardware).
 * Behaviour is specified in docs/state-machine.md. All temperatures are degrees C * 100.
 * Times are monotonic seconds since boot (NOT the RTC).
 */
#ifndef ALARM_H
#define ALARM_H

#include <stdbool.h>
#include <stdint.h>

#include "protocol.h"

#define ALARM_FILTER_N 3u

typedef enum {
    LED_OFF = 0,
    LED_BLINK,
    LED_SOLID
} led_mode_t;

typedef struct {
    int16_t  t_low_x100;        /* allowed band, default 200  (2.0 C) */
    int16_t  t_high_x100;       /*                default 800  (8.0 C) */
    int16_t  warn_margin_x100;  /* warning zone inside each limit, default 50 */
    int16_t  hyst_x100;         /* must return this far inside to clear, default 30 */
    int16_t  crit_low_x100;     /* immediate alarm below this, default 0 */
    int16_t  crit_high_x100;    /* immediate alarm above this, default 1000 */
    uint32_t alarm_hold_s;      /* time outside band before alarming, default 120 */
    uint8_t  fault_debounce;    /* consecutive sensor failures before fault, default 3 */
    uint32_t mute_timeout_s;    /* mute expires after this, default 900 */
} alarm_config_t;

typedef struct {
    alarm_config_t cfg;
    alarm_state_t  state;
    int16_t        samples[ALARM_FILTER_N];
    uint8_t        n_samples;
    uint8_t        next;
    int16_t        filtered_x100;
    uint8_t        fail_count;
    bool           sensor_fault;
    bool           out_active;
    uint32_t       out_since_s;
    bool           muted;
    uint32_t       mute_since_s;
} alarm_t;

void alarm_config_default(alarm_config_t *cfg);
void alarm_init(alarm_t *a, const alarm_config_t *cfg);

/* Call once per sample. If sensor_ok is false, temp_x100 is ignored. */
void alarm_update(alarm_t *a, uint32_t now_s, bool sensor_ok, int16_t temp_x100);

/* Call often (every loop): expires the mute. */
void alarm_tick(alarm_t *a, uint32_t now_s);

/* Silences the buzzer. Only has an effect in ALARM. Returns true if the mute state changed. */
bool alarm_mute(alarm_t *a, uint32_t now_s);

bool         alarm_buzzer_on(const alarm_t *a);
led_mode_t   alarm_led_mode(const alarm_t *a);
bool         alarm_sensor_fault(const alarm_t *a);
alarm_state_t alarm_state(const alarm_t *a);

#endif /* ALARM_H */
