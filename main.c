#define _POSIX_SOURCE
#include <include/tomie.h>
#include <stdio.h>
#include <string.h>

int main() {
    int listenfd = tomie_listen_with_default(2020);
    if (listenfd < 0) {
        fprintf(stderr, "tomie_listen_with_default(): %m\n");
        return 1;
    }

    struct tomie_queue *tq = tomie_queue_init();
    if (!tq) {
        fprintf(stderr, "tomie_queue_init(): error\n");
        return 1;
    }
    struct tomie_data *ud = tomie_make_data(2, 2, 1024);
    if (!ud) {
        fprintf(stderr, "tomie_make_data(): error\n");
        return 1;
    }
    ud->listen_socket = listenfd;
    tomie_async_accept(ud, tq);
    tomie_queue_submit(tq);

    int i = 0;
    while (++i) {
        int ret = tomie_await(tq, &ud);
        if (ret < 0) {
            fprintf(stderr, "tomie_await(%d, ?): %s\n", ud->event_type, strerror(-ret));
            continue;
        }

        switch (ud->event_type) {
        case TOMIE_READ:
            ud->iovec_offset = 1;
            ud->iovec_used = 1;
            ud->iov[ud->iovec_offset].iov_len = 1024;
            break;
        case TOMIE_WRITE:
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
        tomie_async_forward(ud, tq);
    }
}
