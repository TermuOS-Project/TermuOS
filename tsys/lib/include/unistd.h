#pragma once

#include <stddef.h>
#include <syscall.h>

ssize_t read(int fd, void *buf, size_t n);
ssize_t write(int fd, const void *buf, size_t n);
int open(const char *path, int flags, ...);
int close(int fd);
void _exit(int code);
void exit(int code);
