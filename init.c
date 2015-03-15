#include <comp421/hardware.h>
#include <comp421/yalnix.h>

int
main(int argc, char **argv)
{
    (void) argc;
    (void) argv;
    
    TracePrintf(0, "Init =)\n");
    
    for (;;) {
        TracePrintf(1, "Init's current pid = %d\n", GetPid());
        Delay(5);
    }
}
