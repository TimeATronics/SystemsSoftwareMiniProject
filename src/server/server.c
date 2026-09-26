#include "bank.h"
#include "db.h"
#include "ipc.h"
#include "net.h"
#include "handlers.h"
#include "server.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

static int listen_fd = -1;
static volatile sig_atomic_t stop = 0;
static volatile sig_atomic_t child_stop = 0;
static int selfpipe[2] = { -1, -1 };
static pid_t children[256];
static int nchildren = 0;

static void on_sigchld(int sig) { /* Wakes the parent when a child exits. */
    (void)sig;
    if (selfpipe[1] >= 0) {
        ssize_t ignored = write(selfpipe[1], "c", 1);
        (void)ignored;
    }
}

static void on_term(int sig) { /* Requests a graceful server shutdown. */
    (void)sig;
    stop = 1;
    if (selfpipe[1] >= 0) {
        ssize_t ignored = write(selfpipe[1], "t", 1);
        (void)ignored;
    }
}

static void on_child_term(int sig) { /* Requests a graceful handler shutdown. */
    (void)sig;
    child_stop = 1;
}

static void child_loop(int fd) { /* Serves one connected client. */
    int uid = 0;
    pid_t parent = getppid();
    signal(SIGTERM, on_child_term);
    signal(SIGINT, on_child_term);
    signal(SIGPIPE, SIG_IGN);
    while (!child_stop && getppid() == parent) {
        fd_set rfds;
        struct timeval tv;
        struct request req;
        struct response resp;
        int ready;
        FD_ZERO(&rfds);
        FD_SET(fd, &rfds);
        tv.tv_sec = 1;
        tv.tv_usec = 0;
        ready = select(fd + 1, &rfds, NULL, NULL, &tv);
        if (ready < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (ready == 0) continue;
        if (recv_all(fd, &req, sizeof(req)) < 0) break;
        memset(&resp, 0, sizeof(resp));
        handle_request(&req, &resp, &uid, getpid());
        if (send_all(fd, &resp, sizeof(resp)) < 0) break;
    }
    if (uid > 0) session_remove_user(uid);
    session_remove_pid(getpid());
    close(fd);
    _exit(0);
}

static void add_child(pid_t pid) { /* Records a handler pid. */
    if (nchildren < 256) children[nchildren++] = pid;
}

static void reap_children(void) { /* Reaps exited handlers and clears sessions. */
    pid_t pid;
    while ((pid = waitpid(-1, NULL, WNOHANG)) > 0) {
        session_remove_pid(pid);
        for (int i = 0; i < nchildren; i++) {
            if (children[i] == pid) { children[i] = children[--nchildren]; break; }
        }
    }
}

static void shutdown_server(void) { /* Stops handlers, logging, and IPC. */
    stop = 1;
    if (listen_fd >= 0) close(listen_fd);
    for (int i = 0; i < nchildren; i++)
        kill(children[i], SIGTERM);
    for (int i = 0; i < 20; i++) {
        reap_children();
        if (nchildren == 0) break;
        usleep(100000);
    }
    for (int i = 0; i < nchildren; i++)
        kill(children[i], SIGKILL);
    reap_children();
    stop_logger();
    ipc_stop();
    db_close();
}

int main(int argc, char *argv[]) { /* Creates the server, accepts clients, and forks handlers. */
    int port = DEFAULT_PORT;
    int one = 1;
    struct sockaddr_in addr;
    struct sigaction sa;
    if (argc > 1) port = atoi(argv[1]);
    if (port <= 0 || port > 65535) port = DEFAULT_PORT;
    mkdir(DATA_DIR, 0700);
    if (db_open(DATA_DIR) < 0) { perror("db_open"); return 1; }
    if (ipc_start() < 0) { perror("ipc"); return 1; }
    start_logger();
    listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) { perror("socket"); return 1; }
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons((uint16_t)port);
    if (bind(listen_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0 ||
        listen(listen_fd, 16) < 0) {
        perror("bind/listen");
        return 1;
    }
    if (pipe(selfpipe) < 0) { perror("pipe"); return 1; }
    fcntl(selfpipe[0], F_SETFL, O_NONBLOCK);
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = on_sigchld;
    sigaction(SIGCHLD, &sa, NULL);
    sa.sa_handler = on_term;
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGINT, &sa, NULL);
    signal(SIGPIPE, SIG_IGN);
    printf("Bank server ready on 127.0.0.1:%d\n", port);
    fflush(stdout);
    while (!stop) {
        fd_set rfds;
        struct timeval tv;
        int maxfd = listen_fd > selfpipe[0] ? listen_fd : selfpipe[0];
        int ready;
        FD_ZERO(&rfds);
        FD_SET(listen_fd, &rfds);
        FD_SET(selfpipe[0], &rfds);
        tv.tv_sec = 1;
        tv.tv_usec = 0;
        ready = select(maxfd + 1, &rfds, NULL, NULL, &tv);
        if (ready < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (FD_ISSET(selfpipe[0], &rfds)) {
            char tmp[64];
            while (read(selfpipe[0], tmp, sizeof(tmp)) > 0)
                ;
        }
        if (FD_ISSET(listen_fd, &rfds)) {
            int fd = accept(listen_fd, NULL, NULL);
            pid_t pid;
            if (fd < 0) continue;
            pid = fork();
            if (pid < 0) { close(fd); continue; }
            if (pid == 0) {
                close(listen_fd);
                close(selfpipe[0]);
                close(selfpipe[1]);
                child_loop(fd);
            }
            close(fd);
            add_child(pid);
        }
        reap_children();
    }
    shutdown_server();
    return 0;
}
