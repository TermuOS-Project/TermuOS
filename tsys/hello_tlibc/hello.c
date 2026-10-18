#include <unistd.h>

int main(void)
{
    write(1, "hi\n", 3);
    return 0;
}
