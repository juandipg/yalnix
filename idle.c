#include <comp421/hardware.h>

int
main(int argc, char **argv)
{
    (void) argc;
    (void) argv;
    
    for (;;) {
        TracePrintf(1, "I'm idle =)\n");
        Pause();
    }
}
