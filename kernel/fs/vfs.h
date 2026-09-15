#pragma once
#include <stdint.h>
#include <stddef.h>

#define VFS_NAME_MAX 64
#define VFS_PATH_MAX 256
#define VFS_MAX_FDS 64
#define VFS_MAX_MOUNTS 16

// Node types
#define VFS_FILE 1
#define VFS_DIR 2
#define VFS_CHARDEV 3

// Open flags
#define O_RDONLY 0x01
#define O_WRONLY 0x02
#define O_RDWR 0x03
#define O_CREAT 0x04
#define O_TRUNC 0x08
#define O_APPEND 0x10

typedef struct vfs_node vfs_node_t;

typedef struct
{
    int (*read)(vfs_node_t *node, uint64_t off, size_t len, uint8_t *buf);
    int (*write)(vfs_node_t *node, uint64_t off, size_t len, const uint8_t *buf);
    int (*readdir)(vfs_node_t *node, uint32_t idx, char *name_out);
    vfs_node_t *(*finddir)(vfs_node_t *node, const char *name);
    int (*create)(vfs_node_t *node, const char *name, uint32_t type);
    int (*unlink)(vfs_node_t *node, const char *name);
    void (*close)(vfs_node_t *node);
} vfs_ops_t;

struct vfs_node
{
    char name[VFS_NAME_MAX];
    uint32_t type;
    uint64_t size;
    uint32_t inode;
    vfs_ops_t *ops;
    void *fs_data;
};

typedef struct
{
    vfs_node_t *node;
    uint64_t offset;
    uint32_t flags;
    int used;
    uint32_t owner; /* pid that must release this fd; 0 = kernel-owned */
} vfs_fd_t;

void vfs_init(void);
int vfs_mount(const char *path, vfs_node_t *root);
vfs_node_t *vfs_resolve(const char *path);

int vfs_open(const char *path, uint32_t flags);
int vfs_close(int fd);

/*
 * Descriptor ownership. A descriptor opened for a process is tagged with its
 * pid so it can be reclaimed when that process exits — the descriptor table
 * is global, so a program that exits without closing would otherwise leak a
 * slot for the rest of the boot. Descriptors the kernel opens for itself stay
 * owner 0 and are never reclaimed automatically.
 */
void vfs_fd_set_owner(int fd, uint32_t owner);
uint32_t vfs_fd_owner(int fd);
int vfs_close_all_owned(uint32_t owner);
int vfs_read(int fd, void *buf, size_t len);
int vfs_write(int fd, const void *buf, size_t len);
int vfs_readdir(int fd, uint32_t idx, char *name_out);
int vfs_mkdir(const char *path);
int vfs_create(const char *path);
int vfs_unlink(const char *path);
int vfs_stat(const char *path, uint32_t *type_out, uint64_t *size_out);