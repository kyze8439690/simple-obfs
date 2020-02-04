/*
 * android.c - Setup IPC for shadowsocks-android
 *
 * Copyright (C) 2013 - 2016, Max Lv <max.c.lv@gmail.com>
 *
 * This file is part of the simple-obfs.
 *
 * simple-obfs is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * simple-obfs is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with simple-obfs; see the file COPYING. If not, see
 * <http://www.gnu.org/licenses/>.
 */

#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <locale.h>
#include <signal.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

#include <errno.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>

#include <sys/un.h>
#include <ancillary.h>

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "netutils.h"
#include "utils.h"

#ifdef SS_NG
#include "protobuf/surfboard.pb-c.h"
#endif

int
protect_socket(int fd)
{
    int sock;
    struct sockaddr_un addr;

    if ((sock = socket(AF_UNIX, SOCK_STREAM, 0)) == -1) {
        LOGE("[android] socket() failed: %s (socket fd = %d)\n", strerror(errno), sock);
        return -1;
    }

    // Set timeout to 1s
    struct timeval tv;
    tv.tv_sec  = 1;
    tv.tv_usec = 0;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (char *)&tv, sizeof(struct timeval));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, (char *)&tv, sizeof(struct timeval));

    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, "protect_path", sizeof(addr.sun_path) - 1);

    if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) == -1) {
        LOGE("[android] connect() failed for protect_path: %s (socket fd = %d)\n",
             strerror(errno), sock);
        close(sock);
        return -1;
    }

    if (ancil_send_fd(sock, fd)) {
        ERROR("[android] ancil_send_fd");
        close(sock);
        return -1;
    }

    char ret = 0;

    if (recv(sock, &ret, 1, 0) == -1) {
        ERROR("[android] recv");
        close(sock);
        return -1;
    }

    close(sock);
    return ret;
}

#ifdef SS_NG
#define MAX_MSG_SIZE 1024

int get_ss_proxy_info(char *name, char *proxy_host, char *proxy_port, char *method,
                      char *password, char *obfs, char *obfs_host, int speedTest) {
    int sock;
    struct sockaddr_un addr;

    if ((sock = socket(AF_UNIX, SOCK_STREAM, 0)) == -1) {
        LOGE("[android] get_proxy() failed: %s (socket fd = %d)\n", strerror(errno), sock);
        return -1;
    }

    // Set timeout to 3s
    struct timeval tv;
    tv.tv_sec  = 3;
    tv.tv_usec = 0;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (char *)&tv, sizeof(struct timeval));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, (char *)&tv, sizeof(struct timeval));

    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, "proxy_path", sizeof(addr.sun_path) - 1);

    // connect
    if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) == -1) {
        LOGE("[android] get_proxy() failed for proxy_path: %s (socket fd = %d)\n", strerror(errno),
             sock);
        close(sock);
        return -1;
    }

    SSProxyRequest request = SSPROXY_REQUEST__INIT;
    request.name = name;
    request.speed_test = speedTest;
    request.from_obfs = 1;
    unsigned request_len = ssproxy_request__get_packed_size(&request);
    void *request_buf = ss_malloc(request_len);
    ssproxy_request__pack(&request, request_buf);
    if (send(sock, request_buf, request_len, 0) == -1) {
        LOGE("[android] get_proxy() failed for send: %s (socket fd = %d)\n", strerror(errno), sock);
        close(sock);
        ss_free(request_buf);
        return -1;
    }
    ss_free(request_buf);

    uint8_t result_buf[MAX_MSG_SIZE];
    ssize_t result_len;
    if ((result_len = recv(sock, result_buf, MAX_MSG_SIZE, 0)) == -1) {
        LOGE("[android] get_proxy() failed for recv: %s (socket fd = %d)\n", strerror(errno), sock);
        close(sock);
        return -1;
    }

    SSProxyResult *result = ssproxy_result__unpack(NULL, (size_t) result_len, result_buf);
    if (result == NULL || !result->success) {
        LOGE("[android] get_proxy() failed for unpack: (socket fd = %d)\n", sock);
        close(sock);
        if (result != NULL) {
            ssproxy_result__free_unpacked(result, NULL);
        }
        return -1;
    }

    strcpy(proxy_host, result->host);
    strcpy(proxy_port, result->port);
    strcpy(method, result->method);
    strcpy(password, result->password);
    strcpy(obfs, result->obfs);
    strcpy(obfs_host, result->obfshost);

    ssproxy_result__free_unpacked(result, NULL);
    return 0;
}
#endif