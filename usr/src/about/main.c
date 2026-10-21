#include <stdio.h>
#include <ipc.h>
#include <luna.h>

int main(void)
{
    if (luna_connect() != 0) {
        printf("about: luna_connect failed (is Luna running?)\n");
        return 1;
    }

    int id = luna_window_create("About TermuOS", 340, 200);
    if (id < 0) {
        printf("about: create failed\n");
        return 1;
    }

    if (luna_window_show(id) != 0) {
        printf("about: show failed\n");
        return 1;
    }

    printf("about: window %d\n", id);

    for (;;)
        ;

    return 0;
}
