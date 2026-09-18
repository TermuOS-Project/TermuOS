#pragma once
#include <stdint.h>

#define DRIVER_NAME_MAX 32
#define DRIVER_MAX 64

typedef int (*driver_init_fn)(void);

typedef struct driver
{
    const char *name;
    driver_init_fn init;
    int priority;
} driver_t;

typedef struct driver_info
{
    char name[DRIVER_NAME_MAX];
    int priority;
    int status;
} driver_info_t;

void drivers_register_all(void);
void drivers_init(void);
void driver_register(const driver_t *d);
int drivers_list(driver_info_t *out, int max);
