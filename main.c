#define _POSIX_SOURCE
#include <tomie.c>

int main() {
    int listenfd = tomie_listen_with_default(2020);
    if (listenfd < 0) {
        fprintf(stderr, "tomie_listen_with_default(): %m\n");
        return 1;
    }

    struct io_uring ring;
    struct io_uring_cqe *cqe;
    io_uring_queue_init(256, &ring, 0);

    struct tomie_data *ud = tomie_make_data(2, 2, 1024);
    ud->listen_socket = listenfd;
    tomie_async_accept(ud, &ring);
    io_uring_submit(&ring);

    int i = 0;
    while (++i) {
        int ret = io_uring_wait_cqe(&ring, &cqe);
        if (ret < 0) {
            fprintf(stderr, "io_uring_wait_cqe(): %m\n");
            if (cqe) io_uring_cqe_seen(&ring, cqe);
            continue;
        }

        struct tomie_data *ud = (struct tomie_data *)cqe->user_data;
        switch (ud->event_type) {
        case TOMIE_READ:
            ud->iovec_offset = 1;
            ud->iovec_used = 1;
            ud->iov[ud->iovec_offset].iov_len = 1024;
            break;
        case TOMIE_WRITE:
            ud->iov[1].iov_len = cqe->res;
            ud->iovec_offset = 0;
            ud->iovec_used = 2;
            sprintf(ud->iov[0].iov_base,
                "HTTP/1.0 200 OK\r\n"
                "Server: tomie/0.1\r\n"
                "Content-Type: text; charset=utf-8\r\n"
                "Content-Length: %lu\r\n"
                "Connection: close\r\n"
                "\r\n",
                ud->iov[1].iov_len);
            ud->iov[0].iov_len = strlen(ud->iov[0].iov_base);
            break;
        default:
            break;
        }
        tomie_forward_result(cqe, &ring);
    }
}
