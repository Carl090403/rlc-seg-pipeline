#ifndef BQUEUE_H
#define BQUEUE_H
#include <pthread.h>
#include <stdbool.h>
#include <stddef.h>

/*
 * Bounded multi-producer / multi-consumer queue of pointers (mutex + two condition variables).
 *
 * Wake-up policy: blocked producers are woken only once the queue has drained to half capacity
 * (hysteresis), so one wake-up admits ~cap/2 pushes instead of one. Signals are skipped entirely
 * when nobody is waiting. This avoids a futex wake per item when consumers are the bottleneck.
 */
typedef struct {
    void **slots;
    size_t cap, head, tail, count;
    size_t low_water;           /* producers are woken when count drops to this level */
    unsigned prod_waiting, cons_waiting;
    bool closed;
    pthread_mutex_t mu;
    pthread_cond_t not_empty, not_full;
} bqueue_t;

int bq_init(bqueue_t *q, size_t capacity);
void bq_destroy(bqueue_t *q);
/* Blocks while full. Returns false if the queue was closed. */
bool bq_push(bqueue_t *q, void *item);
/* Blocks while empty. Returns false once closed AND drained. */
bool bq_pop(bqueue_t *q, void **item);
/* Wake every waiter; pending items can still be popped. */
void bq_close(bqueue_t *q);
#endif
