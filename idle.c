#include <comp421/hardware.h>
#include <comp421/yalnix.h>

int
main(int argc, char **argv)
{
    (void) argc;
    (void) argv;
    
    for (;;) {
        TracePrintf(1, "I'm idle =)\n");
        TracePrintf(1, "IDLE's current pid = %d\n", GetPid());
        Pause();
    }
}
