#define _POSIX_SOURCE
#include "include/tomie.h"
#include "tomie_http.c"
#include <sqlite3.h>
#include <stdio.h>
#include <string.h>

static sqlite3 *db;

#define FLAG_SQLITE_OPEN \
    SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX

void init_db() {
    int rc;
    if ((rc = sqlite3_initialize())) {
        fprintf(stderr, "internal: initializing sqlite: %s", sqlite3_errstr(rc));
        abort();
    } else if ((rc = sqlite3_open_v2("tomie.db", &db, FLAG_SQLITE_OPEN, 0))) {
        fprintf(stderr, "internal: opening database: %s", sqlite3_errstr(rc));
        abort();
    }
}

#define SQL_GET_PAGE "SELECT content FROM routes WHERE uri == ?"

void write_handler(struct iovec iov[]) {
    struct tomie_http_parse_result result;
    int rc = tomie_http_path(iov[0].iov_base, iov[0].iov_len, &result);
    if (rc) {
        sprintf(iov[1].iov_base, "Bad request: %d\r\n", rc);
        iov[1].iov_len = strlen(iov[1].iov_base);
        sprintf(iov[0].iov_base,
            "HTTP/1.0 400 Bad Request\r\n"
            "Server: tomie/0.1\r\n"
            "Content-Type: text; charset=utf-8\r\n"
            "Content-Length: %lu\r\n"
            "Connection: close\r\n"
            "\r\n",
            iov[1].iov_len);
    } else if (result.method != 1) {
        sprintf(iov[1].iov_base, "Method not allowed: %d\r\n", result.method);
        iov[1].iov_len = strlen(iov[1].iov_base);
        sprintf(iov[0].iov_base,
            "HTTP/1.0 405 Method Not Allowed\r\n"
            "Server: tomie/0.1\r\n"
            "Content-Type: text; charset=utf-8\r\n"
            "Content-Length: %lu\r\n"
            "Connection: close\r\n"
            "\r\n",
            iov[1].iov_len);
    } else {
        if (!db) init_db();
        sqlite3_stmt *pStmt;
        sqlite3_prepare(db, SQL_GET_PAGE, strlen(SQL_GET_PAGE), &pStmt, 0);
        sqlite3_bind_text(pStmt, 1, result.path_start, result.path_length, 0);
        int rc = sqlite3_step(pStmt);
        if (rc == SQLITE_ROW) {
            const unsigned char *text = sqlite3_column_text(pStmt, 0);
            sprintf(iov[0].iov_base, "%s", text);
            /* iov[1].iov_len = strlen(iov[1].iov_base); */
            /* sprintf(iov[0].iov_base, */
            /*     "HTTP/1.0 200 OK\r\n" */
            /*     "Server: tomie/0.1\r\n" */
            /*     "Content-Type: text; charset=utf-8\r\n" */
            /*     "Content-Length: %lu\r\n" */
            /*     "Connection: close\r\n" */
            /*     "\r\n", */
            /*     iov[1].iov_len); */
        } else if (rc == SQLITE_DONE) {
            sprintf(iov[1].iov_base, "Not found: %s\r\n", sqlite3_errmsg(db));
            iov[1].iov_len = strlen(iov[1].iov_base);
            sprintf(iov[0].iov_base,
                "HTTP/1.0 404 Not Found\r\n"
                "Server: tomie/0.1\r\n"
                "Content-Type: text; charset=utf-8\r\n"
                "Content-Length: %lu\r\n"
                "Connection: close\r\n"
                "\r\n",
                iov[1].iov_len);
        } else {
            sprintf(iov[1].iov_base, "Internal server error: %s\r\n", sqlite3_errmsg(db));
            iov[1].iov_len = strlen(iov[1].iov_base);
            sprintf(iov[0].iov_base,
                "HTTP/1.0 500 Internal Server Error\r\n"
                "Server: tomie/0.1\r\n"
                "Content-Type: text; charset=utf-8\r\n"
                "Content-Length: %lu\r\n"
                "Connection: close\r\n"
                "\r\n",
                iov[1].iov_len);
        };
        sqlite3_finalize(pStmt);
    }
    iov[0].iov_len = strlen(iov[0].iov_base);
}

int main() {
    int listenfd = tomie_listen_with_default(2020);
    if (listenfd < 0) {
        fprintf(stderr, "tomie_listen_with_default(): %s\n", strerror(-listenfd));
        return 1;
    }

    struct tomie_loop *tl = tomie_loop_init();
    if (!tl) {
        fprintf(stderr, "tomie_queue_init(): error\n");
        return 1;
    }
    struct tomie_data *ud = tomie_make_data(2, 2, 1024);
    if (!ud) {
        fprintf(stderr, "tomie_make_data(): error\n");
        return 1;
    }
    ud->listen_socket = listenfd;
    tomie_async_accept(ud, tl);
    tomie_loop_refresh(tl);

    while (1) {
        int ret = tomie_await(tl, &ud);
        if (ret < 0) {
            fprintf(stderr, "tomie_await(%d, ?): %s\n", ud->event_type, strerror(-ret));
            continue;
        }

        switch (ud->event_type) {
        case TOMIE_READ:
            ud->iovec_offset = 0;
            ud->iovec_used = 1;
            ud->iov[ud->iovec_offset].iov_len = 1024;
            break;
        case TOMIE_WRITE:
            ud->iovec_offset = 0;
            ud->iovec_used = 2;
            write_handler(ud->iov);
            break;
        default:
            break;
        }
        tomie_async_forward(ud, tl);
    }
}
