#ifndef _THREADPOOL_H
#define _THREADPOOL_H

#include <stdlib.h>
#include <pthread.h>
#include <strings.h>
#include <string.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>

enum {
    TPOOL_ERROR,
    TPOOL_WARNING,
    TPOOL_INFO,
    TPOOL_DEBUG
};

#define debug(level, ...) do { \
    if (level <= TPOOL_DEBUG) {\
        flockfile(stdout); \
        printf("###%p.%s: ", (void *)pthread_self(), __func__); \
        printf(__VA_ARGS__); \
        putchar('\n'); \
        fflush(stdout); \
        funlockfile(stdout);\
    }\
} while (0)

#define WORK_QUEUE_POWER 8
#define WORK_QUEUE_SIZE (1 << WORK_QUEUE_POWER)
#define WORK_QUEUE_MASK (WORK_QUEUE_SIZE - 1)

/*
 * Just main thread can increase thread->in, we can make it safely.
 * However,  thread->out may be increased in both main thread and
 * worker thread during balancing thread load when new threads are added
 * to our thread pool...
*/
#define thread_out_val(thread)      (__sync_val_compare_and_swap(&(thread)->out, 0, 0))
#define thread_queue_len(thread)   ((thread)->in - thread_out_val(thread))
#define thread_queue_empty(thread) (thread_queue_len(thread) == 0)
#define thread_queue_full(thread)  (thread_queue_len(thread) == WORK_QUEUE_SIZE)
#define queue_offset(val)           ((val) & WORK_QUEUE_MASK)

typedef struct tpool_work {
    void    (*call_back)(void *);
    void    *arg;
} tpool_work_t;

typedef struct {
    pthread_t    tid;
    int          shutdown;

    uint8_t in;        /* offset from start of work_queue where to put work next */
    uint8_t out;   /* offset from start of work_queue where to get work next */
    tpool_work_t work_queue[WORK_QUEUE_SIZE];

} thread_t;

typedef struct tpool_s tpool_t;
typedef thread_t* (*schedule_thread_func)(tpool_t *tpool);
struct tpool_s {
    int                 num_threads;
    thread_t            *threads;
    schedule_thread_func schedule_thread;
};

// inital
tpool_t *tpool_init(int num_worker_threads);
// add
int tpool_add_work(tpool_t *tpool, void (* call_back)(void *), void *arg);
// destroy
void tpool_destroy(tpool_t *tpool);

// 工作线程
void *tpool_thread(void *arg);

#endif