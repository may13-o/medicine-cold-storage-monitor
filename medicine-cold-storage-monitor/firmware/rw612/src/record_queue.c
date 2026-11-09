#include <stddef.h>
#include <string.h>

#include "protocol.h"
#include "record_queue.h"

static gw_record_t *at(record_queue_t *q, size_t logical)
{
    return &q->items[(q->head + logical) % GW_QUEUE_CAPACITY];
}

void rq_init(record_queue_t *q)
{
    memset(q, 0, sizeof *q);
}

size_t rq_count(const record_queue_t *q)
{
    return q->count;
}

static void remove_at(record_queue_t *q, size_t logical)
{
    if (logical == 0u) {
        q->head = (uint16_t)((q->head + 1u) % GW_QUEUE_CAPACITY);
    } else {
        for (size_t k = logical; k + 1u < q->count; k++) {
            *at(q, k) = *at(q, k + 1u);
        }
    }
    q->count--;
}

bool rq_push(record_queue_t *q, const gw_record_t *r)
{
    bool dropped = false;

    if (q->count >= GW_QUEUE_CAPACITY) {
        size_t victim = 0u;  /* default: oldest overall */
        for (size_t k = 0; k < q->count; k++) {
            if (at(q, k)->state == (uint8_t)ALARM_STATE_NORMAL) {
                victim = k;
                break;
            }
        }
        remove_at(q, victim);
        q->dropped++;
        dropped = true;
    }

    *at(q, q->count) = *r;
    q->count++;
    return dropped;
}

bool rq_peek(const record_queue_t *q, gw_record_t *out)
{
    if (q->count == 0u) {
        return false;
    }
    *out = q->items[q->head];
    return true;
}

void rq_pop(record_queue_t *q)
{
    if (q->count > 0u) {
        remove_at(q, 0u);
    }
}
