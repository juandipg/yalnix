#include <comp421/hardware.h>

// yalnix.c
void allocatePage(int vpn, struct pte *pageTable);
void TrapKernel(ExceptionStackFrame *frame);
void TrapClock(ExceptionStackFrame *frame);
void TrapIllegal(ExceptionStackFrame *frame);
void TrapMemory(ExceptionStackFrame *frame);
void TrapMath(ExceptionStackFrame *frame);
void TrapTtyReceive(ExceptionStackFrame *frame);
void TrapTtyTransmit(ExceptionStackFrame *frame);

typedef struct PCB PCB;

struct PCB {
    int pid;
    struct pte pageTable[PAGE_TABLE_LEN];
    SavedContext savedContext;
    PCB *nextProc;
    int brkVPN;
    int userStackVPN;
};

typedef struct FreePage FreePage;
struct FreePage {
    FreePage *next;
};

// load.c
int LoadProgram(char *name, char **args, ExceptionStackFrame *frame, PCB *pcb);
