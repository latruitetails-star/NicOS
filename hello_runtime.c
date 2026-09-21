#include "nicos.h"

int main(void) {
    nicos_write(1, "Hello from NicOS Runtime!\n", 26);
    return 42;
}
