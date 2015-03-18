#include <comp421/hardware.h>
#include <comp421/yalnix.h>
#include <string.h>
#include <stdlib.h>
#include "stdint.h"

void TrapKernel(ExceptionStackFrame *frame);
void TrapClock(ExceptionStackFrame *frame);
void TrapIllegal(ExceptionStackFrame *frame);
void TrapMemory(ExceptionStackFrame *frame);
void TrapMath(ExceptionStackFrame *frame);
void TrapTtyReceive(ExceptionStackFrame *frame);
void TrapTtyTransmit(ExceptionStackFrame *frame);

void * vector_table[7];
//struct pte region1PageTable[VMEM_1_SIZE / PAGESIZE];
struct pte *region1PageTable;
//extern struct pte region0PageTable[VMEM_0_SIZE / PAGESIZE];

typedef struct FreePage FreePage;

int availPages = 0;
struct FreePage {
    FreePage *next;
};
struct FreePage * firstFreePage = NULL;

typedef struct PCB PCB;

struct PCB {
    int pid;
    struct pte pageTable[VMEM_0_SIZE / PAGESIZE];
    SavedContext savedContext;
    PCB *nextProc;
//    void *pc;
//    void *sp;
};

int LoadProgram(char *name, char **args, ExceptionStackFrame *frame, PCB *pcb);

PCB initPCB;
PCB idlePCB;

PCB *currentPCB;

void *topR1PagePointer = (void *)VMEM_1_LIMIT - PAGESIZE;
int currentProcClockTicks = 0;
int requestedClockTicks = 0;
int nextPid = 0;

int virtualMemoryEnabled = 0;
void *kernel_brk;
int global_pmem_size;

/*
 * Expects:
 *  A page already marked as valid and kernel-readable,
 *  the vpn for this page, and the region for this page.
 *
 * Returns:
 *  Nothing
 */
void
allocatePage(int vpn, struct pte *pageTable)
{
    // Map the provided virtual page to a free physical page
    // and then use that virtual address to get the
    // pointer to the next free physical page
    TracePrintf(1, "vpn = %d\n", vpn);
    int newPFN = (long) firstFreePage / PAGESIZE;
    pageTable[vpn].pfn = newPFN;
    
    // TODO: derive vpn from pointer
    region1PageTable[(VMEM_1_SIZE / PAGESIZE) - 1].pfn = newPFN;
    FreePage *p  = (FreePage *)topR1PagePointer;
    TracePrintf(1, "p address = %p\n", p);
    TracePrintf(1, "Page table entry pfn: %d\n", pageTable[vpn].pfn);

    // Clear TLB for this page
    WriteRegister(REG_TLB_FLUSH, (RCS421RegVal) p);
    // Now save that pointer, shortening the list by 1
    TracePrintf(1, "page p->next = %p\n", p->next);
    firstFreePage = p->next;
    availPages--;
}

void
initPageTable(PCB *pcb) {
    // invalidate everything on the user's space
    int i;
    for (i = 0; i < KERNEL_STACK_BASE / PAGESIZE; i++) {
        pcb->pageTable[i].valid = 0;
    }
    
    // initialize kernel stack area
    for (; i < (long)VMEM_0_LIMIT / PAGESIZE; i++) {
        allocatePage(i, pcb->pageTable);
        TracePrintf(1, "pcb->pagetable[%d].pfn = %d\n", i, pcb->pageTable[i].pfn);
        pcb->pageTable[i].uprot = 0;
        pcb->pageTable[i].kprot = PROT_READ | PROT_WRITE;
        pcb->pageTable[i].valid = 1;
    }
}


SavedContext *
startInit(SavedContext *ctx, void *frame, void *p2)
{
    
    TracePrintf(1, "inside startInit \n");
    (void)p2;
    // Step 1: setup init page table, copying 
    // over kernel stack into new physical pages
    initPageTable(&initPCB);
    int vpn = KERNEL_STACK_BASE / PAGESIZE;
    for (; vpn < KERNEL_STACK_LIMIT / PAGESIZE; vpn++) {
        TracePrintf(1, "vpn = %d \n", vpn);
        TracePrintf(1, "topR1PagePointer = %p\n", topR1PagePointer);
        
        void *r0Pointer = (void *) (VMEM_0_BASE + (long)(vpn * PAGESIZE));
        TracePrintf(1, "r0Pointer = %p\n", r0Pointer);
        
        TracePrintf(1, "PFN = %d\n", initPCB.pageTable[vpn].pfn);
        
        region1PageTable[(VMEM_1_SIZE / PAGESIZE) - 1].pfn = initPCB.pageTable[vpn].pfn;
        WriteRegister(REG_TLB_FLUSH, (RCS421RegVal)topR1PagePointer);
        memcpy(topR1PagePointer, r0Pointer, PAGESIZE);
    }
    
    initPCB.pid = nextPid++;
    
    // Step 2: switch region 0 page table to new one
    WriteRegister(REG_PTR0, (RCS421RegVal) &initPCB.pageTable);
    
    // Step 3: flush TLB
    WriteRegister(REG_TLB_FLUSH, TLB_FLUSH_0);
    
    // Step 4: call LoadProgram(init)
    char * args[2];
    char *name = "init";
    args[0] = name;
    args[1] = NULL;
    TracePrintf(1, "about to load program \n");
    LoadProgram("init", args, (ExceptionStackFrame *)frame, &initPCB);
    
    // Step 5: return
    TracePrintf(1, "about to return\n");
    idlePCB.savedContext = *ctx;
    currentPCB = &initPCB;

    return ctx;
}

SavedContext *
yalnixContextSwitch(SavedContext *ctx, void *p1, void *p2)
{
    PCB *pcb1 = (PCB *)p1; //init
    PCB *pcb2 = (PCB *)p2; //idle
    
    pcb1->savedContext = *ctx;
    
    WriteRegister(REG_PTR0, (RCS421RegVal) &pcb2->pageTable);
    
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
    ContextSwitch(yalnixContextSwitch, &initPCB.savedContext, &initPCB, &idlePCB);
    return 0;
}

int
YalnixGetPid(void)
{
    return currentPCB->pid;
}

//TRAPS
void
TrapKernel(ExceptionStackFrame *frame)
{
    (void) frame;
    TracePrintf(0, "trapkernel\n");
    if (frame->code == YALNIX_DELAY) {
        frame->regs[0] = YalnixDelay(frame->regs[1]);
    }
    if (frame->code == YALNIX_GETPID) {
        frame->regs[0] = YalnixGetPid();
    }
}

void
TrapClock(ExceptionStackFrame *frame)
{
    currentProcClockTicks++;
    (void) frame;
    TracePrintf(0, "trapclock\n");
    if (currentProcClockTicks >= requestedClockTicks) {
        ContextSwitch(yalnixContextSwitch, &idlePCB.savedContext, &idlePCB, &initPCB);
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
    (void) frame;
    (void) pmem_size;
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

    WriteRegister(REG_VECTOR_BASE, (RCS421RegVal) &vector_table);
    TracePrintf(0, "Saved addr of ivt to REG_VECTOR_BASE register\n");
    
    // Step 2: Build list of available page tables
    FreePage * page;
    
    // two loops
    // first one, from PMEM_BASE (0) to KERNEL_STACK_BASE
    // this is everything in region 0 except for the kernel stack
    // TODO: is KERNEL_STACK_BASE addressable?
    for (page = (FreePage *) PMEM_BASE + MEM_INVALID_SIZE; 
            page < (FreePage *) KERNEL_STACK_BASE; 
            page += PAGESIZE) 
    {
        page->next = firstFreePage;
        TracePrintf(5, "Page %p 's next page is %p\n", page, page->next);
        firstFreePage = page;
        availPages++;
    }
    
    // second one, from orig_brk to pmem_size
    // pmem_size is in bytes, which conveniently is the smallest
    // addressable unit of memory
    TracePrintf(5, "Top address = %p\n", PMEM_BASE + pmem_size);
    for (page = (FreePage *) kernel_brk;
            page < ((FreePage *) PMEM_BASE) + pmem_size / sizeof(void *);
            page += PAGESIZE) 
    {
        page->next = firstFreePage;
        TracePrintf(5, "Page %p 's next page is %p\n", page, page->next);
        firstFreePage = page;
        availPages++;
    }
    
    // Step 3: Build page tables for Region 1 and Region 0
    
    //build R1 page table
    
    // 1st allocate space in the heap to store the page table
    region1PageTable = malloc((VMEM_1_SIZE / PAGESIZE) * sizeof(struct pte));
    TracePrintf(1, "requesting %d bytes\n", (VMEM_1_SIZE / PAGESIZE) * sizeof(struct pte));
    TracePrintf(1, "malloc provided %p\n", region1PageTable);
    
    int pfn = VMEM_1_BASE / PAGESIZE;
    
    //Add PTEs for Kernel text
    int i;
    for (i = 0; i < (long) UP_TO_PAGE(&_etext - VMEM_1_BASE) / PAGESIZE; i++) {
        TracePrintf(4, "vpn = %d, pfn = %d\n", i, pfn);
        region1PageTable[i].pfn = pfn;
        region1PageTable[i].uprot = 0;
        region1PageTable[i].kprot = PROT_READ | PROT_EXEC;
        region1PageTable[i].valid = 1;
        TracePrintf(1, "region1PageTable[%d] is at %p\n", i, &region1PageTable[i]);
        pfn++;
    }
    
    //Add PTEs for kernel data/bss/heap
    for (; i < (long) (UP_TO_PAGE(kernel_brk) - VMEM_1_BASE)/ PAGESIZE; i++) {
        TracePrintf(4, "vpn = %d, pfn = %d\n", i, pfn);
        region1PageTable[i].pfn = pfn;
        region1PageTable[i].uprot = 0;
        region1PageTable[i].kprot = PROT_READ | PROT_WRITE;
        region1PageTable[i].valid = 1;
        TracePrintf(1, "region1PageTable[%d] is at %p\n", i, &region1PageTable[i]);
        pfn++;
    }
    
    //Add invalid PTEs for the rest of memory in R1
    for (; i < VMEM_1_SIZE / PAGESIZE; i++) {
        region1PageTable[i].valid = 0;
        pfn++;
    }
    
    //Initialize the ad-hoc page in R1 to PROT_READ | PROT_WRITE
    region1PageTable[(VMEM_1_SIZE / PAGESIZE) - 1].valid = 1;
    region1PageTable[(VMEM_1_SIZE / PAGESIZE) - 1].kprot = PROT_WRITE | PROT_READ;
    
    // build R0 page table for idle process
    // TODO: consider moving this to a function
    // and somehow make it generic so that
    // the pfn is allocated for new processes
    // but not for this special case
    // (here we have to make them the same as virtual pages)
    pfn = VMEM_0_BASE / PAGESIZE;
    for (i=0; i < KERNEL_STACK_BASE / PAGESIZE; i++) {
        idlePCB.pageTable[i].valid = 0;
        pfn++;
    }

    for (; i < VMEM_0_LIMIT / PAGESIZE; i++) {
        TracePrintf(4, "vpn = %d, pfn = %d\n", i, pfn);
        idlePCB.pageTable[i].pfn = pfn;
        idlePCB.pageTable[i].uprot = 0;
        idlePCB.pageTable[i].kprot = PROT_READ | PROT_WRITE;
        idlePCB.pageTable[i].valid = 1;
        pfn++;
    }
    
    //tell hardware where the page tables are
    WriteRegister(REG_PTR0, (RCS421RegVal) &idlePCB.pageTable);
    WriteRegister(REG_PTR1, (RCS421RegVal) region1PageTable);
    
    // PAGE TABLE SANITY CHECK
    // R0
    int j;
    for (j=0; j < VMEM_0_LIMIT/ PAGESIZE; j++) 
    {
        TracePrintf(10, "j = %d, pfn = %d, kprot = %d\n", 
                j, idlePCB.pageTable[j].pfn, idlePCB.pageTable[j].kprot);
    }
    TracePrintf(1, "PT 0 array size = %d\n", VMEM_0_SIZE / PAGESIZE);
    
    // R1
    for (j=0; j < (VMEM_1_LIMIT - VMEM_1_BASE)/ PAGESIZE; j++) 
    {
        TracePrintf(10, "j = %d, pfn = %d, kprot = %d\n", 
                j, region1PageTable[j].pfn, region1PageTable[j].kprot);
    }
    TracePrintf(1, "PT 1 array size = %d\n", VMEM_1_SIZE / PAGESIZE);
    
    // Step 4: Switch on virtual memory
    WriteRegister(REG_VM_ENABLE, 1);
    virtualMemoryEnabled = 1;
    
    // Step 5: load idle program
    char * args[2];
    char *name = "idle";
    args[0] = name;
    args[1] = NULL;
    LoadProgram("idle", args, frame, &idlePCB);
    idlePCB.pid = nextPid++;

    // Step 6: call context switch to save idle and load init
    ContextSwitch(startInit, &idlePCB.savedContext, frame, NULL);
    TracePrintf(1, "done with ContextSwitch \n");
   
    
    // Step 7: return
    
    // Step ?: call load program
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
    // Virtual memory not enabled
    if (virtualMemoryEnabled == 0) {
        // Check to make sure there is enough available memory
        if ((long) addr > PMEM_BASE + global_pmem_size) {
            return -1;
        }
        
        // Move the global_orig_brk up to the new memory address
        kernel_brk = addr;
    } else { //Virtual memory enabled
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
            allocatePage(vpn, region1PageTable);
        }
        kernel_brk = addr;
    }
    TracePrintf(1, "kernel_brk is now %p\n", kernel_brk);
    return 0;
}
