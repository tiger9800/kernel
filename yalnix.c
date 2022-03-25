#include <comp421/yalnix.h>
#include <comp421/hardware.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>

void (*interruptHandlers[TRAP_VECTOR_SIZE])(ExceptionInfo *);
struct pte region0[PAGE_TABLE_LEN];
struct pte region1[PAGE_TABLE_LEN];

struct PCB {
    pid_t pid;
    // void * pc;
    // void * sp;
    void * region0_addr;
}  

struct physical_frame {
    int pfn;
    struct physical_frame *next;
};
struct physical_frame *free_pages_head = NULL;

struc


void trap_kernel_handler(ExceptionInfo *info);
void trap_clock_handler(ExceptionInfo *info);
void trap_illegal_handler(ExceptionInfo *info);
void trap_memory_handler(ExceptionInfo *info);
void trap_math_handler(ExceptionInfo *info);
void trap_tty_transmit_handler(ExceptionInfo *info);
void trap_tty_receive_handler(ExceptionInfo *info);

static void getFreePages(int startPage, int endPage) {
    //first page that we can accessed
    // int pageNum = 0;
    struct physical_frame* prevAddr = free_pages_head;
    int curr_page = startPage;
    while (curr_page < endPage) {
        struct physical_frame currFrame;
        currFrame.pfn = curr_page;
        currFrame.next = prevAddr;
        struct physical_frame *addr = (struct physical_frame *)(uintptr_t)(curr_page << PAGESHIFT);
        *addr = currFrame;
        prevAddr = (struct physical_frame *)(uintptr_t)(curr_page << PAGESHIFT);
        curr_page++;
    } 
    free_pages_head = prevAddr;
}

void KernelStart(ExceptionInfo * info, unsigned int pmem_size, void * orig_brk, char ** cmd_args) {


    interruptHandlers[TRAP_KERNEL] = trap_kernel_handler;
    interruptHandlers[TRAP_CLOCK] = trap_clock_handler;
    interruptHandlers[TRAP_ILLEGAL] = trap_illegal_handler;
    interruptHandlers[TRAP_MATH] = trap_math_handler;
    interruptHandlers[TRAP_TTY_TRANSMIT] = trap_tty_transmit_handler;
    interruptHandlers[TRAP_TTY_RECEIVE] = trap_tty_transmit_handler;


    (void)info;
    (void)pmem_size;
    (void)orig_brk;
    (void)cmd_args;

    int i;
    for (i = 7; i < TRAP_VECTOR_SIZE; i++) {
        interruptHandlers[i] = NULL;
    }

    //make struct PCB initialization for idle process + free page ll
    //divide up the space into physical pages
    //free pages are from VMEM_BASE to STACK_BASE anf from orig_brk to pmem_size

    
    getFreePages(MEM_INVALID_PAGES, DOWN_TO_PAGE(KERNEL_STACK_BASE) >> PAGESHIFT);

    getFreePages(UP_TO_PAGE(orig_brk) >> PAGESHIFT, DOWN_TO_PAGE(orig_brk) >> PAGESHIFT);

    WriteRegister(REG_VECTOR_BASE, (RCS421RegVal)interruptHandlers);

    //put into region 1 page table

    //first put text into the region 1 table
    int curr_page = VMEM_1_BASE >> PAGESHIFT;
    while(curr_page < (intptr_t)(&_etext) >> PAGESHIFT) {
        struct pte new_entry;
        new_entry.pfn = curr_page;
        new_entry.uprot = PROT_NONE;
        new_entry.kprot = PROT_READ|PROT_EXEC;
        new_entry.valid = 1;
        region1[curr_page - 512] = new_entry;
        printf("Text VPN: %i\n", curr_page);
        curr_page++;
    }

    //data/bss
    printf("Text end address: %lu\n", (uintptr_t)(&_etext) >> PAGESHIFT);
    
    curr_page = (uintptr_t)(&_etext) >> PAGESHIFT;
    while(curr_page < (UP_TO_PAGE(orig_brk) >> PAGESHIFT)) {
        struct pte new_entry;
        new_entry.pfn = curr_page;
        new_entry.uprot = PROT_NONE;
        new_entry.kprot = PROT_READ|PROT_WRITE;
        new_entry.valid = 1;
        region1[curr_page-512] = new_entry;
        curr_page++;
    }

    //put kernel stack in region 0.
    curr_page = (uintptr_t)(KERNEL_STACK_BASE) >> PAGESHIFT;
    while(curr_page < KERNEL_STACK_LIMIT >> PAGESHIFT) {
        struct pte new_entry;
        new_entry.pfn = curr_page;
        new_entry.uprot = PROT_NONE;
        new_entry.kprot = PROT_READ|PROT_WRITE;
        new_entry.valid = 1;
        region0[curr_page] = new_entry;
        printf("VPN: %i\n",curr_page);
        curr_page++;
    }
    

    //set pc to idle

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
    return 0;
}