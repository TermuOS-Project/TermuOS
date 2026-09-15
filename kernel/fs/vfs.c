#include "vfs.h"
#include "../mm/heap.h"
#include "../lib/printf.h"
#include <stdint.h>
#include <stddef.h>

// Small helpers

static int vfs_strlen(const char *s)
{
    int n = 0;
    while (s[n])
        n++;
    return n;
}

static int vfs_strcmp(const char *a, const char *b) __attribute__((unused));
static int vfs_strcmp(const char *a, const char *b)
{
    while (*a && *b && *a == *b)
    {
        a++;
        b++;
    }
    return *a - *b;
}

static void vfs_strcpy(char *dst, const char *src, int max)
{
    int i = 0;
    while (src[i] && i < max - 1)
    {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

// Join two path segments safely.
static void vfs_pathjoin(char *out, int max, const char *base, const char *name) __attribute__((unused));
static void vfs_pathjoin(char *out, int max, const char *base, const char *name)
{
    vfs_strcpy(out, base, max);
    int bl = vfs_strlen(out);
    if (bl > 0 && out[bl - 1] != '/' && bl < max - 1)
        out[bl++] = '/';
    int i = 0;
    while (name[i] && bl + i < max - 1)
    {
        out[bl + i] = name[i];
        i++;
    }
    out[bl + i] = '\0';
}

// Mount table and file-descriptor table

typedef struct
{
    char path[VFS_PATH_MAX];
    vfs_node_t *root;
    int used;
} mount_t;

static mount_t mounts[VFS_MAX_MOUNTS];
static vfs_fd_t fds[VFS_MAX_FDS];

// Initialization

void vfs_init(void)
{
    for (int i = 0; i < VFS_MAX_MOUNTS; i++)
        mounts[i].used = 0;
    for (int i = 0; i < VFS_MAX_FDS; i++)
    {
        fds[i].used = 0;
        fds[i].owner = 0;
    }
}

int vfs_mount(const char *path, vfs_node_t *root)
{
    for (int i = 0; i < VFS_MAX_MOUNTS; i++)
    {
        if (!mounts[i].used)
        {
            vfs_strcpy(mounts[i].path, path, VFS_PATH_MAX);
            mounts[i].root = root;
            mounts[i].used = 1;
            return 0;
        }
    }
    return -1;
}

// Path resolution

vfs_node_t *vfs_resolve(const char *path)
{
    if (!path || path[0] != '/')
        return NULL;

    // Pick the deepest matching mount point.
    mount_t *best = NULL;
    int best_len = -1;
    for (int i = 0; i < VFS_MAX_MOUNTS; i++)
    {
        if (!mounts[i].used)
            continue;
        int mlen = vfs_strlen(mounts[i].path);
        int matches = 1;
        for (int j = 0; j < mlen; j++)
            if (path[j] != mounts[i].path[j])
            {
                matches = 0;
                break;
            }
        // A root mount matches any absolute path.
        int after_ok = (mlen == 1) || (path[mlen] == '/' || path[mlen] == '\0');
        if (matches && after_ok && mlen > best_len)
        {
            best = &mounts[i];
            best_len = mlen;
        }
    }
    if (!best)
        return NULL;

    vfs_node_t *node = best->root;
    const char *p = path + best_len;
    if (*p == '/')
        p++;

    while (*p && node)
    {
        // Ignore repeated slashes.
        while (*p == '/')
            p++;
        if (!*p)
            break;

        // Read the next path component.
        char component[VFS_NAME_MAX];
        int ci = 0;
        while (*p && *p != '/' && ci < VFS_NAME_MAX - 1)
            component[ci++] = *p++;
        component[ci] = '\0';

        if (ci == 0)
            break;
        if (!node->ops || !node->ops->finddir)
        {
            if (node != best->root && node->ops && node->ops->close)
                node->ops->close(node);
            return NULL;
        }
        vfs_node_t *prev = node;
        node = node->ops->finddir(node, component);
        // free intermediate node (not the mount root)
        if (prev != best->root && prev->ops && prev->ops->close)
            prev->ops->close(prev);
    }

    return node;
}

// Parent path helper

// Split a path like "/a/b/c" into its parent directory and final name.
static void split_path(const char *path, char *parent, char *name)
{
    int last = 0;
    for (int i = 0; path[i]; i++)
        if (path[i] == '/')
            last = i;

    // parent
    if (last == 0)
    {
        parent[0] = '/';
        parent[1] = '\0';
    }
    else
    {
        for (int i = 0; i < last; i++)
            parent[i] = path[i];
        parent[last] = '\0';
    }

    // name
    int i = 0;
    const char *n = path + last + 1;
    while (n[i] && i < VFS_NAME_MAX - 1)
    {
        name[i] = n[i];
        i++;
    }
    name[i] = '\0';
}

// File-descriptor allocation

/*
 * Descriptors 0, 1 and 2 are reserved for stdin/stdout/stderr, so an open()
 * from userspace can never collide with them: sys_read()/sys_write() route
 * those three to the console and everything above them to the VFS.
 */
#define VFS_FD_FIRST 3

static int alloc_fd(void)
{
    for (int i = VFS_FD_FIRST; i < VFS_MAX_FDS; i++)
        if (!fds[i].used)
            return i;
    return -1;
}

// Public API

int vfs_open(const char *path, uint32_t flags)
{
    vfs_node_t *node = vfs_resolve(path);

    if (!node && (flags & O_CREAT))
    {
        if (vfs_create(path) < 0)
            return -1;
        node = vfs_resolve(path);
    }
    if (!node)
        return -1;

    int fd = alloc_fd();
    if (fd < 0)
        return -1;

    // Handle O_TRUNC — write zero bytes at offset 0 to signal truncate
    if ((flags & O_TRUNC) && node->type == VFS_FILE)
    {
        node->size = 0;
        if (node->ops && node->ops->write)
            node->ops->write(node, 0, 0, 0);
    }

    fds[fd].node = node;
    fds[fd].offset = (flags & O_APPEND) ? node->size : 0;
    fds[fd].flags = flags;
    fds[fd].used = 1;
    fds[fd].owner = 0; /* kernel-owned until a caller claims it */
    return fd;
}

int vfs_close(int fd)
{
    if (fd < 0 || fd >= VFS_MAX_FDS || !fds[fd].used)
        return -1;
    if (fds[fd].node->ops && fds[fd].node->ops->close)
        fds[fd].node->ops->close(fds[fd].node);
    fds[fd].used = 0;
    fds[fd].owner = 0;
    return 0;
}

void vfs_fd_set_owner(int fd, uint32_t owner)
{
    if (fd < 0 || fd >= VFS_MAX_FDS || !fds[fd].used)
        return;
    fds[fd].owner = owner;
}

uint32_t vfs_fd_owner(int fd)
{
    if (fd < 0 || fd >= VFS_MAX_FDS || !fds[fd].used)
        return 0;
    return fds[fd].owner;
}

/* Release every descriptor a process still held. Returns how many. */
int vfs_close_all_owned(uint32_t owner)
{
    int closed = 0;

    if (owner == 0)
        return 0; /* never sweep the kernel's own descriptors */

    for (int i = 0; i < VFS_MAX_FDS; i++)
        if (fds[i].used && fds[i].owner == owner)
            if (vfs_close(i) == 0)
                closed++;

    return closed;
}

int vfs_read(int fd, void *buf, size_t len)
{
    if (fd < 0 || fd >= VFS_MAX_FDS || !fds[fd].used)
        return -1;
    vfs_node_t *node = fds[fd].node;
    if (!node->ops || !node->ops->read)
        return -1;
    int n = node->ops->read(node, fds[fd].offset, len, (uint8_t *)buf);
    if (n > 0)
        fds[fd].offset += n;
    return n;
}

int vfs_write(int fd, const void *buf, size_t len)
{
    if (fd < 0 || fd >= VFS_MAX_FDS || !fds[fd].used)
        return -1;
    vfs_node_t *node = fds[fd].node;
    if (!node->ops || !node->ops->write)
        return -1;
    int n = node->ops->write(node, fds[fd].offset, len, (const uint8_t *)buf);
    if (n > 0)
        fds[fd].offset += n;
    return n;
}

int vfs_readdir(int fd, uint32_t idx, char *name_out)
{
    if (fd < 0 || fd >= VFS_MAX_FDS || !fds[fd].used)
        return -1;
    vfs_node_t *node = fds[fd].node;
    if (!node->ops || !node->ops->readdir)
        return -1;
    return node->ops->readdir(node, idx, name_out);
}

int vfs_stat(const char *path, uint32_t *type_out, uint64_t *size_out)
{
    vfs_node_t *node = vfs_resolve(path);
    if (!node)
        return -1;
    if (type_out)
        *type_out = node->type;
    if (size_out)
        *size_out = node->size;
    return 0;
}

int vfs_mkdir(const char *path)
{
    char parent[VFS_PATH_MAX], name[VFS_NAME_MAX];
    split_path(path, parent, name);
    if (!name[0])
        return -1;
    vfs_node_t *dir = vfs_resolve(parent);
    if (!dir || !dir->ops || !dir->ops->create)
        return -1;
    return dir->ops->create(dir, name, VFS_DIR);
}

int vfs_create(const char *path)
{
    char parent[VFS_PATH_MAX], name[VFS_NAME_MAX];
    split_path(path, parent, name);
    if (!name[0])
        return -1;
    vfs_node_t *dir = vfs_resolve(parent);
    if (!dir || !dir->ops || !dir->ops->create)
        return -1;
    return dir->ops->create(dir, name, VFS_FILE);
}

int vfs_unlink(const char *path)
{
    char parent[VFS_PATH_MAX], name[VFS_NAME_MAX];
    split_path(path, parent, name);
    if (!name[0])
        return -1;
    vfs_node_t *dir = vfs_resolve(parent);
    if (!dir || !dir->ops || !dir->ops->unlink)
        return -1;
    return dir->ops->unlink(dir, name);
}