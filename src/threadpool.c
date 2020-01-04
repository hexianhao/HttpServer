#include <stdlib.h>
#include "threadpool.h"

// counting index
uint32_t countToIndex(uint32_t count) {
    return count % TASKQUE_SIZE;
}

// create thread pool
void create_thread(thread_pool_t *threadpool, int thread_size) {

    threadpool->threads = (pthread_t *)malloc(sizeof(pthread_t) * thread_size);
    
    for(int i = 0; i < thread_size; i++) {
        pthread_create(&threadpool->threads[i], NULL, worker, NULL);
        pthread_detach(threadpool->threads[i]);
    }
}

// add
void addtask(thread_pool_t *threadpool, threadpool_task_t task) {
    uint32_t currentReadIndex;
    uint32_t currentWriteIndex;
    
    while(1)
    {
        currentReadIndex = threadpool->ReadIndex;
        currentWriteIndex = threadpool->WriteIndex;

        if(countToIndex(currentWriteIndex + 1) ==
                countToIndex(currentReadIndex)) continue;   // queue is full
        
        if(CAS(&threadpool->WriteIndex, currentWriteIndex, (currentWriteIndex + 1))) break;
    }

    // We know now that this index is reserved for us. Use it to save the data
    threadpool->Queue[countToIndex(currentWriteIndex)] = task;

    // update the maximum read index after saving the data. It wouldn't fail if there is only one thread 
    // inserting in the queue. It might fail if there are more than 1 producer threads because this
    // operation has to be done in the same order as the previous CAS
    while(!CAS(&threadpool->maxReadIndex, currentWriteIndex, (currentWriteIndex + 1))) {
        // this is a good place to yield the thread in case there are more
        // software threads than hardware processors and you have more
        // than 1 producer thread
        // have a look at sched_yield (POSIX.1b)
        sched_yield();
    }
}

// get task
threadpool_task_t fetch(thread_pool_t *threadpool) {
    uint32_t currentMaxReadIndex;
    uint32_t currentReadIndex;
    threadpool_task_t task;

    while(1)
    {
        currentReadIndex    = threadpool->ReadIndex;
        currentMaxReadIndex = threadpool->maxReadIndex;
        
        if(countToIndex(currentReadIndex) == 
                countToIndex(currentMaxReadIndex)) continue;

        // retrieve the data from the queue
        task = threadpool->Queue[countToIndex(currentReadIndex)];

        if(CAS(&threadpool->ReadIndex, currentReadIndex, (currentReadIndex + 1))) break;
    }
    
    return task;
}

// worker thread
void *worker(void *arg) {
    thread_pool_t *threadpool = (thread_pool_t *)arg;

    while(1) 
    {
        threadpool_task_t task = fetch(threadpool);
        task.call_back(task.arg);
    }

    return NULL;
}