/*
 * gw_core.h - gateway logic (portable C): receive node frames, detect gaps, downsample,
 * store-and-forward, heartbeat and time sync to the node, JSON formatting.
 *
 * All hardware and network access goes through gw_io_t, so this is tested on a PC.
 * Wi-Fi, SNTP and MQTT/TLS are implemented in src/board_rw612.c (stub today).
 */
#ifndef GW_CORE_H
#define GW_CORE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "protocol.h"
#include "record_queue.h"

#define GW_TIME_RETRY_MS         10000u
#define GW_MAX_PUBLISH_PER_POLL  8u
#define GW_BUFFERED_AFTER_MS     5000u   /* queued longer than this => "buffered": true */

typedef enum {
    GW_FMT_FLAT = 0,        /* payload described in docs/cloud.md */
    GW_FMT_THINGSBOARD = 1  /* {"ts":<ms>,"values":{...}} on v1/devices/me/telemetry */
} gw_format_t;

typedef struct {
    bool     (*net_is_up)(void);
    /* Return true only once the message was accepted (for example MQTT publish acknowledged). */
    bool     (*net_publish)(const char *topic, const char *payload);
    void     (*uart_write)(const uint8_t *data, size_t len);
    uint32_t (*utc_now)(void);   /* SNTP-synced UTC epoch seconds, 0 until synced */
    uint32_t (*millis)(void);
} gw_io_t;

typedef struct {
    const char *node_id;
    gw_format_t format;
    uint32_t    publish_period_ms;  /* downsample: one routine record per period */
    uint32_t    hb_period_ms;
    uint32_t    time_resync_ms;
} gw_config_t;

typedef struct {
    gw_config_t    cfg;
    gw_io_t        io;
    frame_parser_t parser;
    record_queue_t queue;

    bool     have_prev_seq;
    uint32_t prev_seq;
    uint32_t missed_frames;
    uint32_t node_restarts;

    bool     have_last;
    uint8_t  last_state, last_fault, last_muted;
    uint32_t last_queue_ms;

    bool     hb_sent;
    uint32_t last_hb_ms;
    bool     time_sent;
    uint32_t last_time_ms;
    bool     node_needs_time;

    uint32_t frames_seen;
    uint32_t published;
    uint32_t publish_failures;
} gw_core_t;

void gw_config_default(gw_config_t *cfg);
void gw_core_init(gw_core_t *g, const gw_config_t *cfg, const gw_io_t *io);

/* Feed every byte received from the node UART. */
void gw_core_on_byte(gw_core_t *g, uint8_t byte);

/* Call regularly from the main loop (every 100 ms is plenty). */
void gw_core_poll(gw_core_t *g);

/* Exposed for tests and for the board layer. Return the snprintf length, or -1 if truncated. */
int gw_format_topic(const gw_config_t *cfg, char *out, size_t n);
int gw_format_payload(const gw_config_t *cfg, const gw_record_t *r, bool buffered, char *out, size_t n);

#endif /* GW_CORE_H */
