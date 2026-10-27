#include "event.h"
#include "../sched/scheduler.h"
#include "../mm/heap.h"
#include "../lib/printf.h"
#include <stddef.h>

static int ev_strlen(const char *s)
{
    int n = 0;
    while (s[n])
        n++;
    return n;
}

static void ev_strcpy(char *d, const char *s, int max)
{
    int i = 0;
    while (s[i] && i < max - 1) {
        d[i] = s[i];
        i++;
    }
    d[i] = 0;
}

object_header_t *ob_event_create(const char *name, int manual_reset)
{
    if (!name || !name[0])
        return NULL;

    event_body_t *body = kmalloc(sizeof(event_body_t));
    if (!body)
        return NULL;
    body->signaled = 0;
    body->manual_reset = manual_reset ? 1 : 0;
    body->nwaiters = 0;
    for (int i = 0; i < EVENT_MAX_WAITERS; i++)
        body->waiters[i] = NULL;

    const char *leaf = name;
    for (const char *p = name; *p; p++)
        if (*p == '\\')
            leaf = p + 1;

    object_header_t *obj = ob_create(&ObTypeEvent, leaf, body);
    if (!obj) {
        kfree(body);
        return NULL;
    }

    char path[64];
    ev_strcpy(path, "\\Event\\", 64);
    int n = ev_strlen(path);
    for (int i = 0; leaf[i] && n < 63; i++)
        path[n++] = leaf[i];
    path[n] = 0;

    if (ob_insert(path, obj) < 0) {
        kprintf("ob: event insert failed %s\n", path);
    }
    return obj;
}

static event_body_t *body_of(object_header_t *obj)
{
    if (!obj || obj->type != & ObTypeEvent || !obj->body)
        return NULL;
    return (event_body_t *)obj->body;
}

void ob_event_set(object_header_t *obj)
{
    event_body_t *e = body_of(obj);
    if (!e)
        return;

    e->signaled = 1;

    for (int i = 0; i < e->nwaiters; i++) {
        thread_t *t = (thread_t *)e->waiters[i];
        if (t)
            thread_unblock(t);
        e->waiters[i] = NULL;
    }
    e->nwaiters = 0;

    if (!e->manual_reset)
        e->signaled = 0;
}

void ob_event_reset(object_header_t *obj)
{
    event_body_t *e = body_of(obj);
    if (!e)
        return;
    e->signaled = 0;
}

int ob_event_wait(object_header_t *obj)
{
    event_body_t *e = body_of(obj);
    if (!e)
        return -1;

    if (e->signaled) {
        if (!e->manual_reset)
            e->signaled = 0;
        return 0;
    }

    thread_t *self = thread_current();
    if (!self)
        return -1;

    if (e->nwaiters >= EVENT_MAX_WAITERS)
        return -1;

    e->waiters[e->nwaiters++] = self;
    thread_block();
    scheduler_yield();

    if (e->signaled && !e->manual_reset)
        e->signaled = 0;
    return 0;
}
