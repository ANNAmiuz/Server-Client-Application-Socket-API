#include <atomic>
#include <iostream>
#include <string>
#include <thread>
#include <sys/epoll.h>
#include <fcntl.h>

#include "message.pb.h"

#include "utils.h"

#define MAXEVENT 2048
#define DEBUGx

std::mutex print_lock_server;

/* thread-safe version of display */
void ts_printf(int64_t counter)
{
    print_lock_server.lock();
    printf("%ld\n", counter);
    print_lock_server.unlock();
    fflush(stdout);
}

// set fd in non-blocking manner
void setnonblocking(int fd)
{
    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);
}

/* ---------------------------------------------------------------------- */
std::atomic<int64_t> number{0};
std::atomic<int64_t> conn_nb{0};

/* storage of epoll fd per thread */
std::vector<int> epfds;

/* storage for epoll events per thread */
// std::vector<struct epoll_event *> epevents;

/* thread routine listen on the port and accept new connections */
void listen_server(int listenfd, int num_threads)
{
    int connfd{0}, cur_conn_nb{0};
    struct epoll_event ev;
    struct epoll_event events[MAXEVENT];
    bzero(&events, sizeof(events));

    /* add listen fd to epoll sets */
    bzero(&ev, sizeof(ev));
    ev.data.fd = listenfd;
    ev.events = EPOLLIN;
    // setnonblocking(connfd);
    epoll_ctl(epfds[0], EPOLL_CTL_ADD, listenfd, &ev);

    /* event loop */
    while (true)
    {
        /* epoll wait for all new events */
#ifdef DEBUG
        fprintf(stderr, "listen thread: doing epoll wait on fd = %d\n", epfds[0]);
#endif
        int nfds = epoll_wait(epfds[0], events, MAXEVENT, -1);
        err_report(nfds == -1, "epoll wait error");
#ifdef DEBUG
        fprintf(stderr, "listen thread: doing epoll wait get %d fds\n", nfds);
#endif
        /* deal with each new event */
        for (int i = 0; i < nfds; i++)
        {
            /* accept new connections */
            if (events[i].data.fd == listenfd)
            {
#ifdef DEBUG
                fprintf(stderr, "listen thread: epoll wait get listenfd == %d\n", listenfd);
#endif
                connfd = accept_connection(listenfd);
                err_report(connfd == 1, "accept connection fail");
                cur_conn_nb = conn_nb.fetch_add(1);

                /* send connfd to the target threads */
                int target_thread = cur_conn_nb % num_threads;
                bzero(&ev, sizeof(ev));
                ev.data.fd = connfd;
                ev.events = EPOLLIN | EPOLLET;
                // setnonblocking(connfd);
                epoll_ctl(epfds[target_thread], EPOLL_CTL_ADD, connfd, &ev);
#ifdef DEBUG
                fprintf(stderr, "listen thread: epoll wait accpet connfd == %d\n", connfd);
#endif
            }

            /* connfd event */
            else if (events[i].events & EPOLLIN)
            {
                int connfd = events[i].data.fd;
#ifdef DEBUG
                fprintf(stderr, "listen thread: epoll wait get connfd == %d\n", connfd);
#endif
                while (true)
                {
                    // recv mesg from client connfd
                    int32_t operation_type{0};
                    int64_t argument{0};
                    int ret = recv_msg(connfd, &operation_type, &argument);
                    err_report(ret == 1, "recv msg fail");

                    int cur_number{0};
                    // ADD
                    if (operation_type == 1)
                        cur_number = number.fetch_add(argument);
                    // SUB
                    else if (operation_type == 2)
                        cur_number = number.fetch_add(-argument);
                    // TERMINATION: send counter back and break
                    else if (operation_type == 3)
                    {
                        cur_number = number.load();
                        send_msg(connfd, 4, cur_number);
                        close(connfd);
                        ts_printf(cur_number);

#ifdef DEBUG
                        fprintf(stderr, "response is %d on closed fd %d\n", number.load(), connfd);
#endif
                        break;
                    }
#ifdef DEBUG
                    // fprintf(stderr, "current connfd is %d\n", connfd);
                    // fprintf(stderr, "epoll wait get connfd == %d\n", connfd);

#endif
                }
            }
#ifdef DEBUG
            // fprintf(stderr, "current connfd is %d\n", connfd);
#endif
        }
    }
}

/* thread routine work on its epoll set of connfds */
void conn_server(int ep_idx)
{
    struct epoll_event events[MAXEVENT];
    bzero(&events, sizeof(events));
#ifdef DEBUG
    // printf("current thread running epoll_wait\n");
#endif
    /* event loop */
    while (true)
    {
        /* epoll wait for all new events */
        int nfds = epoll_wait(epfds[ep_idx], events, MAXEVENT, -1);
        err_report(nfds == -1, "epoll wait error");
#ifdef DEBUG
        // printf("current epoll_wait return is %d\n", nfds);
#endif
        /* deal with each new event */
        for (int i = 0; i < nfds; i++)
        {
            if (events[i].events & EPOLLIN)
            {
                while (true)
                {
#ifdef DEBUG
                    // printf("current number is %d\n", number.load());
#endif
                    // recv mesg from client connfd
                    int32_t operation_type{0};
                    int64_t argument{0};
                    int connfd = events[i].data.fd;
                    int ret = recv_msg(connfd, &operation_type, &argument);
                    // err_report(ret == 1, "recv msg fail");

                    int cur_number{0};
                    // ADD
                    if (operation_type == 1)
                        cur_number = number.fetch_add(argument);
                    // SUB
                    else if (operation_type == 2)
                        cur_number = number.fetch_add(-argument);
                    // TERMINATION: send counter back and break
                    else if (operation_type == 3)
                    {
                        cur_number = number.load();
                        send_msg(connfd, 4, cur_number);
                        close(connfd);
                        ts_printf(cur_number);
                        break;
                    }
#ifdef DEBUG
                    printf("current connfd is %d\n", connfd);

#endif
                }
            }
        }
    }
}

int main(int args, char *argv[])
{
    if (args < 3)
    {
        std::cerr << "usage: ./server <numThreads> <port>\n";
        exit(1);
    }

    /* get server argument */
    int numThreads = std::atoi(argv[1]);
    int port = std::atoi(argv[2]);

    /* listen fd init */
    int listenfd = listening_socket(port);
    err_report(listenfd == -1, "listen socket error");

    /* spawn threads: work on connfd after accept */
    std::vector<std::thread> workers;

    /* worker on listenfd: on main thread */
    // workers.push_back(std::thread{listen_server, listenfd, numThreads});
    int epfd = epoll_create1(0);
    err_report(epfd == -1, "epoll create fail");
    epfds.push_back(epfd); // epfds[0] for main thread

    /* worker on connfd */
    for (int i = 1; i < numThreads; ++i)
    {
        /* epoll fd for each worker thread */
        epfd = epoll_create1(0);
        err_report(epfd == -1, "epoll create fail");
        epfds.push_back(epfd);

        /* create worker thread */
        workers.push_back(std::thread{conn_server, i});
    }

    /* worker on listenfd*/
    listen_server(listenfd, numThreads);

    /* join threads */
    for (auto &worker : workers)
    {
        worker.join();
    }

    return 0;
}
