#include <stdio.h>
#include <http.h>

int main(void)
{
    uint8_t ip[4] = {10, 0, 2, 2};
    char buf[4096];
    int n = http_get(ip, 18080, "/", buf, sizeof(buf));
    if (n < 0) {
        printf("http_get failed\n");
        return 1;
    }
    printf("%s\n", http_body(buf));
    return 0;
}
