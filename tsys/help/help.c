#include <unistd.h>

int main(void) {
    write(1, "test", 4);
    return 0;
}
