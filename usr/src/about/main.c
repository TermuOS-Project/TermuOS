#include <stdio.h>
#include <ipc.h>

#define LUNA_MSG_CREATE 1
#define LUNA_MSG_SHOW 2
#define LUNA_MSG_CLOSE 3
#define LUNA_MSG_REPLY 100

#define LUNA_TITLE_MAX 24
#define LUNA_RPORT_MAX 24

struct luna_create {
    char title[LUNA_TITLE_MAX];
    int32_t w, h;
    char reply_port[LUNA_RPORT_MAX];
}; /* 24+4+4+24 = 56 bytes */

struct luna_reply {
    int32_t window_id;
};

int main(void)
{
    long reply = port_create("luna-r-test");
    long luna = port_find("luna");
    if (reply < 0 || luna < 0) {
        printf("ports fail\n");
        return 1;
    }

    struct luna_create c = {0};
    /* title */
    const char *t = "ExtAbout";
    for (int i = 0; t[i] && i < 23; i++) c.title[i] = t[i];
    c.w = 320;
    c.h = 200;
    const char *r = "luna-r-test";
    for (int i = 0; r[i] && i < 23; i++) c.reply_port[i] = r[i];

    port_send(luna, LUNA_MSG_CREATE, &c, sizeof c);

    ipc_message_user_t m;
    if (port_receive(reply, &m) != 0) {
        printf("no reply\n");
        return 1;
    }
    struct luna_reply *rep = (struct luna_reply *)m.inline_data;
    printf("window_id=%d\n", rep->window_id);

    int32_t id = rep->window_id;
    port_send(luna, LUNA_MSG_SHOW, &id, sizeof id);

    for (;;)
        ;
    return 0;
}
