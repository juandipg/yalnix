#include <comp421/hardware.h>
#include <comp421/yalnix.h>

#define NULL 0;

void TrapKernel(ExceptionStackFrame *frame);
void TrapClock(ExceptionStackFrame *frame);
void TrapIllegal(ExceptionStackFrame *frame);
void TrapMemory(ExceptionStackFrame *frame);
void TrapMath(ExceptionStackFrame *frame);
void TrapTtyReceive(ExceptionStackFrame *frame);
void TrapTtyTransmit(ExceptionStackFrame *frame);

void * vector_table[7];
struct pte region1PageTable[VMEM_1_SIZE / PAGESIZE];
struct pte region0PageTable[VMEM_0_SIZE / PAGESIZE];


typedef struct FreePage FreePage;

struct FreePage {
    FreePage *next;
};

struct FreePage * firstFreePage = NULL;

void
TrapKernel(ExceptionStackFrame *frame)
{
    (void) frame;
    TracePrintf(0, "trapkernel\n");
    Halt();
}

void
TrapClock(ExceptionStackFrame *frame)
{
    (void) frame;
    TracePrintf(0, "trapclock\n");
    Halt();
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
    (void) orig_brk;
    (void) cmd_args;
    
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
        firstFreePage = page;
    }
    
    // second one, from orig_brk to pmem_size
    // pmem_size is in bytes, which conveniently is the smallest
    // addressable unit of memory
    for (page = (FreePage *) orig_brk; 
            page < ((FreePage *) PMEM_BASE) + pmem_size / sizeof(void *); 
            page += PAGESIZE) 
    {
        page->next = firstFreePage;
        firstFreePage = page;
    }
    
    // Step 3: Build page tables for Region 1 and Region 0
    
    
    //build R1 page table
    void * physicalPageAddress = (void *) DOWN_TO_PAGE(VMEM_1_BASE);
    
    //Add PTEs for Kernel text
    int i;
    for (i = 0; i < (long) UP_TO_PAGE(&_etext - VMEM_1_BASE) / PAGESIZE; i++) {
        TracePrintf(1, "i = %d, pfn = %d\n", i, (long) physicalPageAddress / PAGESIZE);
        region1PageTable[i].pfn = (long) physicalPageAddress / PAGESIZE;
        region1PageTable[i].uprot = 0;
        region1PageTable[i].kprot = PROT_READ | PROT_EXEC;
        region1PageTable[i].valid = 1;
        physicalPageAddress += PAGESIZE;
    }
    
    //Add PTEs for kernel data/bss/heap
    for (; i < (long) (orig_brk - VMEM_1_BASE)/ PAGESIZE; i++) {
        region1PageTable[i].pfn = (long) physicalPageAddress / PAGESIZE;
        region1PageTable[i].uprot = 0;
        region1PageTable[i].kprot = PROT_READ | PROT_WRITE;
        region1PageTable[i].valid = 1;
        physicalPageAddress += PAGESIZE;
    }
    
    //Add invalid PTEs for the rest of memory in R1
    for (; i < VMEM_1_SIZE / PAGESIZE; i++) {
        region1PageTable[i].valid = 0;
        physicalPageAddress += PAGESIZE;
    }
    
    //build R0 page table
    physicalPageAddress = DOWN_TO_PAGE(VMEM_0_BASE);
    for (i=0; i < KERNEL_STACK_BASE / PAGESIZE; i++) {
        region0PageTable[i].valid = 0;
        physicalPageAddress += PAGESIZE;
    }

    for (; i < VMEM_0_LIMIT / PAGESIZE; i++) {
        region0PageTable[i].pfn = (long) physicalPageAddress / PAGESIZE;
        region0PageTable[i].uprot = 0;
        region0PageTable[i].kprot = PROT_READ | PROT_WRITE;
        region0PageTable[i].valid = 1;
        physicalPageAddress += PAGESIZE;
    }
    
    //tell hardware where the page tables are
    WriteRegister(REG_PTR0, (RCS421RegVal) &region0PageTable);
    WriteRegister(REG_PTR1, (RCS421RegVal) &region1PageTable);
    
    // Step 4: Switch on virtual memory
    //PAGE TABLE SANITY CHECK
    int j;
    for (j=0; j < (VMEM_1_LIMIT - VMEM_1_BASE)/ PAGESIZE; j++) {
        TracePrintf(10, "j = %d, pfn = %d, kprot = %d\n", j, region1PageTable[j].pfn, region1PageTable[j].kprot);
    }
    TracePrintf(10, "PT 1 array size = %d\n", VMEM_1_SIZE / PAGESIZE);
    WriteRegister(REG_VM_ENABLE, 1);
    
    // Step 5: ?
    
    Halt(); // TODO: remove this
}

/*
 * We'll need this once we start using Malloc
 * Empty for now for compilation
 */
int
SetKernelBrk(void *addr)
{
    (void) addr;
    TracePrintf(0, "setkernerlbrk\n");
    Halt();
}
