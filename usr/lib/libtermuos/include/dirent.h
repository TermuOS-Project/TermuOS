#pragma once
#include <stdint.h>

#define VFS_NAME_MAX 64
#define VFS_PATH_MAX 256
#define VFS_FILE 1
#define VFS_DIR 2

struct user_stat
{
    uint32_t type;
    uint64_t size;
};

#ifdef __cplusplus
extern "C"
{
#endif
    int readdir_vfs(int fd, unsigned idx, char *name_out);
    int stat_path(const char *path, struct user_stat *st);
#ifdef __cplusplus
}
#endif
