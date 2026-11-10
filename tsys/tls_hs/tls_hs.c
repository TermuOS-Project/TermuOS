#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include "mbedtls/ssl.h"
#include "mbedtls/entropy.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/error.h"
#include "mbedtls/platform.h"

#include "tcp.h"

static int bio_send(void *ctx, const unsigned char *buf, size_t len)
{
    (void)ctx;
    int n = tcp_send(buf, len);
    if (n < 0)
        return MBEDTLS_ERR_SSL_INTERNAL_ERROR;
    if (n == 0)
        return MBEDTLS_ERR_SSL_WANT_WRITE;
    return n;
}

static int bio_recv(void *ctx, unsigned char *buf, size_t len)
{
    (void)ctx;
    int n = tcp_recv(buf, len);
    if (n < 0)  
        return MBEDTLS_ERR_SSL_INTERNAL_ERROR;
    if (n == 0)
        return MBEDTLS_ERR_SSL_WANT_READ;
    return n;
}

static void spin_poll(void)
{
    for (volatile int i = 0; i < 1000; i++)
        ;
}

int main(void)
{
    int ret;
    mbedtls_ssl_context ssl;
    mbedtls_ssl_config conf;
    mbedtls_entropy_context entropy;
    mbedtls_ctr_drbg_context ctr_drbg;
    const char *pers = "termuos-t1";

    printf("tls_hs: start\n");

    mbedtls_ssl_init(&ssl);
    mbedtls_ssl_config_init(&conf);
    mbedtls_entropy_init(&entropy);
    mbedtls_ctr_drbg_init(&ctr_drbg);

    ret = mbedtls_ctr_drbg_seed(&ctr_drbg, mbedtls_entropy_func, &entropy, (const unsigned char *)pers, strlen(pers));
    if (ret != 0) {
        printf("tls_hs: seed failed %d\n", ret);
        return 1;
    }

    ret = mbedtls_ssl_config_defaults(&conf, MBEDTLS_SSL_IS_CLIENT, MBEDTLS_SSL_TRANSPORT_STREAM, MBEDTLS_SSL_PRESET_DEFAULT);
    if (ret != 0) {
        printf("tls_hs: config_defaults %d\n", ret);
        return 1;
    }

    mbedtls_ssl_conf_authmode(&conf, MBEDTLS_SSL_VERIFY_NONE);
    mbedtls_ssl_conf_rng(&conf, mbedtls_ctr_drbg_random, &ctr_drbg);

    ret = mbedtls_ssl_setup(&ssl, &conf);
    if (ret != 0) {
        printf("tls_hs: ssl_setup %d\n", ret);
        return 1;
    }

    mbedtls_ssl_set_hostname(&ssl, "localhost");

    uint8_t ip[4] = { 10, 0, 2, 2 };
    if (tcp_connect(ip, 4433) != 0) {
        printf("tls_hs: tcp_connect failed\n");
        return 1;
    }
    printf("tls_hs: tcp connected\n");

    mbedtls_ssl_set_bio(&ssl, NULL, bio_send, bio_recv, NULL);

    while ((ret = mbedtls_ssl_handshake(&ssl)) != 0) {
        if (ret == MBEDTLS_ERR_SSL_WANT_READ ||
            ret == MBEDTLS_ERR_SSL_WANT_WRITE) {
            spin_poll();
            continue;
        }
        printf("tls_hs: handshake failed %d\n", ret);
        tcp_close();
        return 1;
    }

    printf("tls_hs: handshake ok\n");

    mbedtls_ssl_close_notify(&ssl);
    tcp_close();

    mbedtls_ssl_free(&ssl);
    mbedtls_ssl_config_free(&conf);
    mbedtls_ctr_drbg_free(&ctr_drbg);
    mbedtls_entropy_free(&entropy);

    return 0;
}