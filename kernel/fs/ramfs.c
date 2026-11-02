#include "Kernel.h"

#define RAMFS_MAX_CHILDREN 64
#define RAMFS_INITIAL_SIZE 256

// ─── Ramfs node private data ──────────────────────────────────────────────────

typedef struct ramfs_node
{
    vfs_node_t vnode;
    uint8_t *data; // file data (NULL for dirs)
    size_t capacity;
    struct ramfs_node *children[RAMFS_MAX_CHILDREN];
    int nchildren;
    uint32_t next_inode;
} ramfs_node_t;

static uint32_t inode_counter = 1;

// ─── Helpers ──────────────────────────────────────────────────────────────────

static int rf_strcmp(const char *a, const char *b)
{
    while (*a && *b && *a == *b)
    {
        a++;
        b++;
    }
    return *a - *b;
}

static void rf_strcpy(char *dst, const char *src, int max)
{
    int i = 0;
    while (src[i] && i < max - 1)
    {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

// ─── VFS operations ───────────────────────────────────────────────────────────

static int ramfs_read(vfs_node_t *node, uint64_t off, size_t len, uint8_t *buf)
{
    ramfs_node_t *rn = (ramfs_node_t *)node;
    if (!rn->data || off >= node->size)
        return 0;
    if (off + len > node->size)
        len = node->size - off;
    for (size_t i = 0; i < len; i++)
        buf[i] = rn->data[off + i];
    return (int)len;
}

static int ramfs_write(vfs_node_t *node, uint64_t off, size_t len, const uint8_t *buf)
{
    ramfs_node_t *rn = (ramfs_node_t *)node;

    // Grow buffer if needed
    if (off + len > rn->capacity)
    {
        size_t new_cap = off + len + RAMFS_INITIAL_SIZE;
        uint8_t *new_data = kmalloc(new_cap);
        if (!new_data)
            return -1;
        // Copy old data
        for (size_t i = 0; i < rn->capacity && rn->data; i++)
            new_data[i] = rn->data[i];
        // Zero new region
        for (size_t i = rn->capacity; i < new_cap; i++)
            new_data[i] = 0;
        kfree(rn->data);
        rn->data = new_data;
        rn->capacity = new_cap;
    }

    for (size_t i = 0; i < len; i++)
        rn->data[off + i] = buf[i];

    if (off + len > node->size)
        node->size = off + len;

    return (int)len;
}

static int ramfs_readdir(vfs_node_t *node, uint32_t idx, char *name_out)
{
    ramfs_node_t *rn = (ramfs_node_t *)node;
    if ((int)idx >= rn->nchildren)
        return -1;
    rf_strcpy(name_out, rn->children[idx]->vnode.name, VFS_NAME_MAX);
    return 0;
}

static vfs_node_t *ramfs_finddir(vfs_node_t *node, const char *name)
{
    ramfs_node_t *rn = (ramfs_node_t *)node;
    for (int i = 0; i < rn->nchildren; i++)
    {
        if (rf_strcmp(rn->children[i]->vnode.name, name) == 0)
            return &rn->children[i]->vnode;
    }
    return NULL;
}

static int ramfs_create(vfs_node_t *node, const char *name, uint32_t type)
{
    ramfs_node_t *parent = (ramfs_node_t *)node;
    if (parent->nchildren >= RAMFS_MAX_CHILDREN)
        return -1;

    ramfs_node_t *child = (ramfs_node_t *)kcalloc(1, sizeof(ramfs_node_t));
    if (!child)
        return -1;

    rf_strcpy(child->vnode.name, name, VFS_NAME_MAX);
    child->vnode.type = type;
    child->vnode.size = 0;
    child->vnode.inode = inode_counter++;
    child->vnode.ops = node->ops; // share ops table
    child->vnode.fs_data = NULL;
    child->nchildren = 0;

    if (type == VFS_FILE)
    {
        child->data = kmalloc(RAMFS_INITIAL_SIZE);
        child->capacity = RAMFS_INITIAL_SIZE;
    }

    parent->children[parent->nchildren++] = child;
    return 0;
}

static int ramfs_unlink(vfs_node_t *node, const char *name)
{
    ramfs_node_t *parent = (ramfs_node_t *)node;
    for (int i = 0; i < parent->nchildren; i++)
    {
        if (rf_strcmp(parent->children[i]->vnode.name, name) == 0)
        {
            ramfs_node_t *child = parent->children[i];
            kfree(child->data);
            kfree(child);
            // Shift remaining children
            for (int j = i; j < parent->nchildren - 1; j++)
                parent->children[j] = parent->children[j + 1];
            parent->nchildren--;
            return 0;
        }
    }
    return -1;
}

static vfs_ops_t ramfs_ops = {
    .read = ramfs_read,
    .write = ramfs_write,
    .readdir = ramfs_readdir,
    .finddir = ramfs_finddir,
    .create = ramfs_create,
    .unlink = ramfs_unlink,
    .close = NULL,
};

// ─── Create root ──────────────────────────────────────────────────────────────

vfs_node_t *ramfs_create_root(void)
{
    ramfs_node_t *root = (ramfs_node_t *)kcalloc(1, sizeof(ramfs_node_t));
    if (!root)
        return NULL;

    rf_strcpy(root->vnode.name, "ramfs", VFS_NAME_MAX);
    root->vnode.type = VFS_DIR;
    root->vnode.size = 0;
    root->vnode.inode = inode_counter++;
    root->vnode.ops = &ramfs_ops;
    root->nchildren = 0;

    return &root->vnode;
}