#include <comp421/hardware.h>
#include <stdbool.h>

int availPages;

// different status codes for a process
#define STATUS_RUNNING 1
#define STATUS_READY   2
#define STATUS_WAIT_BLOCKED 3
#define STATUS_DELAY_BLOCKED 4
#define STATUS_TERMINAL_BLOCKED 5

typedef struct PCB PCB;
//typedef struct child child;
typedef struct ExitStatus ExitStatus;
typedef struct FreePage FreePage;
typedef struct PTFreePage PTFreePage;
typedef struct PCBQueue PCBQueue;
typedef struct ExitStatusQueue ExitStatusQueue;
typedef struct inputLine inputLine;
typedef struct lineQueue lineQueue;

//struct child {
//    PCB * pcb;
//    child * next;
//    child * prev;
//};

struct ExitStatus {
    int status;
    int pid;
    ExitStatus *next;
};

struct PCB {
    int pid;
    int delayTimeLeft;
    struct pte *pageTable;
    SavedContext savedContext;
    PCB *nextProc;
    PCB *prevProc;
    int brkVPN;
    int userStackVPN;
    int status;
    int exitStatus;
    PCB * firstChild;
    PCB * nextSibling;
    PCB * prevSibling;
    PCB * parent;
    ExitStatusQueue *childExitStatuses;
};

struct FreePage {
    FreePage *next;
};

struct PTFreePage {
    unsigned int free : 32;
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

struct inputLine {
    struct inputLine *next;
    char *buf;
};

struct lineQueue {
    struct inputLine *first;
    struct inputLine *last;
};

struct terminal {
    lineQueue inputLineQueue;
    PCBQueue readBlockedPCBs;
    PCBQueue writeBlockedPCBs;
    bool writeActive;
};

int LoadProgram(char *name, char **args, ExceptionStackFrame *frame, PCB *pcb, 
        struct pte * pageTable);
void addProcessToEndOfQueue(PCB *pcb, PCBQueue *queue);
PCB *removePCBFromFrontOfQueue(PCBQueue *queue);
void destroyProcess(PCB *proc);
void addExitStatusToEndOfQueue(ExitStatus *exitStatus, ExitStatusQueue *esq);
void removePCBFromQueue(PCBQueue *queue, PCB *pcb);
ExitStatus *removeExitStatusFromFrontOfQueue(ExitStatusQueue *esq);
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
SavedContext * yalnixContextSwitch(SavedContext *ctx, void *p1, void *p2);
int YalnixTtyRead(int tty_id, void *buf, int len);
void addInputLineToEndOfQueue(inputLine *line, lineQueue *queue);
inputLine * removeInputLineFromFrontOfQueue(lineQueue *queue);
