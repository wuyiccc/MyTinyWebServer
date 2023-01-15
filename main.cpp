#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/epoll.h>
#include "locker.h"
#include "threadpool.h"
#include "http_conn.h"

#define MAX_FD 65536   // 最大的文件描述符个数
#define MAX_EVENT_NUMBER 10000  // 监听的最大的事件数量

extern void addfd(int epollfd, int fd, bool one_shot);

extern void removefd(int epollfd, int fd);

void addsig(int sig, void(handler)(int)) {
    struct sigaction sa;
    memset(&sa, '\0', sizeof(sa));
    sa.sa_handler = handler;
    sigfillset(&sa.sa_mask);
    assert(sigaction(sig, &sa, NULL) != -1);
}

int main(int argc, char *argv[]) {

    if (argc < 2) {
        printf("Usage: %s port number\n", basename(argv[0]));
        return 1;
    }

    int port = atoi(argv[1]);
    // Prevents process exit due to tcp sigpipe signals
    addsig(SIGPIPE, SIG_IGN);

    threadpool<http_conn> *pool = NULL;
    try {
        pool = new threadpool<http_conn>;
    } catch (...) {
        return 1;
    }

    http_conn *users = new http_conn[MAX_FD];

    int listenfd = socket(AF_INET, SOCK_STREAM, 0);
    int ret = 0;
    struct sockaddr_in address;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_family = AF_INET;
    address.sin_port = htons(port);

    // port reuse
    int reuse = 1;
    setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    ret = bind(listenfd, (struct sockaddr *) &address, sizeof(address));
    ret = listen(listenfd, 5);

    epoll_event events[MAX_EVENT_NUMBER];
    int epollfd = epoll_create(5);
    addfd(epollfd, listenfd, false);
    http_conn::m_epollfd = epollfd;

    while (true) {
        int number = epoll_wait(epollfd, events, MAX_EVENT_NUMBER, -1);
        if ((number < 0) && (errno != EINTR)) {
            printf("epoll failure\n");
            break;
        }

        for (int i = 0; i < number; i++) {
            int sockfd = events[i].data.fd;
            if (sockfd == listenfd) {

                struct sockaddr_in client_address;
                socklen_t client_address_len = sizeof(client_address);
                int client_fd =
                        accept(listenfd, (struct sockaddr *) &client_address, &client_address_len);

                if (client_fd < 0) {
                    printf("errno is : %d\n", errno);
                    continue;
                }

                if (http_conn::m_user_count >= MAX_FD) {
                    close(client_fd);
                    continue;
                }
                users[client_fd].init(client_fd, client_address);
            } else if (events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)) {
                users[sockfd].close_conn();
            } else if (events[i].events & EPOLLIN) {
                if (users[sockfd].read()) {
                    pool->append(users + sockfd);
                } else {
                    users[sockfd].close_conn();
                }
            } else if (events[i].events & EPOLLOUT) {
                if (!users[sockfd].write()) {
                    users[sockfd].close_conn();
                }
            }
        }

    }
    close(epollfd);
    close(listenfd);
    delete[]users;
    delete pool;
    return 0;
}
