#include "tcp.h"
#include "../lib/printf.h"
#include "../lib/string.h"
#include "net.h"
#include <stdint.h>
#include <stddef.h>

#include "../drivers/net/virtio_net.h"

tcp_pcb_t tcp_pcb;

extern uint8_t net_tx_buf[];
uint16_t net_checksum(const void *data, size_t len);
ip4_t net_route(ip4_t dst);
int net_arp_lookup(ip4_t ip, mac_t *mac_out);

void tcp_init(void)
{
    memset(&tcp_pcb, 0, sizeof(tcp_pcb));
    tcp_pcb.state = TCP_STATE_CLOSED;
    tcp_pcb.smtp_state = SMTP_STATE_IDLE;
}

static uint16_t tcp_checksum(const ip4_hdr_t *ip, const void *data, size_t len)
{
    struct {
        uint32_t src;
        uint32_t dst;
        uint8_t zero;
        uint8_t proto;
        uint16_t tcp_len;
    } __attribute__((packed)) pseudo;

    uint8_t buf[2048];
    size_t total = sizeof(pseudo) + len;
    if (total > sizeof(buf))
        return 0;

    pseudo.src = ((uint32_t)ip->src.b[0] << 24) |
                 ((uint32_t)ip->src.b[1] << 16) |
                 ((uint32_t)ip->src.b[2] << 8) |
                 (uint32_t)ip->src.b[3];
    pseudo.dst = ((uint32_t)ip->dst.b[0] << 24) |
                 ((uint32_t)ip->dst.b[1] << 16) |
                 ((uint32_t)ip->dst.b[2] << 8) |
                 (uint32_t)ip->dst.b[3];
    pseudo.zero = 0;
    pseudo.proto = IP_PROTO_TCP;
    pseudo.tcp_len = net_htons((uint16_t)len);

    memcpy(buf, &pseudo, sizeof(pseudo));
    memcpy(buf + sizeof(pseudo), data, len);
    return net_checksum(buf, total);
}

int tcp_send_segment(ip4_t dst, uint16_t src_port, uint16_t dst_port,
                     uint32_t seq, uint32_t ack, uint8_t flags,
                     const void *data, size_t len)
{
    mac_t dst_mac;
    ip4_t nexthop = net_route(dst);
    if (net_arp_lookup(nexthop, &dst_mac) < 0) {
        net_send_arp_request(nexthop);
        return -1;
    }

    eth_hdr_t *eth = (eth_hdr_t *)net_tx_buf;
    ip4_hdr_t *ip = (ip4_hdr_t *)(net_tx_buf + sizeof(eth_hdr_t));
    tcp_hdr_t *tcp = (tcp_hdr_t *)(net_tx_buf + sizeof(eth_hdr_t) + sizeof(ip4_hdr_t));
    uint8_t *pay = net_tx_buf + sizeof(eth_hdr_t) + sizeof(ip4_hdr_t) + sizeof(tcp_hdr_t);

    eth->dst = dst_mac;
    eth->src = netif.mac;
    eth->ethertype = net_htons(ETH_IPV4);

    ip->ver_ihl = 0x45;
    ip->dscp_ecn = 0;
    ip->total_len = net_htons((uint16_t)(sizeof(ip4_hdr_t) + sizeof(tcp_hdr_t) + len));
    ip->id = 0;
    ip->flags_frag = 0;
    ip->ttl = 64;
    ip->proto = IP_PROTO_TCP;
    ip->checksum = 0;
    ip->src = netif.ip;
    ip->dst = dst;

    tcp->src_port = net_htons(src_port);
    tcp->dst_port = net_htons(dst_port);
    tcp->seq = net_htonl(seq);
    tcp->ack = net_htonl(ack);
    tcp->offset_reserved = 0x50;
    tcp->flags = flags;
    tcp->window = net_htons(0xffff);
    tcp->checksum = 0;
    tcp->urgent = 0;

    if (len && data) {
        const uint8_t *d = (const uint8_t *)data;
        for (size_t i = 0; i < len; i++)
            pay[i] = d[i];
    }

    tcp->checksum = tcp_checksum(ip, tcp, sizeof(tcp_hdr_t) + len);
    ip->checksum = net_checksum(ip, sizeof(ip4_hdr_t));

    if (netif.send)
        netif.send(net_tx_buf,
                   sizeof(eth_hdr_t) + sizeof(ip4_hdr_t) + sizeof(tcp_hdr_t) + len);
    return 0;
}

void smtp_on_tcp_data(const uint8_t *data, size_t len);

void tcp_input(const ip4_hdr_t *ip, const tcp_hdr_t *tcp,
               const uint8_t *data, size_t len)
{
    uint16_t src_port = net_htons(tcp->src_port);
    uint16_t dst_port = net_htons(tcp->dst_port);

    kprintf("net: TCP from " IP_FMT ":%u -> %u flags=0x%x payload=%u\n",
            IP_ARGS(ip->src), src_port, dst_port, tcp->flags, (unsigned)len);

    if (tcp_pcb.active) {
        if ((tcp->flags & TCP_SYN) && (tcp->flags & TCP_ACK)) {
            tcp_pcb.irs = net_htonl(tcp->seq);
            tcp_pcb.rcv_nxt = tcp_pcb.irs + 1;
            if (tcp_pcb.state == TCP_STATE_SYN_SENT) {
                tcp_pcb.state = TCP_STATE_ESTABLISHED;
                kprintf("tcp: established\n");
                tcp_send_segment(ip->src,
                                 net_htons(tcp->dst_port),
                                 net_htons(tcp->src_port),
                                 tcp_pcb.snd_nxt,
                                 tcp_pcb.rcv_nxt,
                                 TCP_ACK,
                                 NULL,
                                 0);
            }
        } else if ((tcp->flags & TCP_ACK) && tcp_pcb.state == TCP_STATE_SYN_SENT) {
            tcp_pcb.state = TCP_STATE_ESTABLISHED;
            kprintf("tcp: established (ack)\n");
        }
    }

    if (len > 0 && tcp_pcb.smtp_state != SMTP_STATE_IDLE)
        smtp_on_tcp_data(data, len);

    if (len > 0) {
        if (tcp_pcb.active)
            tcp_pcb.rcv_nxt += (uint32_t)len;
        tcp_send_segment(ip->src,
                         net_htons(tcp->dst_port),
                         net_htons(tcp->src_port),
                         tcp_pcb.snd_nxt,
                         tcp_pcb.rcv_nxt,
                         TCP_ACK,
                         NULL,
                         0);
    }
}

int tcp_is_established(void)
{
    return tcp_pcb.active && tcp_pcb.state == TCP_STATE_ESTABLISHED;
}

int tcp_connect(ip4_t dst, uint16_t dst_port, uint16_t local_port)
{
    if (local_port == 0)
        local_port = 50000;

    tcp_pcb.active = 1;
    tcp_pcb.remote_ip = dst;
    tcp_pcb.local_port = local_port;
    tcp_pcb.remote_port = dst_port;
    tcp_pcb.iss = 0x1000;
    tcp_pcb.irs = 0;
    tcp_pcb.snd_nxt = tcp_pcb.iss;
    tcp_pcb.rcv_nxt = 0;
    tcp_pcb.state = TCP_STATE_CLOSED;
    tcp_pcb.smtp_state = SMTP_STATE_IDLE;

    /* SYN */
    if (tcp_send_segment(dst, local_port, dst_port,
                         tcp_pcb.iss, 0, TCP_SYN, NULL, 0) < 0) {
        kprintf("tcp: SYN deferred (ARP), retrying...\n");
        for (int i = 0; i < 1000; i++) {
            virtio_net_poll();
            for (volatile int j = 0; j < 10000; j++)
                ;
        }
        if (tcp_send_segment(dst, local_port, dst_port,
                             tcp_pcb.iss, 0, TCP_SYN, NULL, 0) < 0) {
            kprintf("tcp: SYN failed\n");
            tcp_pcb.active = 0;
            return -1;
        }
    }

    tcp_pcb.snd_nxt = tcp_pcb.iss + 1;
    tcp_pcb.state = TCP_STATE_SYN_SENT;
    kprintf("tcp: SYN_SENT " IP_FMT ":%u\n", IP_ARGS(dst), dst_port);

    for (int i = 0; i < 2000; i++) {
        virtio_net_poll();
        for (volatile int j = 0; j < 10000; j++)
            ;
        if (tcp_pcb.state == TCP_STATE_ESTABLISHED) {
            kprintf("tcp: ESTABLISHED\n");
            return 0;
        }
    }

    kprintf("tcp: connect timeout\n");
    tcp_pcb.state = TCP_STATE_CLOSED;
    tcp_pcb.active = 0;
    return -1;
}
