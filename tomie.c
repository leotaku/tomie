#define _POSIX_SOURCE
#include <liburing.h>
#include <malloc.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <systemd/sd-daemon.h>
#include <unistd.h>

#include <netinet/in.h>
#include <stdlib.h>
#include <sys/ioctl.h>

enum {
    AWAIT_NOOP,
    AWAIT_ACCEPT,
    AWAIT_READ,
    AWAIT_WRITE,
    AWAIT_SHUTDOWN,
} await_type;

int listen_on(int port) {
    union {
        struct sockaddr sa;
        struct sockaddr_in in;
    } sa;
    int fd, rc;

    fd = socket(PF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        fprintf(stderr, "socket(): %m\n");
        exit(1);
    }

    rc = setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &(int){1}, sizeof(fd));
    if (rc < 0) {
        fprintf(stderr, "setsockopt(): %m\n");
        close(fd);
        exit(1);
    }

    memset(&sa, 0, sizeof(sa));
    sa.in.sin_family = AF_INET;
    sa.in.sin_port = htons(port);
    sa.in.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(fd, &sa.sa, sizeof(sa)) < 0) {
        fprintf(stderr, "bind(): %m\n");
        exit(1);
    }

    if (listen(fd, SOMAXCONN) < 0) {
        fprintf(stderr, "listen(): %m\n");
        exit(1);
    }

    return fd;
}

struct udata {
    int event_type;
    int iovec_count;
    int socket;
    struct iovec iov[];
};

void async_accept(int listenfd, struct io_uring *ring) {
    struct udata *ud = calloc(1, sizeof(*ud) + sizeof(struct iovec));
    struct sockaddr *addr = 0;
    socklen_t len;

    struct io_uring_sqe *sqe = io_uring_get_sqe(ring);
    // sqe->ioprio = IOPRIO_PRIO_VALUE(IOPRIO_CLASS_BE, 4);
    // sqe->flags |= IOSQE_ASYNC;
    ud->event_type = AWAIT_ACCEPT;
    io_uring_prep_accept(sqe, listenfd, (struct sockaddr *)addr, &len, 0);
    io_uring_sqe_set_data(sqe, ud);
}

void async_read(struct udata *ud, struct io_uring *ring) {
    struct io_uring_sqe *sqe = io_uring_get_sqe(ring);
    // sqe->ioprio = IOPRIO_PRIO_VALUE(IOPRIO_CLASS_BE, 1);
    // sqe->flags |= IOSQE_ASYNC;
    ud->event_type = AWAIT_READ;
    ud->iovec_count = 1;
    ud->iov[0].iov_base = realloc(ud->iov[0].iov_base, 512);
    ud->iov[0].iov_len = 512;
    io_uring_prep_readv(sqe, ud->socket, &ud->iov[0], 1, 0);
    io_uring_sqe_set_data(sqe, ud);
}

void async_write(struct udata *ud, struct io_uring *ring) {
    struct io_uring_sqe *sqe = io_uring_get_sqe(ring);
    // sqe->ioprio = IOPRIO_PRIO_VALUE(IOPRIO_CLASS_BE, 1);
    // sqe->flags |= IOSQE_ASYNC;
    ud->event_type = AWAIT_WRITE;
    ud->iovec_count = 1;
    ud->iov[0].iov_base = realloc(ud->iov[0].iov_base, 512);
    ud->iov[0].iov_len = 512;
    strcpy(ud->iov[0].iov_base,
        "HTTP/1.0 200 OK\r\n"
        "Server: tomie/0.1\r\n"
        "Content-Type: text; charset=utf-8\r\n"
        "Content-Length: 7\r\n"
        "Connection: close\r\n"
        "\r\n"
        "Hello\r\n\0");
    ud->iov[0].iov_len = strlen(ud->iov[0].iov_base);
    io_uring_prep_writev(sqe, ud->socket, ud->iov, ud->iovec_count, 0);
    io_uring_sqe_set_data(sqe, ud);
}

void async_cleanup(struct udata *ud, struct io_uring *ring) {
    struct io_uring_sqe *sqe = io_uring_get_sqe(ring);
    // sqe->ioprio = IOPRIO_PRIO_VALUE(IOPRIO_CLASS_BE, 0);
    // sqe->flags |= IOSQE_ASYNC;
    ud->event_type = AWAIT_SHUTDOWN;
    io_uring_prep_close(sqe, ud->socket);
    io_uring_sqe_set_data(sqe, ud);

    for (int i = 0; i < ud->iovec_count; i++) {
        free(ud->iov[i].iov_base);
    };
}

int main() {
    int listenfd;
    if (sd_listen_fds(0) == 1) {
        listenfd = SD_LISTEN_FDS_START + 0;
    } else {
        listenfd = listen_on(2020);
    }

    struct io_uring ring;
    struct io_uring_cqe *cqe;
    io_uring_queue_init(256, &ring, 0);
    async_accept(listenfd, &ring);
    io_uring_submit(&ring);

    int concurrent = 0;
    int i = 0;
    while (++i) {
        int ret = io_uring_wait_cqe(&ring, &cqe);
        if (ret < 0) {
            fprintf(stderr, "io_uring_wait_cqe(): %m\n");
            if (cqe) io_uring_cqe_seen(&ring, cqe);
            continue;
        }

        struct udata *ud = (struct udata *)cqe->user_data;
        if (cqe->res < 0) {
            fprintf(stderr, "async(?, %i): %s\n", ud->event_type, strerror(-cqe->res));
            if (cqe) io_uring_cqe_seen(&ring, cqe);
            continue;
        }

        switch (ud->event_type) {
        case AWAIT_ACCEPT:
            ud->socket = cqe->res;
            concurrent++;
            async_read(ud, &ring);
            async_accept(listenfd, &ring);
            io_uring_submit(&ring);
            break;
        case AWAIT_READ:
            async_write(ud, &ring);
            io_uring_submit(&ring);
            break;
        case AWAIT_WRITE:
            async_cleanup(ud, &ring);
            io_uring_submit(&ring);
            concurrent--;
            break;
        }
        io_uring_cqe_seen(&ring, cqe);
    }
}
