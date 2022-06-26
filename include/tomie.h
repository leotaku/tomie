#ifndef _TOMIE_H_
#define _TOMIE_H_

#include <sys/uio.h>

struct tomie_queue;

enum tomie_await_type {
    TOMIE_ACCEPT,
    TOMIE_READ,
    TOMIE_WRITE,
    TOMIE_CLEANUP,
};

struct tomie_data {
    enum tomie_await_type event_type;
    int connected_socket;
    int listen_socket;
    int iovec_offset;
    int iovec_used;
    struct iovec iov[];
};

int tomie_listen_port(int port);

int tomie_listen_with_default(int default_port);

struct tomie_data *tomie_make_data(int nmemb, int initialize, int size);

void tomie_free_data(struct tomie_data *ud);

struct tomie_queue *tomie_queue_init();

int tomie_queue_submit(struct tomie_queue *tq);

int tomie_await(struct tomie_queue *tq, struct tomie_data **ud_ptr);

void tomie_async_forward(struct tomie_data *ud, struct tomie_queue *tq);

void tomie_async_accept(struct tomie_data *ud, struct tomie_queue *tq);

#endif
