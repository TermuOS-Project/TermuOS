#pragma once
#include <stdint.h>

typedef int (*driver_init_fn)(void);

typedef struct driver
{
    const char *name;
    driver_init_fn init;
    int priority;
} driver_t;

void drivers_register_all(void);
void drivers_init(void);
void driver_register(const driver_t *d);
