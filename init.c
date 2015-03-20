#include <comp421/hardware.h>
#include <comp421/yalnix.h>
#include <stdlib.h>
#include <string.h>

char *str1;
int *intArr;

int
main(int argc, char **argv)
{
    (void) argc;
    (void) argv;
    
    TracePrintf(0, "Init =)\n");
    
    str1 = malloc(10*sizeof(char));
    TracePrintf(0, "malloc str1\n");
    intArr = malloc(200 * sizeof(int));
    TracePrintf(0, "Address of int array = %p\n", intArr);
    TracePrintf(0, "malloc intArr\n");
    
    strcpy(str1, "abcdefgh\n");
    intArr[10] = 20;
    TracePrintf(1, "Array allocated from init has 10th element = %d\n", intArr[10]);
    TracePrintf(1, "String allocated from init: %s\n", str1);
    
    for (;;) {
        TracePrintf(1, "Init's current pid = %d\n", GetPid());
        Delay(5);
    }
}
