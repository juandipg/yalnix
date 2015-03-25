#include <comp421/hardware.h>
#include <comp421/yalnix.h>
#include <stdlib.h>
#include <string.h>


int
main(int argc, char **argv)
{
    (void) argc;
    (void) argv;
    
//    myArray[100] = 10;
//    myArray[50] = 50;
//    int pid;
//    int pid2;
//  
    //TracePrintf(1, "about to allocate space for string, hopfully in stack\n");
    //str1 = malloc(sizeof(char)*1500);
    //strcpy(str1, "abcdefghijklmnopqrs\n");
//    TracePrintf(1, "myArray[100] = %d", myArray[100]);
//    TracePrintf(1, "myArray[50] = %d", myArray[50]);
// 
    int pid = Fork();
    if (pid == 0) {
        //int myArray[4294967296];
        //TracePrintf(1, "%d\n", myArray[30]);
        TracePrintf(1, "I'm a real process! (child)\n");
//        char * args[2];
//        char *name = "testExec";
//        args[0] = name;
//        args[1] = NULL;
//        Exec(name, args);
        int pid2 = Fork();
        if (pid2 == 0) {
            TracePrintf(1, "I'm a child of a child!!!\n");
        }
    } else if (pid == -1) {
        TracePrintf(1, "yalnix refused my child-making\n");
    } 
    TracePrintf(1, "THIS SHOULD GET PRINTED 3 TIMES??\n");
    TracePrintf(1, "my pid = %d and I did not exit \n", pid);
    for (;;) {
        //TracePrintf(1, "Init's current pid = %d\n", GetPid());
        //Delay(5);
    }
    return 0;
}
