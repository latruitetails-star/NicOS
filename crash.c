#include "nicos.h"
int main(void) {
    nicos_write(1, "Test Ring 3 avec CLI...\n", 24);
    
    
    __asm__ volatile("cli");
    
    nicos_write(1, "Echec: On est en Ring 0\n", 24);
    return 0;
}
