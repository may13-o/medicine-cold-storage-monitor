/*
 * record_queue.h - FIFO store-and-forward queue for the gateway (portable C).
 *
 * When full, it drops the oldest NORMAL record first so alarm/warning history survives an
 * outage; only if every record is an alarm/warning does it drop the oldest overall.
 */
#ifndef RECORD_QUEUE_H
#define RECORD_QUEUE_H

#include <stdbool.h>
#include <stdint.h>

#ifndef GW_QUEUE_CAPACITY
#define GW_QUEUE_CAPACITY 2048u   /* 20 bytes each, about 40 KB */
#endif

typedef enum {
    TS_SRC_NONE = 0,     /* no trusted time: cloud should use its receive time */
    TS_SRC_RTC = 1,      /* stamped by the node RTC */
    TS_SRC_GATEWAY = 2   /* stamped by the gateway (SNTP) */
} ts_src_t;

typedef struct {
    uint32_t epoch;
    uint32_t seq;
    uint32_t queued_ms;  /* gateway millis() when queued */
    int16_t  temp_x100;
    uint16_t rh_x100;
    uint8_t  state;
    uint8_t  fault;
    uint8_t  muted;
    uint8_t  ts_src;
} gw_record_t;

typedef struct {
    gw_record_t items[GW_QUEUE_CAPACITY];
    uint16_t    head;
    uint16_t    count;
    uint32_t    dropped;
} record_queue_t;

void   rq_init(record_queue_t *q);
size_t rq_count(const record_queue_t *q);

/* Returns true if an older record had to be dropped to make room. */
bool rq_push(record_queue_t *q, const gw_record_t *r);

/* Copies the oldest record without removing it. */
bool rq_peek(const record_queue_t *q, gw_record_t *out);

/* Removes the oldest record. Call only after it was published successfully. */
void rq_pop(record_queue_t *q);

#endif /* RECORD_QUEUE_H */
