#include "luna.h"
#include "luna_ipc.h"
#include <ipc.h>
#include <stdio.h>

static long g_luna = -1;
static long g_reply = -1;
static char g_reply_name[LUNA_RPORT_MAX];

static void make_reply_name(void)
{
    const char *p = "luna-r-app";
    int i = 0;
    while (p[i] && i < LUNA_RPORT_MAX - 1) {
        g_reply_name[i] = p[i];
        i++;
    }
    g_reply_name[i] = '\0';
}

int luna_connect(void)
{
    make_reply_name();

    g_reply = port_create(g_reply_name);
    if (g_reply < 0)
        return -1;

    g_luna = port_find(LUNA_PORT_NAME);
    if (g_luna < 0)
        return -1;

    return 0;
}

void luna_disconnect(void)
{
    g_luna = -1;
    g_reply = -1;
}

int luna_window_create(const char *title, int w, int h)
{
    if (g_luna < 0 || g_reply < 0)
        return -1;

    struct luna_create c;
    int i;

    for (i = 0; i < (int)sizeof c; i++)
        ((char *)&c)[i] = 0;

    if (title) {
        for (i = 0; title[i] && i < LUNA_TITLE_MAX - 1; i++)
            c.title[i] = title[i];
    }
    c.w = w;
    c.h = h;
    for (i = 0; g_reply_name[i] && i < LUNA_RPORT_MAX - 1; i++)
        c.reply_port[i] = g_reply_name[i];

    if (port_send(g_luna, LUNA_MSG_CREATE, &c, sizeof c) != 0)
        return -1;

    ipc_message_user_t msg;
    if (port_receive(g_reply, &msg) != 0)
        return -1;

    if (msg.code != LUNA_MSG_REPLY || msg.length < sizeof(struct luna_reply))
        return -1;

    struct luna_reply *r = (struct luna_reply *)msg.inline_data;
    return (int)r->window_id;
}

int luna_window_show(int id)
{
    if (g_luna < 0 || id < 0)
        return -1;
    int32_t wid = (int32_t)id;
    return port_send(g_luna, LUNA_MSG_SHOW, &wid, sizeof wid) == 0 ? 0 : -1;
}

int luna_window_close(int id)
{
    if (g_luna < 0 || id < 0)
        return -1;
    int32_t wid = (int32_t)id;
    return port_send(g_luna, LUNA_MSG_CLOSE, &wid, sizeof wid) == 0 ? 0 : -1;
}
