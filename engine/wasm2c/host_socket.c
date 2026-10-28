#define _POSIX_C_SOURCE 200809L
#define _DARWIN_C_SOURCE
#include "host_thread.h"
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

typedef struct w2c_eidos Host;
static WASM_RT_THREAD_LOCAL int last_error;

static int sock(Host *h, uint32_t id)
{
    if (!id || id >= 256 || h->sockets[id] < 0) { last_error = EBADF; return -1; }
    return h->sockets[id];
}

static uint32_t store(Host *h, int fd)
{
    unsigned i;
    if (fd < 0) { last_error = errno; return UINT32_MAX; }
    for (i = 1; i < 256; ++i) if (h->sockets[i] < 0) {
        h->sockets[i] = fd;
        return i;
    }
    close(fd);
    last_error = EMFILE;
    return UINT32_MAX;
}

static void timeouts(int fd)
{
    struct timeval tv = { 5, 0 };
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
#ifdef SO_NOSIGPIPE
    { int on = 1; setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &on, sizeof(on)); }
#endif
}

uint32_t w2c_eidos_socket_new(Host *h)
{
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd >= 0) timeouts(fd);
    return store(h, fd);
}

uint32_t w2c_eidos_socket_connect(Host *h, uint32_t id, uint32_t host, uint32_t port)
{
    char name[256];
    struct addrinfo hints, *addresses = NULL;
    struct pollfd p;
    struct sockaddr_in address;
    int fd = sock(h, id), flags, result, err = 0;
    socklen_t len = sizeof(err);
    unsigned i;
    if (fd < 0) return UINT32_MAX;
    for (i = 0; i < sizeof(name); ++i) {
        name[i] = *(char *)eidos_host_memory(h, host + i, 1);
        if (!name[i]) break;
    }
    if (i == sizeof(name)) { last_error = ENAMETOOLONG; return UINT32_MAX; }
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_port = htons((uint16_t)port);
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    eidos_host_leave(h);
    result = getaddrinfo(name[0] ? name : "127.0.0.1", NULL, &hints, &addresses);
    if (!result) {
        address.sin_addr = ((struct sockaddr_in *)addresses->ai_addr)->sin_addr;
        freeaddrinfo(addresses);
        flags = fcntl(fd, F_GETFL);
        result = flags < 0 ? -1 : fcntl(fd, F_SETFL, flags | O_NONBLOCK);
        if (!result) {
            result = connect(fd, (struct sockaddr *)&address, sizeof(address));
            if (result && errno == EINPROGRESS) {
                p.fd = fd; p.events = POLLOUT; p.revents = 0;
                result = poll(&p, 1, 5000);
                if (result > 0 && !getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &len) && !err)
                    result = 0;
                else { errno = err ? err : ETIMEDOUT; result = -1; }
            }
            last_error = result ? errno : 0;
            if (fcntl(fd, F_SETFL, flags) < 0 && !result) {
                last_error = errno; result = -1;
            }
        } else last_error = errno;
    } else last_error = EHOSTUNREACH;
    eidos_host_enter(h);
    return result ? UINT32_MAX : 0;
}

uint32_t w2c_eidos_socket_listen(Host *h, uint32_t port)
{
    struct sockaddr_in address;
    int fd = socket(AF_INET, SOCK_STREAM, 0), on = 1, flags;
    if (fd < 0) return store(h, fd);
    timeouts(fd);
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_port = htons((uint16_t)port);
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    flags = fcntl(fd, F_GETFL);
    if (bind(fd, (struct sockaddr *)&address, sizeof(address)) || listen(fd, 8)
            || flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK)) {
        last_error = errno; close(fd); return UINT32_MAX;
    }
    return store(h, fd);
}

uint32_t w2c_eidos_socket_accept(Host *h, uint32_t id)
{
    int fd = sock(h, id), accepted;
    if (fd < 0) return UINT32_MAX;
    accepted = accept(fd, NULL, NULL);
    if (accepted >= 0) timeouts(accepted);
    return store(h, accepted);
}

uint32_t w2c_eidos_socket_wait(Host *h, uint32_t id, uint32_t timeout, uint32_t write)
{
    struct pollfd p;
    int result;
    p.fd = sock(h, id); p.events = write ? POLLOUT : POLLIN; p.revents = 0;
    if (p.fd < 0) return UINT32_MAX;
    eidos_host_leave(h);
    result = poll(&p, 1, (int)timeout);
    last_error = result < 0 ? errno : 0;
    eidos_host_enter(h);
    return result < 0 ? UINT32_MAX : result > 0 ? 1 : 0;
}

uint32_t w2c_eidos_socket_recv(Host *h, uint32_t id, uint32_t buffer, uint32_t size, uint32_t out)
{
    int fd = sock(h, id);
    void *bytes = eidos_host_memory(h, buffer, size);
    void *count = eidos_host_memory(h, out, 4);
    uint32_t n = 0;
    ssize_t got;
    memcpy(count, &n, 4);
    if (fd < 0) return UINT32_MAX;
    got = recv(fd, bytes, size, MSG_DONTWAIT);
    if (got < 0) {
        last_error = errno;
        return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR ? 0 : UINT32_MAX;
    }
    if (!got) return UINT32_MAX;
    n = (uint32_t)got;
    memcpy(count, &n, 4);
    return 1;
}

uint32_t w2c_eidos_socket_send(Host *h, uint32_t id, uint32_t buffer, uint32_t size)
{
    int fd = sock(h, id);
    const char *bytes = eidos_host_memory(h, buffer, size);
    uint32_t sent = 0;
    ssize_t n;
    if (fd < 0) return UINT32_MAX;
    eidos_host_leave(h);
    while (sent < size) {
#ifdef MSG_NOSIGNAL
        n = send(fd, bytes + sent, size - sent, MSG_NOSIGNAL);
#else
        n = send(fd, bytes + sent, size - sent, 0);
#endif
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) { last_error = n < 0 ? errno : EPIPE; break; }
        sent += (uint32_t)n;
    }
    eidos_host_enter(h);
    return sent == size ? 0 : UINT32_MAX;
}

void w2c_eidos_socket_close(Host *h, uint32_t id)
{
    int fd = sock(h, id);
    if (fd >= 0) { close(fd); h->sockets[id] = -1; }
}

void w2c_eidos_socket_error(Host *h, uint32_t buffer, uint32_t size)
{
    char *out = eidos_host_memory(h, buffer, size);
    if (size) snprintf(out, size, "socket error %d: %s", last_error, strerror(last_error));
}
