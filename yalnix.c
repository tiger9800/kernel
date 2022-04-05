#include <comp421/yalnix.h>
#include <comp421/hardware.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>

void (*interruptHandlers[TRAP_VECTOR_SIZE])(ExceptionInfo *);
struct pte region1[PAGE_TABLE_LEN];
struct pte idle_region0[PAGE_TABLE_LEN];//idle always exists, so we can define it here

struct PCB {
    pid_t pid;
    SavedContext* ctx;
    void *page_table0;
};

struct PCB idle_PCB = {0, NULL, idle_region0};
//struct PCB init_PCB = {1, NULL, init_regio0};

struct physical_frame {
    int pfn;
    struct physical_frame *next;
};

struct free_pages {
    int count;
    struct physical_frame *head;
};

struct free_pages free_ll = {0, NULL};

struct PCB *activeQ;
struct PCB *readyQ;
struct PCB *blockedQ;

static void idle_process();
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
    int curr_page = startPage;
    while (curr_page < endPage) {
        struct physical_frame* currFrame = (struct physical_frame *)(uintptr_t)(curr_page << PAGESHIFT);
        currFrame->pfn = curr_page;
        currFrame->next = free_ll.head;
        // struct physical_frame *addr = (struct physical_frame *)curr_page << PAGESHIFT);
        // *addr = currFrame;
        free_ll.head = currFrame;
        curr_page++;
        free_ll.count++;
    } 
}



void KernelStart(ExceptionInfo * info, unsigned int pmem_size, void * orig_brk, char ** cmd_args) {


    interruptHandlers[TRAP_KERNEL] = trap_kernel_handler;
    interruptHandlers[TRAP_CLOCK] = trap_clock_handler;
    interruptHandlers[TRAP_ILLEGAL] = trap_illegal_handler;
    interruptHandlers[TRAP_MATH] = trap_math_handler;
    interruptHandlers[TRAP_TTY_TRANSMIT] = trap_tty_transmit_handler;
    interruptHandlers[TRAP_TTY_RECEIVE] = trap_tty_transmit_handler;
    interruptHandlers[TRAP_MEMORY] = trap_memory_handler;


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

    getFreePages(UP_TO_PAGE(orig_brk) >> PAGESHIFT, DOWN_TO_PAGE(pmem_size)>> PAGESHIFT);

    WriteRegister(REG_VECTOR_BASE, (RCS421RegVal)interruptHandlers);

    //put into region 1 page table
    //first put text into the region 1 table


    unsigned int curr_page = VMEM_1_BASE >> PAGESHIFT;
    while(curr_page < (uintptr_t)(&_etext) >> PAGESHIFT) {
        //struct pte new_entry;
        region1[curr_page%PAGE_TABLE_LEN].pfn = curr_page;
        region1[curr_page%PAGE_TABLE_LEN].uprot = PROT_NONE;
        region1[curr_page%PAGE_TABLE_LEN].kprot = PROT_READ|PROT_EXEC;
        region1[curr_page%PAGE_TABLE_LEN].valid = 1;
        //egion1[curr_page%PAGE_TABLE_LEN] = new_entry;
        //printf("Text VPN: %i\n", curr_page);
        curr_page++;
    }

    //data/bss
    //printf("Text end address: %lu\n", (uintptr_t)(&_etext) >> PAGESHIFT);
    
    curr_page = (uintptr_t)(&_etext) >> PAGESHIFT;
    while(curr_page < (UP_TO_PAGE(orig_brk) >> PAGESHIFT)) {
        //struct pte new_entry;
        region1[curr_page%PAGE_TABLE_LEN].pfn = curr_page;
        region1[curr_page%PAGE_TABLE_LEN].uprot = PROT_NONE;
        region1[curr_page%PAGE_TABLE_LEN].kprot = PROT_READ|PROT_WRITE;
        region1[curr_page%PAGE_TABLE_LEN].valid = 1;
        //region1[curr_page%PAGE_TABLE_LEN] = new_entry;
        curr_page++;
    }

    curr_page = (uintptr_t)(orig_brk) >> PAGESHIFT;
    while((curr_page%PAGE_TABLE_LEN) == 0) {
        region1[curr_page%PAGE_TABLE_LEN].valid = 0;
        curr_page++;
    }

    //put kernel stack in region 0.
    curr_page = (uintptr_t)(KERNEL_STACK_BASE) >> PAGESHIFT;
    while(curr_page < KERNEL_STACK_LIMIT >> PAGESHIFT) {
        idle_region0[curr_page].pfn = curr_page;
        idle_region0[curr_page].uprot = PROT_NONE;
        idle_region0[curr_page].kprot = PROT_READ|PROT_WRITE;
        idle_region0[curr_page].valid = 1;
        //printf("VPN: %i\n",curr_page);
        curr_page++;
    }

    curr_page = (uintptr_t)(PMEM_BASE) >> PAGESHIFT;
    while(curr_page < (uintptr_t)(KERNEL_STACK_BASE) >> PAGESHIFT) {
        idle_region0[curr_page].valid = 0;
        curr_page++;
    }
    //MAKE VALID BITS 0 FOR UNUSED



    //set pc to idle
    TracePrintf(0, "I am about to enable virtual memeory\n");
    WriteRegister(REG_PTR0, (RCS421RegVal)idle_region0);

    WriteRegister(REG_PTR1, (RCS421RegVal)region1);
    
    WriteRegister(REG_VM_ENABLE, (RCS421RegVal)1);

    TracePrintf(0, "Enabled virtual memory\n");


    TracePrintf(0, "Here is addr of idle process %i\n", idle_process);
    
    info->pc = idle_process;
    //enable virtual
    //static char myArr[200];
    info->sp = (void *)KERNEL_STACK_LIMIT;
    info->psr = 1;
    TracePrintf(0, "I'm done with the kernel start\n");
}



static void idle_process() {
    while(1) {
        TracePrintf(0, "idle loop");
        Pause();
    }
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
    TracePrintf(0, "trap_memory_handler, addr:%p\n", info->addr);
    TracePrintf(0, "trap_memory_handler, code:%i\n", info->code);
    TracePrintf(0, "trap_memory_handler, pc:%i\n", info->pc);
    TracePrintf(0, "trap_memory_handler, sp:%i\n", info->sp);
    region1[PAGE_TABLE_LEN - 1].valid = 1;
    region1[PAGE_TABLE_LEN - 1].pfn = ((uintptr_t)free_ll.head >> PAGESHIFT);
    region1[PAGE_TABLE_LEN - 1].uprot = PROT_NONE;
    region1[PAGE_TABLE_LEN - 1].kprot = PROT_READ|PROT_WRITE;

    if(info->addr >= (void *)VMEM_1_BASE) {
        //create mapping
        struct physical_frame *head = (struct physical_frame *)((PAGE_TABLE_LEN - 1) << PAGESHIFT);
        TracePrintf(0, "head value: %p\n", *head);
        TracePrintf(0, "head next addr: %p\n", head->next);
        free_ll.head = head->next;
        free_ll.count--;
        region1[(uintptr_t)info->addr>>PAGESHIFT].valid = 1;
        region1[(uintptr_t)info->addr>>PAGESHIFT].pfn = head->pfn;
        region1[(uintptr_t)info->addr>>PAGESHIFT].uprot = PROT_NONE;
        region1[(uintptr_t)info->addr>>PAGESHIFT].kprot = PROT_READ|PROT_WRITE;
    } else {
        struct physical_frame *head = free_ll.head;
        TracePrintf(0, "head addr: %p\n", head);
        free_ll.head = head->next;
        free_ll.count--;
        idle_region0[(uintptr_t)info->addr >> PAGESHIFT].valid = 1;
        idle_region0[(uintptr_t)info->addr >> PAGESHIFT].pfn = head->pfn;
        idle_region0[(uintptr_t)info->addr >> PAGESHIFT].uprot = PROT_READ|PROT_WRITE;
        idle_region0[(uintptr_t)info->addr >> PAGESHIFT].kprot = PROT_READ|PROT_WRITE;
    }
    //Halt();
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

//Called when malloc is caled by the kernel.
int SetKernelBrk(void * addr) {
    (void)addr;  
    return 0;
}