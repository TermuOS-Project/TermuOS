#include <stdio.h>
#include <unistd.h>

int main(void)
{
    long pid = spawn("/bin/echo.tsys");
    printf("spawn -> %d\n", pid);
    //if (pid >= 0)
    //    printf("wait -> %d\n", waitpid(pid));
    return 0;
}
