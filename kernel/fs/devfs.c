#include "Kernel.h"

typedef struct
{
    const char *name;
    int (*read)(uint64_t off, size_t len, uint8_t *buf);
    int (*write)(uint64_t off, size_t len, const uint8_t *buf);
} devfs_device_t;

struct fb0_info
{
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
    uint32_t bpp;
};

// devices

static int dev_null_read(uint64_t off, size_t len, uint8_t *buf)
{
    (void)off;
    (void)len;
    (void)buf;
    return 0;
}

static int dev_null_write(uint64_t off, size_t len, const uint8_t *buf)
{
    (void)off;
    (void)buf;
    return (int)len;
}

static int dev_zero_read(uint64_t off, size_t len, uint8_t *buf)
{
    (void)off;
    for (size_t i = 0; i < len; i++)
        buf[i] = 0;
    return (int)len;
}

static int dev_zero_write(uint64_t off, size_t len, const uint8_t *buf)
{
    (void)off;
    (void)buf;
    return (int)len;
}

static int dev_console_read(uint64_t off, size_t len, uint8_t *buf)
{
    (void)off;
    if (!buf || len == 0)
        return 0;

    while (!keyboard_haschar())
        scheduler_yield();

    char c = keyboard_getchar();
    buf[0] = (uint8_t)c;
    return 1;
}

static int dev_console_write(uint64_t off, size_t len, const uint8_t *buf)
{
    (void)off;
    for (size_t i = 0; i < len; i++)
        terminal_putchar((char)buf[i]);
    return (int)len;
}

static int dev_fb0_read(uint64_t off, size_t len, uint8_t *buf)
{
    struct limine_framebuffer *fb = fb_get();
    if (!fb || !buf)
        return -1;

    struct fb0_info info;
    info.width = (uint32_t)fb->width;
    info.height = (uint32_t)fb->height;
    info.pitch = (uint32_t)fb->pitch;
    info.bpp = (uint32_t)fb->bpp;

    if (off >= sizeof(info))
        return 0;
    size_t n = sizeof(info) - (size_t)off;
    if (n > len)
        n = len;
    const uint8_t *src = (const uint8_t *)&info + off;
    for (size_t i = 0; i < n; i++)
        buf[i] = src[i];
    return (int)n;
}

static int dev_fb0_write(uint64_t off, size_t len, const uint8_t *buf)
{
    (void)off;
    (void)buf;
    return (int)len;
}

static devfs_device_t g_devs[] = {
    {"null", dev_null_read, dev_null_write},
    {"zero", dev_zero_read, dev_zero_write},
    {"console", dev_console_read, dev_console_write},
    {"fb0", dev_fb0_read, dev_fb0_write},
};
#define N_DEVS (sizeof(g_devs) / sizeof(g_devs[0]))

// chardev ops

static int char_read(vfs_node_t *node, uint64_t off, size_t len, uint8_t *buf)
{
    devfs_device_t *d = (devfs_device_t *)node->fs_data;
    if (!d || !d->read)
        return -1;
    return d->read(off, len, buf);
}

static int char_write(vfs_node_t *node, uint64_t off, size_t len, const uint8_t *buf)
{
    devfs_device_t *d = (devfs_device_t *)node->fs_data;
    if (!d || !d->write)
        return -1;
    return d->write(off, len, buf);
}

static vfs_ops_t char_ops = {
    .read = char_read,
    .write = char_write,
    .readdir = 0,
    .finddir = 0,
    .create = 0,
    .unlink = 0,
    .close = 0,
};

// directory ops

static vfs_node_t g_dev_nodes[N_DEVS];
static vfs_node_t g_dev_root;
static int g_ready;

static vfs_node_t *dir_finddir(vfs_node_t *node, const char *name)
{
    (void)node;
    for (uint32_t i = 0; i < N_DEVS; i++)
    {
        const char *n = g_devs[i].name;
        const char *p = name;
        while (*n && *n == *p)
        {
            n++;
            p++;
        }
        if (*n == '\0' && *p == '\0')
            return &g_dev_nodes[i];
    }
    return 0;
}

static int dir_readdir(vfs_node_t *node, uint32_t idx, char *name_out)
{
    (void)node;
    if (idx >= N_DEVS)
        return -1;
    const char *n = g_devs[idx].name;
    int i = 0;
    while (n[i] && i < VFS_NAME_MAX - 1)
    {
        name_out[i] = n[i];
        i++;
    }
    name_out[i] = '\0';
    return 0;
}

static vfs_ops_t dir_ops = {
    .read = 0,
    .write = 0,
    .readdir = dir_readdir,
    .finddir = dir_finddir,
    .create = 0,
    .unlink = 0,
    .close = 0,
};

// public

vfs_node_t *devfs_create(void)
{
    if (g_ready)
        return &g_dev_root;

    for (uint32_t i = 0; i < N_DEVS; i++)
    {
        vfs_node_t *n = &g_dev_nodes[i];
        int j = 0;
        const char *nm = g_devs[i].name;
        while (nm[j] && j < VFS_NAME_MAX - 1)
        {
            n->name[j] = nm[j];
            j++;
        }
        n->name[j] = '\0';
        n->type = VFS_CHARDEV;
        n->size = 0;
        n->inode = i + 1;
        n->ops = &char_ops;
        n->fs_data = &g_devs[i];
    }

    g_dev_root.name[0] = 'd';
    g_dev_root.name[1] = 'e';
    g_dev_root.name[2] = 'v';
    g_dev_root.name[3] = '\0';
    g_dev_root.type = VFS_DIR;
    g_dev_root.size = 0;
    g_dev_root.inode = 0;
    g_dev_root.ops = &dir_ops;
    g_dev_root.fs_data = 0;

    g_ready = 1;
    return &g_dev_root;
}
