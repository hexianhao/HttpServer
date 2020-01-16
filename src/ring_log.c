#include <unistd.h>
#include <time.h>
#include <sys/time.h>
#include <stdarg.h>
#include <sys/stat.h>
#include <errno.h>
#include <assert.h>
#include <gperftools/tcmalloc.h>

#include "ring_log.h"

// declare ring log and it's singleton
static ring_log_t *RING_LOG;

static ring_log_t *ins() {
    return RING_LOG;
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

    buf->data = (char *)tc_malloc(sizeof(char) * len);
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
    ring_log_t *rlog = (ring_log_t *)tc_malloc(sizeof(ring_log_t));
    if(rlog == NULL) {
        printf("cannot create ring log\n");
        return;
    }

    rlog->buff_cnt = 3;
    rlog->curr_buf = NULL;
    rlog->persist_buf = NULL;
    rlog->fp = NULL;
    rlog->log_cnt = 0;
    rlog->log_err_sec = 0;
    rlog->env_ok = 0;
    rlog->level = INFO;
    rlog->buff_len = BUFF_LENGTH;
    init_utc_timer(&rlog->utc_timer);

    //create double linked list
    cell_buffer_t *head = (cell_buffer_t *)tc_malloc(sizeof(cell_buffer_t));
    if(head == NULL) {
        fprintf(stderr, "no space to allocate cell_buffer\n");
        return;
    }
    init_cell_buffer(head, rlog->buff_len);

    cell_buffer_t *current;
    cell_buffer_t *prev = head;
    for(int i = 1; i < rlog->buff_cnt; i++) {
        current = (cell_buffer_t *)tc_malloc(sizeof(cell_buffer_t));
        if(current == NULL) {
            fprintf(stderr, "no space to allocate cell_buffer\n");
            return;
        }
        init_cell_buffer(current, rlog->buff_len);

        current->prev = prev;
        prev->next = current;
        prev = current;
    }
    prev->next = head;
    head->prev = prev;

    rlog->curr_buf = head;
    rlog->persist_buf = head;

    rlog->pid = getpid();

    RING_LOG = rlog;
}

void init_path(const char* log_dir, const char* prog_name, int level) {
    // firstly, create ring log
    init_ring_log();

    ring_log_t *rlog = ins();
    if(rlog == NULL) {
        return;
    }

    pthread_mutex_lock(&rlog->mutex);

    strncpy(rlog->log_dir, log_dir, 128);
    //name format:  name_year-mon-day-t[tid].log.n
    strncpy(rlog->prog_name, prog_name, 128);

    mkdir(rlog->log_dir, 0777);
    //查看是否存在此目录、目录下是否允许创建文件
    if(access(rlog->log_dir, F_OK | W_OK) == -1) {
        fprintf(stderr, "logdir: %s error: %s\n", rlog->log_dir, strerror(errno));
    } else {
        rlog->env_ok = 1;
    }

    if (level > TRACE)
        level = TRACE;
    if (level < FATAL)
        level = FATAL;
    rlog->level = level;

    pthread_mutex_unlock(&rlog->mutex);
}

int get_level() {
    ring_log_t *rlog = ins();
    return rlog->level;
}

void log_persist() {
    ring_log_t *rlog = ins();
    if(rlog == NULL) {
        return;
    }

    while(1) 
    {
        //check if persist_buf need to be persist
        pthread_mutex_lock(&rlog->mutex);
        if(rlog->persist_buf->status == FREE) {
            struct timespec tsp;
            struct timeval now;
            gettimeofday(&now, NULL);
            tsp.tv_sec = now.tv_sec;
            tsp.tv_nsec = now.tv_usec * 1000;   //nanoseconds
            tsp.tv_sec += BUFF_WAIT_TIME;   // wait for 1 seconds
            pthread_cond_timedwait(&rlog->cond, &rlog->mutex, &tsp);
        }

        if(buf_empty(rlog->persist_buf)) 
        {
            //give up, go to next turn
            pthread_mutex_unlock(&rlog->mutex);
            continue;
        }

        if(rlog->persist_buf->status == FREE) {
            assert(rlog->curr_buf == rlog->persist_buf);
            rlog->curr_buf->status = FULL;
            rlog->curr_buf = rlog->curr_buf->next;
        }

        int year = rlog->utc_timer.year;
        int mon = rlog->utc_timer.mon;
        int day = rlog->utc_timer.day;
        pthread_mutex_unlock(&rlog->mutex);

        //decision which file to write
        if (!decis_file(year, mon, day)) {
            continue;
        }

        //write
        buf_persist(rlog->persist_buf, rlog->fp);
        fflush(rlog->fp);

        pthread_mutex_lock(&rlog->mutex);
        buf_clear(rlog->persist_buf);
        rlog->persist_buf = rlog->persist_buf->next;
        pthread_mutex_unlock(&rlog->mutex);
    }

}

void log_append(const char* lvl, const char* format, ...) {
    ring_log_t *rlog = ins();
    if(rlog == NULL) {
        return;
    }

    int ms;
    uint64_t curr_sec = get_curr_time(&rlog->utc_timer, &ms);
    if(rlog->log_err_sec && curr_sec - rlog->log_err_sec < RELOG_THRESOLD) {
        return;
    }

    char log_line[LOG_LEN_LIMIT];
    int head_len = snprintf(log_line, LOG_LEN_LIMIT, "%s[%s.%03d]", lvl, rlog->utc_timer.utc_format, ms);

    va_list arg_ptr;
    va_start(arg_ptr, format);

    // message body
    int body_len = vsnprintf(log_line + head_len, LOG_LEN_LIMIT - head_len, format, arg_ptr);

    va_end(arg_ptr);

    uint32_t len = head_len + body_len;

    rlog->log_err_sec = 0;
    int notify_back = 0;

    pthread_mutex_lock(&rlog->mutex);
    if(rlog->curr_buf->status == FREE && avail_len(rlog->curr_buf) >= len) {
        buf_append(rlog->curr_buf, log_line, len);
    }
    else {
        //1. curr_buf->status = cell_buffer::FREE but curr_buf->avail_len() < len
        //2. curr_buf->status = cell_buffer::FULL
        if(rlog->curr_buf->status == FREE) {
            rlog->curr_buf->status = FULL;
            cell_buffer_t *next_buf = rlog->curr_buf->next;
            // notify backend thread
            notify_back = 1;

            //it suggest that this buffer is under the persist job
            if(next_buf->status == FULL) {
                //if mem use < MEM_USE_LIMIT, allocate new cell_buffer
                if(rlog->buff_len * (rlog->buff_cnt + 1) > MEM_USE_LIMIT) {
                    rlog->curr_buf = next_buf;
                    rlog->log_err_sec = curr_sec;
                } else {
                    // allocate new cell_buffer, and init it
                    cell_buffer_t *new_buffer = (cell_buffer_t *)tc_malloc(sizeof(cell_buffer_t));
                    init_cell_buffer(new_buffer, rlog->buff_len);

                    rlog->buff_cnt += 1;
                    new_buffer->prev = rlog->curr_buf;
                    rlog->curr_buf->next = new_buffer;
                    new_buffer->next = next_buf;
                    next_buf->prev = new_buffer;
                    rlog->curr_buf = new_buffer;
                }
            } else {
                //next buffer is free, we can use it
                rlog->curr_buf = next_buf;
            }

            if(!rlog->log_err_sec) {
                buf_append(rlog->curr_buf, log_line, len);
            }
        }
        else { //curr_buf->status == cell_buffer::FULL, assert persist is on here too!
            rlog->log_err_sec = curr_sec;
        }
    }
    pthread_mutex_unlock(&rlog->mutex);
    if(notify_back) {
        pthread_cond_signal(&rlog->cond);
    }
}

int decis_file(int year, int mon, int day) {
    ring_log_t *rlog = ins();
    if(rlog == NULL) {
        return -1;
    }

    if(!rlog->env_ok) {
        if(rlog->fp) {
            fclose(rlog->fp);
        }
        rlog->fp = fopen("/dev/null", "w");
        return rlog->fp != NULL;
    }

    if(rlog->fp == NULL) {
        rlog->year = year;
        rlog->mon = mon;
        rlog->day = day;
        char log_path[256] = {};
        sprintf(log_path, "%s/%s.%d%02d%02d.%u.log", rlog->log_dir, rlog->prog_name, rlog->year, 
                                                     rlog->mon, rlog->day, rlog->pid);
        rlog->fp = fopen(log_path, "w");
        if(rlog->fp == NULL) {
            fprintf(stderr, "logdir: %s open error\n", rlog->log_dir);
            return 0;
        }
        rlog->log_cnt += 1;
    }
    else if(rlog->day != day) {
        fclose(rlog->fp);
        char log_path[256] = {};
        rlog->year = year;
        rlog->mon = mon;
        rlog->day = day;
        sprintf(log_path, "%s/%s.%d%02d%02d.%u.log", rlog->log_dir, rlog->prog_name, rlog->year, 
                                                     rlog->mon, rlog->day, rlog->pid);
        rlog->fp = fopen(log_path, "w");
        if(rlog->fp == NULL) {
            fprintf(stderr, "logdir: %s open error\n", rlog->log_dir);
            return 0;
        }
        rlog->log_cnt = 1;
    }
    else if(ftell(rlog->fp) >= LOG_USE_LIMIT) {
        fclose(rlog->fp);
        char old_path[256] = {};
        char new_path[256] = {};
        //mv xxx.log.[i] xxx.log.[i + 1]
        for(int i = rlog->log_cnt - 1; i > 0; i--) {
            sprintf(old_path, "%s/%s.%d%02d%02d.%u.log.%d", rlog->log_dir, rlog->prog_name, rlog->year, 
                                                            rlog->mon, rlog->day, rlog->pid, i);
            sprintf(new_path, "%s/%s.%d%02d%02d.%u.log.%d", rlog->log_dir, rlog->prog_name, rlog->year, 
                                                            rlog->mon, rlog->day, rlog->pid, i + 1);
            rename(old_path, new_path);
        }
        //mv xxx.log xxx.log.1
        sprintf(old_path, "%s/%s.%d%02d%02d.%u.log", rlog->log_dir, rlog->prog_name, rlog->year, 
                                                     rlog->mon, rlog->day, rlog->pid);
        sprintf(new_path, "%s/%s.%d%02d%02d.%u.log.1", rlog->log_dir, rlog->prog_name, rlog->year, 
                                                       rlog->mon, rlog->day, rlog->pid);
        rename(old_path, new_path);

        rlog->fp = fopen(old_path, "w");
        if(rlog->fp == NULL) {
            fprintf(stderr, "logdir: %s open error\n", rlog->log_dir);
            return 0;
        }
        rlog->log_cnt = 1;
    }
    return 1;
}

// persistence thread
void* be_thdo(void* args) {
    // the thread only persist log
    log_persist();
    return NULL;
}