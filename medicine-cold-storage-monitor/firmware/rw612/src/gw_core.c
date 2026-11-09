#include "gw_core.h"

#include <stdio.h>
#include <string.h>

/* ------------------------------------------------------------ formatting */

static const char *state_name(uint8_t s)
{
    switch (s) {
    case ALARM_STATE_NORMAL:
        return "NORMAL";
    case ALARM_STATE_WARNING:
        return "WARNING";
    case ALARM_STATE_ALARM:
        return "ALARM";
    default:
        return "UNKNOWN";
    }
}

static const char *ts_src_name(uint8_t s)
{
    switch (s) {
    case TS_SRC_RTC:
        return "rtc";
    case TS_SRC_GATEWAY:
        return "gateway";
    default:
        return "none";
    }
}

/* Integer-only formatting: no floating-point printf needed. */
static void fmt_temp(char *out, size_t n, int16_t t_x100)
{
    int32_t v = t_x100;
    const char *sign = "";
    if (v < 0) {
        sign = "-";
        v = -v;
    }
    snprintf(out, n, "%s%ld.%02ld", sign, (long)(v / 100), (long)(v % 100));
}

int gw_format_topic(const gw_config_t *cfg, char *out, size_t n)
{
    int w;
    if (cfg->format == GW_FMT_THINGSBOARD) {
        w = snprintf(out, n, "v1/devices/me/telemetry");
    } else {
        w = snprintf(out, n, "coldstore/%s/telemetry", cfg->node_id);
    }
    return (w < 0 || (size_t)w >= n) ? -1 : w;
}

int gw_format_payload(const gw_config_t *cfg, const gw_record_t *r, bool buffered, char *out, size_t n)
{
    char temp[12];
    int w;

    fmt_temp(temp, sizeof temp, r->temp_x100);

    if (cfg->format == GW_FMT_THINGSBOARD) {
        /* ThingsBoard wants "ts" in milliseconds. Appending "000" to epoch seconds avoids
         * 64-bit printf. Without a trusted time, omit ts so the server stamps it. */
        char head[40] = "";  /* {"ts":<10 digits>000,"values": is 29 chars + NUL */
        const char *tail = "";
        if (r->epoch != 0u) {
            snprintf(head, sizeof head, "{\"ts\":%lu000,\"values\":", (unsigned long)r->epoch);
            tail = "}";
        }
        w = snprintf(out, n,
                     "%s{\"temp\":%s,\"rh\":%u.%02u,\"state\":\"%s\",\"fault\":%u,"
                     "\"muted\":%s,\"seq\":%lu,\"buffered\":%s}%s",
                     head, temp, (unsigned)(r->rh_x100 / 100u), (unsigned)(r->rh_x100 % 100u),
                     state_name(r->state), (unsigned)r->fault, r->muted ? "true" : "false",
                     (unsigned long)r->seq, buffered ? "true" : "false", tail);
    } else {
        w = snprintf(out, n,
                     "{\"node\":\"%s\",\"ts\":%lu,\"ts_src\":\"%s\",\"temp\":%s,\"rh\":%u.%02u,"
                     "\"state\":\"%s\",\"fault\":%u,\"muted\":%s,\"seq\":%lu,\"buffered\":%s}",
                     cfg->node_id, (unsigned long)r->epoch, ts_src_name(r->ts_src), temp,
                     (unsigned)(r->rh_x100 / 100u), (unsigned)(r->rh_x100 % 100u),
                     state_name(r->state), (unsigned)r->fault, r->muted ? "true" : "false",
                     (unsigned long)r->seq, buffered ? "true" : "false");
    }
    return (w < 0 || (size_t)w >= n) ? -1 : w;
}

/* ------------------------------------------------------------------ core */

void gw_config_default(gw_config_t *cfg)
{
    cfg->node_id = "node1";
    cfg->format = GW_FMT_FLAT;
    cfg->publish_period_ms = 30000u;
    cfg->hb_period_ms = 10000u;
    cfg->time_resync_ms = 6u * 60u * 60u * 1000u;
}

void gw_core_init(gw_core_t *g, const gw_config_t *cfg, const gw_io_t *io)
{
    memset(g, 0, sizeof *g);
    g->cfg = *cfg;
    g->io = *io;
    frame_parser_init(&g->parser);
    rq_init(&g->queue);
}

static void send_frame(gw_core_t *g, uint8_t type, const uint8_t *payload, uint8_t len)
{
    uint8_t buf[FRAME_MAX_LEN];
    int n = frame_encode(type, payload, len, buf, sizeof buf);
    if (n > 0) {
        g->io.uart_write(buf, (size_t)n);
    }
}

static void on_telemetry(gw_core_t *g, const telemetry_t *t)
{
    const uint32_t now = g->io.millis();
    gw_record_t rec;

    g->frames_seen++;

    /* Sequence tracking: a gap means lost frames, a backwards step means the node rebooted. */
    if (g->have_prev_seq) {
        if (t->seq > g->prev_seq) {
            g->missed_frames += t->seq - g->prev_seq - 1u;
        } else if (t->seq != g->prev_seq) {
            g->node_restarts++;
        }
    }
    g->have_prev_seq = true;
    g->prev_seq = t->seq;

    /* Time: prefer the node RTC, fall back to gateway SNTP, else unknown. */
    g->node_needs_time = (t->epoch == 0u) || ((t->fault & FAULT_RTC) != 0u);
    rec.epoch = t->epoch;
    rec.ts_src = (uint8_t)TS_SRC_RTC;
    if (t->epoch == 0u) {
        uint32_t utc = g->io.utc_now();
        rec.epoch = utc;
        rec.ts_src = (utc != 0u) ? (uint8_t)TS_SRC_GATEWAY : (uint8_t)TS_SRC_NONE;
    }

    rec.seq = t->seq;
    rec.queued_ms = now;
    rec.temp_x100 = t->temp_x100;
    rec.rh_x100 = t->rh_x100;
    rec.state = t->state;
    rec.fault = t->fault;
    rec.muted = t->muted;

    /* Downsample: queue immediately on any change, otherwise once per publish period. */
    bool changed = !g->have_last || t->state != g->last_state || t->fault != g->last_fault ||
                   t->muted != g->last_muted;
    bool due = (uint32_t)(now - g->last_queue_ms) >= g->cfg.publish_period_ms;

    if (changed || due) {
        rq_push(&g->queue, &rec);
        g->last_queue_ms = now;
    }
    g->have_last = true;
    g->last_state = t->state;
    g->last_fault = t->fault;
    g->last_muted = t->muted;
}

void gw_core_on_byte(gw_core_t *g, uint8_t byte)
{
    if (frame_parser_feed(&g->parser, byte, g->io.millis())) {
        telemetry_t t;
        if (g->parser.frame.msg_type == MSG_TELEMETRY &&
            telemetry_unpack(g->parser.frame.payload, g->parser.frame.len, &t)) {
            on_telemetry(g, &t);
        }
    }
}

void gw_core_poll(gw_core_t *g)
{
    const uint32_t now = g->io.millis();

    /* Heartbeat so the node can tell the gateway is alive. */
    if (!g->hb_sent || (uint32_t)(now - g->last_hb_ms) >= g->cfg.hb_period_ms) {
        send_frame(g, (uint8_t)MSG_CMD_HB, NULL, 0u);
        g->hb_sent = true;
        g->last_hb_ms = now;
    }

    /* Time sync: once SNTP has a time, at start, periodically, and whenever the node asks. */
    uint32_t utc = g->io.utc_now();
    if (utc != 0u) {
        uint32_t since = (uint32_t)(now - g->last_time_ms);
        bool due = !g->time_sent || since >= g->cfg.time_resync_ms ||
                   (g->node_needs_time && since >= GW_TIME_RETRY_MS);
        if (due) {
            uint8_t p[4];
            p[0] = (uint8_t)(utc & 0xFFu);
            p[1] = (uint8_t)((utc >> 8) & 0xFFu);
            p[2] = (uint8_t)((utc >> 16) & 0xFFu);
            p[3] = (uint8_t)(utc >> 24);
            send_frame(g, (uint8_t)MSG_CMD_TIME, p, 4u);
            g->time_sent = true;
            g->last_time_ms = now;
        }
    }

    /* Drain the queue oldest-first. A record is removed only after a successful publish. */
    for (unsigned i = 0; i < GW_MAX_PUBLISH_PER_POLL && g->io.net_is_up(); i++) {
        gw_record_t rec;
        char topic[64];
        char payload[256];

        if (!rq_peek(&g->queue, &rec)) {
            break;
        }
        bool buffered = (uint32_t)(now - rec.queued_ms) > GW_BUFFERED_AFTER_MS;
        if (gw_format_topic(&g->cfg, topic, sizeof topic) < 0 ||
            gw_format_payload(&g->cfg, &rec, buffered, payload, sizeof payload) < 0) {
            rq_pop(&g->queue);  /* cannot be formatted: drop rather than block the queue forever */
            continue;
        }
        if (!g->io.net_publish(topic, payload)) {
            g->publish_failures++;
            break;  /* retry on the next poll */
        }
        rq_pop(&g->queue);
        g->published++;
    }
}
