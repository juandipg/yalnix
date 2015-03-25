#include <comp421/hardware.h>
#include <comp421/yalnix.h>
#include <stdlib.h>
#include <string.h>


int
main(int argc, char **argv)
{
    (void) argc;
    (void) argv;
    
    TracePrintf(1, "I'm in the testExec program! It worked! \n");
    
    TracePrintf(1, "About to exit\n");
    Exit(0);
    TracePrintf(1, "Yikes, something went horribly wrong and I didn't exit :-( \n");
    return 0;
}
