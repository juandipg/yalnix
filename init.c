#include <comp421/hardware.h>
#include <comp421/yalnix.h>
#include <stdlib.h>
#include <string.h>

int
main(int argc, char **argv)
{
    (void) argc;
    (void) argv;
    //    TracePrintf(1, "name: %s\n", argv[0]);
    //    TracePrintf(1, "param1: %s\n", argv[1]);
    //    TracePrintf(1, "param2: %s\n", argv[2]);

    //    int pid = Fork();
    //    if (pid == 0) {
    //        TracePrintf(1, "child about to delay from fork\n");
    //        int pid2 = Fork();
    //        if (pid2 == 0) {
    //            TracePrintf(1, "Grandchild about to busy loop\n");
    //
    //        } else {
    //            Delay(5);
    //    
    //            TracePrintf(1, "child done delaying!\n");
    //        }
    //    } else {
    //        TracePrintf(1, "I'm the parent process running while my child delays\n");
    //        TracePrintf(1, "I'm the parent about to delay\n");
    //        Delay(2);
    //        TracePrintf(1, "I'm the parent and I'm done delaying\n");
    //
    //    }

    int pid = Fork();
    TracePrintf(1, "pid : %d about to get blocked\n", pid);
    if (pid == 0) {
        TracePrintf(1, "hello from child\n");
    } else {
        int *st = (void *) USER_STACK_LIMIT;
        TracePrintf(1, "using address %p\n", st);
        int p = Wait(st);
        TracePrintf(1, "got code %d\n", p);
    }
    return 0;
}
