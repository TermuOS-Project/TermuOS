#include "object.h"
#include "../mm/heap.h"
#include "../lib/printf.h"
#include <stdint.h>
#include <stddef.h>

/* Helper functions */
static int ob_strcmp(const char *a, const char *b)
{
    while (*a && *b && *a == *b)
    {
        a++;
        b++;
    }
    return *a - *b;
}

static int ob_strlen(const char *s) __attribute__((unused));
static int ob_strlen(const char *s)
{
    int n = 0;
    while (s[n])
        n++;
    return n;
}

static void ob_strcpy(char *d, const char *s, int max)
{
    int i = 0;
    while (s[i] && i < max - 1)
    {
        d[i] = s[i];
        i++;
    }
    d[i] = '\0';
}

/* Namespace root */
static object_header_t ns_root;

/* Built-in type callbacks */
static void proc_delete(object_header_t *obj)
{
    (void)obj;
    kprintf("ob: process deleted\n");
}

static void thread_delete(object_header_t *obj)
{
    (void)obj;
    kprintf("ob: thread deleted\n");
}

static void dir_delete(object_header_t *obj)
{
    (void)obj;
}

static void device_delete(object_header_t *obj)
{
    (void)obj;
    kprintf("ob: device deleted\n");
}

static void driver_delete(object_header_t *obj)
{
    (void)obj;
    kprintf("ob: driver deleted\n");
}

static void event_delete(object_header_t *obj)
{
    (void)obj;
    kprintf("ob: event deleted\n");
}

static void port_delete(object_header_t *obj)
{
    (void)obj;
    kprintf("ob: port deleted\n");
}

static void file_delete(object_header_t *obj)
{
    (void)obj;
    kprintf("ob: file deleted\n");
}

object_type_t ObTypeProcess = {"Process", proc_delete};
object_type_t ObTypeThread = {"Thread", thread_delete};
object_type_t ObTypeDirectory = {"Directory", dir_delete};
object_type_t ObTypeDevice = { "Device", device_delete };
object_type_t ObTypeDriver = { "Driver", driver_delete };
object_type_t ObTypeEvent    = { "Event",    event_delete };
object_type_t ObTypePort     = { "Port",     port_delete };
object_type_t ObTypeFile     = { "File",     file_delete };

/* Type registry */
static object_type_t *type_table[OB_TYPE_MAX];
static int type_count;

void ob_type_register(object_type_t *type)
{
    if (!type || !type->name || type_count >= OB_TYPE_MAX)
        return;
    for (int i = 0; i < type_count; i++)
        if (type_table[i] == type || ob_strcmp(type_table[i]->name, type->name) == 0)
            return;
    type_table[type_count++] = type;
}

object_type_t *ob_type_find(const char *name)
{
    if (!name)
        return NULL;
    for (int i = 0; i < type_count; i++)
        if (ob_strcmp(type_table[i]->name, name) == 0)  
            return type_table[i];
    return NULL;
}

void ob_type_list(void)
{
    kprintf("ob: types (%d)\n", type_count);
    for (int i = 0; i < type_count; i++)
        kprintf("  %s\n", type_table[i]->name);
}

/* Object pool */
#define OB_POOL_SIZE 256
static object_header_t ob_pool[OB_POOL_SIZE];
static uint8_t ob_pool_used[OB_POOL_SIZE];

static object_header_t *ob_alloc_header(void)
{
    for (int i = 0; i < OB_POOL_SIZE; i++)
    {
        if (!ob_pool_used[i])
        {
            ob_pool_used[i] = 1;
            object_header_t *h = &ob_pool[i];
            h->ref_count = 0;
            h->ns_parent = NULL;
            h->ns_children = NULL;
            h->ns_next = NULL;
            h->body = NULL;
            return h;
        }
    }
    kprintf("ob: object pool exhausted\n");
    return NULL;
}

static void ob_free_header(object_header_t *obj)
{
    int idx = (int)(obj - ob_pool);
    if (idx >= 0 && idx < OB_POOL_SIZE)
        ob_pool_used[idx] = 0;
}

/* Initialization */
void ob_init(void)
{
    for (int i = 0; i < OB_POOL_SIZE; i++)
        ob_pool_used[i] = 0;

    type_count = 0;
    ob_type_register(&ObTypeDirectory);
    ob_type_register(&ObTypeProcess);
    ob_type_register(&ObTypeThread);
    ob_type_register(&ObTypeDevice);
    ob_type_register(&ObTypeDriver);
    ob_type_register(&ObTypeEvent);
    ob_type_register(&ObTypePort);
    ob_type_register(&ObTypeFile);

    ns_root.type = &ObTypeDirectory;
    ns_root.ref_count = 1;
    ns_root.ns_parent = NULL;
    ns_root.ns_children = NULL;
    ns_root.ns_next = NULL;
    ns_root.body = NULL;
    ob_strcpy(ns_root.name, "\\", OB_NAME_MAX);

    ob_mkdir("\\Driver");
    ob_mkdir("\\Device");
    ob_mkdir("\\Process");
    ob_mkdir("\\Thread");
    ob_mkdir("\\Port");
    ob_mkdir("\\Event");

    kprintf("ob: object manager ready\n");
    ob_type_list();
}

/* Object lifecycle */
object_header_t *ob_create(object_type_t *type, const char *name, void *body)
{
    object_header_t *obj = ob_alloc_header();
    if (!obj)
        return NULL;
    obj->type = type;
    obj->body = body;
    obj->ref_count = 1;
    ob_strcpy(obj->name, name, OB_NAME_MAX);
    return obj;
}

void ob_ref(object_header_t *obj)
{
    if (obj)
        obj->ref_count++;
}

void ob_deref(object_header_t *obj)
{
    if (!obj)
        return;
    if (obj->ref_count > 0)
        obj->ref_count--;
    if (obj->ref_count == 0)
    {
        if (obj->type && obj->type->on_delete)
            obj->type->on_delete(obj);
        ob_free_header(obj);
    }
}

/* Namespace handling */

/* Walk the path components, return the parent directory node, and fill in the final component name. */
static object_header_t *ns_walk(const char *path, const char **leaf)
{
    if (!path || path[0] != '\\')
        return NULL;

    object_header_t *dir = &ns_root;

    /* Skip the leading backslash. */
    const char *p = path + 1;
    if (!*p)
    {
        if (leaf)
            *leaf = NULL;
        return dir;
    }

    while (1)
    {
        /* Read the next path component. */
        char component[OB_NAME_MAX];
        int ci = 0;
        while (*p && *p != '\\' && ci < OB_NAME_MAX - 1)
            component[ci++] = *p++;
        component[ci] = '\0';

        int is_last = (*p == '\0' || (*p == '\\' && *(p + 1) == '\0'));

        if (is_last)
        {
            if (leaf)
                *leaf = path + (p - (path + 1) - ci);
            /* find where leaf name starts in original path */
            /* just return component via a static buffer */
            static char leaf_buf[OB_NAME_MAX];
            ob_strcpy(leaf_buf, component, OB_NAME_MAX);
            if (leaf)
                *leaf = leaf_buf;
            return dir;
        }

        /* Move into the matching child directory. */
        object_header_t *child = dir->ns_children;
        object_header_t *found = NULL;
        while (child)
        {
            if (ob_strcmp(child->name, component) == 0 &&
                child->type == &ObTypeDirectory)
            {
                found = child;
                break;
            }
            child = child->ns_next;
        }
        if (!found)
            return NULL;
        dir = found;
        if (*p == '\\')
            p++;
    }
}

object_header_t *ob_mkdir(const char *path)
{
    const char *leaf = NULL;
    object_header_t *parent = ns_walk(path, &leaf);
    if (!parent || !leaf)
        return NULL;

    /* Make sure the directory does not already exist. */
    object_header_t *c = parent->ns_children;
    while (c)
    {
        if (ob_strcmp(c->name, leaf) == 0)
            return c;
        c = c->ns_next;
    }

    object_header_t *dir = ob_alloc_header();
    if (!dir)
        return NULL;
    dir->type = &ObTypeDirectory;
    dir->ref_count = 1;
    dir->body = NULL;
    ob_strcpy(dir->name, leaf, OB_NAME_MAX);
    dir->ns_parent = parent;
    dir->ns_children = NULL;
    dir->ns_next = parent->ns_children;
    parent->ns_children = dir;

    kprintf("ob: mkdir %s\n", path);
    return dir;
}

int ob_insert(const char *path, object_header_t *obj)
{
    const char *leaf = NULL;
    object_header_t *parent = ns_walk(path, &leaf);
    if (!parent || !leaf)
    {
        kprintf("ob: insert failed, bad path: %s\n", path);
        return -1;
    }

    ob_strcpy(obj->name, leaf, OB_NAME_MAX);
    obj->ns_parent = parent;
    obj->ns_next = parent->ns_children;
    parent->ns_children = obj;
    ob_ref(obj);
    return 0;
}

object_header_t *ob_lookup(const char *path)
{
    const char *leaf = NULL;
    object_header_t *parent = ns_walk(path, &leaf);
    if (!parent)
        return NULL;
    if (!leaf)
        return parent; /* path was root or a directory itself */

    object_header_t *c = parent->ns_children;
    while (c)
    {
        if (ob_strcmp(c->name, leaf) == 0)
            return c;
        c = c->ns_next;
    }
    return NULL;
}

void ob_list(const char *path)
{
    object_header_t *dir;
    if (!path || ob_strcmp(path, "\\") == 0)
    {
        dir = &ns_root;
    }
    else
    {
        dir = ob_lookup(path);
    }

    if (!dir)
    {
        kprintf("ob: %s not found\n", path);
        return;
    }
    if (dir->type != &ObTypeDirectory)
    {
        kprintf("%s  [%s] refs=%u\n", dir->name,
                dir->type ? dir->type->name : "?", dir->ref_count);
        return;
    }

    kprintf("Directory: %s\n", dir->name);
    object_header_t *c = dir->ns_children;
    if (!c)
    {
        kprintf("  (empty)\n");
        return;
    }
    while (c)
    {
        if (c->type == &ObTypeDirectory)
            kprintf("  %s\\  [Directory]\n", c->name);
        else
            kprintf("  %s  [%s] refs=%u\n", c->name,
                    c->type ? c->type->name : "?", c->ref_count);
        c = c->ns_next;
    }
}

/* ── handle table ───────────────────────────────────────────────────────── */
void handle_table_init(handle_table_t *ht)
{
    for (int i = 0; i < MAX_HANDLES; i++)
    {
        ht->entries[i].object = NULL;
        ht->entries[i].access = 0;
    }
}

int handle_alloc(handle_table_t *ht, object_header_t *obj, uint32_t access)
{
    for (int i = 0; i < MAX_HANDLES; i++)
    {
        if (!ht->entries[i].object)
        {
            ht->entries[i].object = obj;
            ht->entries[i].access = access;
            ob_ref(obj);
            return i;
        }
    }
    kprintf("ob: handle table full\n");
    return -1;
}

object_header_t *handle_get(handle_table_t *ht, int handle, uint32_t access)
{
    if (handle < 0 || handle >= MAX_HANDLES)
        return NULL;
    handle_entry_t *e = &ht->entries[handle];
    if (!e->object)
        return NULL;
    if ((e->access & access) != access)
    {
        kprintf("ob: access denied on handle %d\n", handle);
        return NULL;
    }
    return e->object;
}

void handle_close(handle_table_t *ht, int handle)
{
    if (handle < 0 || handle >= MAX_HANDLES)
        return;
    handle_entry_t *e = &ht->entries[handle];
    if (e->object)
    {
        ob_deref(e->object);
        e->object = NULL;
        e->access = 0;
    }
}
