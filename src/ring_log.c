#include <unistd.h>
#include <time.h>
#include <sys/time.h>
#include <stdarg.h>
#include <sys/stat.h>
#include <errno.h>
#include <assert.h>

#include "ring_log.h"

pthread_key_t global_log_key;
static pthread_once_t log_key_once = PTHREAD_ONCE_INIT;

static void log_key_destructor(void *data) {
	free(data);
}

static void log_key_creator(void) {
	assert(pthread_key_create(&global_log_key, log_key_destructor) == 0);
	assert(pthread_setspecific(global_log_key, NULL) == 0);

	return;
}

static ring_log_t *ins() {
    return pthread_getspecific(global_log_key);
}


/********************************utc_timer******************************/
void reset_utc_format(utc_timer_t *utc_timer) {
    snprintf(utc_timer->utc_format, 20, "%d-%02d-%02d %02d:%02d:%02d", 
                                        utc_timer->year, 
                                        utc_timer->mon, 
                                        utc_timer->day, 
                                        utc_timer->hour, 
                                        utc_timer->min, 
                                        utc_timer->sec);
}

void reset_utc_format_sec(utc_timer_t *utc_timer) {
    snprintf(utc_timer->utc_format + 17, 3, "%02d", utc_timer->sec);
}

void init_utc_timer(utc_timer_t *utc_timer) {
    struct timeval tv;
    gettimeofday(&tv, NULL);

    utc_timer->sys_acc_sec = tv.tv_sec;
    utc_timer->sys_acc_min = utc_timer->sys_acc_sec / 60;

    struct tm cur_tm;
    localtime_r((time_t *)&utc_timer->sys_acc_sec, &cur_tm);

    utc_timer->year = cur_tm.tm_year + 1900;
    utc_timer->mon = cur_tm.tm_mon + 1;
    utc_timer->day = cur_tm.tm_mday;
    utc_timer->hour = cur_tm.tm_hour;
    utc_timer->min = cur_tm.tm_min;
    utc_timer->sec = cur_tm.tm_sec;
    reset_utc_format(utc_timer);
}

uint64_t get_curr_time(utc_timer_t *utc_timer, int *p_msec) {
    struct timeval tv;
    // get current ts
    gettimeofday(&tv, NULL);
    if(p_msec) {
        *p_msec = tv.tv_usec / 1000;
    }

    // if not in same seconds
    if((uint32_t)tv.tv_sec != utc_timer->sys_acc_sec) {
        utc_timer->sec = tv.tv_sec % 60;
        utc_timer->sys_acc_sec = tv.tv_sec;
        // or if not in same minutes
        if(utc_timer->sys_acc_sec / 60 != utc_timer->min) {
            //use sys_acc_sec update year, mon, day, hour, min, sec
            utc_timer->sys_acc_min = utc_timer->sys_acc_sec / 60;
            struct tm cur_tm;
            localtime_r((time_t*)&utc_timer->sys_acc_sec, &cur_tm);
            utc_timer->year = cur_tm.tm_year + 1900;
            utc_timer->mon  = cur_tm.tm_mon + 1;
            utc_timer->day  = cur_tm.tm_mday;
            utc_timer->hour = cur_tm.tm_hour;
            utc_timer->min  = cur_tm.tm_min;
            //reformat utc format
            reset_utc_format(utc_timer);
        }
        else {
            //reformat utc format only sec
            reset_utc_format_sec(utc_timer);
        }
    }

    return tv.tv_sec;
}


/************************ cell buffer *******************/
int init_cell_buffer(cell_buffer_t *buf, uint32_t len) {
    buf->status = FREE;
    buf->prev = NULL;
    buf->next = NULL;
    buf->total_len = len;
    buf->used_len = 0;

    buf->data = (char *)malloc(sizeof(char) * len);
    if(buf->data == NULL) {
        perror("allocate buffer failed");
        return -1;
    }

    return 0;
}

uint32_t avail_len(cell_buffer_t *buf) {
    return buf->total_len - buf->used_len;
}

int buf_empty(cell_buffer_t *buf) {
    return buf->used_len == 0;
}

void buf_append(cell_buffer_t *buf, const char* log_line, uint32_t len) {
    if (avail_len(buf) < len) {
        return;
    }

    memcpy(buf->data + buf->used_len, log_line, len);
    buf->used_len += len;
}

void buf_clear(cell_buffer_t *buf)
{
    buf->used_len = 0;
    buf->status = FREE;
}

void buf_persist(cell_buffer_t *buf, FILE* fp)
{
    uint32_t wt_len = fwrite(buf->data, 1, buf->used_len, fp);
    if (wt_len != buf->used_len)
    {
        fprintf(stderr, "write log to disk error, wt_len %u\n", wt_len);
    }
}


/*******************************ring long******************************/
void init_ring_log() {
    ring_log_t *log = (ring_log_t *)malloc(sizeof(ring_log_t));
    if(log == NULL) {
        printf("cannot create ring log\n");
        return;
    }

    log_key_creator();
    assert(pthread_setspecific(global_log_key, log) == 0);

    log->buff_cnt = 3;
    log->curr_buf = NULL;
    log->persist_buf = NULL;
    log->fp = NULL;
    log->log_cnt = 0;
    log->log_err_sec = 0;
    log->env_ok = 0;
    log->level = INFO;
    init_utc_timer(&log->utc_timer);

    //create double linked list
    cell_buffer_t *head = (cell_buffer_t *)malloc(sizeof(log->buff_len));
    if(head == NULL) {
        fprintf(stderr, "no space to allocate cell_buffer\n");
        return;
    }

    cell_buffer_t *current;
    cell_buffer_t *prev = head;
    for(int i = 1; i < log->buff_len; i++) {
        current = (cell_buffer_t *)malloc(sizeof(log->buff_len));
        if(current == NULL) {
            fprintf(stderr, "no space to allocate cell_buffer\n");
            return;
        }
        current->prev = prev;
        prev->next = current;
        prev = current;
    }
    prev->next = head;
    head->prev = prev;

    log->curr_buf = head;
    log->persist_buf = head;

    log->pid = getpid();
}

void init_path(const char* log_dir, const char* prog_name, int level) {
    ring_log_t *log = ins();
    if(log == NULL) {
        return;
    }

    pthread_mutex_lock(&log->mutex);

    strncpy(log->log_dir, log_dir, 128);
    //name format:  name_year-mon-day-t[tid].log.n
    strncpy(log->prog_name, prog_name, 128);

    mkdir(log->log_dir, 0777);
    //查看是否存在此目录、目录下是否允许创建文件
    if(access(log->log_dir, F_OK | W_OK) == -1) {
        fprintf(stderr, "logdir: %s error: %s\n", log->log_dir, strerror(errno));
    } else {
        log->env_ok = 1;
    }

    if (level > TRACE)
        level = TRACE;
    if (level < FATAL)
        level = FATAL;
    log->level = level;

    pthread_mutex_unlock(&log->mutex);
}

int get_level() {
    ring_log_t *log = ins();
    return log->level;
}

void log_persist() {
    ring_log_t *log = ins();
    if(log == NULL) {
        return;
    }

    while(1) 
    {
        //check if persist_buf need to be persist
        pthread_mutex_lock(&log->mutex);
        if(log->persist_buf->status == FREE) {
            struct timespec tsp;
            struct timeval now;
            gettimeofday(&now, NULL);
            tsp.tv_sec = now.tv_sec;
            tsp.tv_nsec = now.tv_usec * 1000;   //nanoseconds
            tsp.tv_sec += BUFF_WAIT_TIME;   // wait for 1 seconds
            pthread_cond_timedwait(&log->cond, &log->mutex, &tsp);
        }

        if(buf_empty(log->persist_buf)) 
        {
            //give up, go to next turn
            pthread_mutex_unlock(&log->mutex);
            continue;
        }

        if(log->persist_buf->status == FREE) {
            assert(log->curr_buf == log->persist_buf);
            log->curr_buf->status = FULL;
            log->curr_buf = log->curr_buf->next;
        }

        int year = log->utc_timer.year;
        int mon = log->utc_timer.mon;
        int day = log->utc_timer.day;
        pthread_mutex_unlock(&log->mutex);

        //decision which file to write
        if (!decis_file(year, mon, day)) {
            continue;
        }

        //write
        buf_persist(log->persist_buf, log->fp);
        fflush(log->fp);

        pthread_mutex_lock(&log->mutex);
        buf_clear(log->persist_buf);
        log->persist_buf = log->persist_buf->next;
        pthread_mutex_unlock(&log->mutex);
    }

}

void log_append(const char* lvl, const char* format, ...) {
    ring_log_t *log = ins();
    if(log == NULL) {
        return;
    }

    int ms;
    uint64_t curr_sec = get_curr_time(&log->utc_timer, &ms);
    if(log->log_err_sec && curr_sec - log->log_err_sec < RELOG_THRESOLD) {
        return;
    }

    char log_line[LOG_LEN_LIMIT];
    int head_len = snprintf(log_line, LOG_LEN_LIMIT, "%s[%s.%03d]", lvl, log->utc_timer.utc_format, ms);

    va_list arg_ptr;
    va_start(arg_ptr, format);

    // message body
    int body_len = vsnprintf(log_line + head_len, LOG_LEN_LIMIT - head_len, format, arg_ptr);

    va_end(arg_ptr);

    uint32_t len = head_len + body_len;

    log->log_err_sec = 0;
    int notify_back = 0;

    pthread_mutex_lock(&log->mutex);
    if(log->curr_buf->status == FREE && avail_len(log->curr_buf) >= len) {
        buf_append(log->curr_buf, log_line, len);
    }
    else {
        //1. curr_buf->status = cell_buffer::FREE but curr_buf->avail_len() < len
        //2. curr_buf->status = cell_buffer::FULL
        if(log->curr_buf->status == FREE) {
            log->curr_buf->status = FULL;
            cell_buffer_t *next_buf = log->curr_buf->next;
            // notify backend thread
            notify_back = 1;

            //it suggest that this buffer is under the persist job
            if(next_buf->status == FULL) {
                //if mem use < MEM_USE_LIMIT, allocate new cell_buffer
                if(log->buff_len * (log->buff_cnt + 1) > MEM_USE_LIMIT) {
                    log->curr_buf = next_buf;
                    log->log_err_sec = curr_sec;
                } else {
                    cell_buffer_t *new_buffer = (cell_buffer_t *)malloc(sizeof(log->buff_len));
                    log->buff_cnt += 1;
                    new_buffer->prev = log->curr_buf;
                    log->curr_buf->next = new_buffer;
                    new_buffer->next = next_buf;
                    next_buf->prev = new_buffer;
                    log->curr_buf = new_buffer;
                }
            } else {
                //next buffer is free, we can use it
                log->curr_buf = next_buf;
            }

            if(!log->log_err_sec) {
                buf_append(log->curr_buf, log_line, len);
            }
        }
        else { //curr_buf->status == cell_buffer::FULL, assert persist is on here too!
            log->log_err_sec = curr_sec;
        }
    }
    pthread_mutex_unlock(&log->mutex);
    if(notify_back) {
        pthread_cond_signal(&log->cond);
    }
}

int decis_file(int year, int mon, int day) {
    ring_log_t *log = ins();
    if(log == NULL) {
        return;
    }

    if(!log->env_ok) {
        if(log->fp) {
            fclose(log->fp);
        }
        log->fp = fopen("/dev/null", "w");
        return log->fp != NULL;
    }

    if(log->fp == NULL) {
        log->year = year;
        log->mon = mon;
        log->day = day;
        char log_path[256] = {};
        sprintf(log_path, "%s/%s.%d%02d%02d.%u.log", log->log_dir, log->prog_name, log->year, 
                                                     log->mon, log->day, log->pid);
        log->fp = fopen(log_path, "w");
        if(log->fp == NULL) {
            fprintf(stderr, "logdir: %s open error\n", log->log_dir);
            return 0;
        }
        log->log_cnt += 1;
    }
    else if(log->day != day) {
        fclose(log->fp);
        char log_path[256] = {};
        log->year = year;
        log->mon = mon;
        log->day = day;
        sprintf(log_path, "%s/%s.%d%02d%02d.%u.log", log->log_dir, log->prog_name, log->year, 
                                                     log->mon, log->day, log->pid);
        log->fp = fopen(log_path, "w");
        if(log->fp == NULL) {
            fprintf(stderr, "logdir: %s open error\n", log->log_dir);
            return 0;
        }
        log->log_cnt = 1;
    }
    else if(ftell(log->fp) >= LOG_USE_LIMIT) {
        fclose(log->fp);
        char old_path[256] = {};
        char new_path[256] = {};
        //mv xxx.log.[i] xxx.log.[i + 1]
        for(int i = log->log_cnt - 1; i > 0; i--) {
            sprintf(old_path, "%s/%s.%d%02d%02d.%u.log.%d", log->log_dir, log->prog_name, log->year, 
                                                            log->mon, log->day, log->pid, i);
            sprintf(new_path, "%s/%s.%d%02d%02d.%u.log.%d", log->log_dir, log->prog_name, log->year, 
                                                            log->mon, log->day, log->pid, i + 1);
            rename(old_path, new_path);
        }
        //mv xxx.log xxx.log.1
        sprintf(old_path, "%s/%s.%d%02d%02d.%u.log", log->log_dir, log->prog_name, log->year, 
                                                     log->mon, log->day, log->pid);
        sprintf(new_path, "%s/%s.%d%02d%02d.%u.log.1", log->log_dir, log->prog_name, log->year, 
                                                       log->mon, log->day, log->pid);
        rename(old_path, new_path);

        log->fp = fopen(old_path, "w");
        if(log->fp == NULL) {
            fprintf(stderr, "logdir: %s open error\n", log->log_dir);
            return 0;
        }
        log->log_cnt = 1;
    }
    return 1;
}