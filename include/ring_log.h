#ifndef __RING_LOG_H
#define __RING_LOG_H

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <pthread.h>
#include <sys/types.h>

#define MEM_USE_LIMIT (3u * 1024 * 1024 * 1024)//3GB
#define LOG_USE_LIMIT (1u * 1024 * 1024 * 1024)//1GB
#define LOG_LEN_LIMIT (4 * 1024)//4K
#define RELOG_THRESOLD 5
#define BUFF_WAIT_TIME 1
#define BUFF_LENGTH (30 * 1024 * 1024)

typedef enum {
    FATAL = 1,
    ERROR,
    WARN,
    INFO,
    DEBUG,
    TRACE
}LOG_LEVEL;

typedef enum {
    FREE,
    FULL
}buffer_status;

typedef struct utc_timer_s {
    int year;
    int mon;
    int day;
    int hour;
    int min;
    int sec;

    char utc_format[20];

    uint64_t sys_acc_min;
    uint64_t sys_acc_sec;

}utc_timer_t;

void init_utc_timer(utc_timer_t *utc_timer);
uint64_t get_curr_time(utc_timer_t *utc_timer, int *p_msec);
void reset_utc_format(utc_timer_t *utc_timer);
void reset_utc_format_sec(utc_timer_t *utc_timer);


typedef struct cell_buffer_s {
    buffer_status status;

    struct cell_buffer_s *prev;
    struct cell_buffer_s *next;

    uint32_t total_len;
    uint32_t used_len;
    char *data;

}cell_buffer_t;

int init_cell_buffer(cell_buffer_t *buf, uint32_t len);
uint32_t avail_len(cell_buffer_t *buf);
int buf_empty(cell_buffer_t *buf);
void buf_append(cell_buffer_t *buf, const char* log_line, uint32_t len);
void buf_clear(cell_buffer_t *buf);
void buf_persist(cell_buffer_t *buf, FILE *fp);


typedef struct ring_log_s {
    int buff_cnt;

    cell_buffer_t *curr_buf;
    cell_buffer_t *persist_buf;     // 持久化缓冲
    cell_buffer_t *last_buf;

    FILE *fp;
    pid_t pid;
    int year, mon, day, log_cnt;
    char prog_name[128];
    char log_dir[128];

    int env_ok;        // if log dir ok
    int level;
    uint64_t log_err_sec;   //last can't log error time(s) if value != 0, log error happened last time

    utc_timer_t utc_timer;

    pthread_mutex_t mutex;
    pthread_cond_t cond;

    uint32_t buff_len;
    
}ring_log_t;

ring_log_t *ins();
void init_ring_log();
void init_path(const char* log_dir, const char* prog_name, int level);
int get_level();
void log_persist();
void log_append(const char* lvl, const char* format, ...);
int decis_file(int year, int mon, int day);
// persistence thread
void* be_thdo(void* args);

#define LOG_MEM_SET(mem_lmt) \
    do \
    { \
        if (mem_lmt < 90 * 1024 * 1024) \
        { \
            mem_lmt = 90 * 1024 * 1024; \
        } \
        else if (mem_lmt > 1024 * 1024 * 1024) \
        { \
            mem_lmt = 1024 * 1024 * 1024; \
        } \
        log->buff_len = mem_lmt; \
    } while (0)

#define LOG_INIT(log_dir, prog_name, level) \
    do \
    { \
        init_path(log_dir, prog_name, level); \
        pthread_t tid; \
        pthread_create(&tid, NULL, be_thdo, NULL); \
        pthread_detach(tid); \
    } while (0)

//format: [LEVEL][yy-mm-dd h:m:s.ms][tid]file_name:line_no(func_name):content
#define LOG_TRACE(format, args...) \
    do \
    { \
        if (get_level() >= TRACE) \
        { \
            log_append("[TRACE]", "[%u]%s:%d(%s): " format "\n", \
                    gettid(), __FILE__, __LINE__, __FUNCTION__, ##args); \
        } \
    } while (0)

#define LOG_DEBUG(format, args...) \
    do \
    { \
        if (get_level() >= DEBUG) \
        { \
            log_append("[DEBUG]", "[%u]%s:%d(%s): " format "\n", \
                    gettid(), __FILE__, __LINE__, __FUNCTION__, ##args); \
        } \
    } while (0)

#define LOG_INFO(format, args...) \
    do \
    { \
        if (get_level() >= INFO) \
        { \
            log_append("[INFO]", "[%u]%s:%d(%s): " format "\n", \
                    gettid(), __FILE__, __LINE__, __FUNCTION__, ##args); \
        } \
    } while (0)

#define LOG_NORMAL(format, args...) \
    do \
    { \
        if (get_level() >= INFO) \
        { \
            log_append("[INFO]", "[%u]%s:%d(%s): " format "\n", \
                    gettid(), __FILE__, __LINE__, __FUNCTION__, ##args); \
        } \
    } while (0)

#define LOG_WARN(format, args...) \
    do \
    { \
        if (get_level() >= WARN) \
        { \
            log_append("[WARN]", "[%u]%s:%d(%s): " format "\n", \
                    gettid(), __FILE__, __LINE__, __FUNCTION__, ##args); \
        } \
    } while (0)

#define LOG_ERROR(format, args...) \
    do \
    { \
        if (get_level() >= ERROR) \
        { \
            log_append("[ERROR]", "[%u]%s:%d(%s): " format "\n", \
                gettid(), __FILE__, __LINE__, __FUNCTION__, ##args); \
        } \
    } while (0)

#define LOG_FATAL(format, args...) \
    do \
    { \
        log_append("[FATAL]", "[%u]%s:%d(%s): " format "\n", \
            gettid(), __FILE__, __LINE__, __FUNCTION__, ##args); \
    } while (0)


#endif