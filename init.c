#include <comp421/hardware.h>
#include <comp421/yalnix.h>
#include <stdlib.h>
#include <string.h>

int
main(int argc, char **argv)
{
    (void)argc;
    (void)argv;
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
//    char *buf = "abcdefgh\n\0";
//    char *buf2 = "Hey terminal 2!\n\0";
//    char *buf3 = "This should be in terminal 3\n\0";
//    TtyWrite(1, buf, 200);
//    TtyWrite(2, buf2, 200);
//    TtyWrite(3, buf3, 200);
//    TracePrintf(1, "done writing to terminal 1!\n");
    char *args[2];
    char name[36] = { 't','t','y', 'w','r','i', 't','e','3'}; 
    
    args[1] = NULL;
    Exec(name, args);
    return 0;
}
