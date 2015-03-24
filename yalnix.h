#include <comp421/hardware.h>
#include <stdbool.h>

// yalnix.c
int availPages;

int allocatePage();
void freePage(int pfn);
void freeVirtualPage(int vpn, struct pte *pageTable);
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
struct pte * getVirtualAddress(void *physicalAddr, void *pageVirtualAddr);


typedef struct PCB PCB;

struct PCB {
    int pid;
    struct pte *pageTable;
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

typedef struct PTFreePage PTFreePage;
struct PTFreePage {
    bool isFull;
    PTFreePage *next;
    PTFreePage *prev;
};

typedef struct Queue Queue;
struct Queue {
    PCB *firstPCB;
    PCB *lastPCB;
};
// load.c
int LoadProgram(char *name, char **args, ExceptionStackFrame *frame, PCB *pcb, 
        struct pte * pageTable);
void addProcessToEndOfQueue(PCB *pcb, Queue *queue);
PCB *removePCBFromFrontOfQueue(Queue *queue);
void destroyProcess(PCB *proc);