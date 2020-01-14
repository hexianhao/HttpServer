#include <unistd.h>
#include <stdio.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <errno.h>
#include "util.h"

int Bind(int sockfd, const struct sockaddr *myaddr, socklen_t addrlen) {
    int ret = bind(sockfd, myaddr, addrlen);
    if(ret < 0) {
        perror("bind error");
        exit(-1);
    }
    return ret;
}

/*客户端采用非阻塞的connect*/
int Connect(int sockfd, const struct sockaddr *servaddr, socklen_t addrlen, int nsec) {
    int flags, n, error;
    socklen_t len;
    fd_set rset, wset;
    struct timeval tval;

    /*将sockfd设置为非阻塞的*/
    flags = fcntl(sockfd, F_GETFL, 0);
    fcntl(sockfd, F_SETFL, flags | O_NONBLOCK);

    error = 0;
    if((n = connect(sockfd, servaddr, addrlen)) < 0) {
        if(errno != EINPROGRESS) {
            perror("connect error");
            exit(-1);
        }
    }
    if(n == 0)
        goto done;
    
    FD_ZERO(&rset);
    FD_SET(sockfd, &rset);
    wset = rset;
    tval.tv_sec = nsec;
    tval.tv_usec = 0;

    if((n = select(sockfd + 1, &rset, &wset, NULL, &tval)) == 0) {
        /*连接超时*/
        close(sockfd);
        errno = ETIMEDOUT; 
        return -1;
    }

    if(FD_ISSET(sockfd, &rset) || FD_ISSET(sockfd, &wset)) {
        len = sizeof(error);
        if(getsockopt(sockfd, SOL_SOCKET, SO_ERROR, &error, &len) < 0) {
            return -1;
        }
    } else {
        perror("select error");
        exit(-1);
    }

done:
    fcntl(sockfd, F_SETFL, flags);  //复原之前的flags
    if(error) {
        close(sockfd);
        errno = error;
        return -1;
    }
    return 0;
}

int Accept(int sockfd, struct sockaddr *cliaddr, socklen_t *addrlen) {
    int connfd;
    while((connfd = accept(sockfd, cliaddr, addrlen)) < 0) {
        if(errno == EINTR) continue;
        perror("accept error");
        exit(-1);
    }
    return connfd;
}

int Listen(int fd, int backlog) {
    int ret = listen(fd, backlog);
    if(ret < 0) {
        perror("listen error");
        exit(-1);
    }
    return ret;
}


ssize_t Write(int fd, const void *buffer, size_t len) {
    ssize_t n;
    while((n = write(fd, buffer, len)) < 0)
    {
        if(errno == EINTR) continue;
        perror("write error");
        return -1;
    }
    return n;
}

size_t Writev(int fd, const struct iovec* iov, int count) {
    ssize_t n;
    while((n = writev(fd, iov, count)) < 0)
    {
        if(errno == EINTR) continue;
        perror("writev error");
        return -1;
    }
    return n;
}

ssize_t Read(int fd, void *buffer, size_t len) {
    size_t n;
    while((n = read(fd, buffer, len)) < 0) {
        if(errno == EINTR) continue;
        perror("read error");
        return -1;
    }
    return n;
}

size_t Readv(int fd, const struct iovec* iov, int count) {
    size_t n;
    while((n = readv(fd, iov, count)) < 0)
    {
        if(errno == EINTR || errno == EAGAIN) continue;
        perror("readv error");
        return -1;
    }
    return n;
}


ssize_t Readn(int fd, void *vptr, size_t n)
{
    size_t  nleft;              //usigned int 剩余未读取的字节数
    ssize_t nread;              //int 实际读到的字节数
    char    *ptr;

    ptr = (char *)vptr;
    nleft = n;

    while (nleft > 0) {
        if ((nread = read(fd, ptr, nleft)) < 0) {
            if (errno == EINTR)
                nread = 0;
            else
                return -1;
        } else if (nread == 0)
            break;

        nleft -= nread;
        ptr += nread;
    }
    return n - nleft;
}


int open_listenfd(int port) 
{
    if (port <= 0) {
        port = 3000;
    }

    int listenfd, optval=1;
    struct sockaddr_in serveraddr;
  
    /* Create a socket descriptor */
    if ((listenfd = Socket(AF_INET, SOCK_STREAM, 0)) < 0)
	    return -1;
 
    /* Eliminates "Address already in use" error from bind. */
    if (setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, 
		   (const void *)&optval , sizeof(int)) < 0)
	    return -1;

    /* Listenfd will be an endpoint for all requests to port
       on any IP address for this host */
    bzero((char *) &serveraddr, sizeof(serveraddr));
    serveraddr.sin_family = AF_INET; 
    serveraddr.sin_addr.s_addr = htonl(INADDR_ANY); 
    serveraddr.sin_port = htons((unsigned short)port); 
    if (Bind(listenfd, (struct sockaddr *)&serveraddr, sizeof(serveraddr)) < 0)
	    return -1;

    /* Make it a listening socket ready to accept connection requests */
    if (Listen(listenfd, LISTENQ) < 0)
	    return -1;

    return listenfd;
}


/*
    make a socket non blocking. If a listen socket is a blocking socket, after it comes out from epoll and accepts the last connection, the next accpet will block, which is not what we want
*/
int set_socket_non_blocking(int fd) {
    int flags, s;
    flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) {
        log_err("fcntl");
        return -1;
    }

    flags |= O_NONBLOCK;
    s = fcntl(fd, F_SETFL, flags);
    if (s == -1) {
        log_err("fcntl");
        return -1;
    }

    return 0;
}


/*
* Read configuration file
* TODO: trim input line
*/
int read_conf(char *filename, conf_t *cf, char *buf, int len) {
    FILE *fp = fopen(filename, "r");
    if (!fp) {
        log_err("cannot open config file: %s", filename);
        return CONF_ERROR;
    }

    int pos = 0;
    char *delim_pos;
    int line_len;
    char *cur_pos = buf+pos;

    while (fgets(cur_pos, len-pos, fp)) {
        delim_pos = strstr(cur_pos, DELIM);
        line_len = strlen(cur_pos);
        
        /*
        debug("read one line from conf: %s, len = %d", cur_pos, line_len);
        */
        if (!delim_pos)
            return CONF_ERROR;
        
        if (cur_pos[strlen(cur_pos) - 1] == '\n') {
            cur_pos[strlen(cur_pos) - 1] = '\0';
        }

        if (strncmp("root", cur_pos, 4) == 0) {
            cf->root = delim_pos + 1;
        }

        if (strncmp("port", cur_pos, 4) == 0) {
            cf->port = atoi(delim_pos + 1);     
        }

        if (strncmp("loglevel", cur_pos, 8) == 0) {
            cf->loglevel = atoi(delim_pos + 1);     
        }

        if (strncmp("threadnum", cur_pos, 9) == 0) {
            cf->thread_num = atoi(delim_pos + 1);
        }

        if (strncmp("ipaddr", cur_pos, 6) == 0) {
            cf->ipaddr = delim_pos + 1;
        }

        if (strncmp("progname", cur_pos, 8) == 0) {
            cf->progname = delim_pos + 1;
        }

        if (strncmp("logdir", cur_pos, 6) == 0) {
            cf->logdir = delim_pos + 1;
        }

        cur_pos += line_len;
    }

    fclose(fp);
    return CONF_OK;
}