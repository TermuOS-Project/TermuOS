#include "install.h"
#include "vfs.h"
#include "../lib/printf.h"
#include <limine.h>
#include <stdint.h>

extern volatile struct limine_module_request module_request;

static int ends_with(const char *s, const char *suf)
{
    if (!s || !suf)
        return 0;
    int ls = 0, lt = 0;
    while (s[ls])
        ls++;
    while (suf[lt])
        lt++;
    if (lt > ls)
        return 0;
    for (int i = 0; i < lt; i++)
        if (s[ls - lt + i] != suf[i])
            return 0;
    return 1;
}

static const char *basename_path(const char *path)
{
    const char *b = path;
    for (const char *p = path; *p; p++)
        if (*p == '/')
            b = p + 1;
    return b;
}

int install_bin_from_modules(void)
{
    if (!module_request.response) {
        kprintf("install: no modules\n");
        return -1;
    }

    vfs_mkdir("/bin");

    int n = 0;
    for (uint64_t i = 0; i < module_request.response->module_count; i++) {
        struct limine_file *f = module_request.response->modules[i];
        if (!f || !f->path || !f->address || f->size == 0)
            continue;
        if (!ends_with(f->path, ".tsys"))
            continue;

        const char *base = basename_path(f->path);
        char dest[96];
        /* "/bin/" + base */
        dest[0] = '/'; dest[1] = 'b'; dest[2] = 'i'; dest[3] = 'n'; dest[4] = '/';
        int j = 5;
        for (int k = 0; base[k] && j < 95; k++)
            dest[j++] = base[k];
        dest[j] = '\0';

        int fd = vfs_open(dest, O_WRONLY | O_CREAT | O_TRUNC);
        if (fd < 0) {
            kprintf("install: open %s failed\n", dest);
            continue;
        }

        const uint8_t *p = (const uint8_t *)f->address;
        uint64_t left = f->size;
        while (left) {
            size_t chunk = left > 4096 ? 4096 : (size_t)left;
            int w = vfs_write(fd, p, chunk);
            if (w <= 0) {
                kprintf("install: write %s failed\n", dest);
                break;
            }
            p += w;
            left -= (uint64_t)w;
        }
        vfs_close(fd);

        if (left == 0) {
            kprintf("install: %s (%u bytes)\n", dest, (uint32_t)f->size);
            n++;
        }
    }

    kprintf("install: %d binaries\n", n);
    return n > 0 ? 0 : -1;
}
