#define _POSIX_SOURCE
#include <malloc.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <liburing.h>
#include <netinet/in.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <systemd/sd-daemon.h>
#include <unistd.h>

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
    if (sd_listen_fds(0) == 1) {
        return SD_LISTEN_FDS_START + 0;
    } else {
        return tomie_listen_port(default_port);
    }
}

/* Server */

enum tomie_await_type {
    TOMIE_READ,
    TOMIE_WRITE,
    TOMIE_CLEANUP,
    TOMIE_REACCEPT,
};

struct tomie_data {
    enum tomie_await_type event_type;
    int connected_socket;
    int listen_socket;
    int iovec_offset;
    int iovec_used;
    struct iovec iov[];
};

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

void tomie_async_accept(struct tomie_data *ud, struct io_uring *ring) {
    struct sockaddr *addr = 0;
    socklen_t len;
    struct io_uring_sqe *sqe = io_uring_get_sqe(ring);
    ud->event_type = TOMIE_READ;
    io_uring_prep_accept(sqe, ud->listen_socket, (struct sockaddr *)addr, &len, 0);
    io_uring_sqe_set_data(sqe, ud);
}

void tomie_async_read(struct tomie_data *ud, struct io_uring *ring) {
    struct io_uring_sqe *sqe = io_uring_get_sqe(ring);
    ud->event_type = TOMIE_WRITE;
    io_uring_prep_readv(
        sqe, ud->connected_socket, &ud->iov[ud->iovec_offset], ud->iovec_used, 0);
    io_uring_sqe_set_data(sqe, ud);
}

void tomie_async_write(struct tomie_data *ud, struct io_uring *ring) {
    struct io_uring_sqe *sqe = io_uring_get_sqe(ring);
    ud->event_type = TOMIE_CLEANUP;
    io_uring_prep_writev(
        sqe, ud->connected_socket, &ud->iov[ud->iovec_offset], ud->iovec_used, 0);
    io_uring_sqe_set_data(sqe, ud);
}

void tomie_async_cleanup(struct tomie_data *ud, struct io_uring *ring) {
    struct io_uring_sqe *sqe = io_uring_get_sqe(ring);
    ud->event_type = TOMIE_REACCEPT;
    io_uring_prep_close(sqe, ud->connected_socket);
    io_uring_sqe_set_data(sqe, ud);
}

void tomie_forward_result(struct io_uring_cqe *cqe, struct io_uring *ring) {
    struct tomie_data *ud = (struct tomie_data *)cqe->user_data;
    if (cqe->res < 0) {
        fprintf(stderr, "async(?, %i): %s\n", ud->event_type, strerror(-cqe->res));
        if (cqe) io_uring_cqe_seen(ring, cqe);
        return;
    }

    switch (ud->event_type) {
    case TOMIE_READ:
        ud->connected_socket = cqe->res;
        tomie_async_read(ud, ring);
        io_uring_submit(ring);
        io_uring_cqe_seen(ring, cqe);
        break;
    case TOMIE_WRITE:
        tomie_async_write(ud, ring);
        io_uring_submit(ring);
        io_uring_cqe_seen(ring, cqe);
        break;
    case TOMIE_CLEANUP:
        tomie_async_cleanup(ud, ring);
        io_uring_submit(ring);
        io_uring_cqe_seen(ring, cqe);
        break;
    case TOMIE_REACCEPT:
        tomie_async_accept(ud, ring);
        io_uring_submit(ring);
        io_uring_cqe_seen(ring, cqe);
        break;
    }
}
