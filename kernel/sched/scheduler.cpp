extern "C" {
#include "Kernel.h"
}

extern "C" {
thread_t threads[MAX_THREADS];
int current = 0;
volatile int g_in_syscall = 0;
}

extern "C" void context_switch(uint64_t *old_rsp, uint64_t new_rsp);
extern "C" void preempt_switch(uint64_t *old_rsp, uint64_t new_rsp, uint64_t new_cr3);

enum class ThreadState : int {
    Dead    = THREAD_DEAD,
    Ready   = THREAD_READY,
    Running = THREAD_RUNNING,
    Blocked = THREAD_BLOCKED,
};

static inline ThreadState state_of(const thread_t &t)
{
    return static_cast<ThreadState>(t.state);
}

static inline void set_state(thread_t &t, ThreadState s)
{
    t.state = static_cast<thread_state_t>(s);
}

uint64_t *syscall_user_rsp_ptr;
uint64_t syscall_kstack_top;

class Scheduler {
public:
    void init();
    thread_t *create(const char *name, void (*entry)(void), process_t *owner);
    void exit_current();
    void block_current();
    void unblock(thread_t *t);
    thread_t *current_thread();
    void yield();
    void tick(registers_t *r);

private:
    int initialized_ = 0;

    void update_syscall_stack();
    void load_thread(int idx);
    int next_thread();
};

static Scheduler g_sched;

void Scheduler::update_syscall_stack()
{
    thread_t *t = &threads[current];
    syscall_user_rsp_ptr = &t->syscall_user_rsp;
    syscall_kstack_top = t->kstack_top;
}

void Scheduler::load_thread(int idx)
{
    current = idx;
    update_syscall_stack();
    if (threads[idx].kstack_top)
        tss_set_kernel_stack(threads[idx].kstack_top);
}

int Scheduler::next_thread()
{
    for (int pass = 0; pass < 2; pass++) {
        for (int i = 1; i < MAX_THREADS; i++) {
            int idx = (current + i) % MAX_THREADS;
            if (idx == 0)
                continue;

            thread_t &t = threads[idx];
            ThreadState st = state_of(t);

            if (st == ThreadState::Dead || st == ThreadState::Blocked)
                continue;

            /* reaped / missing process */
            if (!t.owner)
                continue;
            if (t.owner->state == PROC_DEAD || t.owner->state == PROC_ZOMBIE)
                continue;

            if (pass == 0 && st == ThreadState::Ready)
                return idx;
            if (pass == 1 && idx != current)
                return idx;
        }
    }

    if (state_of(threads[0]) != ThreadState::Dead)
        return 0;
    return -1;
}

void Scheduler::init()
{
    for (int i = 0; i < MAX_THREADS; i++)
        set_state(threads[i], ThreadState::Dead);

    threads[0].id = 0;
    set_state(threads[0], ThreadState::Running);
    threads[0].stack_top = 0;
    __builtin_memcpy(threads[0].name, "idle", 5);
    threads[0].owner = proc_kernel();
    threads[0].ob_header = ob_create(&ObTypeThread, "0", &threads[0]);
    ob_insert("\\Thread\\0", threads[0].ob_header);

    current = 0;
    initialized_ = 1;

    uint8_t *ks = static_cast<uint8_t *>(kmalloc(8192));
    threads[0].kstack_mem = ks;
    threads[0].kstack_top = ks ? reinterpret_cast<uint64_t>(ks + 8192) : 0;
    threads[0].syscall_user_rsp = 0;

    update_syscall_stack();
}

thread_t *Scheduler::create(const char *name, void (*entry)(void), process_t *owner)
{
    int slot = -1;
    for (int i = 1; i < MAX_THREADS; i++) {
        if (state_of(threads[i]) == ThreadState::Dead) {
            slot = i;
            break;
        }
    }
    if (slot < 0) {
        kprintf("scheduler: no free slots\n");
        return nullptr;
    }

    uint8_t *stack = static_cast<uint8_t *>(kmalloc(STACK_SIZE));
    if (!stack) {
        kprintf("scheduler: no memory\n");
        return nullptr;
    }

    thread_t *t = &threads[slot];
    t->id = static_cast<uint64_t>(slot);
    set_state(*t, ThreadState::Ready);
    t->stack_top = reinterpret_cast<uint64_t>(stack + STACK_SIZE);

    int ni = 0;
    while (name[ni] && ni < 31) {
        t->name[ni] = name[ni];
        ni++;
    }
    t->name[ni] = '\0';
    t->owner = owner ? owner : proc_kernel();

    uint8_t *ks = static_cast<uint8_t *>(kmalloc(8192));
    t->kstack_mem = ks;
    t->kstack_top = ks ? reinterpret_cast<uint64_t>(ks + 8192) : 0;
    t->syscall_user_rsp = 0;

    uint64_t *sp = reinterpret_cast<uint64_t *>(t->stack_top);
    *--sp = reinterpret_cast<uint64_t>(&thread_exit);
    *--sp = reinterpret_cast<uint64_t>(entry);
    *--sp = 0; /* rbp */
    *--sp = 0; /* rbx */
    *--sp = 0; /* r12 */
    *--sp = 0; /* r13 */
    *--sp = 0; /* r14 */
    *--sp = 0; /* r15 */
    t->rsp = reinterpret_cast<uint64_t>(sp);

    char ob_path[32];
    const char *prefix = "\\Thread\\";
    int pi = 0;
    while (prefix[pi]) {
        ob_path[pi] = prefix[pi];
        pi++;
    }
    uint64_t id_tmp = t->id;
    if (id_tmp == 0) {
        ob_path[pi++] = '0';
    } else {
        char tmp[16];
        int ti = 0;
        while (id_tmp) {
            tmp[ti++] = static_cast<char>('0' + (id_tmp % 10));
            id_tmp /= 10;
        }
        while (ti > 0)
            ob_path[pi++] = tmp[--ti];
    }
    ob_path[pi] = '\0';

    t->ob_header = ob_create(&ObTypeThread, ob_path, t);
    ob_insert(ob_path, t->ob_header);
    return t;
}

void Scheduler::exit_current()
{
    __asm__ volatile("cli");
    set_state(threads[current], ThreadState::Dead);
    __asm__ volatile("sti");
    yield();
    for (;;)
        __asm__ volatile("hlt");
}

void Scheduler::block_current()
{
    set_state(threads[current], ThreadState::Blocked);
    yield();
}

void Scheduler::unblock(thread_t *t)
{
    __asm__ volatile("cli");
    if (state_of(*t) == ThreadState::Blocked)
        set_state(*t, ThreadState::Ready);
    __asm__ volatile("sti");
}

thread_t *Scheduler::current_thread()
{
    return &threads[current];
}

void Scheduler::yield()
{
    if (!initialized_)
        return;

    __asm__ volatile("cli");

    int prev = current;
    int next = next_thread();

    if (next < 0) {
        __asm__ volatile("sti");
        __asm__ volatile("hlt");
        return;
    }

    if (state_of(threads[prev]) == ThreadState::Running)
        set_state(threads[prev], ThreadState::Ready);

    set_state(threads[next], ThreadState::Running);
    load_thread(next);

    if (threads[prev].owner != threads[next].owner) {
        process_t *own = threads[next].owner;
        pagemap_t pm = (own && own->pagemap) ? own->pagemap : proc_kernel()->pagemap;
        vmm_switch(pm);
    }

    __asm__ volatile("sti");
    context_switch(&threads[prev].rsp, threads[next].rsp);
}

void Scheduler::tick(registers_t *r)
{
    (void)r;
    if (!initialized_)
        return;

    int next = next_thread();
    if (next < 0 || next == current)
        return;

    int prev = current;
    if (state_of(threads[prev]) == ThreadState::Running)
        set_state(threads[prev], ThreadState::Ready);
    set_state(threads[next], ThreadState::Running);
    load_thread(next);

    uint64_t new_cr3 = 0;
    if (threads[prev].owner != threads[next].owner) {
        process_t *own = threads[next].owner;
        new_cr3 = (own && own->pagemap) ? own->pagemap : proc_kernel()->pagemap;
    }

    preempt_switch(&threads[prev].rsp, threads[next].rsp, new_cr3);
}

extern "C" void scheduler_init(void)
{
    g_sched.init();
}

extern "C" thread_t *thread_create(const char *name, void (*entry)(void), process_t *owner)
{
    return g_sched.create(name, entry, owner);
}

extern "C" void thread_exit(void)
{
    g_sched.exit_current();
}

extern "C" void thread_block(void)
{
    g_sched.block_current();
}

extern "C" void thread_unblock(thread_t *t)
{
    g_sched.unblock(t);
}

extern "C" thread_t *thread_current(void)
{
    return g_sched.current_thread();
}

extern "C" void scheduler_yield(void)
{
    g_sched.yield();
}

extern "C" void scheduler_tick(registers_t *r)
{
    g_sched.tick(r);
}

extern "C" void sched_set_in_syscall(int v)
{
    g_in_syscall = v;
}

extern "C" int sched_in_syscall(void)
{
    return g_in_syscall;
}
