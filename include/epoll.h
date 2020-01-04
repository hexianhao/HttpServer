#ifndef __EPOLL_H
#define __EPOLL_H

#include <sys/epoll.h>

#define MAXEVENTS 1024

extern int epfd;

int Epoll_Create(int flags);
void Epoll_Add(int epfd, int fd, struct epoll_event *event);
void Epoll_Mod(int epfd, int fd, struct epoll_event *event);
void Epoll_Del(int epfd, int fd, struct epoll_event *event);
int Epoll_Wait(int epfd, struct epoll_event *events, int maxevents, int timeout);

#endif