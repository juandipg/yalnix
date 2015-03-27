#include <comp421/hardware.h>
#include <comp421/yalnix.h>
#include <stdlib.h>
#include <string.h>

int
main(int argc, char **argv)
{
    (void) argc;
    (void) argv;


//    int pid = Fork();
//    if (pid == 0) {
//        // p1
//        int pid2 = Fork();
//        if (pid2 == 0) {
//            //p2
//            int pid3 = Fork();
//            if (pid3 == 0) {
//                //p3
//                TracePrintf(1, "About to exit p3\n");
//                Exit(3);
//            }
//            TracePrintf(1, "About to exit p2\n");
//            Exit(2);
//        }
//    } else if (pid == -1) {
//        TracePrintf(1, "yalnix refused my child-making\n");
//    } else {
//        int status;
//        TracePrintf(1, "About to wait\n");
//        int waited = Wait(&status);
//        TracePrintf(1, "Waited for child process pid %d to exit with status %d\n", waited, status);
//
//        waited = Wait(&status);
//        TracePrintf(1, "Waited for child process pid %d to exit with status %d\n", waited, status);
//    }
//    //parent busy waits
//    for (;;) {
//
//    }

    int pid = Fork();
    if (pid == 0) {
        TracePrintf(1, "I'm a child with pid = %d\n", GetPid());
        int i = 0;
        while (i<1000000000) {
            i++;
            //TracePrintf(1, "I'm a child: %d\n", i);
        }
        TracePrintf(1, "I'm the first child about to exit\n", GetPid());
        Exit(10);
    } //else {
    int pid2 = Fork();
    if (pid2 == 0) {
        TracePrintf(1, "I'm a second child! %d \n", GetPid());
        int i;
        while (i<1000000000) {
            i++;
        }
        int status;
        TracePrintf(1, "I'm a second child about to wait! %d \n", GetPid());
        int result = Wait(&status);
        result++;
        TracePrintf(1, "I'm the second child about to exit\n", GetPid());
        Exit(10);
    } else {
    TracePrintf(1, "I'm the parent with pid = %d and I'm about to wait\n", GetPid());
    int status;
    int prid = Wait(&status);
    TracePrintf(1, "Waited for child process pid %d to exit with status %d\n", prid, status);
    //}
    for (;;) {
        
    }
    }
    return 0;
}
