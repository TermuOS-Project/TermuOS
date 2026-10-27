#pragma once
#include "object.h"
#include <stdint.h>

#define EVENT_MAX_WAITERS 16

typedef struct
{
    int signaled;
    int manual_reset;
    int nwaiters;
    void *waiters[EVENT_MAX_WAITERS];
} event_body_t;

object_header_t *ob_event_create(const char *name, int manual_reset);
void ob_event_set(object_header_t *obj);
void ob_event_reset(object_header_t *obj);
int ob_event_wait(object_header_t *obj);
