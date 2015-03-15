#include <comp421/hardware.h>
#include <comp421/yalnix.h>

int
main(int argc, char **argv)
{
    (void) argc;
    (void) argv;
    
    TracePrintf(0, "Init =)\n");
    
    for (;;) {
        TracePrintf(0, "About to delay =)\n");
        Delay(5);
        TracePrintf(0, "Done delaying =)\n");
    }
}
