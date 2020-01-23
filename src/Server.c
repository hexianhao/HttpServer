#include <unistd.h>
#include <stdio.h>
#include <getopt.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <gperftools/tcmalloc.h>

#include "http.h"
#include "http_request.h"
#include "http_parse.h"
#include "epoll.h"
#include "timer.h"
#include "threadpool.h"
#include "util.h"

#define CONF                "httpserver.conf"
#define PROGRAM_VERSION     "0.1"

extern int epfd;
extern struct epoll_event *events;

char conf_buf[BUFLEN];
conf_t cf;

static const struct option long_options[]=
{
    {"help",no_argument,NULL,'?'},
    {"version",no_argument,NULL,'V'},
    {"conf",required_argument,NULL,'c'},
    {NULL,0,NULL,0}
};

static void usage() {
   fprintf(stderr,
	"httpserver [option]... \n"
	"  -c|--conf <config file>  Specify config file. Default ./httpserver.conf.\n"
	"  -?|-h|--help             This information.\n"
	"  -V|--version             Display program version.\n"
	);
}

int main(int argc, char* argv[]) {
    int ret;
    int opt;
    int opt_idx = 0;
    char *conf_file = CONF;

    // parse args
    if(argc == 1) {
        usage();
        return 0;
    }

    while ((opt=getopt_long(argc, argv, "Vc:?h", long_options, &opt_idx)) != EOF) {
        switch (opt) {
            case  0 : break;
            case 'c':
                conf_file = optarg;
                break;
            case 'V':
                printf(PROGRAM_VERSION"\n");
                return 0;
            case ':':
            case 'h':
            case '?':
                usage();
                return 0;
        }
    }

    printf("conffile = %s\n", conf_file);

    if (optind < argc) {
        printf("non-option ARGV-elements: ");
        while (optind < argc)
            printf("%s ", argv[optind++]);
        return 0;
    }

    /*
    * read confile file
    */
    ret = read_conf(conf_file, &cf, conf_buf, BUFLEN);
    if(ret != CONF_OK) {
        printf("read conf_file error\n");
        return 0;
    }

    /*
    *   install signal handle for SIGPIPE
    *   when a fd is closed by remote, writing to this fd will cause system send
    *   SIGPIPE to this process, which exit the program
    */
    signal(SIGPIPE, SIG_IGN);

    /*
    * initialize listening socket
    */
    int listenfd = open_listenfd(cf.port);

    /*
    * create epoll and add listenfd to ep
    */
    epfd = Epoll_Create(0);
    struct epoll_event event;

    http_request_t *request = (http_request_t *)tc_malloc(sizeof(http_request_t));
    init_request_t(request, listenfd, epfd, &cf);

    event.data.ptr = (void *)request;
    event.events = EPOLLIN | EPOLLET;
    Epoll_Add(epfd, listenfd, &event);

    // create thread pool
    tpool_t *tpool = tpool_init(cf.thread_num);

    // init log
    LOG_INIT(cf.logdir, cf.progname, cf.loglevel);

    // init timer
    event_timer_init();

    LOG_INFO("httpserver started.");
    uint64_t timer;
    int fd;
    int nready;

    while(1)
    {   
        timer = event_find_timer();
        nready = Epoll_Wait(epfd, events, MAXEVENTS, timer);

        for(int i = 0; i < nready; i++) {
            http_request_t *r = (http_request_t *)events[i].data.ptr;
            fd = r->fd;

            if(fd == listenfd) {  
                tpool_add_work(tpool, handle_conn, (void *)&listenfd);
            } else {
                if(events[i].events & EPOLLIN) {
                    tpool_add_work(tpool, handle_read, (void *)r);
                } else if(events[i].events & EPOLLOUT) {
                    tpool_add_work(tpool, handle_write, (void *)r);
                }
            }
        }

        // check timeout event
        event_expire_timers();
    }

    tpool_destroy(tpool);

    return 0;
}