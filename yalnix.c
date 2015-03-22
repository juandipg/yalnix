#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <comp421/hardware.h>
#include <comp421/yalnix.h>
#include "stdint.h"
#include "yalnix.h"

void * vector_table[TRAP_VECTOR_SIZE];
//struct pte region1PageTable[VMEM_1_SIZE / PAGESIZE];
struct pte *region1PageTable;

// linked list of free pages
int availPages = 0;
struct FreePage * firstFreePage = NULL;
struct PTFreePage * firstHalfPage = NULL;

PCB *initPCB;
PCB *idlePCB;

PCB *currentPCB;

PCB *firstReadyPCB;
PCB *lastReadyPCB;

PCB *firstBlockedPCB;
PCB *lastBlockedPCB;

PCB *sparePCB;

int currentProcClockTicks = 0;
int requestedClockTicks = 0;
int nextPid = 0;

bool virtualMemoryEnabled = false;
void *kernel_brk;
int global_pmem_size;

void *topR1PagePointer = (void *)VMEM_1_LIMIT - PAGESIZE;
void *currentR0PageTableVirtualPointer = (void *)VMEM_1_LIMIT - (2*PAGESIZE);
void *otherR0PageTableVirtualPointer = (void *)VMEM_1_LIMIT - (3*PAGESIZE);

struct pte *topR1PTE;
struct pte *currentR0PageTablePTE;
struct pte *otherR0PageTablePTE;

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
    // TODO: derive vpn from pointer
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
    void *physicalAddr;
    if (firstHalfPage == NULL) {
        // allocate a new page of physical memory, get the physical address
        int pfn = allocatePage();
        physicalAddr = (void *) ((long)pfn * PAGESIZE);
        
        topR1PTE->pfn = pfn;
        WriteRegister(REG_TLB_FLUSH, (RCS421RegVal) topR1PagePointer);
        PTFreePage *topHalf  = (PTFreePage *)topR1PagePointer + PAGE_TABLE_SIZE;
        
        // update the list of half pages with the other half of the page
        // we just allocated
        topHalf->isFull = false;
        topHalf->next = NULL;
        topHalf->prev = NULL;
        firstHalfPage = physicalAddr + PAGE_TABLE_SIZE;
    } else {
        // TODO finish implementing
        physicalAddr = firstHalfPage;
    }
    return physicalAddr;
}

void
initPageTable(PCB *pcb) {
    // invalidate everything on the user's space
    int i;
    for (i = 0; i < KERNEL_STACK_BASE / PAGESIZE; i++) {
        pcb->pageTable[i].valid = 0;
    }
    
    // initialize kernel stack area
    for (; i < (long) VMEM_0_LIMIT / PAGESIZE; i++) {
        pcb->pageTable[i].pfn = allocatePage();
        pcb->pageTable[i].uprot = 0;
        pcb->pageTable[i].kprot = PROT_READ | PROT_WRITE;
        pcb->pageTable[i].valid = 1;
    }
}

SavedContext *
startInit(SavedContext *ctx, void *frame, void *p2)
{
    (void)p2;
    // Step 1: setup init page table, copying 
    // over kernel stack into new physical pages
    initPageTable(initPCB);
    int vpn = KERNEL_STACK_BASE / PAGESIZE;
    for (; vpn < KERNEL_STACK_LIMIT / PAGESIZE; vpn++) {
        void *r0Pointer = (void *) (VMEM_0_BASE + (long)(vpn * PAGESIZE));
        topR1PTE->pfn = initPCB->pageTable[vpn].pfn;
        WriteRegister(REG_TLB_FLUSH, (RCS421RegVal)topR1PagePointer);
        memcpy(topR1PagePointer, r0Pointer, PAGESIZE);
    }
    
    initPCB->pid = nextPid++;
    
    // Step 2: switch region 0 page table to new one
    WriteRegister(REG_PTR0, (RCS421RegVal) virtualToPhysicalR1(initPCB->pageTable));
    
    // Step 3: flush TLB
    TracePrintf(1, "About to flush TLB in startInit\n");
    WriteRegister(REG_TLB_FLUSH, TLB_FLUSH_0);
    
    // Step 4: call LoadProgram(init)
    char * args[2];
    char *name = "init";
    args[0] = name;
    args[1] = NULL;
    LoadProgram(name, args, (ExceptionStackFrame *)frame, initPCB);
    
    // Step 5: return
    // idlePCB->savedContext = *ctx;
    currentPCB = initPCB;

    return ctx;
}

SavedContext *
yalnixContextSwitch(SavedContext *ctx, void *p1, void *p2)
{
    PCB *pcb1 = (PCB *)p1; //init
    PCB *pcb2 = (PCB *)p2; //idle
    
    pcb1->savedContext = *ctx;
    
    WriteRegister(REG_PTR0, (RCS421RegVal) virtualToPhysicalR1(pcb2->pageTable));
    
    WriteRegister(REG_TLB_FLUSH, TLB_FLUSH_0);
    
    currentPCB = pcb2;
    return &pcb2->savedContext; //return idle's context, which will run right after
}

//KERNEL CALLS
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
        int vpn;
        for (vpn = vpnStart; vpn < vpnStart + numPagesNeeded; vpn++) {
            currentPCB->pageTable[vpn].kprot = PROT_READ | PROT_WRITE;
            currentPCB->pageTable[vpn].uprot = PROT_READ | PROT_WRITE;
            currentPCB->pageTable[vpn].valid = 1;
            currentPCB->pageTable[vpn].pfn = allocatePage();
            WriteRegister(REG_TLB_FLUSH, (RCS421RegVal) ((long) vpn * PAGESIZE));
        }
    }
    currentPCB->brkVPN += numPagesNeeded;
    TracePrintf(4, "done allocating memory. new PCB brkVPN = %d\n", currentPCB->brkVPN);
    
    return 0;
}

void
YalnixFork(ExceptionStackFrame *frame)
{
    TracePrintf(1, "yalnixfork\n");
    
    // create a new PCB for the new process
    PCB *childPCB = sparePCB;
    
    TracePrintf(1, "storing child pcb at %p\n", childPCB);
    
    TracePrintf(1, "allocated space for child pcb\n");
    
    // clone the current PCB
    //*childPCB = *currentPCB;
    
    // allocate space for the child's page table
    childPCB->savedContext = currentPCB->savedContext;
    childPCB->brkVPN = currentPCB->brkVPN;
    childPCB->userStackVPN = currentPCB->userStackVPN;
    
    // assign it a new pid
    childPCB->pid = nextPid++;
    
    // call CloneProcess inside ContextSwitch
    ContextSwitch(CloneProcess, &currentPCB->savedContext, childPCB, frame);
}

SavedContext *
CloneProcess(SavedContext *ctx, void *p1, void *p2)
{
    TracePrintf(1, "clone start, flushing to see what happens\n");
    WriteRegister(REG_TLB_FLUSH, TLB_FLUSH_0);
    
    PCB *childPCB = (PCB *) p1;
    ExceptionStackFrame *frame = (ExceptionStackFrame *) p2;
    
    // return value for the parent
    frame->regs[0] = childPCB->pid;
    
    // make sure we have enough space for the new process
    int reqdPageCount = 0;
    int vpn;
    for (vpn = MEM_INVALID_PAGES; vpn < VMEM_0_LIMIT / PAGESIZE; vpn++) {
        reqdPageCount += currentPCB->pageTable[vpn].valid;
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
        childPCB->pageTable[vpn].valid = currentPCB->pageTable[vpn].valid;
        if (childPCB->pageTable[vpn].valid == 1) {
            childPCB->pageTable[vpn].pfn = allocatePage();
            childPCB->pageTable[vpn].kprot = currentPCB->pageTable[vpn].kprot;
            childPCB->pageTable[vpn].uprot = currentPCB->pageTable[vpn].uprot;
            void *r0Pointer = (void *) (VMEM_0_BASE + (long)(vpn * PAGESIZE));
            topR1PTE->pfn = childPCB->pageTable[vpn].pfn;
            WriteRegister(REG_TLB_FLUSH, (RCS421RegVal)topR1PagePointer);
            memcpy(topR1PagePointer, r0Pointer, PAGESIZE);
        }
    }
    
    TracePrintf(1, "flushing to see what happens (Again)\n");
    WriteRegister(REG_TLB_FLUSH, TLB_FLUSH_0);
    
    // switch to region 0 page table of new process and flush TLB
    WriteRegister(REG_PTR0, (RCS421RegVal) virtualToPhysicalR1(childPCB->pageTable));
    
    PageTableSanityCheck(10, 10, childPCB->pageTable);
    
    TracePrintf(1, "child pcb r0 page table is at %p\n", childPCB->pageTable);
    TracePrintf(1, "flushing r0 tlb now\n");
    WriteRegister(REG_TLB_FLUSH, TLB_FLUSH_0);
    
    // return value for the child
    frame->regs[0] = 0;
    
    // move parent to ready queue
    // TODO?
//    currentPCB->nextProc = firstReadyPCB;
//    currentPCB->prevProc = NULL;
//    firstReadyPCB = currentPCB;
//    lastReadyPCB = currentPCB;
    
    // set current process to child
    currentPCB = childPCB;
    
    TracePrintf(1, "done cloning process\n");
    
    // return;
    return ctx;
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
}

void
TrapClock(ExceptionStackFrame *frame)
{
    currentProcClockTicks++;
    (void) frame;
    TracePrintf(0, "trapclock\n");
    // TODO: generalize trapclock and remove return
    return;
    if (currentProcClockTicks >= requestedClockTicks) {
        ContextSwitch(yalnixContextSwitch, &idlePCB->savedContext, idlePCB, initPCB);
    }
    
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
    (void) frame;
    TracePrintf(0, "trapmemory\n");
    TracePrintf(1, "Address causing trapmem = %p\n", frame->addr);
    Halt();
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
    
    initPCB = malloc(sizeof(PCB));
    initPCB->pageTable = malloc(PAGE_TABLE_SIZE);
    
    sparePCB = malloc(sizeof(PCB));
    sparePCB->pageTable = malloc(PAGE_TABLE_SIZE);
    
    TracePrintf(1, "Address of sparePCB after amlloc: %p\n", sparePCB);
    TracePrintf(1, "Address of idlePCB after malloc: %p\n", idlePCB);

    region1PageTable = malloc(PAGE_TABLE_SIZE);
    
    topR1PTE = &region1PageTable[(VMEM_1_SIZE / PAGESIZE) - 1];
    currentR0PageTablePTE = &region1PageTable[(VMEM_1_SIZE / PAGESIZE) - 2];
    otherR0PageTablePTE = &region1PageTable[(VMEM_1_SIZE / PAGESIZE) - 3];
    
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
    
    PageTableSanityCheck(1, 1, idlePCB->pageTable);
    
    // Step 4: Switch on virtual memory
    TracePrintf(1, "enabling virtual memory\n");
    WriteRegister(REG_VM_ENABLE, 1);
    virtualMemoryEnabled = 1;
    
    //allocate space for init PCB's
    // TODO: fix this so it doesn't cause issues
    //initPCB = malloc(sizeof(PCB));
    
    // Step 5: load idle program
    TracePrintf(1, "Loading idle program\n");
    char * args[2];
    char *name = "idle";
    args[0] = name;
    args[1] = NULL;
    LoadProgram("idle", args, frame, idlePCB);
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
