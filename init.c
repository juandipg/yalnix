#include <comp421/hardware.h>
#include <comp421/yalnix.h>
#include <stdlib.h>
#include <string.h>

char *str1;
int *intArr;
int pid;
int pid2;

int
main(int argc, char **argv)
{
    (void) argc;
    (void) argv;
    
    pid = Fork();
    if (pid == 0) {
        TracePrintf(1, "I'm a real process! (child)\n");
        pid2 = Fork();
        if (pid2 == 0) {
            TracePrintf(1, "I'm a child of a child!!!\n");
        }
    } else if (pid == -1) {
        TracePrintf(1, "yalnix refused my child-making\n");
    } else {
        TracePrintf(1, "uninmplemented wth?\n");
    }
    TracePrintf(1, "THIS SHOULD GET PRINTED 3 TIMES??\n");
    for (;;) {
        //TracePrintf(1, "Init's current pid = %d\n", GetPid());
        //Delay(5);
    }
}
