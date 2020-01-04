#ifndef __UTIL_H
#define __UTIL_H

#include <sys/socket.h>

#define PORT_NUM 80
#define SERV_IP "192.168.8.100"

// max number of listen queue
#define LISTENQ     1024

#define BUFLEN      8192

#define DELIM       "="

#define CONF_OK      0
#define CONF_ERROR   100

#define MIN(a,b) ((a) < (b) ? (a) : (b))

struct conf_s {
    void *root;
    int port;
    int thread_num;
};

typedef struct conf_s conf_t;

int open_listenfd(int port);
int set_socket_non_blocking(int fd);

int read_conf(char *filename, conf_t *cf, char *buf, int len);

int Socket(int family, int type, int protocol);
int Connect(int sockfd, const struct sockaddr *servaddr, socklen_t addrlen, int nsec);
int Bind(int sockfd, const struct sockaddr *myaddr, socklen_t addrlen);
int Listen(int sockfd, int backlong);
int Accept(int sockfd, struct sockaddr *cliaddr, socklen_t *addrlen);
ssize_t Read(int fd, void *buffer, size_t len);
size_t Readline(int fd, char *buffer, size_t maxlen);
ssize_t Write(int fd, const void *buffer, size_t len);
ssize_t Readn(int fd, void *vptr, size_t n);

#endif