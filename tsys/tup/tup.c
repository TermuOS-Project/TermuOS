#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <http.h>
#include <stdint.h>

#define O_WRONLY 0x02
#define O_CREAT 0x04
#define O_TRUNC 0x08

#define REG_IP0 10
#define REG_IP1 0
#define REG_IP2 2
#define REG_IP3 2
#define REG_PORT 18080

#define HTTP_MAX (256 * 1024)
#define IDX_MAX 8192

static uint8_t g_ip[4] = { REG_IP0, REG_IP1, REG_IP2, REG_IP3 };
static char g_http[HTTP_MAX];
static char g_index[IDX_MAX];

static int streq(const char *a, const char *b)
{
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return *a == *b;
}

static int starts(const char *s, const char *p)
{
    while (*p) {
        if (*s++ != *p++)
            return 0;
    }
    return 1;
}

static int next_tok(const char **pp, const char **tok)
{
    const char *p = *pp;
    while (*p == ' ' || *p == '\t')
        p++;
    *tok = p;
    int n = 0;
    while (p[n] && p[n] != ' ' && p[n] != '\t' && p[n] != '\n' && p[n] != '\r')
        n++;
    *pp = p + n;
    return n;
}

static void copy_n(char *dst, size_t max, const char *src, int n)
{
    size_t i = 0;
    if (n < 0)
        n = 0;
    while (i + i < max && i < (size_t)n) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static const char *fetch_body(const char *url_path, size_t *blen)
{
    int n = http_get(g_ip, REG_PORT, url_path, g_http, sizeof(g_http));
    if (n < 0)
        return NULL;

    const char *body = http_body(g_http);
    if (!body)
        body = g_http;

    size_t total = (size_t)n;
    size_t off = (size_t)(body - g_http);
    if (off > total)
        return NULL;
    *blen = total - off;
    return body;
}

static int cmd_update(void)
{
    size_t blen = 0;
    const char *body = fetch_body("/index.txt", &blen);
    if (!body) {
        printf("tup: failed to fetch index\n");
        return 1;
    }

    if (blen >= sizeof(g_index))
        blen = sizeof(g_index) - 1;
    for (size_t i = 0; i < blen; i++)
        g_index[i] = body[i];
    g_index[blen] = '\0';

    printf("tup: index (%u bytes)\n", (unsigned)blen);
    printf("%s", g_index);
    if (blen && g_index[blen - 1] != '\n')
        printf("\n");
    return 0;
}

static int find_pkg(const char *index, const char *name,
                    char *ver, size_t ver_max,
                    char *path, size_t path_max)
{
    const char *line = index;
    while (*line) {
        const char *eol = line;
        while (*eol && *eol != '\n')
            eol++;
        
        if (line[0] != '#' && line[0] != '\n' && line[0] != '\r') {
            const char *p = line;
            const char *t0, *t1, *t2;
            int n0 = next_tok(&p, &t0);
            int n1 = next_tok(&p, &t1);
            int n2 = next_tok(&p, &t2);

            if (n0 > 0 && n1 > 0 && n2 > 0) {
                char nm[64];
                copy_n(nm, sizeof(nm), t0, n0);
                if (streq(nm, name)) {
                    copy_n(ver, ver_max, t1, n1);
                    copy_n(path, path_max, t2, n2);
                    return 0;
                }
            }
        }

        if (*eol == '\n')
            line = eol + 1;
        else
            break;
    }
    return -1;
}

static int write_file(const char *path, const void *data, size_t len)
{
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC);
    if (fd < 0) {
        printf("tup: open %s failed\n", path);
        return -1;
    }
    const char *p = (const char *)data;
    size_t off = 0;
    while (off < len) {
        ssize_t w = write(fd, p + off, len - off);
        if (w <= 0) {
            close(fd);
            printf("tup: write failed\n");
            return -1;
        }
        off += (size_t)w;
    }
    close(fd);
    return 0;
}

static int cmd_install(const char *name)
{
    size_t blen = 0;
    const char *idx = fetch_body("/index.txt", &blen);
    if (!idx) {
        printf("tup: failed to fetch index\n");
        return 1;
    }

    if (blen >= sizeof(g_index))
        blen = sizeof(g_index) - 1;
    for (size_t i = 0; i < blen; i++)
        g_index[i] = idx[i];
    g_index[blen] = '\0';

    char ver[32], rel[128], url[160], outpath[96];
    if (find_pkg(g_index, name, ver, sizeof(ver), rel, sizeof(rel)) != 0) {
        printf("tup: package '%s' not in index\n", name);
        return 1;
    }

    if (rel[0] == '/')
        copy_n(url, sizeof(url), rel, (int)strlen(rel));
    else {
        url[0] = '/';
        copy_n(url + 1, sizeof(url) - 1, rel, (int)strlen(rel));
    }

    printf("tup: installing %s %s from %s\n", name, ver, url);

    const char *body = fetch_body(url, &blen);
    if (!body) {
        printf("tup: download failed\n");
        return 1;
    }

    copy_n(outpath, sizeof(outpath), "/bin/", 5);
    {
        size_t o = 5;
        for (const char *c = name; *c && o + 6 < sizeof(outpath); c++)
            outpath[o++] = *c;
        const char *suf = ".tsys";
        while (*suf && o + 1 < sizeof(outpath))
            outpath[o++] = *suf++;
        outpath[o] = '\0';
    }

    if (write_file(outpath, body, blen) != 0)
        return 1;

    printf("tup: installed %s (%u bytes) -> %s\n",
           name, (unsigned)blen, outpath);
    return 0;
}

static void usage(void)
{
    printf("usage:\n");
    printf("  tup update\n");
    printf("  tup install <name>\n");
    printf("registry: %u.%u.%u.%u:%u\n",
           REG_IP0, REG_IP1, REG_IP2, REG_IP3, REG_PORT);
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        usage();
        return 1;
    }

    if (streq(argv[1], "update"))
        return cmd_update();

    if (streq(argv[1], "install")) {
        if (argc < 3) {
            usage();
            return 1;
        }
        return cmd_install(argv[2]);
    } 

    usage();
    return 1;
}