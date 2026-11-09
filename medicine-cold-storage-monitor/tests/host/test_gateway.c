#include <string.h>

#include "check.h"
#include "gw_core.h"
#include "record_queue.h"

/* ------------------------------------------------------------ mock I/O */

static uint32_t m_now;
static uint32_t m_utc;
static bool m_net_up;
static bool m_publish_ok;
static char m_topics[64][64];
static char m_payloads[64][256];
static int m_pub_count;
static uint8_t m_tx[512];
static size_t m_tx_len;

static bool io_net_is_up(void) { return m_net_up; }
static uint32_t io_millis(void) { return m_now; }
static uint32_t io_utc(void) { return m_utc; }

static bool io_publish(const char *topic, const char *payload)
{
    if (!m_publish_ok) {
        return false;
    }
    if (m_pub_count < 64) {
        strncpy(m_topics[m_pub_count], topic, 63);
        strncpy(m_payloads[m_pub_count], payload, 255);
        m_pub_count++;
    }
    return true;
}

static void io_uart_write(const uint8_t *d, size_t n)
{
    if (m_tx_len + n <= sizeof m_tx) {
        memcpy(m_tx + m_tx_len, d, n);
        m_tx_len += n;
    }
}

static gw_core_t gw;

static void reset_gw(gw_format_t fmt)
{
    gw_config_t cfg;
    gw_config_default(&cfg);
    cfg.format = fmt;
    gw_io_t io = { io_net_is_up, io_publish, io_uart_write, io_utc, io_millis };
    gw_core_init(&gw, &cfg, &io);
    m_now = 100000u;
    m_utc = 0;
    m_net_up = false;
    m_publish_ok = true;
    m_pub_count = 0;
    m_tx_len = 0;
}

static void node_sends(uint32_t seq, uint32_t epoch, int16_t temp, uint8_t state, uint8_t fault, uint8_t muted)
{
    telemetry_t t = { seq, epoch, temp, 6120u, state, fault, muted };
    uint8_t payload[TELEMETRY_WIRE_LEN];
    uint8_t frame[FRAME_MAX_LEN];
    telemetry_pack(&t, payload);
    int n = frame_encode(MSG_TELEMETRY, payload, TELEMETRY_WIRE_LEN, frame, sizeof frame);
    for (int i = 0; i < n; i++) {
        gw_core_on_byte(&gw, frame[i]);
    }
}

/* Parse everything the gateway wrote to the node. */
static int count_tx(uint8_t type, uint32_t *last_time_epoch)
{
    frame_parser_t p;
    int count = 0;
    frame_parser_init(&p);
    for (size_t i = 0; i < m_tx_len; i++) {
        if (frame_parser_feed(&p, m_tx[i], 0u) && p.frame.msg_type == type) {
            count++;
            if (type == MSG_CMD_TIME && p.frame.len == 4u && last_time_epoch != NULL) {
                *last_time_epoch = (uint32_t)p.frame.payload[0] | ((uint32_t)p.frame.payload[1] << 8) |
                                   ((uint32_t)p.frame.payload[2] << 16) | ((uint32_t)p.frame.payload[3] << 24);
            }
        }
    }
    return count;
}

static void queue_tests(void)
{
    static record_queue_t q;
    gw_record_t r;
    rq_init(&q);

    memset(&r, 0, sizeof r);
    for (uint32_t i = 0; i < GW_QUEUE_CAPACITY; i++) {
        r.seq = i;
        r.state = (i == 0u) ? (uint8_t)ALARM_STATE_ALARM : (uint8_t)ALARM_STATE_NORMAL;
        CHECK(!rq_push(&q, &r));
    }
    CHECK_EQ(rq_count(&q), GW_QUEUE_CAPACITY);

    /* Full: the oldest NORMAL record (seq 1) is dropped, the old ALARM (seq 0) survives. */
    r.seq = 5000u;
    r.state = ALARM_STATE_NORMAL;
    CHECK(rq_push(&q, &r));
    CHECK_EQ(rq_count(&q), GW_QUEUE_CAPACITY);
    CHECK_EQ(q.dropped, 1);

    gw_record_t out;
    CHECK(rq_peek(&q, &out));
    CHECK_EQ(out.seq, 0);
    rq_pop(&q);
    CHECK(rq_peek(&q, &out));
    CHECK_EQ(out.seq, 2);  /* seq 1 was dropped, order preserved */

    /* Drain completely and confirm FIFO order and empty behaviour. */
    uint32_t prev = out.seq;
    while (rq_count(&q) > 1u) {
        rq_pop(&q);
        rq_peek(&q, &out);
        CHECK(out.seq > prev);
        prev = out.seq;
    }
    CHECK_EQ(out.seq, 5000);
    rq_pop(&q);
    CHECK(!rq_peek(&q, &out));
    rq_pop(&q);  /* popping an empty queue is harmless */
    CHECK_EQ(rq_count(&q), 0);

    /* All ALARM records: the oldest overall is dropped. */
    rq_init(&q);
    r.state = ALARM_STATE_ALARM;
    for (uint32_t i = 0; i < GW_QUEUE_CAPACITY; i++) {
        r.seq = i;
        rq_push(&q, &r);
    }
    r.seq = 999u;
    CHECK(rq_push(&q, &r));
    rq_peek(&q, &out);
    CHECK_EQ(out.seq, 1);
}

int main(void)
{
    queue_tests();

    /* ---------- JSON formats ---------- */
    char buf[256];
    gw_record_t rec = { 1794096000u, 1042u, 0u, 445, 6120u, ALARM_STATE_NORMAL, 0, 0, TS_SRC_RTC };
    gw_config_t cfg;
    gw_config_default(&cfg);

    CHECK(gw_format_payload(&cfg, &rec, false, buf, sizeof buf) > 0);
    CHECK(strcmp(buf,
        "{\"node\":\"node1\",\"ts\":1794096000,\"ts_src\":\"rtc\",\"temp\":4.45,\"rh\":61.20,"
        "\"state\":\"NORMAL\",\"fault\":0,\"muted\":false,\"seq\":1042,\"buffered\":false}") == 0);

    rec.temp_x100 = -15;
    rec.state = ALARM_STATE_ALARM;
    rec.fault = 3;
    rec.muted = 1;
    rec.ts_src = TS_SRC_GATEWAY;
    CHECK(gw_format_payload(&cfg, &rec, true, buf, sizeof buf) > 0);
    CHECK(strstr(buf, "\"temp\":-0.15") != NULL);
    CHECK(strstr(buf, "\"state\":\"ALARM\"") != NULL);
    CHECK(strstr(buf, "\"muted\":true") != NULL);
    CHECK(strstr(buf, "\"buffered\":true") != NULL);
    CHECK(strstr(buf, "\"ts_src\":\"gateway\"") != NULL);

    cfg.format = GW_FMT_THINGSBOARD;
    rec.temp_x100 = 445;
    rec.state = ALARM_STATE_NORMAL;
    rec.fault = 0;
    rec.muted = 0;
    CHECK(gw_format_payload(&cfg, &rec, false, buf, sizeof buf) > 0);
    CHECK(strcmp(buf,
        "{\"ts\":1794096000000,\"values\":{\"temp\":4.45,\"rh\":61.20,\"state\":\"NORMAL\","
        "\"fault\":0,\"muted\":false,\"seq\":1042,\"buffered\":false}}") == 0);
    rec.epoch = 0;  /* no trusted time: no ts wrapper */
    CHECK(gw_format_payload(&cfg, &rec, false, buf, sizeof buf) > 0);
    CHECK(buf[0] == '{' && strstr(buf, "\"ts\"") == NULL && strstr(buf, "\"values\"") == NULL);
    CHECK(gw_format_topic(&cfg, buf, sizeof buf) > 0);
    CHECK(strcmp(buf, "v1/devices/me/telemetry") == 0);
    cfg.format = GW_FMT_FLAT;
    CHECK(gw_format_topic(&cfg, buf, sizeof buf) > 0);
    CHECK(strcmp(buf, "coldstore/node1/telemetry") == 0);
    CHECK(gw_format_payload(&cfg, &rec, false, buf, 20) == -1);  /* truncation is reported */

    /* ---------- Store and forward with downsampling ---------- */
    reset_gw(GW_FMT_FLAT);
    /* Node sends every 10 s; routine records are kept once per 30 s. Network is down. */
    for (uint32_t i = 0; i < 13; i++) {  /* 120 s of data */
        node_sends(i, 1794096000u + i * 10u, 500, ALARM_STATE_NORMAL, 0, 0);
        m_now += 10000u;
    }
    /* First frame, then one per 30 s: frames 0, 3, 6, 9, 12 -> 5 records. */
    CHECK_EQ(rq_count(&gw.queue), 5);

    /* A state change is queued immediately, even mid-period. */
    node_sends(13u, 1794096000u + 130u, 900, ALARM_STATE_ALARM, 0, 0);
    CHECK_EQ(rq_count(&gw.queue), 6);

    /* Network comes back: everything uploads oldest first, marked buffered. */
    m_net_up = true;
    gw_core_poll(&gw);
    CHECK_EQ(m_pub_count, 6);
    CHECK_EQ(rq_count(&gw.queue), 0);
    CHECK(strstr(m_payloads[0], "\"seq\":0") != NULL);
    CHECK(strstr(m_payloads[0], "\"buffered\":true") != NULL);
    CHECK(strstr(m_payloads[5], "\"seq\":13") != NULL);
    CHECK(strstr(m_payloads[5], "\"state\":\"ALARM\"") != NULL);
    CHECK(strcmp(m_topics[0], "coldstore/node1/telemetry") == 0);

    /* Live record while online is not marked buffered. */
    m_pub_count = 0;
    node_sends(14u, 1794096000u + 140u, 900, ALARM_STATE_WARNING, 0, 0);
    gw_core_poll(&gw);
    CHECK_EQ(m_pub_count, 1);
    CHECK(strstr(m_payloads[0], "\"buffered\":false") != NULL);

    /* ---------- Publish failure keeps the record and retries ---------- */
    reset_gw(GW_FMT_FLAT);
    m_net_up = true;
    m_publish_ok = false;
    node_sends(0u, 1794096000u, 500, ALARM_STATE_NORMAL, 0, 0);
    gw_core_poll(&gw);
    CHECK_EQ(rq_count(&gw.queue), 1);
    CHECK_EQ(gw.publish_failures, 1);
    m_publish_ok = true;
    gw_core_poll(&gw);
    CHECK_EQ(rq_count(&gw.queue), 0);
    CHECK_EQ(m_pub_count, 1);

    /* ---------- Sequence gaps and node restarts ---------- */
    reset_gw(GW_FMT_FLAT);
    node_sends(1u, 1794096000u, 500, ALARM_STATE_NORMAL, 0, 0);
    node_sends(2u, 1794096010u, 500, ALARM_STATE_NORMAL, 0, 0);
    node_sends(5u, 1794096040u, 500, ALARM_STATE_NORMAL, 0, 0);
    CHECK_EQ(gw.missed_frames, 2);
    node_sends(0u, 1794096050u, 500, ALARM_STATE_NORMAL, 0, 0);
    CHECK_EQ(gw.node_restarts, 1);
    CHECK_EQ(gw.frames_seen, 4);

    /* ---------- Corrupted frame is ignored ---------- */
    reset_gw(GW_FMT_FLAT);
    {
        telemetry_t t = { 1u, 1794096000u, 500, 6000u, 0, 0, 0 };
        uint8_t payload[TELEMETRY_WIRE_LEN];
        uint8_t frame[FRAME_MAX_LEN];
        telemetry_pack(&t, payload);
        int n = frame_encode(MSG_TELEMETRY, payload, TELEMETRY_WIRE_LEN, frame, sizeof frame);
        frame[7] ^= 0x01;
        for (int i = 0; i < n; i++) {
            gw_core_on_byte(&gw, frame[i]);
        }
    }
    CHECK_EQ(gw.frames_seen, 0);
    CHECK_EQ(rq_count(&gw.queue), 0);
    CHECK_EQ(gw.parser.crc_errors, 1);

    /* ---------- Time: gateway stamps when the node has no time ---------- */
    reset_gw(GW_FMT_FLAT);
    m_utc = 1794096123u;
    node_sends(0u, 0u, 500, ALARM_STATE_NORMAL, FAULT_RTC, 0);
    gw_record_t head;
    CHECK(rq_peek(&gw.queue, &head));
    CHECK_EQ(head.epoch, 1794096123u);
    CHECK_EQ(head.ts_src, TS_SRC_GATEWAY);
    CHECK(gw.node_needs_time);

    /* No SNTP yet and no node time: record is marked as having no trusted time. */
    reset_gw(GW_FMT_FLAT);
    m_utc = 0;
    node_sends(0u, 0u, 500, ALARM_STATE_NORMAL, FAULT_RTC, 0);
    rq_peek(&gw.queue, &head);
    CHECK_EQ(head.epoch, 0);
    CHECK_EQ(head.ts_src, TS_SRC_NONE);

    /* ---------- Heartbeat and time commands to the node ---------- */
    reset_gw(GW_FMT_FLAT);
    m_utc = 0;
    gw_core_poll(&gw);
    CHECK_EQ(count_tx(MSG_CMD_HB, NULL), 1);
    CHECK_EQ(count_tx(MSG_CMD_TIME, NULL), 0);   /* no SNTP yet */
    m_now += 5000u;
    gw_core_poll(&gw);
    CHECK_EQ(count_tx(MSG_CMD_HB, NULL), 1);     /* not due yet */
    m_now += 5000u;
    gw_core_poll(&gw);
    CHECK_EQ(count_tx(MSG_CMD_HB, NULL), 2);

    m_utc = 1794096200u;                          /* SNTP syncs */
    gw_core_poll(&gw);
    uint32_t sent_epoch = 0;
    CHECK_EQ(count_tx(MSG_CMD_TIME, &sent_epoch), 1);
    CHECK_EQ(sent_epoch, 1794096200u);
    m_now += 1000u;
    gw_core_poll(&gw);
    CHECK_EQ(count_tx(MSG_CMD_TIME, NULL), 1);   /* not repeated */

    /* A node that reports an invalid RTC gets the time again after the retry interval. */
    node_sends(1u, 0u, 500, ALARM_STATE_NORMAL, FAULT_RTC, 0);
    m_now += GW_TIME_RETRY_MS;
    gw_core_poll(&gw);
    CHECK_EQ(count_tx(MSG_CMD_TIME, NULL), 2);

    FINISH("test_gateway");
}
