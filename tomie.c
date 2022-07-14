#define _GNU_SOURCE
#include <malloc.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <liburing.h>
#include <netinet/in.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

#include "include/tomie.h"

/* Listen */

int tomie_listen_port(int port) {
    union {
        struct sockaddr sa;
        struct sockaddr_in in;
    } sa;
    int fd, rc;

    fd = socket(PF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        return fd;
    }

    rc = setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &(int){1}, sizeof(fd));
    if (rc < 0) {
        close(fd);
        return rc;
    }

    memset(&sa, 0, sizeof(sa));
    sa.in.sin_family = AF_INET;
    sa.in.sin_port = htons(port);
    sa.in.sin_addr.s_addr = htonl(INADDR_ANY);

    if ((rc = bind(fd, &sa.sa, sizeof(sa)) < 0)) {
        close(fd);
        return rc;
    }

    if ((rc = listen(fd, SOMAXCONN)) < 0) {
        close(fd);
        return rc;
    }

    return fd;
}

int tomie_listen_with_default(int default_port) {
    return tomie_listen_port(default_port);
}

/* Data */

struct tomie_data *tomie_make_data(int nmemb, int initialize, int size) {
    struct tomie_data *ud = calloc(1, sizeof(*ud) + sizeof(struct iovec) * nmemb);
    ud->iovec_used = initialize;
    for (int i = 0; i < initialize; i++) {
        ud->iov[i].iov_base = calloc(size, sizeof(char));
        ud->iov[i].iov_len = size;
    }

    return ud;
}

void tomie_free_data(struct tomie_data *ud) {
    for (int i = 0; i < ud->iovec_used; i++) {
        free(ud->iov[i].iov_base);
    }
    free(ud);
}

/* Loop */

struct tomie_loop {
    struct io_uring ring;
};

struct tomie_loop *tomie_loop_init() {
    struct tomie_loop *tl = calloc(1, sizeof(*tl));
    io_uring_queue_init(256, &tl->ring, 0);
    return tl;
}

int tomie_loop_refresh(struct tomie_loop *tl) { return io_uring_submit(&tl->ring); }

int tomie_await(struct tomie_loop *tl, struct tomie_data **ud_ptr) {
    struct io_uring_cqe *cqe;
    int ret = io_uring_wait_cqe(&tl->ring, &cqe);
    if (ret < 0) {
        return ret;
    }
    io_uring_cqe_seen(&tl->ring, cqe);
    if (cqe->res < 0) {
        return cqe->res;
    }

    struct tomie_data *ud = (struct tomie_data *)cqe->user_data;
    switch (ud->event_type) {
    case TOMIE_READ:
        ud->connected_socket = cqe->res;
        break;
    case TOMIE_WRITE:
        for (int i = ud->iovec_offset; i < ud->iovec_offset + ud->iovec_used; i++) {
            if (ud->iov[i].iov_len >= (size_t)(cqe->res)) {
                ud->iov[i].iov_len = cqe->res;
            } else {
                cqe->res -= ud->iov[i].iov_len;
            }
        }
        break;
    default:
        break;
    }

    *ud_ptr = ud;
    return 0;
}

void tomie_async_accept(struct tomie_data *ud, struct tomie_loop *tl) {
    struct sockaddr *addr = 0;
    socklen_t len;
    struct io_uring_sqe *sqe = io_uring_get_sqe(&tl->ring);
    ud->event_type = TOMIE_READ;
    io_uring_prep_accept(sqe, ud->listen_socket, (struct sockaddr *)addr, &len, 0);
    io_uring_sqe_set_data(sqe, ud);
}

void tomie_async_read(struct tomie_data *ud, struct tomie_loop *tl) {
    struct io_uring_sqe *sqe = io_uring_get_sqe(&tl->ring);
    ud->event_type = TOMIE_WRITE;
    io_uring_prep_readv(
        sqe, ud->connected_socket, &ud->iov[ud->iovec_offset], ud->iovec_used, 0);
    io_uring_sqe_set_data(sqe, ud);
}

void tomie_async_write(struct tomie_data *ud, struct tomie_loop *tl) {
    struct io_uring_sqe *sqe = io_uring_get_sqe(&tl->ring);
    ud->event_type = TOMIE_CLEANUP;
    io_uring_prep_writev(
        sqe, ud->connected_socket, &ud->iov[ud->iovec_offset], ud->iovec_used, 0);
    io_uring_sqe_set_data(sqe, ud);
}

void tomie_async_cleanup(struct tomie_data *ud, struct tomie_loop *tl) {
    struct io_uring_sqe *sqe = io_uring_get_sqe(&tl->ring);
    ud->event_type = TOMIE_ACCEPT;
    io_uring_prep_close(sqe, ud->connected_socket);
    io_uring_sqe_set_data(sqe, ud);
}

void tomie_async_forward(struct tomie_data *ud, struct tomie_loop *tl) {
    switch (ud->event_type) {
    case TOMIE_ACCEPT:
        tomie_async_accept(ud, tl);
        io_uring_submit(&tl->ring);
        break;
    case TOMIE_READ:
        tomie_async_read(ud, tl);
        io_uring_submit(&tl->ring);
        break;
    case TOMIE_WRITE:
        tomie_async_write(ud, tl);
        io_uring_submit(&tl->ring);
        break;
    case TOMIE_CLEANUP:
        tomie_async_cleanup(ud, tl);
        io_uring_submit(&tl->ring);
        break;
    }
}
