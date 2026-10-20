#include "http.h"
#include "net.h"
#include <stddef.h>

static size_t cpy(char *dst, size_t n, size_t max, const char *s)
{
    while (*s && n + 1 < max)
        dst[n++] = *s++;
    if (n < max)
        dst[n] = '\0';
    return n;
}

int http_get(const uint8_t ip[4], uint16_t port,
             const char *path, char *out, size_t out_max)
{
    char req[256];
    size_t n = 0;

    if (!ip || !path || !out || out_max < 2)
        return -1;
    if (!path[0])
        path = "/";
    
    n = cpy(req, n, sizeof(req), "GET ");
    n = cpy(req, n, sizeof(req), path);
    n = cpy(req, n, sizeof(req), " HTTP/1.0\r\nHost: a\r\nConnection: close\r\n\r\n");

    if (tcp_connect_ip(ip, port, 0) != 0)
        return -1;

    if (tcp_send(req, n) < 0) {
        tcp_close();
        return -1;
    }

    size_t got = 0;
    for (int spins = 0; spins < 10000 && got + 1 < out_max; spins++) {
        int r = tcp_recv(out + got, out_max - 1 - got);
        if (r < 0)
            break;
        if (r > 0) {
            got += (size_t)r;
            continue;
        }
        for (volatile int k = 0; k < 5000; k++)
            ;
    }
    out[got] = '\0';
    tcp_close();
    for (volatile int i = 0; i < 500000; i++)
        ;
    return (int)got;
}

const char *http_body(const char *buf)
{
    if (!buf)
        return buf;
    for (const char *p = buf; p[0] && p[1] && p[2] && p[3]; p++) {
        if (p[0] == '\r' && p[1] == '\n' && p[2] == '\r' && p[3] == '\n')
            return p + 4;
    }
    return buf;
}