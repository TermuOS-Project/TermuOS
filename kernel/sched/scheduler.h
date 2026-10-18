#pragma once
#include <stdint.h>
#include <stddef.h>
#include "../arch/x86_64/idt.h"
#include "../proc/process.h"
#include "../ob/object.h"

#define MAX_THREADS 64
#define STACK_SIZE 16384 // 16 KB per thread

typedef enum
{
    THREAD_DEAD = 0,
    THREAD_READY = 1,
    THREAD_RUNNING = 2,
    THREAD_BLOCKED = 3,
} thread_state_t;

typedef struct thread
{
    uint64_t rsp;
    uint64_t stack_top;
    uint64_t id;
    thread_state_t state;
    char name[32];
    object_header_t *ob_header;
    struct process *owner;
    
    uint64_t syscall_user_rsp;
    uint64_t kstack_top;
    uint8_t *kstack_mem;
} thread_t;

extern thread_t threads[MAX_THREADS];
extern int current;

void scheduler_init(void);
thread_t *thread_create(const char *name, void (*entry)(void), process_t *owner);
void thread_exit(void);
void thread_block(void);
void thread_unblock(thread_t *t);
thread_t *thread_current(void);
void scheduler_tick(registers_t *r);
void scheduler_yield(void);
void preempt_switch(uint64_t *old_rsp, uint64_t new_rsp, uint64_t new_cr3);
void context_switch(uint64_t *old_rsp, uint64_t new_rsp);
