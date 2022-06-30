#ifndef _TOMIE_H_
#define _TOMIE_H_

#include <sys/uio.h>

struct tomie_loop;

enum tomie_event_type {
    TOMIE_ACCEPT,
    TOMIE_READ,
    TOMIE_WRITE,
    TOMIE_CLEANUP,
};

struct tomie_data {
    enum tomie_event_type event_type;
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

struct tomie_loop *tomie_loop_init();

int tomie_loop_refresh(struct tomie_loop *tl);

int tomie_await(struct tomie_loop *tl, struct tomie_data **ud_ptr);

void tomie_async_forward(struct tomie_data *ud, struct tomie_loop *tl);

void tomie_async_accept(struct tomie_data *ud, struct tomie_loop *tl);

#endif
