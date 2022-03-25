#include <comp421/yalnix.h>
#include <comp421/hardware.h>

void (*interruptHandlers[TRAP_VECTOR_SIZE])(ExceptionInfo *);
struct pte region0[PAGE_TABLE_LEN];
struct pte region1[PAGE_TABLE_LEN];

struct physical_frame {
    int pfn;
    physicial_frame *next;
}
physical_frame *free_pages_head;


void trap_kernel_handler(ExceptionInfo *info);
void trap_clock_handler(ExceptionInfo *info);
void trap_illegal_handler(ExceptionInfo *info);
void trap_memory_handler(ExceptionInfo *info);
void trap_math_handler(ExceptionInfo *info);
void trap_tty_transmit_handler(ExceptionInfo *info);
void trap_tty_receive_handler(ExceptionInfo *info);

void KernelStart(ExceptionInfo * info, unsigned int pmem_size, void * orig_brk, char ** cmd_args) {
    interruptHandlers[TRAP_KERNEL] = trap_kernel_handler;
    interruptHandlers[TRAP_CLOCK] = trap_clock_handler;
    interruptHandlers[TRAP_ILLEGAL] = trap_illegal_handler;
    interruptHandlers[TRAP_MATH] = trap_math_handler;
    interruptHandlers[TRAP_TTY_TRANSMIT] = trap_tty_transmit_handler;
    interruptHandlers[TRAP_TTY_RECEIVE] = trap_tty_transmit_handler;


    for (int i = 7; i < TRAP_VECTOR_SIZE; i++) {
        interruptHandlers[i] = NULL;
    }

    //make struct PCB initialization for idle process + free page ll

    WriteRegister(REG_VECTOR_BASE, (RCS421RegVal)interruptHandlers);

    WriteRegister(REG_PTR0, (RCS421RegVal)region0);

    WriteRegister(REG_PTR1, (RCS421RegVal)region1);

    WriteRegister(REG_VM_ENABLE, (RCS421RegVal)1);

    
    
    //enable virtual

}


//handler
void trap_kernel_handler(ExceptionInfo *info) {
    (void)info;
    TracePrintf(0, "trap_clock_handler");
    Halt();
}

void trap_clock_handler(ExceptionInfo *info) {
    (void)info;
    TracePrintf(0, "trap_clock_handler");
    Halt();
}


void trap_illegal_handler(ExceptionInfo *info) {
    (void)info;
    TracePrintf(0, "trap_illegal_handler");
    Halt();
}

void trap_memory_handler(ExceptionInfo *info) {
    (void)info;
    TracePrintf(0, "trap_memory_handler");
    Halt();
}

void trap_math_handler(ExceptionInfo *info) {
    (void)info;
    TracePrintf(0, "trap_math_handler");
    Halt();
}

void trap_tty_transmit_handler(ExceptionInfo *info) {
    (void)info;
    TracePrintf(0, "trap_tty_transmit_handler");
    Halt();
}

void trap_tty_receive_handler(ExceptionInfo *info) {
    (void)info;
    TracePrintf(0, "trap_tty_receive_handler");
    Halt();
}   

int SetKernelBrk(void * var) {
    (void)var;
}