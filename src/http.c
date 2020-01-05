#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <sys/epoll.h>
#include <netinet/in.h>
#include <string.h>

#include "http.h"
#include "http_parse.h"
#include "http_request.h"
#include "util.h"
#include "timer.h"
#include "epoll.h"
#include "ring_log.h"

extern int epfd;
static const char* get_file_type(const char *type);
static void parse_uri(char *uri, int length, char *filename, char *querystring);
static void do_error(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg);
static void serve_static(int fd, char *filename, size_t filesize, http_out_t *out);
static char *ROOT = NULL;


mime_type_t mime[] = {
    {".html", "text/html"},
    {".xml", "text/xml"},
    {".xhtml", "application/xhtml+xml"},
    {".txt", "text/plain"},
    {".rtf", "application/rtf"},
    {".pdf", "application/pdf"},
    {".word", "application/msword"},
    {".png", "image/png"},
    {".gif", "image/gif"},
    {".jpg", "image/jpeg"},
    {".jpeg", "image/jpeg"},
    {".au", "audio/basic"},
    {".mpeg", "video/mpeg"},
    {".mpg", "video/mpeg"},
    {".avi", "video/x-msvideo"},
    {".gz", "application/x-gzip"},
    {".tar", "application/x-tar"},
    {".css", "text/css"},
    {NULL ,"text/plain"}
};


void handle_conn(void *ptr) {
    int listenfd = *(int *)ptr;
    struct sockaddr_in cliaddr;
    socklen_t len;
    struct epoll_event event;
    int ret;

    int sockfd = Accept(listenfd, (struct sockaddr *)&cliaddr, &len);
    if(sockfd < 0) {
        /* impossibly happen */
        if((errno == EAGAIN) || (errno == EWOULDBLOCK)) {
            return;
        } else {
            LOG_ERROR("accept error");
            return;
        }
    }

    ret = set_socket_non_blocking(sockfd);
    if(ret != 0) {
        LOG_ERROR("socket %d set_socket_blocking error", sockfd);
    }
    LOG_INFO("new connection fd %d", sockfd);

    http_request_t *request = (http_request_t *)malloc(sizeof(http_request_t));
    if(request == NULL) {
        LOG_ERROR("memory error");
        return;
    }

    init_request_t(request, sockfd, 1, NULL);
    event.data.ptr = (void *)request;
    event.events = EPOLLIN | EPOLLET | EPOLLONESHOT;

    Epoll_Add(epfd, sockfd, &event);
    
    /* add timer */
    event_add_timer(request, TIMEOUT_DEFAULT);
}

void handle_read(void *ptr) {
    http_request_t *request = (http_request_t *)ptr;
    int fd = request->fd;
    int ret;
    ssize_t n;
    ROOT = request->root;

    char *plast = NULL;
    size_t remain_size;

    struct epoll_event event = {0, {0}};
    event.data.ptr = ptr;
    Epoll_Del(epfd, fd, &event);

    /* delete timer */
    event_del_timer(request);

    for(;;) {
        plast = &request->buf[request->last % MAX_BUF];
        remain_size = MIN(MAX_BUF - (request->last - request->pos) - 1, MAX_BUF - request->last % MAX_BUF);

        n = Read(fd, plast, remain_size);
        if(request->last - request->pos >= MAX_BUF) {

        }

        if(n == 0) {
            // EOF
            LOG_INFO("fd %d finished", fd);
            goto err;
        }

        if(n < 0) {
            if(errno != EAGAIN) {
                LOG_ERROR("read error");
                goto err;
            }
            break;
        }

        request->last += n;
        if(request->last - request->pos >= MAX_BUF) {
            
        }

        LOG_INFO("ready to parse request line");
        ret = http_parse_request_line(request);
        if(ret == AGAIN) {
            continue;
        } else if (ret != RETURN_OK){
            log_err("rc != OK");
            goto err;
        }

        LOG_INFO("method == %.*s", (int)(request->method_end - request->request_start), (char *)request->request_start);
        LOG_INFO("uri == %.*s", (int)(request->uri_end - request->uri_start), (char *)request->uri_start);

        ret = http_parse_request_body(request);
        if(ret == AGAIN) {
            continue;
        } else if (ret != RETURN_OK){
            log_err("rc != OK");
            goto err;
        }
    }

    event.data.ptr = ptr;
    event.events = EPOLLOUT | EPOLLET | EPOLLONESHOT;

    Epoll_Add(epfd, fd, &event);

    return;

err:
    ret = http_close_conn(request);
    if(ret != 0) {
        log_err("http close error");
    }
}


void handle_write(void *ptr) {
    http_request_t *request = (http_request_t *)ptr;
    int fd = request->fd;
    int ret;
    ssize_t n;
    char filename[SHORTLINE];
    struct stat sbuf;

    struct epoll_event event = {0, {0}};
    event.data.ptr = ptr;
    Epoll_Del(epfd, fd, &event);

    /*
    *   handle http header
    */
    http_out_t *out = (http_out_t *)malloc(sizeof(http_out_t));
    if (out == NULL) {
        LOG_ERROR("no enough space for http_out_t");
        exit(1);
    }

    ret = init_out_t(out, fd);
    if(ret != RETURN_OK) {

    }

    parse_uri(request->uri_start, request->uri_end - request->uri_start, filename, NULL);

    if(stat(filename, &sbuf) < 0) {
        do_error(fd, filename, "404", "Not Found", "httpserver can't find the file");
        goto fin;
    }

    if (!(S_ISREG(sbuf.st_mode)) || !(S_IRUSR & sbuf.st_mode))
    {
        do_error(fd, filename, "403", "Forbidden",
                "httpserver can't read the file");
        goto fin;
    }

    out->mtime = sbuf.st_mtime;

    http_handle_header(request, out);
    if(list_empty(&(request->list)) == 0) {

    }

    if(out->status == 0) {
        out->status = HTTP_OK;
    }

    serve_static(fd, filename, sbuf.st_size, out);

    if(!out->keep_alive) {
        LOG_INFO("no keep_alive! ready to close");
        free_out_t(out);
        goto fin;
    }

    event.data.ptr = ptr;
    event.events = EPOLLIN | EPOLLET | EPOLLONESHOT;

    Epoll_Add(epfd, fd, &event);
    event_add_timer(request, TIMEOUT_DEFAULT);

    return;

fin:
    ret = http_close_conn(request);
    if(ret != 0) {
        LOG_ERROR("http close error");
    }

}

static void parse_uri(char *uri, int uri_length, char *filename, char *querystring) {
    if(uri == NULL) {
        perror("URL is NULL");
        return;
    }
    uri[uri_length] = '\0';

    char *question_mark = strchr(uri, '?');
    int file_length;
    if(question_mark) {
        file_length = (int)(question_mark - uri);
    } else {
        file_length = uri_length;
    }

    if(querystring) {
        //TODO
    }

    strcpy(filename, ROOT);

    // uri_length can not be too long
    if (uri_length > (SHORTLINE >> 1)) {
        LOG_ERROR("uri too long: %.*s", uri_length, uri);
        return;
    }

    strncat(filename, uri, file_length);

    char *last_comp = strrchr(filename, '/');
    char *last_dot = strrchr(last_comp, '.');
    if (last_dot == NULL && filename[strlen(filename)-1] != '/') {
        strcat(filename, "/");
    }
    
    if(filename[strlen(filename)-1] == '/') {
        strcat(filename, "index.html");
    }

    LOG_ERROR("request file %s", filename);
    return;
}

static void do_error(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg)
{
    char header[MAXLINE], body[MAXLINE];

    sprintf(body, "<html><title>Swift Error</title>");
    sprintf(body, "%s<body bgcolor=""ffffff"">\n", body);
    sprintf(body, "%s%s: %s\n", body, errnum, shortmsg);
    sprintf(body, "%s<p>%s: %s\n</p>", body, longmsg, cause);
    sprintf(body, "%s<hr><em>Swift web server</em>\n</body></html>", body);

    sprintf(header, "HTTP/1.1 %s %s\r\n", errnum, shortmsg);
    sprintf(header, "%sServer: Swift\r\n", header);
    sprintf(header, "%sContent-type: text/html\r\n", header);
    sprintf(header, "%sConnection: close\r\n", header);
    sprintf(header, "%sContent-length: %d\r\n\r\n", header, (int)strlen(body));
    
    Write(fd, header, strlen(header));
    Write(fd, body, strlen(body));
    
    return;
}


static void serve_static(int fd, char *filename, size_t filesize, http_out_t *out) {
    char header[MAXLINE];
    char buf[SHORTLINE];
    size_t n;
    struct tm tm;
    
    const char *file_type;
    const char *dot_pos = strrchr(filename, '.');
    file_type = get_file_type(dot_pos);

    sprintf(header, "HTTP/1.1 %d %s\r\n", out->status, get_shortmsg_from_status_code(out->status));

    if (out->keep_alive) {
        sprintf(header, "%sConnection: keep-alive\r\n", header);
        sprintf(header, "%sKeep-Alive: timeout=%d\r\n", header, TIMEOUT_DEFAULT);
    }

    if (out->modified) {
        sprintf(header, "%sContent-type: %s\r\n", header, file_type);
        sprintf(header, "%sContent-length: %zu\r\n", header, filesize);
        localtime_r(&(out->mtime), &tm);
        strftime(buf, SHORTLINE,  "%a, %d %b %Y %H:%M:%S GMT", &tm);
        sprintf(header, "%sLast-Modified: %s\r\n", header, buf);
    }

    sprintf(header, "%sServer: Swift\r\n", header);
    sprintf(header, "%s\r\n", header);

    n = Write(fd, header, strlen(header));
    
    if (n != strlen(header)) {
        LOG_ERROR("n != strlen(header)");
        goto fin; 
    }

    if (!out->modified) {
        goto fin;
    }

    int srcfd = open(filename, O_RDONLY, 0);
    if(srcfd < 0) {
        perror("open file error");
        goto fin;
    }
    // can use sendfile
    char *srcaddr = mmap(NULL, filesize, PROT_READ, MAP_PRIVATE, srcfd, 0);
    if(srcaddr == NULL) {
        perror("mmap error");
        close(srcfd);
        goto fin;
    }
    close(srcfd);

    n = Write(fd, srcaddr, filesize);

    munmap(srcaddr, filesize);

fin:
    return;
}


static const char* get_file_type(const char *type)
{
    if (type == NULL) {
        return "text/plain";
    }

    int i;
    for (i = 0; mime[i].type != NULL; ++i) {
        if (strcmp(type, mime[i].type) == 0)
            return mime[i].value;
    }
    return mime[i].value;
}