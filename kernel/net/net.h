#pragma once
#include <stdint.h>
#include <stddef.h>

// MAC address
typedef struct
{
    uint8_t b[6];
} __attribute__((packed)) mac_t;

// IPv4 address
typedef struct
{
    uint8_t b[4];
} __attribute__((packed)) ip4_t;

#define MAC_FMT "%02x:%02x:%02x:%02x:%02x:%02x"
#define MAC_ARGS(m) (m).b[0], (m).b[1], (m).b[2], (m).b[3], (m).b[4], (m).b[5]
#define IP_FMT "%u.%u.%u.%u"
#define IP_ARGS(i) (i).b[0], (i).b[1], (i).b[2], (i).b[3]

// Ethernet frame header
typedef struct
{
    mac_t dst;
    mac_t src;
    uint16_t ethertype; // big-endian
} __attribute__((packed)) eth_hdr_t;

#define ETH_IPV4 0x0800
#define ETH_ARP 0x0806

// ARP packet
typedef struct
{
    uint16_t htype; // hardware type (1=ethernet)
    uint16_t ptype; // protocol type (0x0800=IPv4)
    uint8_t hlen;   // hardware addr len (6)
    uint8_t plen;   // protocol addr len (4)
    uint16_t oper;  // 1=request, 2=reply
    mac_t sha;      // sender hardware addr
    ip4_t spa;      // sender protocol addr
    mac_t tha;      // target hardware addr
    ip4_t tpa;      // target protocol addr
} __attribute__((packed)) arp_pkt_t;

// IPv4 header
typedef struct
{
    uint8_t ver_ihl;
    uint8_t dscp_ecn;
    uint16_t total_len;
    uint16_t id;
    uint16_t flags_frag;
    uint8_t ttl;
    uint8_t proto;
    uint16_t checksum;
    ip4_t src;
    ip4_t dst;
} __attribute__((packed)) ip4_hdr_t;

#define IP_PROTO_ICMP 1
#define IP_PROTO_TCP 6
#define IP_PROTO_UDP 17

// ICMP header
typedef struct
{
    uint8_t type;
    uint8_t code;
    uint16_t checksum;
    uint16_t id;
    uint16_t seq;
} __attribute__((packed)) icmp_hdr_t;

// UDP header
typedef struct
{
    uint16_t src_port;
    uint16_t dst_port;
    uint16_t length;
    uint16_t checksum;
} __attribute__((packed)) udp_hdr_t;

// TCP header
typedef struct
{
    uint16_t src_port;
    uint16_t dst_port;
    uint32_t seq;
    uint32_t ack;
    uint8_t offset_reserved;
    uint8_t flags;
    uint16_t window;
    uint16_t checksum;
    uint16_t urgent;
} __attribute__((packed)) tcp_hdr_t;

// Network interface
typedef struct
{
    mac_t mac;
    ip4_t ip;
    ip4_t gateway;
    ip4_t netmask;
    void (*send)(const void *data, size_t len);
} netif_t;

extern netif_t netif;

void net_init(void);
void net_receive(const void *data, size_t len); // called by driver on packet rx

// Send helpers
void net_send_arp_request(ip4_t target_ip);
void net_send_icmp_echo(ip4_t dst, uint16_t id, uint16_t seq);
void net_send_smtp(ip4_t dst, uint16_t dst_port,
                   const char *helo, const char *from,
                   const char *to, const char *subject,
                   const char *body);
void net_send_udp(ip4_t dst, uint16_t src_port, uint16_t dst_port,
                  const void *data, size_t len);

int net_send_tcp(ip4_t dst, uint16_t src_port, uint16_t dst_port,
                  uint32_t seq, uint32_t ack, uint8_t flags,
                  const void *data, size_t len);

uint16_t net_htons(uint16_t x);
uint32_t net_htonl(uint32_t x);

extern uint8_t net_tx_buf[2048];
uint16_t net_checksum(const void *data, size_t len);
ip4_t net_route(ip4_t dst);
int net_arp_lookup(ip4_t ip, mac_t *mac_out);
