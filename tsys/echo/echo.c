#include <stdio.h>

int main(int argc, char **argv)
{
    for (int i = 1; i < argc; i++) {
        printf("%s", argv[i]);
        if (i + 1 < argc)
            printf(" ");
    }
    printf("\n");
    return 0;
}
