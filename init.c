#include <comp421/hardware.h>
#include <comp421/yalnix.h>
#include <stdlib.h>
#include <string.h>

int
main(int argc, char **argv)
{
    (void) argc;
    (void) argv;

    int pid = Fork();
    if (pid == 0) {
        TracePrintf(1, "child about to delay from fork\n");
        int pid2 = Fork();
        if (pid2 == 0) {
            TracePrintf(1, "Grandchild about to busy loop\n");
            for (;;) {
            }
        } else {
            Delay(5);
    
            TracePrintf(1, "child done delaying!\n");
        }
    } else {
        TracePrintf(1, "I'm the parent process running while my child delays\n");
        TracePrintf(1, "I'm the parent about to delay\n");
        Delay(10);
        TracePrintf(1, "I'm the parent and I'm done delaying\n");
        for (;;) {
        }
    }
    return 0;
}
