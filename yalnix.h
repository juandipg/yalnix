#include <comp421/hardware.h>
#include <stdbool.h>

// yalnix.c
void allocatePage(int vpn, struct pte *pageTable);
void TrapKernel(ExceptionStackFrame *frame);
void TrapClock(ExceptionStackFrame *frame);
void TrapIllegal(ExceptionStackFrame *frame);
void TrapMemory(ExceptionStackFrame *frame);
void TrapMath(ExceptionStackFrame *frame);
void TrapTtyReceive(ExceptionStackFrame *frame);
void TrapTtyTransmit(ExceptionStackFrame *frame);
SavedContext * CloneProcess(SavedContext *ctx, void *p1, void *p2);
void PageTableSanityCheck(int r0tl, int r1tl, struct pte *r0PageTable);
void * virtualToPhysicalR1(void * virtualAddress);

typedef struct PCB PCB;

struct PCB {
    int pid;
    struct pte pageTable[PAGE_TABLE_LEN];
    SavedContext savedContext;
    PCB *nextProc;
    PCB *prevProc;
    int brkVPN;
    int userStackVPN;
};

typedef struct FreePage FreePage;
struct FreePage {
    FreePage *next;
};

// load.c
int LoadProgram(char *name, char **args, ExceptionStackFrame *frame, PCB *pcb);
