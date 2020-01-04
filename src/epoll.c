#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include "epoll.h"

struct epoll_event *events;

int Epoll_Create(int flags) {
    int fd = epoll_create1(flags);
    if(fd < 0) {
        perror("epoll_create error");
        return -1;
    }

    events = (struct epoll_event *)malloc(sizeof(struct epoll_event) * MAXEVENTS);
    if(events == NULL) {
        perror("memory error");
        return -1;
    }

    return fd;
}

void Epoll_Add(int epfd, int fd, struct epoll_event *event) {
    int ret = epoll_ctl(epfd, EPOLL_CTL_ADD, fd, event);
    if(ret < 0) {
        perror("epoll_add error");
    }
    return;
}

void Epoll_Mod(int epfd, int fd, struct epoll_event *event) {
    int ret = epoll_ctl(epfd, EPOLL_CTL_MOD, fd, event);
    if(ret < 0) {
        perror("epoll_mod error");
    }
    return;
}

void Epoll_Del(int epfd, int fd, struct epoll_event *event) {
    int ret = epoll_ctl(epfd, EPOLL_CTL_DEL, fd, event);
    if(ret < 0) {
        perror("epoll_del error");
    }
    return;
}

int Epoll_Wait(int epfd, struct epoll_event *events, int maxevents, int timeout) {
    int n = epoll_wait(epfd, events, maxevents, timeout);
    if(n < 0) {
        return -1;
    }
    return n;
}