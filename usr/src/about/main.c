#include <stdio.h>
#include <ipc.h>

int main(void)
{
    long p = port_create("testport");
    ipc_message_user_t m;

    long r0 = port_try_receive(p, &m);
    printf("try empty -> %d\n", r0);

    port_send(p, 42, "hi", 3);
    long r1 = port_try_receive(p, &m);
    printf("try after send -> %d code=%u len=%u\n",
           r1, m.code, m.length);
    
    long r2 = port_try_receive(p, &m);
    printf("try again -> %d\n", r2);
    return 0;
}
