#pragma once
#include <stdint.h>
#include <stddef.h>

int tcp_connect_ip(const uint8_t ip[4], uint16_t port, uint16_t local_port);
int tcp_send(const void *buf, size_t len);
int tcp_recv(void *buf, size_t max);
int tcp_close(void);
