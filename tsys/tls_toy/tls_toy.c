#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "net.h"

/* TLS content types */
#define TLS_CT_CHANGE_CIPHER_SPEC 20
#define TLS_CT_ALERT 21
#define TLS_CT_HANDSHAKE 22
#define TLS_CT_APPLICATION_DATA 23

#define TLS_VER_1_2 0x0303
#define HS_CLIENT_HELLO 1

/* TLS_RSA_WITH_AES_128_CBC_SHA */
#define CS_RSA_AES128_SHA 0x002F

static int g_last_hs_type = -1;

static void put_u16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)(v);
}

static void put_u24(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 16);
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v);
}

static int send_all(const uint8_t *buf, size_t n)
{
    size_t off = 0;
    while (off < n) {
        int r = tcp_send(buf + off, n - off);
        if (r < 0)
            return -1;
        if (r == 0)
            continue;
        off += (size_t)r;
    }
    return 0;
}

static int recv_all(uint8_t *buf, size_t n)
{
    size_t off = 0;
    int spins = 0;
    while (off < n) {
        int r = tcp_recv(buf + off, n - off);
        if (r < 0)
            return -1;
        if (r == 0) {
            if (++spins > 1000000)
                return -2; /* timeout */
            continue;
        }
        spins = 0;
        off += (size_t)r;
    }
    return 0;
}

static int build_client_hello_body(uint8_t *out, size_t out_cap)
{
    uint8_t *p = out;

    put_u16(p, TLS_VER_1_2); p += 2;

    /* random (32) */
    for (int i = 0; i < 32; i++)    
        p[i] = (uint8_t)(0xA0 + i);
    p += 32;

    /* session_id */
    *p++ = 0;

    /* cipher_suites: TLS_RSA_WITH_AES_128_CBC_SHA (0x002F) */
    put_u16(p, 2); p += 2;
    put_u16(p, CS_RSA_AES128_SHA); p += 2;

    /* compression */
    *p++ = 1;
    *p++ = 0;

    /* extensions */
    uint8_t *ext_len_at = p;
    p += 2;

    /* signature_algorithms (0x000d) */
    put_u16(p, 0x000d); p += 2;
    put_u16(p, 8); p += 2;
    put_u16(p, 6); p += 2;
    put_u16(p, 0x0401); p += 2;
    put_u16(p, 0x0501); p += 2;
    put_u16(p, 0x0201); p += 2;

    /* supported_groups (0x000a) */
    put_u16(p, 0x000a); p += 2;
    put_u16(p, 4); p += 2;
    put_u16(p, 2); p += 2;
    put_u16(p, 0x0017); p += 2; /* secp256r1 */

    /* ec_point_formats (0x000b) */
    put_u16(p, 0x000b); p += 2;
    put_u16(p, 2); p += 2;
    *p++ = 1;
    *p++ = 0; /* uncompressed */

    uint16_t ext_len = (uint16_t)(p - (ext_len_at + 2));
    put_u16(ext_len_at, ext_len);
    
    if ((size_t)(p - out) > out_cap)
        return -1;
    return (int)(p - out);
}

static int send_client_hello(void)
{
    uint8_t body[128];
    int blen = build_client_hello_body(body, sizeof body);
    if (blen < 0)
        return -1;

    /* handshake header: type(1) + len(3) + body */
    uint8_t hs[4 + 128];
    hs[0] = HS_CLIENT_HELLO;
    put_u24(hs + 1, (uint32_t)blen);
    memcpy(hs + 4, body, (size_t)blen);
    int hslen = 4 + blen;

    /* TLS record header + handshake message */
    uint8_t rec[5 + 4 + 128];
    rec[0] = TLS_CT_HANDSHAKE;
    put_u16(rec + 1, TLS_VER_1_2);
    put_u16(rec + 3, (uint16_t)hslen);
    memcpy(rec + 5, hs, (size_t)hslen);

    printf("tls_toy: sending ClientHello record len=%d\n", hslen);
    return send_all(rec, (size_t)(5 + hslen));
}

static const char *ct_name(uint8_t t)
{
    switch (t) {
    case TLS_CT_CHANGE_CIPHER_SPEC: return "ccs";
    case TLS_CT_ALERT: return "alert";
    case TLS_CT_HANDSHAKE: return "handshake";
    case TLS_CT_APPLICATION_DATA: return "app_data";
    default: return "other";
    }
}

static int read_one_record(void)
{
    uint8_t hdr[5];
    if (recv_all(hdr, 5) < 0) {
        printf("tls_toy: recv header failed\n");
        return -1;
    }
    
    uint8_t type = hdr[0];
    uint16_t ver = ((uint16_t)hdr[1] << 8) | hdr[2];
    uint16_t len = ((uint16_t)hdr[3] << 8) | hdr[4];

    printf("tls_toy: record type=%u (%s) ver=%u len=%u\n",
           type, ct_name(type), ver, len);
    
    if (len > 16 * 1024) {
        printf("tls_toy: crazy length, abort\n");
        return -1;
    }

    uint8_t chunk[256];
    uint16_t left = len;
    uint8_t first[4];
    int got_hs = 0;

    while (left > 0) {
        size_t n = left > sizeof chunk ? sizeof chunk : left;
        if (recv_all(chunk, n) < 0) {
            printf("tls_toy: recv body failed\n");
            return -1;
        }
        if (!got_hs && type == TLS_CT_HANDSHAKE && n >= 4) {
            first[0] = chunk[0];
            first[1] = chunk[1];
            first[2] = chunk[2];
            first[3] = chunk[3];
            g_last_hs_type = (int)first[0];
            got_hs = 1;
        }
        left = (uint16_t)(left - n);
    }

    if (type == TLS_CT_HANDSHAKE && got_hs) {
        uint32_t ml = ((uint32_t)first[1] << 16) |
                      ((uint32_t)first[2] << 8) |
                      first[3];
        printf("tls_toy: handshake msg_type=%u len=%u\n", first[0], ml);
    }
    if (type == TLS_CT_ALERT && len >= 2) {
        printf("tls_toy: alert (see host openssl log)\n");
    }

    return (int)type;
}

int main(void)
{
    uint8_t ip[4] = {10, 0, 2, 2};

    printf("tls_toy: connect 10.0.2.2:4433\n");
    if (tcp_connect_ip(ip, 4433, 50001) != 0) {
        printf("tls_toy: tcp_connect failed\n");
        return 1;
    }
    printf("tls_toy: connected\n");

    if (send_client_hello() != 0) {
        printf("tls_toy: send ClientHello failed\n");
        tcp_close();
        return 1;
    }

    /* Server may send several records: ServerHello, Certificate, ... */
    for (int i = 0; i < 8; i++) {
        int t = read_one_record();
        if (t < 0)
            break;
        if (g_last_hs_type == 14) { /* ServerHelloDone */
            printf("tls_toy: server hello done, stopping reads\n");
            break;
        }
    }

    tcp_close();
    printf("tls_toy: done\n");
    return 0;
}