#include "net.h"
#include "tcp.h"
#include "../drivers/net/virtio_net.h"
#include "../lib/printf.h"
#include "../lib/string.h"
#include <stdint.h>
#include <stddef.h>

netif_t netif = {0};

// ─── Byte order ───────────────────────────────────────────────────────────────

uint16_t net_htons(uint16_t x) { return (x >> 8) | (x << 8); }
uint32_t net_htonl(uint32_t x)
{
    return ((x & 0xff000000) >> 24) | ((x & 0x00ff0000) >> 8) | ((x & 0x0000ff00) << 8) | ((x & 0x000000ff) << 24);
}

// ─── Packet buffer ────────────────────────────────────────────────────────────

uint8_t net_tx_buf[2048];

uint16_t net_checksum(const void *data, size_t len)
{
    const uint16_t *p = (const uint16_t *)data;
    uint32_t sum = 0;
    while (len > 1)
    {
        sum += *p++;
        len -= 2;
    }
    if (len)
        sum += *(const uint8_t *)p;
    while (sum >> 16)
        sum = (sum & 0xffff) + (sum >> 16);
    return ~sum;
}

ip4_t net_route(ip4_t dst) 
{
    for (int i = 0; i < 4; i++)
        if ((dst.b[i] & netif.netmask.b[i]) != (netif.ip.b[i] & netif.netmask.b[i]))
            return netif.gateway;
    return dst;
}

#define ARP_TABLE_SIZE 4

static struct
{
    ip4_t ip;
    mac_t mac;
    int valid;
} arp_table[ARP_TABLE_SIZE];

static void arp_table_set(ip4_t ip, mac_t mac)
{
    for (int i = 0; i < ARP_TABLE_SIZE; i++)
    {
        if (!arp_table[i].valid ||
            (arp_table[i].ip.b[0] == ip.b[0] &&
             arp_table[i].ip.b[1] == ip.b[1] &&
             arp_table[i].ip.b[2] == ip.b[2] &&
             arp_table[i].ip.b[3] == ip.b[3]))
        {
            arp_table[i].ip = ip;
            arp_table[i].mac = mac;
            arp_table[i].valid = 1;
            return;
        }
    }
}

int net_arp_lookup(ip4_t ip, mac_t *mac_out)
{
    for (int i = 0; i < ARP_TABLE_SIZE; i++)
    {
        if (arp_table[i].valid &&
            arp_table[i].ip.b[0] == ip.b[0] && arp_table[i].ip.b[1] == ip.b[1] &&
            arp_table[i].ip.b[2] == ip.b[2] && arp_table[i].ip.b[3] == ip.b[3])
        {
            *mac_out = arp_table[i].mac;
            return 0;
        }
    }
    return -1;
}

// ─── TCP ──────────────────────────────────────────────────────────────────────

static void append_text(char *buf, size_t *off, const char *s)
{
    while (*s && *off + 1 < 1024)
        buf[(*off)++] = *s++;
    buf[*off] = '\0';
}

static void smtp_queue_line(const char *line, size_t len)
{
    if (len == 0)
        return;
    int next = (tcp_pcb.smtp_line_end + 1) % 4;
    if (next == tcp_pcb.smtp_line_start)
        return; // queue full, drop line
    if (len >= sizeof(tcp_pcb.smtp_lines[0]))
        len = sizeof(tcp_pcb.smtp_lines[0]) - 1;
    memcpy(tcp_pcb.smtp_lines[tcp_pcb.smtp_line_end], line, len);
    tcp_pcb.smtp_lines[tcp_pcb.smtp_line_end][len] = '\0';
    tcp_pcb.smtp_line_end = next;
}

static void smtp_process_data(const uint8_t *data, size_t len)
{
    for (size_t i = 0; i < len; i++)
    {
        uint8_t c = data[i];
        if (c == '\r' || c == '\n')
        {
            if (tcp_pcb.smtp_curr_line_len > 0)
            {
                smtp_queue_line(tcp_pcb.smtp_curr_line, tcp_pcb.smtp_curr_line_len);
                tcp_pcb.smtp_curr_line_len = 0;
            }
            continue;
        }
        if (tcp_pcb.smtp_curr_line_len + 1 < sizeof(tcp_pcb.smtp_curr_line))
            tcp_pcb.smtp_curr_line[tcp_pcb.smtp_curr_line_len++] = (char)c;
    }
}

static void smtp_send_command(ip4_t dst, uint16_t dst_port,
                              const char *cmd)
{
    size_t len = 0;
    static char buf[512];
    const char *s = cmd;
    while (*s && len + 1 < sizeof(buf))
        buf[len++] = *s++;
    buf[len] = '\0';
    if (len == 0)
        return;
    if (net_send_tcp(dst, 50000, dst_port, tcp_pcb.snd_nxt, tcp_pcb.rcv_nxt,
                     TCP_PSH | TCP_ACK, buf, len) == 0)
        tcp_pcb.snd_nxt += (uint32_t)len;
}

static int smtp_get_line(char *out, size_t max)
{
    if (tcp_pcb.smtp_line_start == tcp_pcb.smtp_line_end)
        return 0;
    size_t len = 0;
    const char *src = tcp_pcb.smtp_lines[tcp_pcb.smtp_line_start];
    while (src[len] != '\0' && len < max - 1)
        len++;
    memcpy(out, src, len);
    out[len] = '\0';
    tcp_pcb.smtp_line_start = (tcp_pcb.smtp_line_start + 1) % 4;
    return 1;
}

static int wait_smtp_line(char *line, size_t max, int ticks)
{
    for (int i = 0; i < ticks; i++)
    {
        virtio_net_poll();
        if (smtp_get_line(line, max))
            return 1;
        for (volatile int j = 0; j < 10000; j++)
            ;
    }
    return 0;
}

int net_send_tcp(ip4_t dst, uint16_t src_port, uint16_t dst_port,
                 uint32_t seq, uint32_t ack, uint8_t flags,
                 const void *data, size_t len)
{
    return tcp_send_segment(dst, src_port, dst_port, seq, ack, flags, data, len);   
}

void smtp_on_tcp_data(const uint8_t *data, size_t len)
{
    smtp_process_data(data, len);
}

void net_send_smtp(ip4_t dst, uint16_t dst_port,
                   const char *helo, const char *from,
                   const char *to, const char *subject,
                   const char *body)
{
    (void)helo;
    if (!tcp_pcb.active)
    {
        tcp_pcb.active = 1;
        tcp_pcb.iss = 0x1000;
        tcp_pcb.irs = 0;
        tcp_pcb.snd_nxt = tcp_pcb.iss + 1;
        tcp_pcb.rcv_nxt = 0;
        tcp_pcb.state = TCP_STATE_CLOSED;
        tcp_pcb.smtp_state = SMTP_STATE_IDLE;
        tcp_pcb.smtp_curr_line_len = 0;
        tcp_pcb.smtp_line_start = 0;
        tcp_pcb.smtp_line_end = 0;
    }

    tcp_pcb.smtp_state = SMTP_STATE_WAIT_GREETING;
    tcp_pcb.smtp_curr_line_len = 0;
    tcp_pcb.smtp_line_start = 0;
    tcp_pcb.smtp_line_end = 0;

    if (net_send_tcp(dst, 50000, dst_port, tcp_pcb.iss, 0, TCP_SYN, NULL, 0) < 0)
    {
        kprintf("smtp: waiting for ARP reply before TCP SYN\n");
        for (int i = 0; i < 1000; i++)
        {
            virtio_net_poll();
            for (volatile int j = 0; j < 10000; j++)
                ;
        }
        if (net_send_tcp(dst, 50000, dst_port, tcp_pcb.iss, 0, TCP_SYN, NULL, 0) < 0)
        {
            kprintf("smtp: could not send TCP SYN after ARP\n");
            return;
        }
    }
    tcp_pcb.state = TCP_STATE_SYN_SENT;

    for (int i = 0; i < 2000; i++)
    {
        virtio_net_poll();
        for (volatile int j = 0; j < 10000; j++)
            ;
        if (tcp_pcb.state == TCP_STATE_ESTABLISHED)
            break;
    }

    if (tcp_pcb.state != TCP_STATE_ESTABLISHED)
    {
        kprintf("smtp: TCP handshake did not complete; no SMTP data sent\n");
        return;
    }

    kprintf("smtp: TCP connected, waiting for server greeting...\n");
    tcp_pcb.smtp_state = SMTP_STATE_WAIT_GREETING;

    char line[256];
    if (!wait_smtp_line(line, sizeof(line), 2000))
    {
        kprintf("smtp: timeout waiting for SMTP greeting\n");
        return;
    }
    if (line[0] != '2')
    {
        kprintf("smtp: unexpected greeting: %s\n", line);
        return;
    }

    smtp_send_command(dst, dst_port, "EHLO termuos\r\n");
    tcp_pcb.smtp_state = SMTP_STATE_EHLO_SENT;
    while (wait_smtp_line(line, sizeof(line), 2000))
    {
        if (line[0] == '2' && line[1] == '5' && line[2] == '0' && line[3] == ' ')
            break;
    }

    {
        char cmd[256];
        size_t cmd_len = 0;
        append_text(cmd, &cmd_len, "MAIL FROM:<");
        append_text(cmd, &cmd_len, from);
        append_text(cmd, &cmd_len, ">\r\n");
        smtp_send_command(dst, dst_port, cmd);
    }
    tcp_pcb.smtp_state = SMTP_STATE_MAIL_SENT;
    if (!wait_smtp_line(line, sizeof(line), 2000) || line[0] != '2')
    {
        kprintf("smtp: MAIL FROM rejected: %s\n", line);
        return;
    }

    {
        char cmd[256];
        size_t cmd_len = 0;
        append_text(cmd, &cmd_len, "RCPT TO:<");
        append_text(cmd, &cmd_len, to);
        append_text(cmd, &cmd_len, ">\r\n");
        smtp_send_command(dst, dst_port, cmd);
    }
    tcp_pcb.smtp_state = SMTP_STATE_RCPT_SENT;
    if (!wait_smtp_line(line, sizeof(line), 2000) || line[0] != '2')
    {
        kprintf("smtp: RCPT TO rejected: %s\n", line);
        return;
    }

    smtp_send_command(dst, dst_port, "DATA\r\n");
    tcp_pcb.smtp_state = SMTP_STATE_DATA_SENT;
    if (!wait_smtp_line(line, sizeof(line), 2000) || line[0] != '3')
    {
        kprintf("smtp: DATA command rejected: %s\n", line);
        return;
    }

    static char body_text[1024];
    size_t body_len = 0;
    append_text(body_text, &body_len, "Subject: ");
    append_text(body_text, &body_len, subject);
    append_text(body_text, &body_len, "\r\nFrom: ");
    append_text(body_text, &body_len, from);
    append_text(body_text, &body_len, "\r\nTo: ");
    append_text(body_text, &body_len, to);
    append_text(body_text, &body_len, "\r\n\r\n");
    append_text(body_text, &body_len, body);
    append_text(body_text, &body_len, "\r\n.\r\n");

    if (body_len > 0)
        smtp_send_command(dst, dst_port, body_text);
    tcp_pcb.smtp_state = SMTP_STATE_BODY_SENT;
    if (!wait_smtp_line(line, sizeof(line), 2000) || line[0] != '2')
    {
        kprintf("smtp: message body rejected: %s\n", line);
        return;
    }

    smtp_send_command(dst, dst_port, "QUIT\r\n");
    tcp_pcb.smtp_state = SMTP_STATE_QUIT_SENT;
    if (!wait_smtp_line(line, sizeof(line), 2000) || line[0] != '2')
    {
        kprintf("smtp: QUIT rejected: %s\n", line);
        return;
    }

    kprintf("smtp: message sequence complete\n");
}
// ─── ARP ──────────────────────────────────────────────────────────────────────

void net_send_arp_request(ip4_t target_ip)
{
    eth_hdr_t *eth = (eth_hdr_t *)net_tx_buf;
    arp_pkt_t *arp = (arp_pkt_t *)(net_tx_buf + sizeof(eth_hdr_t));

    // Broadcast ethernet
    for (int i = 0; i < 6; i++)
        eth->dst.b[i] = 0xff;
    eth->src = netif.mac;
    eth->ethertype = net_htons(ETH_ARP);

    arp->htype = net_htons(1);
    arp->ptype = net_htons(ETH_IPV4);
    arp->hlen = 6;
    arp->plen = 4;
    arp->oper = net_htons(1); // request
    arp->sha = netif.mac;
    arp->spa = netif.ip;
    for (int i = 0; i < 6; i++)
        arp->tha.b[i] = 0;
    arp->tpa = target_ip;

    if (netif.send)
        netif.send(net_tx_buf, sizeof(eth_hdr_t) + sizeof(arp_pkt_t));
}

static void handle_arp(const eth_hdr_t *eth, const arp_pkt_t *arp)
{
    // Learn sender
    arp_table_set(arp->spa, arp->sha);

    uint16_t oper = net_htons(arp->oper);
    if (oper == 1)
    {
        // Is it for us?
        if (arp->tpa.b[0] != netif.ip.b[0] || arp->tpa.b[1] != netif.ip.b[1] ||
            arp->tpa.b[2] != netif.ip.b[2] || arp->tpa.b[3] != netif.ip.b[3])
            return;

        // Send ARP reply
        eth_hdr_t *reth = (eth_hdr_t *)net_tx_buf;
        arp_pkt_t *rarp = (arp_pkt_t *)(net_tx_buf + sizeof(eth_hdr_t));

        reth->dst = eth->src;
        reth->src = netif.mac;
        reth->ethertype = net_htons(ETH_ARP);

        rarp->htype = net_htons(1);
        rarp->ptype = net_htons(ETH_IPV4);
        rarp->hlen = 6;
        rarp->plen = 4;
        rarp->oper = net_htons(2); // reply
        rarp->sha = netif.mac;
        rarp->spa = netif.ip;
        rarp->tha = arp->sha;
        rarp->tpa = arp->spa;

        if (netif.send)
            netif.send(net_tx_buf, sizeof(eth_hdr_t) + sizeof(arp_pkt_t));

        kprintf("net: ARP reply sent to " IP_FMT "\n", IP_ARGS(arp->spa));
    }
    else if (oper == 2)
    {
        kprintf("net: ARP reply from " IP_FMT " = " MAC_FMT "\n",
                IP_ARGS(arp->spa), MAC_ARGS(arp->sha));
    }
}

// ─── ICMP ─────────────────────────────────────────────────────────────────────

void net_send_icmp_echo(ip4_t dst, uint16_t id, uint16_t seq)
{
    mac_t dst_mac;
    ip4_t nexthop = net_route(dst);
    if (net_arp_lookup(nexthop, &dst_mac) < 0)
    {
        kprintf("net: no ARP entry for " IP_FMT ", sending ARP request\n",
                IP_ARGS(nexthop));
        net_send_arp_request(nexthop);
        return;
    }

    eth_hdr_t *eth = (eth_hdr_t *)net_tx_buf;
    ip4_hdr_t *ip = (ip4_hdr_t *)(net_tx_buf + sizeof(eth_hdr_t));
    icmp_hdr_t *icmp = (icmp_hdr_t *)(net_tx_buf + sizeof(eth_hdr_t) + sizeof(ip4_hdr_t));

    eth->dst = dst_mac;
    eth->src = netif.mac;
    eth->ethertype = net_htons(ETH_IPV4);

    ip->ver_ihl = 0x45;
    ip->dscp_ecn = 0;
    ip->total_len = net_htons(sizeof(ip4_hdr_t) + sizeof(icmp_hdr_t));
    ip->id = net_htons(id);
    ip->flags_frag = 0;
    ip->ttl = 64;
    ip->proto = IP_PROTO_ICMP;
    ip->checksum = 0;
    ip->src = netif.ip;
    ip->dst = dst;
    ip->checksum = net_checksum(ip, sizeof(ip4_hdr_t));

    icmp->type = 8; // echo request
    icmp->code = 0;
    icmp->checksum = 0;
    icmp->id = net_htons(id);
    icmp->seq = net_htons(seq);
    icmp->checksum = net_checksum(icmp, sizeof(icmp_hdr_t));

    if (netif.send)
        netif.send(net_tx_buf, sizeof(eth_hdr_t) + sizeof(ip4_hdr_t) + sizeof(icmp_hdr_t));

    kprintf("net: ICMP echo sent to " IP_FMT "\n", IP_ARGS(dst));
}

static void handle_icmp(const ip4_hdr_t *ip, const icmp_hdr_t *icmp)
{
    if (icmp->type == 0)
    {
        kprintf("net: ICMP echo reply from " IP_FMT " seq=%u\n",
                IP_ARGS(ip->src), net_htons(icmp->seq));
    }
    else if (icmp->type == 8)
    {
        // Echo request — send reply
        mac_t dst_mac;
        if (net_arp_lookup(ip->src, &dst_mac) < 0)
            return;

        eth_hdr_t *reth = (eth_hdr_t *)net_tx_buf;
        ip4_hdr_t *rip = (ip4_hdr_t *)(net_tx_buf + sizeof(eth_hdr_t));
        icmp_hdr_t *ricmp = (icmp_hdr_t *)(net_tx_buf + sizeof(eth_hdr_t) + sizeof(ip4_hdr_t));

        reth->dst = dst_mac;
        reth->src = netif.mac;
        reth->ethertype = net_htons(ETH_IPV4);

        *rip = *ip;
        rip->src = netif.ip;
        rip->dst = ip->src;
        rip->checksum = 0;
        rip->checksum = net_checksum(rip, sizeof(ip4_hdr_t));

        *ricmp = *icmp;
        ricmp->type = 0; // reply
        ricmp->checksum = 0;
        ricmp->checksum = net_checksum(ricmp, sizeof(icmp_hdr_t));

        if (netif.send)
            netif.send(net_tx_buf, sizeof(eth_hdr_t) + sizeof(ip4_hdr_t) + sizeof(icmp_hdr_t));
    }
}

// ─── UDP ──────────────────────────────────────────────────────────────────────

void net_send_udp(ip4_t dst, uint16_t src_port, uint16_t dst_port,
                  const void *data, size_t len)
{
    mac_t dst_mac;
    ip4_t nexthop = net_route(dst);
    if (net_arp_lookup(nexthop, &dst_mac) < 0)
    {
        net_send_arp_request(nexthop);
        return;
    }

    eth_hdr_t *eth = (eth_hdr_t *)net_tx_buf;
    ip4_hdr_t *ip = (ip4_hdr_t *)(net_tx_buf + sizeof(eth_hdr_t));
    udp_hdr_t *udp = (udp_hdr_t *)(net_tx_buf + sizeof(eth_hdr_t) + sizeof(ip4_hdr_t));
    uint8_t *pay = net_tx_buf + sizeof(eth_hdr_t) + sizeof(ip4_hdr_t) + sizeof(udp_hdr_t);

    eth->dst = dst_mac;
    eth->src = netif.mac;
    eth->ethertype = net_htons(ETH_IPV4);

    ip->ver_ihl = 0x45;
    ip->dscp_ecn = 0;
    ip->total_len = net_htons(sizeof(ip4_hdr_t) + sizeof(udp_hdr_t) + len);
    ip->id = 0;
    ip->flags_frag = 0;
    ip->ttl = 64;
    ip->proto = IP_PROTO_UDP;
    ip->checksum = 0;
    ip->src = netif.ip;
    ip->dst = dst;
    ip->checksum = net_checksum(ip, sizeof(ip4_hdr_t));

    udp->src_port = net_htons(src_port);
    udp->dst_port = net_htons(dst_port);
    udp->length = net_htons(sizeof(udp_hdr_t) + len);
    udp->checksum = 0;

    const uint8_t *d = (const uint8_t *)data;
    for (size_t i = 0; i < len && i < sizeof(net_tx_buf) - 64; i++)
        pay[i] = d[i];

    if (netif.send)
        netif.send(net_tx_buf, sizeof(eth_hdr_t) + sizeof(ip4_hdr_t) + sizeof(udp_hdr_t) + len);
}

static void handle_udp(const ip4_hdr_t *ip, const udp_hdr_t *udp)
{
    uint16_t dst_port = net_htons(udp->dst_port);
    uint16_t len = net_htons(udp->length) - sizeof(udp_hdr_t);
    const uint8_t *data = (const uint8_t *)udp + sizeof(udp_hdr_t);
    kprintf("net: UDP from " IP_FMT ":%u -> port %u (%u bytes)\n",
            IP_ARGS(ip->src), net_htons(udp->src_port), dst_port, len);
    (void)data;
}

// ─── Receive dispatch ─────────────────────────────────────────────────────────

void net_receive(const void *frame, size_t len)
{
    if (len < sizeof(eth_hdr_t))
        return;
    const eth_hdr_t *eth = (const eth_hdr_t *)frame;
    uint16_t etype = net_htons(eth->ethertype);

    if (etype == ETH_ARP)
    {
        if (len < sizeof(eth_hdr_t) + sizeof(arp_pkt_t))
            return;
        handle_arp(eth, (const arp_pkt_t *)(eth + 1));
    }
    else if (etype == ETH_IPV4)
    {
        if (len < sizeof(eth_hdr_t) + sizeof(ip4_hdr_t))
            return;
        const ip4_hdr_t *ip = (const ip4_hdr_t *)(eth + 1);
        if (ip->proto == IP_PROTO_ICMP)
        {
            if (len < sizeof(eth_hdr_t) + sizeof(ip4_hdr_t) + sizeof(icmp_hdr_t))
                return;
            handle_icmp(ip, (const icmp_hdr_t *)(ip + 1));
        }
        else if (ip->proto == IP_PROTO_UDP)
        {
            if (len < sizeof(eth_hdr_t) + sizeof(ip4_hdr_t) + sizeof(udp_hdr_t))
                return;
            handle_udp(ip, (const udp_hdr_t *)(ip + 1));
        }
        else if (ip->proto == IP_PROTO_TCP)
        {
            size_t ip_hdr_len = (size_t)(ip->ver_ihl & 0x0f) * 4;
            if (ip_hdr_len < sizeof(ip4_hdr_t))
                return;

            size_t ip_total = net_htons(ip->total_len);
            if (ip_total < ip_hdr_len)
                return;

            size_t max_ip = len - sizeof(eth_hdr_t);
            if (ip_total > max_ip)
                ip_total = max_ip;

            const uint8_t *tcp_bytes = (const uint8_t *)ip + ip_hdr_len;
            size_t tcp_section = ip_total - ip_hdr_len;
            if (tcp_section < sizeof(tcp_hdr_t))
                return;

            const tcp_hdr_t *tcp = (const tcp_hdr_t *)tcp_bytes;
            size_t tcp_hdr_len = (size_t)(tcp->offset_reserved >> 4) * 4;
            if (tcp_hdr_len < sizeof(tcp_hdr_t) || tcp_hdr_len > tcp_section)
                return;

            size_t payload_len = tcp_section - tcp_hdr_len;
            const uint8_t *data = tcp_bytes + tcp_hdr_len;

            tcp_input(ip, tcp, data, payload_len);
        }
    }
}

void net_init(void)
{
    tcp_init();
    kprintf("net: stack initialised.\n");
    kprintf("net: MAC " MAC_FMT "\n", MAC_ARGS(netif.mac));
    kprintf("net: IP  " IP_FMT "\n", IP_ARGS(netif.ip));
}
