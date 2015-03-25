#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <comp421/hardware.h>
#include <comp421/yalnix.h>
#include "stdint.h"
#include "yalnix.h"

void * vector_table[TRAP_VECTOR_SIZE];
struct pte *region1PageTable;

// linked list of free pages
int availPages = 0;
struct FreePage * firstFreePage = NULL;
struct PTFreePage * firstHalfPage = NULL;

// process maintenance
PCB *initPCB;
PCB *idlePCB;
PCB *currentPCB;
// ready queue
PCBQueue *readyQueue;
//blocked queue
PCBQueue *blockedQueue;

int currentProcClockTicks = 0;
int requestedClockTicks = 0;
int nextPid = 0;

bool virtualMemoryEnabled = false;
void *kernel_brk;
int global_pmem_size;

// add-hoc PTE and virtual pointer for referencing out of context phys mem
struct pte *topR1PTE;
void *topR1PagePointer = (void *)VMEM_1_LIMIT - PAGESIZE;

// ad-hoc virtual pointers for referencing page tables
void *currentR0PageTableVirtualPointer = (void *)VMEM_1_LIMIT - (2*PAGESIZE);
void *otherR0PageTableVirtualPointer = (void *)VMEM_1_LIMIT - (3*PAGESIZE);

/*
 * Expects:
 *  Nothing
 *
 * Returns:
 *  The PFN of the newly allocated page
 */
int
allocatePage()
{
    // Map the provided virtual page to a free physical page
    int newPFN = (long) firstFreePage / PAGESIZE;
    
    // Use the ad-hoc virtual page to recover the pointer
    // to the next free page from physical memory
    topR1PTE->pfn = newPFN;
    FreePage *p  = (FreePage *)topR1PagePointer;

    // Clear TLB for this page
    WriteRegister(REG_TLB_FLUSH, (RCS421RegVal) p);
    // Now save that pointer, shortening the list by 1
    firstFreePage = p->next;
    availPages--;
    return newPFN;
}

void
freePage(int pfn)
{
    // insert the freePage into the list
    // map the pfn to the topR1PagePointer
    topR1PTE->pfn = pfn;
    // flush the TLB for the topR1PagePointer
    WriteRegister(REG_TLB_FLUSH, (RCS421RegVal) topR1PagePointer);
    
    // set the next pointer to the firstFreePage
    ((FreePage *) topR1PagePointer)->next = firstFreePage;
    
    // set firstFreePage to pfn * PAGESIZE
    firstFreePage = (FreePage *) ((long) pfn * PAGESIZE);
    
    availPages++;
}

void
freeVirtualPage(int vpn, struct pte *pageTable)
{
    freePage(pageTable[vpn].pfn);
    pageTable[vpn].valid = 0;
}


/**
 Returns the physical address of the new page
 table. This physical address is mapped to a virtual
 page VPN in R1 page table
 */
struct pte *
allocatePTMemory()
{
    TracePrintf(1, "allocatePTMemory \n");
    void *physicalAddr;
    if (firstHalfPage == NULL) {
        // free half page list is empty
        // allocate a new page of physical memory, get the physical address
        int pfn = allocatePage();
        TracePrintf(1, "pfn inside allocatePTMemory = %d\n", pfn);
        physicalAddr = (void *) ((long)pfn * PAGESIZE);
        
        topR1PTE->pfn = pfn;
        WriteRegister(REG_TLB_FLUSH, (RCS421RegVal) topR1PagePointer);
        PTFreePage *topHalf  = (PTFreePage *)((char *)topR1PagePointer + (PAGESIZE / 2));
        
        // update the list of half pages with the other half of the page
        // we just allocated
        TracePrintf(1, "Adding a free page to the half-page list\n");
        topHalf->isFull = false;
        topHalf->next = NULL;
        topHalf->prev = NULL;
        firstHalfPage = physicalAddr + PAGE_TABLE_SIZE;
    } else {
        TracePrintf(1, "Removing a free page from the half-page list\n");
        physicalAddr = firstHalfPage;
        // get the virtual address corresponding to that physical address
        struct PTFreePage *freeHalfPage = (PTFreePage *)getVirtualAddress(physicalAddr, otherR0PageTableVirtualPointer);
        firstHalfPage = freeHalfPage->next;
        PTFreePage *next = (PTFreePage *)getVirtualAddress(freeHalfPage->next, currentR0PageTableVirtualPointer);
        next->prev = freeHalfPage->prev;
        
    }
    return physicalAddr;
}

void
freePTMemory(void *pageTablePhysicalAddress)
{
    TracePrintf(1, "Freeing page table memory\n");
    struct pte *pageTable = getVirtualAddress(pageTablePhysicalAddress, otherR0PageTableVirtualPointer);
    
    // figure out if you're on the top or the middle
    void *otherPage = (void *)DOWN_TO_PAGE(pageTable);
    
    if (otherPage == pageTable) {
        TracePrintf(1, "Other page is the top one\n");
        otherPage = (char *)pageTable + (PAGESIZE / 2);
    }
    
    PTFreePage *freeOtherPage = (PTFreePage *)otherPage;
    
    // first check to see if matching half is in the list of free half-pages
    if (!freeOtherPage->isFull) {
        TracePrintf(1, "The other page is free. About to take it out of linked free half page list\n");
        // if it is, remove it from the free half-pages list
        // TODO: Factor this out-- have addFreePageToList and removeFreePageFromList(page)
        if (freeOtherPage->prev == NULL) {
            TracePrintf(1, "Removing first free page\n");
            firstHalfPage = freeOtherPage->next;
        } else {
            TracePrintf(1, "Removing non first page\n");
            PTFreePage *prev = (PTFreePage *)getVirtualAddress(freeOtherPage->prev, currentR0PageTableVirtualPointer);
            prev->next = freeOtherPage->next;
        }
        if (freeOtherPage->next != NULL) {
            TracePrintf(1, "Removing page somewhere in the middle\n");
            PTFreePage *next = (PTFreePage *)getVirtualAddress(freeOtherPage->next, currentR0PageTableVirtualPointer);
            next->prev = freeOtherPage->prev;
        }
        
        freePage(DOWN_TO_PAGE(pageTablePhysicalAddress) / PAGESIZE);
    } else {
        TracePrintf(1, "The other half page was full, about to add this page to the list\n");
        // else, add the half-page to the list of free half-pages
        freeOtherPage->isFull = false;
        freeOtherPage->next = firstHalfPage;
        freeOtherPage->prev = NULL;
        firstHalfPage = pageTablePhysicalAddress;
        if (firstHalfPage->next != NULL) {
            TracePrintf(1, "List was nonempty\n");
            PTFreePage *next = (PTFreePage *)getVirtualAddress(freeOtherPage->next, currentR0PageTableVirtualPointer);
            next->prev = pageTablePhysicalAddress;
        }
        TracePrintf(1, "successfull added page\n");
    }
}

SavedContext *
startInit(SavedContext *ctx, void *frame, void *p2)
{
    (void)p2;
    
    // map the location of init's page table to an ad-hoc virtual address
    struct pte * initPageTable =
            getVirtualAddress(initPCB->pageTable, otherR0PageTableVirtualPointer);
    
    // Step 1: setup init page table
    // set the user space to invalid
    int vpn;
    for (vpn = 0; vpn < KERNEL_STACK_BASE / PAGESIZE; vpn++) {
        initPageTable[vpn].valid = 0;
    }
    // initialize kernel stack area
    for (; vpn < (long) VMEM_0_LIMIT / PAGESIZE; vpn++) {
        initPageTable[vpn].pfn = allocatePage();
        initPageTable[vpn].uprot = 0;
        initPageTable[vpn].kprot = PROT_READ | PROT_WRITE;
        initPageTable[vpn].valid = 1;
    }
    // copy over kernel stack into new physical pages
    for (vpn = KERNEL_STACK_BASE / PAGESIZE; vpn < KERNEL_STACK_LIMIT / PAGESIZE; vpn++) {
        void *r0Pointer = (void *) (VMEM_0_BASE + (long)(vpn * PAGESIZE));
        topR1PTE->pfn = initPageTable[vpn].pfn;
        WriteRegister(REG_TLB_FLUSH, (RCS421RegVal)topR1PagePointer);
        memcpy(topR1PagePointer, r0Pointer, PAGESIZE);
    }
    
    PageTableSanityCheck(10, 10, initPageTable);
    
    initPCB->pid = nextPid++;
    
    // Step 2: switch region 0 page table to new one
    WriteRegister(REG_PTR0, (RCS421RegVal) initPCB->pageTable);
    
    // Step 3: flush TLB
    TracePrintf(1, "About to flush TLB in startInit\n");
    WriteRegister(REG_TLB_FLUSH, TLB_FLUSH_0);
    
    // Step 4: call LoadProgram(init)
    char * args[2];
    char *name = "init";
    args[0] = name;
    args[1] = NULL;
    LoadProgram(name, args, (ExceptionStackFrame *)frame, initPCB, initPageTable);
    
    // Step 5: return
    // idlePCB->savedContext = *ctx;
    currentPCB = initPCB;

    return ctx;
}

SavedContext *
destroyAndContextSwitch (SavedContext *ctx, void *p1, void *p2)
{
    // TODO factor out common code between two context switch functions
    (void)ctx;
    PCB *pcb1 = (PCB *)p1; //init
    PCB *pcb2 = (PCB *)p2; //idle
    
    destroyProcess(pcb1);
    
    WriteRegister(REG_PTR0, (RCS421RegVal)pcb2->pageTable);
    
    WriteRegister(REG_TLB_FLUSH, TLB_FLUSH_0);
    
    currentPCB = pcb2;
    return &pcb2->savedContext; //return idle's context, which will run right after
}

SavedContext *
yalnixContextSwitch(SavedContext *ctx, void *p1, void *p2)
{
    PCB *pcb1 = (PCB *)p1; //init
    PCB *pcb2 = (PCB *)p2; //idle
    
    pcb1->savedContext = *ctx;
    
    WriteRegister(REG_PTR0, (RCS421RegVal)pcb2->pageTable);
    
    WriteRegister(REG_TLB_FLUSH, TLB_FLUSH_0);
    
    currentPCB = pcb2;
    return &pcb2->savedContext; //return idle's context, which will run right after
}

//KERNEL CALLS
int 
YalnixExec(char *filename, char **argvec, ExceptionStackFrame *frame)
{
    int result = LoadProgram(filename, argvec, frame, currentPCB, getVirtualAddress(currentPCB->pageTable, currentR0PageTableVirtualPointer));
    if (result < 0) {
        return ERROR;
    }
    return result;
}

int
YalnixDelay(int clock_ticks)
{
    TracePrintf(1, "clock ticks passed to YalnixDelay = %d\n", clock_ticks);
    requestedClockTicks = clock_ticks;
    if (clock_ticks < 0) {
        return ERROR;
    }
    if (clock_ticks == 0) {
        return 0;
    }
    currentProcClockTicks = 0;
    // add pcb1 back onto ready queue
    addProcessToEndOfQueue(initPCB, readyQueue);
    ContextSwitch(yalnixContextSwitch, &initPCB->savedContext, initPCB, idlePCB);
    return 0;
}

int
YalnixGetPid(void)
{
    return currentPCB->pid;
}

int
YalnixBrk(void *addr)
{
    //TODO: can any of this be factored out?
    
    TracePrintf(1, "YalnixBrk\n");
    TracePrintf(1, "Addr passed to yalnixBrk = %p\n", addr);
    
    int addrVPN = UP_TO_PAGE(addr) / PAGESIZE;
    
    // Calculate the number of pages needed
    int numPagesNeeded = addrVPN - currentPCB->brkVPN;
    
    // Check to make sure there will be an extra page between
    // the stack and the heap, and check to make sure there
    // is enough free physical memory
    if (addrVPN > (currentPCB->userStackVPN - 1) || availPages < numPagesNeeded) {
        TracePrintf(1, "Invalid call to YalnixBrk with address %p\n", addr);
        TracePrintf(1, "numPagesNeeded = %d | availPages = %d\n", 
                numPagesNeeded, availPages);
        return -1;
    }
    
    int vpnStart = currentPCB->brkVPN + 1;
    if (numPagesNeeded < 0) {
        // free the pages
        TracePrintf(1, "freeing %d pages in yalnixBrk\n", numPagesNeeded);
        int vpn;
        for (vpn = vpnStart; vpn > vpnStart + numPagesNeeded; vpn--) {
            freeVirtualPage(vpn, currentPCB->pageTable);
            WriteRegister(REG_TLB_FLUSH, (RCS421RegVal) ((long) vpn * PAGESIZE));
        }
        
    } else {
        //allocate pages
        TracePrintf(1, "allocating %d pages in yalnixBrk\n", numPagesNeeded);
        struct pte *pageTable = getVirtualAddress(currentPCB->pageTable, currentR0PageTableVirtualPointer);
        int vpn;
        for (vpn = vpnStart; vpn < vpnStart + numPagesNeeded; vpn++) {
            pageTable[vpn].kprot = PROT_READ | PROT_WRITE;
            pageTable[vpn].uprot = PROT_READ | PROT_WRITE;
            pageTable[vpn].valid = 1;
            pageTable[vpn].pfn = allocatePage();
            WriteRegister(REG_TLB_FLUSH, (RCS421RegVal) ((long) vpn * PAGESIZE));
        }
    }
    currentPCB->brkVPN += numPagesNeeded;
    TracePrintf(1, "done allocating memory. new PCB brkVPN = %d\n", currentPCB->brkVPN);
    
    return 0;
}

void
YalnixFork(ExceptionStackFrame *frame)
{
    TracePrintf(1, "yalnixfork\n");
    
    // create a new PCB for the new process
    PCB *childPCB = malloc(sizeof(PCB));
    
    childPCB->pageTable = allocatePTMemory();
    
    // allocate space for the child's page table
    childPCB->savedContext = currentPCB->savedContext;
    childPCB->brkVPN = currentPCB->brkVPN;
    childPCB->userStackVPN = currentPCB->userStackVPN;
    
    // setup child PCB
    childPCB->pid = nextPid++;
    childPCB->parent = currentPCB;
    childPCB->nextProc = NULL;
    childPCB->prevProc = NULL;
    childPCB->childExitStatuses = malloc(sizeof(ExitStatusQueue));
    childPCB->childExitStatuses->firstExitStatus = NULL;
    childPCB->childExitStatuses->lastExitStatus = NULL;
    childPCB->firstChild = NULL;
    
    // call CloneProcess inside ContextSwitch
    ContextSwitch(CloneProcess, &currentPCB->savedContext, childPCB, frame);
}

void
YalnixExit(int status)
{
    TracePrintf(1, "YalnixExit\n");
    // For each child in the current PCB, set its parent pointer to NULL
    child *currentChild = currentPCB->firstChild;
    while (currentChild != NULL) {
        currentChild->pcb->parent = NULL;
        currentChild = currentChild->sibling;
    }
    
    // If the current PCB has a parent, check it's parent's status
    // If the parent is waiting:
    //  1. move the parent from the blocked queue to the ready queue
    //  2. Add a new childStatus to the parent's FIFO dead child queue
    if (currentPCB->parent != NULL) {
        if (currentPCB->parent->status == BLOCKED) {
            removePCBFromQueue(blockedQueue, currentPCB->parent);
            addProcessToEndOfQueue(currentPCB->parent, readyQueue);
            ExitStatus *exitStatus = malloc(sizeof(ExitStatus));
            exitStatus->pid = currentPCB->pid;
            exitStatus->status = status;
            addExitStatusToEndOfQueue(exitStatus, currentPCB->parent->childExitStatuses);
        }
    }
    
    // Context switch using the destroyAndContextSwitch function
    PCB *nextReadyProc = removePCBFromFrontOfQueue(readyQueue);
    if (nextReadyProc == NULL) {
        nextReadyProc = idlePCB;
    }
    ContextSwitch(destroyAndContextSwitch, &currentPCB->savedContext, currentPCB, nextReadyProc);
}

int
YalnixWait(int *status_ptr)
{
    (void)status_ptr;
    // If the current process's dead child queue is empty:
        // Move the current PCB to the blocked queue
    
        // Context switch to next ready process using yalnixContextSwitch function
    
    // Take first dead child off the parent's dead child queue and 
    // return the status of that dead child
    return 0;
}

struct pte *
getVirtualAddress(void *physicalAddr, void *pageVirtualAddr)
{
    int vpn = (long) (pageVirtualAddr - VMEM_1_BASE) / PAGESIZE;
    TracePrintf(1, "vpn inside getVirtualAddress = %d\n", vpn);
    region1PageTable[vpn].valid = 1;
    region1PageTable[vpn].pfn = DOWN_TO_PAGE(physicalAddr) / PAGESIZE;
    region1PageTable[vpn].kprot = PROT_READ | PROT_WRITE;
    region1PageTable[vpn].uprot = 0;
    WriteRegister(REG_TLB_FLUSH, (RCS421RegVal)pageVirtualAddr);
    
    TracePrintf(1, "about to return virtual address: %p\n", (void *)(((char*)pageVirtualAddr + ((long) physicalAddr & PAGEOFFSET))));
    TracePrintf(1, "page offset = %d\n", (long) physicalAddr & PAGEOFFSET);
    return (struct pte *) (((char*)pageVirtualAddr + ((long) physicalAddr & PAGEOFFSET)));
}

SavedContext *
CloneProcess(SavedContext *ctx, void *p1, void *p2)
{
    PCB *childPCB = (PCB *) p1;
    ExceptionStackFrame *frame = (ExceptionStackFrame *) p2;
    struct pte *childPageTable = getVirtualAddress(childPCB->pageTable, otherR0PageTableVirtualPointer);
    struct pte *currentPageTable = getVirtualAddress(currentPCB->pageTable, currentR0PageTableVirtualPointer);
    
    // return value for the parent
    frame->regs[0] = childPCB->pid;
    
    // make sure we have enough space for the new process
    int reqdPageCount = 0;
    int vpn;
    for (vpn = MEM_INVALID_PAGES; vpn < VMEM_0_LIMIT / PAGESIZE; vpn++) {
        reqdPageCount += currentPageTable[vpn].valid;
    }
    if (reqdPageCount > availPages) {
        TracePrintf(1, "not enough memory to fork. required pages = %d, "
                "available pages = %d \n", reqdPageCount, availPages);
        frame->regs[0] = ERROR;
        // TODO: free the memory being used by childPCB, and the page table
        
        return ctx;
    }
    
    // copy the program into new physical pages
    for (vpn = 0; vpn < VMEM_0_LIMIT / PAGESIZE; vpn++) {
        childPageTable[vpn].valid = currentPageTable[vpn].valid;
        if (childPageTable[vpn].valid == 1) {
            childPageTable[vpn].pfn = allocatePage();
            childPageTable[vpn].kprot = currentPageTable[vpn].kprot;
            childPageTable[vpn].uprot = currentPageTable[vpn].uprot;
            void *r0Pointer = (void *) (VMEM_0_BASE + (long)(vpn * PAGESIZE));
            topR1PTE->pfn = childPageTable[vpn].pfn;
            WriteRegister(REG_TLB_FLUSH, (RCS421RegVal)topR1PagePointer);
            memcpy(topR1PagePointer, r0Pointer, PAGESIZE);
        }
    }
    
    TracePrintf(1, "flushing to see what happens (Again)\n");
    WriteRegister(REG_TLB_FLUSH, TLB_FLUSH_0);
    
    // switch to region 0 page table of new process and flush TLB
    WriteRegister(REG_PTR0, (RCS421RegVal) childPCB->pageTable);
    
    PageTableSanityCheck(10, 10, currentPageTable);
    
    TracePrintf(1, "child pcb r0 page table is at %p\n", childPageTable);
    TracePrintf(1, "flushing r0 tlb now\n");
    WriteRegister(REG_TLB_FLUSH, TLB_FLUSH_0);
    
    // return value for the child
    frame->regs[0] = 0;
    
    // move parent to ready queue
    addProcessToEndOfQueue(currentPCB, readyQueue);
    
    // set current process to child
    currentPCB = childPCB;
    
    TracePrintf(1, "done cloning process\n");
    
    // return;
    return ctx;
}

void
addExitStatusToEndOfQueue(ExitStatus *exitStatus, ExitStatusQueue *esq)
{
    // if the queue is empty
    if (esq->firstExitStatus == NULL) {
        exitStatus->next = NULL;
        esq->lastExitStatus = exitStatus;
        esq->firstExitStatus = exitStatus;
    } else {    // if the queue is nonempty
        esq->lastExitStatus->next = exitStatus;
        exitStatus->next = NULL;
        esq->lastExitStatus = exitStatus;
    }
}

void
addProcessToEndOfQueue(PCB *pcb, PCBQueue *queue)
{
    // if the queue is empty
    if (queue->firstPCB == NULL) {
        pcb->nextProc = NULL;
        queue->lastPCB = pcb;
        queue->firstPCB = pcb;
    } else {    // if the queue is nonempty
        queue->lastPCB->nextProc = pcb;
        pcb->prevProc = queue->lastPCB;
        queue->lastPCB = pcb;
        queue->lastPCB->nextProc = NULL;
    }
}

ExitStatus *
removeExitStatusFromFrontOfQueue(ExitStatusQueue *esq)
{
    ExitStatus *firstES = esq->firstExitStatus;
    if (firstES == NULL) {
        return NULL;
    }
    if (esq->firstExitStatus->next == NULL) {
        esq->lastExitStatus = NULL;
    }
    esq->firstExitStatus = esq->firstExitStatus->next;
    return firstES;
}

PCB *
removePCBFromFrontOfQueue(PCBQueue *queue)
{
    PCB *firstPCB = queue->firstPCB;
    if (firstPCB == NULL) {
        return NULL;
    }
    if (queue->firstPCB->nextProc == NULL) {
        queue->lastPCB = NULL;
    }
    queue->firstPCB->prevProc = NULL;
    queue->firstPCB = queue->firstPCB->nextProc;
    return firstPCB;
}

void
removePCBFromQueue(PCBQueue *queue, PCB *pcb)
{
    if (pcb->prevProc == NULL) {
        removePCBFromFrontOfQueue(queue);
    } else {
        pcb->prevProc->nextProc = pcb->nextProc;
        if (pcb->nextProc != NULL)
            pcb->nextProc->prevProc = pcb->prevProc;
    }
}

//TRAPS
void
TrapKernel(ExceptionStackFrame *frame)
{
    TracePrintf(0, "trapkernel\n");
    if (frame->code == YALNIX_DELAY) {
        frame->regs[0] = YalnixDelay(frame->regs[1]);
    }
    if (frame->code == YALNIX_GETPID) {
        frame->regs[0] = YalnixGetPid();
    }
    if (frame->code == YALNIX_BRK) {
        frame->regs[0] = YalnixBrk((void *)frame->regs[1]);
    }
    if (frame->code == YALNIX_FORK) {
        YalnixFork(frame);
    }
    if (frame->code == YALNIX_EXEC) {
        frame->regs[0] = YalnixExec((char *)frame->regs[1], (char **)frame->regs[2], frame);
    }
    if (frame->code == YALNIX_EXIT) {
        YalnixExit((int)frame->regs[1]);
    }
    if (frame->code == YALNIX_WAIT) {
        frame->regs[0] = YalnixWait((int *)frame->regs[1]);
    }
}

void
TrapClock(ExceptionStackFrame *frame)
{
    currentProcClockTicks++;
    (void) frame;
    TracePrintf(0, "trapclock\n");
    //Get the next process to switch to, and if it's not null, switch to it
    PCB *nextReadyProc = removePCBFromFrontOfQueue(readyQueue);
    
    if (nextReadyProc != NULL) {
        TracePrintf(0, "Going to context switch to process %d \n", nextReadyProc->pid);
        // add pcb1 back onto ready queue
        addProcessToEndOfQueue(currentPCB, readyQueue);
        ContextSwitch(
                yalnixContextSwitch, 
                &currentPCB->savedContext, 
                currentPCB, nextReadyProc);
    } else {
        // Else, do nothing ?
//        ContextSwitch(
//                yalnixContextSwitch, 
//                &currentPCB->savedContext, 
//                currentPCB, idlePCB);
    }
    // TODO: generalize trapclock and remove return
    return;
//    if (currentProcClockTicks >= requestedClockTicks) {
//        ContextSwitch(yalnixContextSwitch, &idlePCB->savedContext, idlePCB, initPCB);
//    }
    
//    Halt();
}

void
TrapIllegal(ExceptionStackFrame *frame)
{
    (void) frame;
    TracePrintf(0, "trapillegal\n");
    Halt();
}

void
TrapMemory(ExceptionStackFrame *frame)
{
    TracePrintf(0, "trapmemory\n");
    TracePrintf(1, "Address causing trapmem = %p\n", frame->addr);
    void *addr = frame->addr;
    
    int addrVPN = DOWN_TO_PAGE(addr) / PAGESIZE;
    
    // Calculate the number of pages needed
    int numPagesNeeded = currentPCB->userStackVPN - addrVPN;
    
    // if addr is in region 0, is below the currently allocated memory
    // for the stack, and above the current break for the currently 
    // executing process, grow the user stack by enough pages
    if (((long)addr < USER_STACK_LIMIT) &&
            (addrVPN < currentPCB->userStackVPN) &&
            (addrVPN > currentPCB->brkVPN - 1)) {
        
        struct pte *pageTable = getVirtualAddress(currentPCB->pageTable, currentR0PageTableVirtualPointer);
        int vpnStart = currentPCB->userStackVPN - numPagesNeeded;
        int vpn;
        for (vpn = vpnStart; vpn < currentPCB->userStackVPN; vpn++) {
            pageTable[vpn].kprot = PROT_READ | PROT_WRITE;
            pageTable[vpn].uprot = PROT_READ | PROT_WRITE;
            pageTable[vpn].valid = 1;
            pageTable[vpn].pfn = allocatePage();
            WriteRegister(REG_TLB_FLUSH, (RCS421RegVal) ((long) vpn * PAGESIZE));
        }
        currentPCB->userStackVPN -= numPagesNeeded;
    }
    
    // else, terminate the currently running process, and context switch
    // to next process
    else {
        PCB *nextReadyProc = removePCBFromFrontOfQueue(readyQueue);
        if (nextReadyProc == NULL) {
            nextReadyProc = idlePCB;
        }
        
        // TODO: Print an error message with the pid and an explanation
        TracePrintf(1, "About to destroy process #%d because there wasn't enough memory!\n", currentPCB->pid);
        ContextSwitch (destroyAndContextSwitch, &currentPCB->savedContext, currentPCB, nextReadyProc);
    }
}

void
destroyProcess(PCB *proc) 
{
    // first, free the page table
    freePTMemory(proc->pageTable);
    
    // then, free the pcb
    free(proc);
}

void
TrapMath(ExceptionStackFrame *frame)
{
    (void) frame;
    TracePrintf(0, "trapmath\n");
    Halt();
}

void
TrapTtyReceive(ExceptionStackFrame *frame)
{
    (void) frame;
    TracePrintf(0, "trapttyreceive\n");
    Halt();
}

void
TrapTtyTransmit(ExceptionStackFrame *frame)
{
    (void) frame;
    TracePrintf(0, "trapttytransmit\n");
    Halt();
}

void
KernelStart(ExceptionStackFrame *frame,
        unsigned int pmem_size, void *orig_brk, char **cmd_args)
{
    // TODO: load the requested program instead of always init
    (void) cmd_args;
    
    kernel_brk = orig_brk;
    global_pmem_size = pmem_size;
    
    // Step 1: Initialize interrupt vector table
    vector_table[TRAP_KERNEL] = TrapKernel;
    vector_table[TRAP_CLOCK] = TrapClock;
    vector_table[TRAP_ILLEGAL] = TrapIllegal;
    vector_table[TRAP_MEMORY] = TrapMemory;
    vector_table[TRAP_MATH] = TrapMath;
    vector_table[TRAP_TTY_RECEIVE] = TrapTtyReceive;
    vector_table[TRAP_TTY_TRANSMIT] = TrapTtyTransmit;
    
    // The rest should be NULL
    int i;
    for (i = TRAP_TTY_TRANSMIT + 1; i < TRAP_VECTOR_SIZE; i++) {
        vector_table[i] = NULL;
    }

    WriteRegister(REG_VECTOR_BASE, (RCS421RegVal) &vector_table);
    TracePrintf(0, "Saved addr of ivt to REG_VECTOR_BASE register\n");
    
    idlePCB = malloc(sizeof(PCB));
    idlePCB->pageTable = malloc(PAGE_TABLE_SIZE);

    
    TracePrintf(1, "Address of idlePCB after malloc: %p\n", idlePCB);

    region1PageTable = malloc(PAGE_TABLE_SIZE);
    
    topR1PTE = &region1PageTable[(VMEM_1_SIZE / PAGESIZE) - 1];
    
    TracePrintf(1, "Done assigning topR1PTE, etc\n");
    
    
    //WARNING: do not make any malloc calls past this 
    //point until virtual memory is enabled
    // Step 2: Build list of available page tables
    FreePage * page;
    // two loops
    // first one, from PMEM_BASE (0) to KERNEL_STACK_BASE
    // this is everything in region 0 except for the kernel stack
    // TODO: is KERNEL_STACK_BASE addressable?
    // TODO: why does this work again?
    for (page = (FreePage *) PMEM_BASE + MEM_INVALID_SIZE;
         page < (FreePage *) KERNEL_STACK_BASE;
         page += PAGESIZE)
    {
        page->next = firstFreePage;
        TracePrintf(5, "Page %p 's next page is %p\n", page, page->next);
        firstFreePage = page;
        availPages++;
    }
    
    TracePrintf(1, "page %p but page + PAGESIZE %p\n", page, page + PAGESIZE);
    
    // second one, from orig_brk to pmem_size
    // pmem_size is in bytes, which conveniently is the smallest
    // addressable unit of memory
    TracePrintf(5, "Top address = %p\n", PMEM_BASE + pmem_size);
    for (page = (FreePage *) kernel_brk;
         page < ((FreePage *) PMEM_BASE) + pmem_size / sizeof(void *);
         page += PAGESIZE / sizeof(void *))
    {
        page->next = firstFreePage;
        TracePrintf(5, "Page %p 's next page is %p\n", page, page->next);
        firstFreePage = page;
        availPages++;
    }
    
    TracePrintf(1, "availPages = %d | pmem_size = %d\n", availPages, pmem_size);
    
    // Step 3: Build page tables for Region 1 and Region 0
    //build R1 page table
    // 1st allocate space in the heap to store the page tabl
    //Add PTEs for Kernel text
    int pfn = VMEM_1_BASE / PAGESIZE;
    int vpn;
    for (vpn = 0; vpn < (long) UP_TO_PAGE(&_etext - VMEM_1_BASE) / PAGESIZE; vpn++) {
        TracePrintf(4, "vpn = %d, pfn = %d\n", vpn, pfn);
        region1PageTable[vpn].pfn = pfn;
        region1PageTable[vpn].uprot = 0;
        region1PageTable[vpn].kprot = PROT_READ | PROT_EXEC;
        region1PageTable[vpn].valid = 1;
        pfn++;
    }
    
    TracePrintf(1, "adding PTEs for kernel data/bss/heap\n");
    //Add PTEs for kernel data/bss/heap
    for (; vpn < (long) (UP_TO_PAGE(kernel_brk) - VMEM_1_BASE)/ PAGESIZE; vpn++) {
        TracePrintf(4, "vpn = %d, pfn = %d\n", vpn, pfn);
        region1PageTable[vpn].pfn = pfn;
        region1PageTable[vpn].uprot = 0;
        region1PageTable[vpn].kprot = PROT_READ | PROT_WRITE;
        region1PageTable[vpn].valid = 1;
        pfn++;
    }
    
    TracePrintf(1, "adding invalid ptes\n");
    //Add invalid PTEs for the rest of memory in R1
    for (; vpn < VMEM_1_SIZE / PAGESIZE; vpn++) {
        region1PageTable[vpn].valid = 0;
        pfn++;
    }
    
    TracePrintf(1, "initializing ad-hoc page in R1\n");
    //Initialize the ad-hoc page in R1 to PROT_READ | PROT_WRITE
    topR1PTE->valid = 1;
    topR1PTE->kprot = PROT_WRITE | PROT_READ;
    
    // build R0 page table for idle process
    // TODO: consider moving this to a function
    // and somehow make it generic so that
    // the pfn is allocated for new processes
    // but not for this special case
    // (here we have to make them the same as virtual pages)
    TracePrintf(1, "building r0 page table now\n");
    pfn = VMEM_0_BASE / PAGESIZE;
    for (vpn = 0; vpn < KERNEL_STACK_BASE / PAGESIZE; vpn++) {
        idlePCB->pageTable[vpn].valid = 0;
        pfn++;
    }

    for (; vpn < VMEM_0_LIMIT / PAGESIZE; vpn++) {
        TracePrintf(4, "vpn = %d, pfn = %d\n", vpn, pfn);
        idlePCB->pageTable[vpn].pfn = pfn;
        idlePCB->pageTable[vpn].uprot = 0;
        idlePCB->pageTable[vpn].kprot = PROT_READ | PROT_WRITE;
        idlePCB->pageTable[vpn].valid = 1;
        pfn++;
    }
    
    // tell hardware where the page tables are
    TracePrintf(1, "telling HW where the two page tables are\n");
    WriteRegister(REG_PTR0, (RCS421RegVal) idlePCB->pageTable);
    WriteRegister(REG_PTR1, (RCS421RegVal) region1PageTable);
    
    PageTableSanityCheck(10, 10, idlePCB->pageTable);
    
    // Step 4: Switch on virtual memory
    TracePrintf(1, "enabling virtual memory\n");
    WriteRegister(REG_VM_ENABLE, 1);
    virtualMemoryEnabled = 1;
    
    // initialize initPCB
    initPCB = malloc(sizeof (PCB));
    initPCB->pageTable = allocatePTMemory();
    initPCB->nextProc = NULL;
    initPCB->prevProc = NULL;
    initPCB->parent = NULL;
    initPCB->childExitStatuses = malloc(sizeof(ExitStatusQueue));
    initPCB->childExitStatuses->firstExitStatus = NULL;
    initPCB->childExitStatuses->lastExitStatus = NULL;
    initPCB->firstChild = NULL;
    
    //allocate process queues
    readyQueue = malloc(sizeof(PCBQueue));
    readyQueue->firstPCB = NULL;
    readyQueue->lastPCB = NULL;
    blockedQueue = malloc(sizeof(PCBQueue));
    blockedQueue->firstPCB = NULL;
    blockedQueue->lastPCB = NULL;
    
    // Step 5: load idle program
    TracePrintf(1, "Loading idle program\n");
    char * args[2];
    char *name = "idle";
    args[0] = name;
    args[1] = NULL;
    LoadProgram("idle", args, frame, idlePCB, idlePCB->pageTable);
    idlePCB->pid = nextPid++;

    // Step 6: call context switch to save idle and load init
    ContextSwitch(startInit, &idlePCB->savedContext, frame, NULL);

    // Step 7: return
}

/*
 * We'll need this once we start using Malloc
 * Empty for now for compilation
 */
int
SetKernelBrk(void *addr)
{
    TracePrintf(0, "setkernerlbrk\n");
    TracePrintf(1, "current kernel_brk is %p\n", kernel_brk);
    
    if (!virtualMemoryEnabled) {
        // Check to make sure there is enough available memory
        if ((long) addr > PMEM_BASE + global_pmem_size) {
            return -1;
        }
        
        // Move the global_orig_brk up to the new memory address
        kernel_brk = addr;
        
    } else { 
        TracePrintf(1, "virtual memory enabled setkernelbrk\n");
        // Calculate the number of pages needed
        int numPagesNeeded = (UP_TO_PAGE(addr) - UP_TO_PAGE(kernel_brk)) / PAGESIZE;
        
        if (addr > topR1PagePointer || availPages < numPagesNeeded) {
            return -1;
        }
        
        int vpnStart = (UP_TO_PAGE(kernel_brk) - VMEM_1_BASE) / PAGESIZE;
        int vpn;
        for (vpn = vpnStart; vpn < vpnStart + numPagesNeeded; vpn++) {
            TracePrintf(1, "allocating page in R1 for vpn = %d\n", vpn);
            region1PageTable[vpn].kprot = PROT_READ | PROT_WRITE;
            region1PageTable[vpn].uprot = 0;
            region1PageTable[vpn].valid = 1;
            region1PageTable[vpn].pfn = allocatePage();
        }
        kernel_brk = addr;
    }
    TracePrintf(1, "kernel_brk is now %p\n", kernel_brk);
    return 0;
}

// TODO: change to take trace levels instead of booleans
void
PageTableSanityCheck(int r0tl, int r1tl, struct pte *r0PageTable)
{
    // R0
        int j;
        for (j = 0; j < VMEM_0_LIMIT / PAGESIZE; j++) {
            TracePrintf(r0tl, "j = %d, valid = %d, pfn = %d, kprot = %d, uprot = %d\n",
                    j, r0PageTable[j].valid, r0PageTable[j].pfn, r0PageTable[j].kprot, r0PageTable[j].uprot);
        }
        TracePrintf(r0tl, "PT 0 array size = %d\n", VMEM_0_SIZE / PAGESIZE);
    
    // R1
        for (j = 0; j < (VMEM_1_LIMIT - VMEM_1_BASE) / PAGESIZE; j++) {
            TracePrintf(r1tl, "j = %d, pfn = %d, kprot = %d\n",
                    j, region1PageTable[j].pfn, region1PageTable[j].kprot);
        }
        TracePrintf(r1tl, "PT 1 array size = %d\n", VMEM_1_SIZE / PAGESIZE);
}

void *
virtualToPhysicalR1(void * virtualAddress)
{
    int vpn = DOWN_TO_PAGE(virtualAddress - VMEM_1_BASE) / PAGESIZE;
    int pfn = region1PageTable[vpn].pfn;
    void *physAddr = (void *) ((pfn * PAGESIZE) | ((long)virtualAddress & PAGEOFFSET));
    TracePrintf(1, "vpn: %d, pfn: %d\n", vpn, pfn);
    TracePrintf(1, "page-aligned phys addr: %p\n", (void *) (long) (pfn * PAGESIZE));
    TracePrintf(1, "virtual addr: %p, physical addr: %p\n", virtualAddress, physAddr);
    return physAddr;
}
