#include "alarm.h"
#include "check.h"

#define STEP_S 10u

static alarm_t a;
static uint32_t now_s;

static void reset(void)
{
    alarm_config_t cfg;
    alarm_config_default(&cfg);
    alarm_init(&a, &cfg);
    now_s = 1000u;
}

/* Feed n samples of a constant temperature, one every STEP_S. */
static void feed(int16_t temp_x100, int n)
{
    for (int i = 0; i < n; i++) {
        alarm_update(&a, now_s, true, temp_x100);
        alarm_tick(&a, now_s);
        now_s += STEP_S;
    }
}

static void feed_fail(int n)
{
    for (int i = 0; i < n; i++) {
        alarm_update(&a, now_s, false, 0);
        alarm_tick(&a, now_s);
        now_s += STEP_S;
    }
}

int main(void)
{
    /* 1. Steady 5 C stays NORMAL, no outputs. */
    reset();
    feed(500, 30);
    CHECK_EQ(alarm_state(&a), ALARM_STATE_NORMAL);
    CHECK(!alarm_buzzer_on(&a));
    CHECK_EQ(alarm_led_mode(&a), LED_OFF);

    /* 2. Inside the band but near a limit: WARNING, LED blinks, buzzer silent. */
    reset();
    feed(770, 10);
    CHECK_EQ(alarm_state(&a), ALARM_STATE_WARNING);
    CHECK_EQ(alarm_led_mode(&a), LED_BLINK);
    CHECK(!alarm_buzzer_on(&a));
    feed(230, 10);  /* near the low limit */
    CHECK_EQ(alarm_state(&a), ALARM_STATE_WARNING);

    /* 3. Door open: 9 C for 60 s then recovery must NOT alarm. */
    reset();
    feed(500, 10);
    feed(900, 6);
    CHECK(alarm_state(&a) != ALARM_STATE_ALARM);
    feed(500, 10);
    CHECK_EQ(alarm_state(&a), ALARM_STATE_NORMAL);

    /* 4. Sustained 9 C: alarms after the hold time (plus up to a few filter samples). */
    reset();
    feed(500, 10);
    uint32_t start = now_s;
    uint32_t alarm_at = 0;
    for (int i = 0; i < 30; i++) {
        feed(900, 1);
        if (alarm_at == 0 && alarm_state(&a) == ALARM_STATE_ALARM) {
            alarm_at = now_s - STEP_S;
        }
    }
    CHECK(alarm_at != 0);
    CHECK(alarm_at - start >= 120u);
    CHECK(alarm_at - start <= 170u);
    CHECK(alarm_buzzer_on(&a));
    CHECK_EQ(alarm_led_mode(&a), LED_SOLID);

    /* 5. Critical overheat alarms within the filter window, far faster than the hold time. */
    reset();
    feed(500, 10);
    start = now_s;
    int guard = 0;
    while (alarm_state(&a) != ALARM_STATE_ALARM && guard++ < 10) {
        feed(1100, 1);
    }
    CHECK(alarm_state(&a) == ALARM_STATE_ALARM);
    CHECK(now_s - start <= 40u);

    /* 6. Freezing: below 0 C is critical. */
    reset();
    feed(500, 10);
    guard = 0;
    while (alarm_state(&a) != ALARM_STATE_ALARM && guard++ < 10) {
        feed(-100, 1);
    }
    CHECK(alarm_state(&a) == ALARM_STATE_ALARM);

    /* 7. Mute silences the buzzer, keeps the LED and the ALARM state, and expires. */
    CHECK(alarm_mute(&a, now_s));
    CHECK(!alarm_buzzer_on(&a));
    CHECK_EQ(alarm_led_mode(&a), LED_SOLID);
    CHECK_EQ(alarm_state(&a), ALARM_STATE_ALARM);
    CHECK(!alarm_mute(&a, now_s));  /* already muted */
    feed(-100, 80);                 /* 800 s later: still muted */
    CHECK(!alarm_buzzer_on(&a));
    feed(-100, 15);                 /* past the 900 s timeout */
    CHECK(alarm_buzzer_on(&a));

    /* 8. Recovery clears the alarm and the mute. */
    alarm_mute(&a, now_s);
    feed(500, 6);
    CHECK_EQ(alarm_state(&a), ALARM_STATE_NORMAL);
    CHECK(!a.muted);
    CHECK(!alarm_buzzer_on(&a));

    /* 9. Mute has no effect outside ALARM. */
    CHECK(!alarm_mute(&a, now_s));

    /* 10. Hysteresis: once in ALARM at 8.5 C, 7.9 C does not clear it, 7.6 C does. */
    reset();
    feed(500, 10);
    feed(850, 25);
    CHECK_EQ(alarm_state(&a), ALARM_STATE_ALARM);
    feed(790, 10);
    CHECK_EQ(alarm_state(&a), ALARM_STATE_ALARM);
    feed(760, 10);
    CHECK_EQ(alarm_state(&a), ALARM_STATE_WARNING);  /* inside band but within warning margin */
    feed(500, 10);
    CHECK_EQ(alarm_state(&a), ALARM_STATE_NORMAL);

    /* 11. Sensor fault is debounced: two failures are ignored, the third alarms. */
    reset();
    feed(500, 10);
    feed_fail(2);
    CHECK_EQ(alarm_state(&a), ALARM_STATE_NORMAL);
    CHECK(!alarm_sensor_fault(&a));
    feed_fail(1);
    CHECK_EQ(alarm_state(&a), ALARM_STATE_ALARM);
    CHECK(alarm_sensor_fault(&a));
    CHECK(alarm_buzzer_on(&a));

    /* 12. A good read in between resets the failure count. */
    reset();
    feed(500, 10);
    feed_fail(2);
    feed(500, 1);
    feed_fail(2);
    CHECK(!alarm_sensor_fault(&a));
    CHECK_EQ(alarm_state(&a), ALARM_STATE_NORMAL);

    /* 13. Sensor returns: fault clears and the node recovers with a fresh filter. */
    reset();
    feed(500, 10);
    feed_fail(5);
    CHECK(alarm_sensor_fault(&a));
    feed(500, 1);
    CHECK(!alarm_sensor_fault(&a));
    CHECK_EQ(alarm_state(&a), ALARM_STATE_NORMAL);

    /* 14. Hovering just over the limit still alarms (timer does not reset on tiny dips). */
    reset();
    feed(500, 10);
    int16_t pattern[] = { 810, 790, 810, 790 };
    for (int i = 0; i < 40; i++) {
        feed(pattern[i % 4], 1);
    }
    CHECK_EQ(alarm_state(&a), ALARM_STATE_ALARM);

    FINISH("test_alarm");
}
