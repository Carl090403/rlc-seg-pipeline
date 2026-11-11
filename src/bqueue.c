#include "bqueue.h"
#include <sched.h>
#include <stdlib.h>

int bq_init(bqueue_t *q, size_t capacity) {
    if (!q || capacity == 0) return -1;
    q->slots = calloc(capacity, sizeof *q->slots);
    if (!q->slots) return -1;
    q->cap = capacity;
    q->low_water = capacity / 2;
    q->head = q->tail = q->count = 0;
    q->prod_waiting = q->cons_waiting = 0;
    q->closed = false;
    pthread_mutex_init(&q->mu, NULL);
    pthread_cond_init(&q->not_empty, NULL);
    pthread_cond_init(&q->not_full, NULL);
    return 0;
}

void bq_destroy(bqueue_t *q) {
    pthread_cond_destroy(&q->not_full);
    pthread_cond_destroy(&q->not_empty);
    pthread_mutex_destroy(&q->mu);
    free(q->slots);
    q->slots = NULL;
}

bool bq_push(bqueue_t *q, void *item) {
    pthread_mutex_lock(&q->mu);
    while (q->count == q->cap && !q->closed) {
        q->prod_waiting++;
        pthread_cond_wait(&q->not_full, &q->mu);
        q->prod_waiting--;
    }
    if (q->closed) {
        pthread_mutex_unlock(&q->mu);
        return false;
    }
    q->slots[q->tail] = item;
    q->tail = (q->tail + 1) % q->cap;
    q->count++;
    if (q->cons_waiting) pthread_cond_signal(&q->not_empty);
    pthread_mutex_unlock(&q->mu);
    return true;
}

bool bq_pop(bqueue_t *q, void **item) {
    pthread_mutex_lock(&q->mu);
    /* Short bounded spin before sleeping: when producers are the bottleneck the queue is
       usually refilled within microseconds, and a futex sleep/wake costs far more than that. */
    for (int spin = 0; spin < 200 && q->count == 0 && !q->closed; spin++) {
        pthread_mutex_unlock(&q->mu);
        sched_yield();
        pthread_mutex_lock(&q->mu);
    }
    while (q->count == 0 && !q->closed) {
        q->cons_waiting++;
        pthread_cond_wait(&q->not_empty, &q->mu);
        q->cons_waiting--;
    }
    if (q->count == 0) { /* closed and drained */
        pthread_mutex_unlock(&q->mu);
        return false;
    }
    *item = q->slots[q->head];
    q->head = (q->head + 1) % q->cap;
    q->count--;
    /* Producers only ever block at count == cap, so every blocked producer sees this
       crossing of low_water happen after it started waiting: no lost wake-ups. */
    if (q->prod_waiting && q->count == q->low_water) pthread_cond_broadcast(&q->not_full);
    pthread_mutex_unlock(&q->mu);
    return true;
}

void bq_close(bqueue_t *q) {
    pthread_mutex_lock(&q->mu);
    q->closed = true;
    pthread_cond_broadcast(&q->not_empty);
    pthread_cond_broadcast(&q->not_full);
    pthread_mutex_unlock(&q->mu);
}
