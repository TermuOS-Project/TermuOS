#pragma once

#include "net.h"
#include <stdint.h>
#include <stddef.h>

#define TCP_FIN 0x01
#define TCP_SYN 0x02
#define TCP_RST 0x04
#define TCP_PSH 0x08
#define TCP_ACK 0x10
#define TCP_URG 0x20

typedef enum {
    TCP_STATE_CLOSED = 0,
    TCP_STATE_SYN_SENT = 1,
    TCP_STATE_ESTABLISHED = 2,
    TCP_STATE_FIN_WAIT_1 = 3,
    TCP_STATE_TIME_WAIT = 4,
} tcp_state_t;

typedef enum {
    SMTP_STATE_IDLE = 0,
    SMTP_STATE_WAIT_GREETING = 1,
    SMTP_STATE_EHLO_SENT = 2,
    SMTP_STATE_MAIL_SENT = 3,
    SMTP_STATE_RCPT_SENT = 4,
    SMTP_STATE_DATA_SENT = 5,
    SMTP_STATE_BODY_SENT = 6,
    SMTP_STATE_QUIT_SENT = 7,
    SMTP_STATE_DONE = 8,
} smtp_state_t;

typedef struct {
    int active;
    uint32_t iss;
    uint32_t irs;
    uint32_t snd_nxt;
    uint32_t rcv_nxt;
    tcp_state_t state;

    smtp_state_t smtp_state;
    char smtp_curr_line[256];
    size_t smtp_curr_line_len;
    char smtp_lines[4][256];
    int smtp_line_start;
    int smtp_line_end;
} tcp_pcb_t;

extern tcp_pcb_t tcp_pcb;

void tcp_init(void);

int tcp_send_segment(ip4_t dst, uint16_t src_port, uint16_t dst_port,
                     uint32_t seq, uint32_t ack, uint8_t flags,
                     const void *payload, size_t len);

void tcp_input(const ip4_hdr_t *ip, const tcp_hdr_t *tcp,
               const uint8_t *data, size_t len);
