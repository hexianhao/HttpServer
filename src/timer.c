#include <unistd.h>
#include <sys/time.h>

#include "http_request.h"
#include "timer.h"
#include "epoll.h"

rbtree_t         event_timer_rbtree;
rbtree_node_t    event_timer_sentinel;
pthread_mutex_t  event_timer_mutex;

void timeout_handle(http_request_t *request) {
    struct epoll_event ev = {0, {0}};
    ev.data.ptr = request;
    Epoll_Del(request->epfd, request->fd, &ev);
    // close connection
    http_close_conn(request);
}

/* 定时器事件初始化 */
int event_timer_init()
{
    /* 初始化红黑树 */
    rbtree_init(&event_timer_rbtree, &event_timer_sentinel,
                    rbtree_insert_timer_value);

    int ret = pthread_mutex_init(&event_timer_mutex, NULL);
    if (ret < 0) {
        return -1;
    }

    LOG_INFO("timer init");

    return 1;
}

uint64_t event_find_timer(void) {
    int64_t timer;
    rbtree_node_t *node, *root, *sentinel;
    struct timeval tv;
    uint64_t curr_msec;

    /* 若红黑树为空 */
    if (event_timer_rbtree.root == &event_timer_sentinel) {
        return TIMER_INFINITE;
    }

    pthread_mutex_lock(&event_timer_mutex);

    root = event_timer_rbtree.root;
    sentinel = event_timer_rbtree.sentinel;

    /* 找出红黑树最小的节点，即最左边的节点 */
    node = rbtree_min(root, sentinel);

    pthread_mutex_unlock(&event_timer_mutex);

    gettimeofday(&tv, NULL);
    curr_msec = tv.tv_sec * 1000 + tv.tv_usec / 1000;

    /* 计算最左节点键值与当前时间的差值timer，当timer大于0表示不超时，不大于0表示超时 */
    timer = (int64_t)(node->key - curr_msec);

    /*
     * 若timer大于0，则事件不超时，返回该值；
     * 若timer不大于0，则事件超时，返回0，标志触发超时事件；
     */
    return (uint64_t)(timer > 0 ? timer : 0);
}


/* 检查定时器中所有事件 */
void event_expire_timers(void) {
    http_request_t  *request;
    rbtree_node_t   *node, *root, *sentinel;
    uint64_t curr_msec;
    struct timeval tv;

    sentinel = event_timer_rbtree.sentinel;

    gettimeofday(&tv, NULL);
    curr_msec = tv.tv_sec * 1000 + tv.tv_usec / 1000;

    /* 循环检查 */
    for(;;) {
        
        pthread_mutex_lock(&event_timer_mutex);

        root = event_timer_rbtree.root;

        /* 若定时器红黑树为空，则直接返回，不做任何处理 */
        if(root == sentinel) {
            pthread_mutex_unlock(&event_timer_mutex);
            return;
        }

        /* 找出定时器红黑树最左边的节点，即最小的节点，同时也是最有可能超时的事件对象 */
        node = rbtree_min(root, sentinel);

        /* node->key <= ngx_current_time */
        /* 若检查到的当前事件已超时 */
        if ((int64_t) (node->key - curr_msec) <= 0) {
            /* 获取超时的具体事件 */
            request = (http_request_t *) ((char *) node - offsetof(http_request_t, timer));

            LOG_INFO("socket %d time out", request->fd);

            /* 将已超时事件对象从现有定时器红黑树中移除 */
            rbtree_delete(&event_timer_rbtree, &request->timer);
            /* 超时处理函数 */
            timeout_handle(request);

            continue;
        }

        break;
    }

    pthread_mutex_unlock(&event_timer_mutex);
}