#ifndef _THREADPOOL_H
#define _THREADPOOL_H

#include <stdlib.h>
#include <pthread.h>
#include <strings.h>
#include <string.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>

#define TASKQUE_SIZE (1<<10)
#define CAS(ptr, oldVal, newVal) __sync_bool_compare_and_swap(ptr, oldVal, newVal)

typedef struct {
    void (* call_back)(void *arg);
    void *arg;
}threadpool_task_t;

typedef struct thread_pool_s {
    threadpool_task_t Queue[TASKQUE_SIZE];
    // 定义TASKQUE_COUNT组线程，每组线程有THREAD_SIZE大小
    pthread_t *threads;
    // 读指针
    uint32_t ReadIndex;
    // 写指针
    uint32_t WriteIndex;
    /*
    最后一个已经完成入列操作的元素在数组中的下标. 如果它的值跟writeIndex不一致,
    表明有写请求尚未完成.这意味着,有写请求成功申请了空间但数据还没完全写进队列.
    所以如果有线程要读取,必须要等到写线程将数完全据写入到队列之后.
    */
    uint32_t maxReadIndex;

}thread_pool_t;


// counting index
uint32_t countToIndex(uint32_t count);
// add
void addtask(thread_pool_t *threadpool, threadpool_task_t task);
// get task
threadpool_task_t fetch(thread_pool_t *threadpool);
// create thread pool
void create_thread(thread_pool_t *threadpool, int thread_size);

// 工作线程
void *worker(void *arg);


#endif