#include <stdio.h>
#include <string.h>

int main(void)
{
    puts("hello from tlibc");
    const char *s = "TermuOS";
    putchar('L');
    putchar('=');
    (void)s;
    return 0;
}