#ifndef __HTTP_H
#define __HTTP_H

#include <strings.h>
#include <stdint.h>

#define MAXLINE 8192
#define SHORTLINE 512

#define str3cmp(m, c0, c1, c2, c3)          \
        *(uint32_t *)m == ((c0 << 24) | (c1 << 16) | (c2 << 8) | c3)

typedef struct mime_type_s {
    const char *type;
    const char *value;
}mime_type_t;

// 处理连接的回调函数
void handle_conn(void *ptr);
// 处理读事件的回调函数
void handle_read(void *ptr);
// 处理写事件的回调函数
void handle_write(void *ptr);

#endif