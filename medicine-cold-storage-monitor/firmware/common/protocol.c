#include "protocol.h"

#include <string.h>

uint8_t proto_crc8(const uint8_t *data, size_t len)
{
    uint8_t crc = 0x00;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int b = 0; b < 8; b++) {
            crc = (crc & 0x80u) ? (uint8_t)((crc << 1) ^ 0x07u) : (uint8_t)(crc << 1);
        }
    }
    return crc;
}

static void put16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)(v >> 8);
}

static void put32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)((v >> 8) & 0xFFu);
    p[2] = (uint8_t)((v >> 16) & 0xFFu);
    p[3] = (uint8_t)(v >> 24);
}

static uint16_t get16(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t get32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

size_t telemetry_pack(const telemetry_t *t, uint8_t out[TELEMETRY_WIRE_LEN])
{
    put32(&out[0], t->seq);
    put32(&out[4], t->epoch);
    put16(&out[8], (uint16_t)t->temp_x100);
    put16(&out[10], t->rh_x100);
    out[12] = t->state;
    out[13] = t->fault;
    out[14] = t->muted;
    return TELEMETRY_WIRE_LEN;
}

bool telemetry_unpack(const uint8_t *in, size_t len, telemetry_t *t)
{
    if (len != TELEMETRY_WIRE_LEN || in[12] > (uint8_t)ALARM_STATE_ALARM) {
        return false;
    }
    t->seq       = get32(&in[0]);
    t->epoch     = get32(&in[4]);
    t->temp_x100 = (int16_t)get16(&in[8]);
    t->rh_x100   = get16(&in[10]);
    t->state     = in[12];
    t->fault     = in[13];
    t->muted     = in[14];
    return true;
}

int frame_encode(uint8_t msg_type, const uint8_t *payload, uint8_t payload_len,
                 uint8_t *out, size_t out_max)
{
    if (payload_len > FRAME_MAX_PAYLOAD || out_max < (size_t)payload_len + FRAME_OVERHEAD) {
        return -1;
    }
    out[0] = (uint8_t)FRAME_SOF;
    out[1] = msg_type;
    out[2] = payload_len;
    if (payload_len > 0u && payload != NULL) {
        memcpy(&out[3], payload, payload_len);
    }
    out[3 + payload_len] = proto_crc8(&out[1], (size_t)payload_len + 2u);
    out[4 + payload_len] = (uint8_t)FRAME_EOF;
    return (int)payload_len + (int)FRAME_OVERHEAD;
}

enum { ST_SOF = 0, ST_TYPE, ST_LEN, ST_PAYLOAD, ST_CRC, ST_EOF };

void frame_parser_init(frame_parser_t *p)
{
    memset(p, 0, sizeof *p);
    p->state = ST_SOF;
}

bool frame_parser_feed(frame_parser_t *p, uint8_t byte, uint32_t now_ms)
{
    /* Abandon a half-received frame if the line went quiet mid-frame. */
    if (p->state != ST_SOF && (uint32_t)(now_ms - p->last_byte_ms) > FRAME_INTERBYTE_TIMEOUT_MS) {
        p->state = ST_SOF;
        p->resyncs++;
    }
    p->last_byte_ms = now_ms;

    switch (p->state) {
    case ST_SOF:
        if (byte == FRAME_SOF) {
            p->state = ST_TYPE;
        }
        break;
    case ST_TYPE:
        p->frame.msg_type = byte;
        p->state = ST_LEN;
        break;
    case ST_LEN:
        if (byte > FRAME_MAX_PAYLOAD) {
            p->state = ST_SOF;
            p->resyncs++;
        } else {
            p->frame.len = byte;
            p->index = 0;
            p->state = (byte == 0u) ? ST_CRC : ST_PAYLOAD;
        }
        break;
    case ST_PAYLOAD:
        p->frame.payload[p->index++] = byte;
        if (p->index >= p->frame.len) {
            p->state = ST_CRC;
        }
        break;
    case ST_CRC:
        p->crc_rx = byte;
        p->state = ST_EOF;
        break;
    case ST_EOF:
        p->state = ST_SOF;
        if (byte == FRAME_EOF) {
            uint8_t buf[2 + FRAME_MAX_PAYLOAD];
            buf[0] = p->frame.msg_type;
            buf[1] = p->frame.len;
            memcpy(&buf[2], p->frame.payload, p->frame.len);
            if (proto_crc8(buf, (size_t)p->frame.len + 2u) == p->crc_rx) {
                p->frames_ok++;
                return true;
            }
        }
        p->crc_errors++;
        break;
    default:
        p->state = ST_SOF;
        break;
    }
    return false;
}
