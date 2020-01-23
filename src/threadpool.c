#include <stdlib.h>
#include <signal.h>
#include <assert.h>
#include <gperftools/tcmalloc.h>

#include "threadpool.h"

static pthread_t master_tid;
static volatile int global_num_thread = 0;

static int tpool_queue_empty(tpool_t *tpool)
{
    int i;

    for (i = 0; i < tpool->num_threads; i++)
        if (!thread_queue_empty(&tpool->threads[i]))
            return 0;
    return 1;
}

static thread_t* round_robin_schedule(tpool_t *tpool)
{
    static int cur_thread_index = -1;

    assert(tpool && tpool->num_threads > 0);
    cur_thread_index = (cur_thread_index + 1) % tpool->num_threads ;
    return &tpool->threads[cur_thread_index];
}

static void sig_do_nothing(int signo)
{
    return;
}

static tpool_work_t *get_work_concurrently(thread_t *thread)
{
    tpool_work_t *work;
    uint8_t tmp;

    do {
        work = NULL;
        if (thread_queue_len(thread) <= 0) {
            break;
        }

        tmp = thread->out;
        //prefetch work
        work = &thread->work_queue[queue_offset(tmp)];

    } while (!__sync_bool_compare_and_swap(&thread->out, tmp, tmp + 1));

    return work;
}

void *tpool_thread(void *arg)
{
    thread_t *thread = arg;
    tpool_work_t *work;
    sigset_t signal_mask, oldmask;
    int ret, sig_caught;

    /* SIGUSR1 handler has been set in tpool_init */
    __sync_fetch_and_add(&global_num_thread, 1);
    pthread_kill(master_tid, SIGUSR1);

    sigemptyset (&oldmask);
    sigemptyset (&signal_mask);
    sigaddset (&signal_mask, SIGUSR1);

    while (1) {
        ret = pthread_sigmask(SIG_BLOCK, &signal_mask, NULL);
        if (ret != 0) {
            debug(TPOOL_ERROR, "SIG_BLOCK failed");
            pthread_exit(NULL);
        }

        while (thread_queue_empty(thread) && !thread->shutdown) {
            debug(TPOOL_DEBUG, "I'm sleep");
            ret = sigwait (&signal_mask, &sig_caught);
            if (ret != 0) {
                debug(TPOOL_ERROR, "sigwait failed");
                pthread_exit(NULL);
            }
            debug(TPOOL_DEBUG, "I'm awake");
        }

        ret = pthread_sigmask(SIG_SETMASK, &oldmask, NULL);
        if (ret != 0) {
            debug(TPOOL_ERROR, "SIG_SETMASK failed");
            pthread_exit(NULL);
        }

        if (thread->shutdown) {
            debug(TPOOL_DEBUG, "exit");
            pthread_exit(NULL);
        }

        work = get_work_concurrently(thread);
        if (work) {
            (*(work->call_back))(work->arg);
        }

        if (thread_queue_empty(thread)) {
            pthread_kill(master_tid, SIGUSR1);
        }       
    }
}

static void spawn_new_thread(tpool_t *tpool, int index)
{   
    int ret;

    memset(&tpool->threads[index], 0, sizeof(thread_t));
    ret = pthread_create(&tpool->threads[index].tid, NULL, tpool_thread,
                       (void *)(&tpool->threads[index]));

    if (ret != 0) {
        debug(TPOOL_ERROR, "pthread_create failed");
        exit(0);
    }
}

static int wait_for_thread_registration(int num_expected)
{
    sigset_t signal_mask, oldmask;
    int ret, sig_caught;

    sigemptyset (&oldmask);
    sigemptyset (&signal_mask);
    sigaddset (&signal_mask, SIGUSR1);

    ret = pthread_sigmask(SIG_BLOCK, &signal_mask, NULL);
    if (ret != 0) {
        debug(TPOOL_ERROR, "SIG_BLOCK failed");
        return -1;
    }

    while (global_num_thread < num_expected) {
        ret = sigwait (&signal_mask, &sig_caught);
        if (ret != 0) {
            debug(TPOOL_ERROR, "sigwait failed");
            return -1;
        }
    }

    ret = pthread_sigmask(SIG_SETMASK, &oldmask, NULL);
    if (ret != 0) {
        debug(TPOOL_ERROR, "SIG_SETMASK failed");
        return -1;
    }

    return 0;
}

tpool_t *tpool_init(int num_threads)
{
    int i;
    tpool_t *tpool;

    tpool = tc_malloc(sizeof(*tpool));
    if (tpool == NULL) {
        debug(TPOOL_ERROR, "tc_malloc failed");
        return NULL;
    }

    memset(tpool, 0, sizeof(*tpool));
    tpool->num_threads = num_threads;
    tpool->schedule_thread = round_robin_schedule;
    tpool->threads = (thread_t *)tc_malloc(sizeof(thread_t) * num_threads);
    if(tpool->threads == NULL) {
        debug(TPOOL_ERROR, "tc_malloc failed");
        return NULL;
    }

    /* all threads are set SIGUSR1 with sig_do_nothing */
    if (signal(SIGUSR1, sig_do_nothing) == SIG_ERR) {
        debug(TPOOL_ERROR, "signal failed");
        return NULL;
    }
    master_tid = pthread_self();

    for (i = 0; i < tpool->num_threads; i++) {
        spawn_new_thread(tpool, i);
    }
        
    if (wait_for_thread_registration(tpool->num_threads) < 0) {
        pthread_exit(NULL);
    }
        
    return tpool;
}

static int dispatch_work2thread(tpool_t *tpool, thread_t *thread, 
                                    void (* call_back)(void *), void *arg)
{
    tpool_work_t *work = NULL;

    if (thread_queue_full(thread)) {
        debug(TPOOL_WARNING, "queue of thread selected is full!!!");
        return -1;
    }

    work = &thread->work_queue[queue_offset(thread->in)];
    work->call_back = call_back;
    work->arg = arg;
    thread->in++;
    
    if (thread_queue_len(thread) == 1) {
        debug(TPOOL_DEBUG, "signal has task");
        pthread_kill(thread->tid, SIGUSR1);
    }

    return 0;
}

int tpool_add_work(tpool_t *tpool, void (*call_back)(void *), void *arg)
{
    thread_t *thread;

    assert(tpool);
    thread = tpool->schedule_thread(tpool);
    return dispatch_work2thread(tpool, thread, call_back, arg);
}

void tpool_destroy(tpool_t *tpool)
{
    sigset_t signal_mask, oldmask;
    int ret, sig_caught;
    int i;

    assert(tpool);

    debug(TPOOL_DEBUG, "wait all work done");

    sigemptyset (&oldmask);
    sigemptyset (&signal_mask);
    sigaddset (&signal_mask, SIGUSR1);

    ret = pthread_sigmask(SIG_BLOCK, &signal_mask, NULL);
    if (ret != 0) {
        debug(TPOOL_ERROR, "SIG_BLOCK failed");
        pthread_exit(NULL);
    }

    while (!tpool_queue_empty(tpool)) {
        ret = sigwait(&signal_mask, &sig_caught);
        if (ret != 0) {
            debug(TPOOL_ERROR, "sigwait failed");
            pthread_exit(NULL);
        }
    }

    ret = pthread_sigmask(SIG_SETMASK, &oldmask, NULL);
    if (ret != 0) {
        debug(TPOOL_ERROR, "SIG_SETMASK failed");
        pthread_exit(NULL);
    }

    /* shutdown all threads */
    for (i = 0; i < tpool->num_threads; i++) {
        tpool->threads[i].shutdown = 1;
        /* wake up thread */
        pthread_kill(tpool->threads[i].tid, SIGUSR1);
    }
    debug(TPOOL_DEBUG, "wait worker thread exit");
    for (i = 0; i < tpool->num_threads; i++) {
        pthread_join(tpool->threads[i].tid, NULL);
    }

    tc_free(tpool->threads);
    tc_free(tpool);
}