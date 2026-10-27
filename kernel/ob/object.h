#pragma once
#include <stdint.h>
#include <stddef.h>

// access rights
#define OB_ACCESS_READ    (1 << 0)
#define OB_ACCESS_WRITE   (1 << 1)
#define OB_ACCESS_EXECUTE (1 << 2)
#define OB_ACCESS_ALL     (OB_ACCESS_READ | OB_ACCESS_WRITE | OB_ACCESS_EXECUTE)

// object type callbacks
typedef struct object_header object_header_t;

typedef struct
{
  const char *name;
  void (*on_delete)(object_header_t *obj); // called when refcount hits 0
} object_type_t;

// object header - prepended to every managed object
#define OB_NAME_MAX 32
#define OB_TYPE_MAX 32

struct object_header
{
  object_type_t *type;
  char name[OB_NAME_MAX];
  uint32_t ref_count;
  object_header_t *ns_parent; // parent dir in namespace
  object_header_t *ns_children; // linked list of children (if dir)
  object_header_t *ns_next; // next sibling
  void *body; // points to the actual object data
};

// handle
#define MAX_HANDLES 64

typedef struct
{
  object_header_t *object;
  uint32_t access;
} handle_entry_t;

typedef struct
{
  handle_entry_t entries[MAX_HANDLES];
} handle_table_t;

// built-in types
extern object_type_t ObTypeProcess;
extern object_type_t ObTypeThread;
extern object_type_t ObTypeDirectory;
extern object_type_t ObTypeDevice;
extern object_type_t ObTypeDriver;
extern object_type_t ObTypeEvent;
extern object_type_t ObTypePort;
extern object_type_t ObTypeFile;

// API
void ob_init(void);

// type registry
void ob_type_register(object_type_t *type);
object_type_t *ob_type_find(const char *name);
void ob_type_list(void);

// object lifecycle
object_header_t *ob_create(object_type_t *type, const char *name, void *body);
void ob_ref(object_header_t *obj);
void ob_deref(object_header_t *obj);

// namespace
object_header_t *ob_mkdir(const char *path);
int ob_insert(const char *path, object_header_t *obj);
object_header_t *ob_lookup(const char *path);
void ob_list(const char *path); // kprintf dir contents

// handle table
void handle_table_init(handle_table_t *ht);
int handle_alloc(handle_table_t *ht, object_header_t *obj, uint32_t access);
object_header_t *handle_get(handle_table_t *ht, int handle, uint32_t access);
void handle_close(handle_table_t *ht, int handle);
