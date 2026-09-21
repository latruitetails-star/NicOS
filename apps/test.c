#include <nicos.h>

int main(void)
{
    const char message[] = "NicOS C application\n";

    nicos_write(
        NICOS_STDOUT,
        message,
        sizeof(message) - 1
    );

    return 42;
}
