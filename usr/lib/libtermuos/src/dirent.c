#include <dirent.h>
#include <syscall.h>

int readdir_vfs(int fd, unsigned idx, char *name_out)
{
    return (int)__syscall3(SYS_READDIR, fd, idx, (long)name_out);
}

int stat_path(const char *path, struct user_stat *st)
{
    return (int)__syscall2(SYS_STAT, (long)path, (long)st);
}
