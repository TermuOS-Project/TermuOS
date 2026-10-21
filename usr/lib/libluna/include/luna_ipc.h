#pragma once
#include <stdint.h>

#define LUNA_PORT_NAME "luna"

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
