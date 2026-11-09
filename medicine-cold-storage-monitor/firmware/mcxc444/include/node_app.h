/*
 * node_app.h - application logic of the cold-storage node (portable, HAL-based).
 */
#ifndef NODE_APP_H
#define NODE_APP_H

#include <stdbool.h>
#include <stdint.h>

#include "alarm.h"
#include "protocol.h"

typedef struct {
    alarm_t        alarm;
    frame_parser_t parser;
    uint32_t       seq;
    uint32_t       next_sample_ms;
    uint32_t       last_hb_ms;
    bool           have_reading;
    int16_t        last_temp_x100;
    uint16_t       last_rh_x100;
    bool           rtc_ok;
    uint32_t       epoch;      /* last good RTC epoch, 0 if invalid */
    bool           oled_ok;
} node_t;

void node_init(node_t *n);

/* Call from the main loop as often as you like (every few ms). Never blocks for long. */
void node_step(node_t *n);

#endif /* NODE_APP_H */
