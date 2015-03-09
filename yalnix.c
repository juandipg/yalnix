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
        TracePrintf(1, "Adding address %p to list\n", page);
        page->next = firstFreePage;
        firstFreePage = page;
    }
    
    TracePrintf(1, "Got through region 0\n");
    TracePrintf(1, "Kernel stack base is at %p\n", (void *) KERNEL_STACK_BASE);
    TracePrintf(1, "We have %d bytes of physical memory\n", pmem_size);
    TracePrintf(1, "Size of void * is: %d\n", sizeof(void *));
    TracePrintf(1, "Size of FreePage * is: %d\n", sizeof(FreePage *));
    TracePrintf(1, "memory ends at %p\n", ((FreePage *) PMEM_BASE) + pmem_size / sizeof(void *));
    
    // second one, from orig_brk to pmem_size
    // pmem_size is in bytes, which conveniently is the smallest
    // addressable unit of memory
    for (page = (FreePage *) orig_brk; 
            page < ((FreePage *) PMEM_BASE) + pmem_size / sizeof(void *); 
            page += PAGESIZE) 
    {
        TracePrintf(1, "Adding address %p to list\n", page);
        page->next = firstFreePage;
        firstFreePage = page;
    }
    
    // Step 3: Build page tables for Region 1 and Region 0
    
    // Step 4: Switch on virtual memory
    
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
