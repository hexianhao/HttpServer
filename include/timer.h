#ifndef __TIMER_H
#define __TIMER_H

#include <pthread.h>
#include "rbtree.h"
#include "ring_log.h"

#define TIMER_INFINITE -1
#define TIMEOUT_DEFAULT 300000     /* ms */
#define TIMER_LAZY_DELAY 500

/* 所有定时器事件组成的红黑树 */
extern rbtree_t         event_timer_rbtree;
/* 红黑树的哨兵节点 */
extern rbtree_node_t    event_timer_sentinel;
/* mutex */
extern pthread_mutex_t  event_timer_mutex;


int event_timer_init(void);
uint64_t event_find_timer(void);
void event_expire_timers(void);
void timeout_handle(http_request_t *);


/* 从定时器中移除事件 */
static inline int
event_del_timer(http_request_t *request)
{
    LOG_INFO("event timer del: %d: %M",
          request->fd, request->timer.key);

    pthread_mutex_lock(&event_timer_mutex);

    if(!request->timerset) {
        /*
        *   request->timerset=0, 说明request->timer被删除
        */
        pthread_mutex_unlock(&event_timer_mutex);
        return -1;
    }

    /* 从红黑树中移除指定事件的节点对象 */
    rbtree_delete(&event_timer_rbtree, &request->timer);
    /* 删除后，timerset要置为0 */
    request->timerset = 0;

    pthread_mutex_unlock(&event_timer_mutex);

    request->timer.left = NULL;
    request->timer.right = NULL;
    request->timer.parent = NULL;

    return 0;
}

/* 将事件添加到定时器中 */
static inline void
event_add_timer(http_request_t *request, uint64_t timer)
{
    uint64_t      key;
    uint64_t      curr_msec;
    int64_t       diff;

    /* 设置事件对象节点的键值 */
    key = curr_msec + timer;

    request->timer.key = key;

    LOG_INFO("event timer add: %d: %M:%M",
          request->fd, timer, request->timer.key);

    pthread_mutex_lock(&event_timer_mutex);

    /* 将事件对象节点插入到红黑树中 */
    rbtree_insert(&event_timer_rbtree, &request->timer);
    /* timerset=1, 表示request->timer在红黑树上 */
    request->timerset = 1;

    pthread_mutex_unlock(&event_timer_mutex);

}

#endif