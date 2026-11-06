#include <stdio.h>
#include <stdlib.h>
#include <http.h>

static int parse_ip(const char *s, uint8_t ip[4])
{
    int part = 0;
    int val = 0;
    int digits = 0;

    for (;;) {
        char c = *s++;
        if (c >= '0' && c <= '9') {
            val = val * 10 + (c - '0');
            if (val > 255)
                return -1;
            digits++;
        } else if (c == '.' || c == '\0') {
            if (digits == 0 || part > 3)
                return -1;
            ip[part++] = (uint8_t)val;
            val = 0;
            digits = 0;
            if (c == '\0')
                break;
        } else {
            return -1;
        }
    }
    return part == 4 ? 0 : -1;
}

static int parse_port(const char *s)
{
    int v = 0;
    if (!s || !*s)
        return -1;
    while (*s) {
        if (*s < '0' || *s > '9')
            return -1;
        v = v * 10 + (*s - '0');
        if (v > 65535)
            return -1;
        s++;
    }
    return v;
}

int main(int argc, char **argv)
{
    uint8_t ip[4];
    int port;
    const char *path = "/";
    static char buf[16384];

    if (argc < 3) {
        printf("usage: httpget <a.b.c.d> <port> [path]\n");
        return 1;
    }

    if (parse_ip(argv[1], ip) != 0) {
        printf("httpget: bad ip\n");
        return 1;
    }

    port = parse_port(argv[2]);
    if (port <= 0) {
        printf("httpget: bad port\n");
        return 1;
    }

    if (argc >= 4 && argv[3][0])
        path = argv[3];

    printf("httpget %u.%u.%u.%u:%d %s\n",
           ip[0], ip[1], ip[2], ip[3], port, path);

    int n = http_get(ip, (uint16_t)port, path, buf, sizeof(buf));
    if (n < 0) {
        printf("httpget: failed\n");
        return 1;
    }

    printf("--- response %d bytes ---\n", n);
    const char *body = http_body(buf);
    if (body && body != buf)
        printf("%s", body);
    else
        printf("%s", buf);

    if (n > 0 && buf[n - 1] != '\n')
        printf("\n");

    return 0;
}
