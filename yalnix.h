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

// different status codes for a process
#define RUNNING 1
#define READY   2
#define BLOCKED 3

typedef struct PCB PCB;
typedef struct child child;
typedef struct ExitStatus ExitStatus;
typedef struct FreePage FreePage;
typedef struct PTFreePage PTFreePage;
typedef struct PCBQueue PCBQueue;
typedef struct ExitStatusQueue ExitStatusQueue;

struct child {
    PCB * pcb;
    child * sibling;
};

struct ExitStatus {
    int status;
    int pid;
    ExitStatus *next;
};

struct PCB {
    int pid;
    struct pte *pageTable;
    SavedContext savedContext;
    PCB *nextProc;
    PCB *prevProc;
    int brkVPN;
    int userStackVPN;
    int status;
    int exitStatus;
    child * firstChild;
    PCB * parent;
    ExitStatusQueue *childExitStatuses;
};

struct FreePage {
    FreePage *next;
};

struct PTFreePage {
    bool isFull;
    PTFreePage *next;
    PTFreePage *prev;
};

struct PCBQueue {
    PCB *firstPCB;
    PCB *lastPCB;
};

struct ExitStatusQueue {
    ExitStatus *firstExitStatus;
    ExitStatus *lastExitStatus;
};

// load.c
int LoadProgram(char *name, char **args, ExceptionStackFrame *frame, PCB *pcb, 
        struct pte * pageTable);
void addProcessToEndOfQueue(PCB *pcb, PCBQueue *queue);
PCB *removePCBFromFrontOfQueue(PCBQueue *queue);
void destroyProcess(PCB *proc);
void addExitStatusToEndOfQueue(ExitStatus *exitStatus, ExitStatusQueue *esq);
void removePCBFromQueue(PCBQueue *queue, PCB *pcb);
ExitStatus *removeExitStatusFromFrontOfQueue(ExitStatusQueue *esq);
