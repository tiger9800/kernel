#include <comp421/yalnix.h>
#include <comp421/hardware.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include "kernel.h"

void (*interruptHandlers[TRAP_VECTOR_SIZE])(ExceptionInfo *);
struct pte region1[PAGE_TABLE_LEN];
struct pte idle_region0[PAGE_TABLE_LEN];//idle always exists, so we can define it here



pcb idle_PCB;
//struct PCB init_PCB = {1, NULL, init_regio0};

//current kernel break address
void *currentBrk;
bool vm_enabled = false;

struct free_pages free_ll = {0, NULL};

struct PCB *activeQ;
struct PCB *readyQ;
struct PCB *blockedQ;

int currPID = 1;


//static void idle_process();
void trap_kernel_handler(ExceptionInfo *info);
void trap_clock_handler(ExceptionInfo *info);
void trap_illegal_handler(ExceptionInfo *info);
void trap_memory_handler(ExceptionInfo *info);
void trap_math_handler(ExceptionInfo *info);
void trap_tty_transmit_handler(ExceptionInfo *info);
void trap_tty_receive_handler(ExceptionInfo *info);
static uintptr_t reservePage(int pfn);
static void unReservePage();
void addPage(int pfn);
void addInvalidPages();

static void initFreePages(int startPage, int endPage) {
    //first page that we can accessed
    // int pageNum = 0;
    int curr_page = startPage;
    while (curr_page < endPage) {
        struct physical_frame* currFrame = (struct physical_frame *)(uintptr_t)(curr_page << PAGESHIFT);
        //TracePrintf(0, "initFreePages: currFrame %p\n", currFrame);
        
        currFrame->next = free_ll.head;
        //TracePrintf(0, "initFreePages: currFrame->next %p\n", currFrame->next);
        //TracePrintf(0, "pfn:  %i\n", (uintptr_t)currFrame >> PAGESHIFT);
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


    (void)cmd_args;

    int i;
    for (i = 7; i < TRAP_VECTOR_SIZE; i++) {
        interruptHandlers[i] = NULL;
    }

    currentBrk = orig_brk;

    //make struct PCB initialization for idle process + free page ll
    //divide up the space into physical pages
    //free pages are from VMEM_BASE to STACK_BASE anf from orig_brk to pmem_size

    
    initFreePages(MEM_INVALID_PAGES, DOWN_TO_PAGE(KERNEL_STACK_BASE) >> PAGESHIFT);

    initFreePages(UP_TO_PAGE(orig_brk) >> PAGESHIFT, DOWN_TO_PAGE(pmem_size)>> PAGESHIFT);


    struct physical_frame *currFrame = free_ll.head;
    for (i = 0; i< 10; i++) {
        currFrame = currFrame->next;
    }
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
    //TracePrintf(0, "I am about to enable virtual memeory\n");
    WriteRegister(REG_PTR0, (RCS421RegVal)idle_region0);

    WriteRegister(REG_PTR1, (RCS421RegVal)region1);
    
    WriteRegister(REG_VM_ENABLE, (RCS421RegVal)1);
    vm_enabled = true;
    
    addInvalidPages();

    //TracePrintf(0, "Enabled virtual memory\n");


    //TracePrintf(0, "Here is addr of idle process %i\n", idle_process);


    char* arg[1] = {NULL};

    idle_PCB.pid = 0;
    idle_PCB.page_table0 = idle_region0;
    idle_PCB.delay_clock = 0;
    idle_PCB.parent =  NULL;
    idle_PCB.next = NULL;
    LoadProgram("idle", arg, info, idle_region0, free_ll);

    pcb* initPCB = malloc(sizeof(pcb));

    struct pte *initPt0 = getNewPageTable();

    initPCB->pid = currPID++;
    initPCB->page_table0 = initPt0;
    initPCB->parent = NULL;
    initPCB->next = NULL;
    initPCB->delay_clock = 0;
    initPCB->ctx = NULL;
    
    

    ContextSwitch(cloneContext, &initPCB->ctx, (void *)&idle_PCB, (void *)initPCB);


    ContextSwitch(switchIdleToInit, &idle_PCB.ctx, (void *)&idle_PCB, (void *)initPCB);
    LoadProgram("init", cmd_args, info, initPt0, free_ll);
    //create PCB for init using malloc
    //create page table for init region0
    //contextswitch to init
    //load init
}

SavedContext  *cloneContext(SavedContext * ctxp, void * p1, void * p2) {
    return &((pcb*)p2)->ctx;
}


SavedContex *switchIdleToInit(SavedContext * ctxp, void * p1, void * p2) {
    
    int curr_page;
    for(curr_page = KERNEL_STACK_BASE >> PAGESHIFT; curr_page < (KERNEL_STACK_LIMIT >> PAGESHIFT); curr_page++) {
        ((pcb*)p2)->page_table0[curr_page].valid = 1;
        ((pcb*)p2)->page_table0[curr_page].kprot = PROT_READ | PROT_WRITE;
        ((pcb*)p2)->page_table0[curr_page].uprot = PROT_NONE;
        unsigned int pfn = getFreePage();
        ((pcb*)p2)->page_table0[curr_page].pfn  = pfn;
        uintptr_t addrToCopy = reservePage(pfn);
        memcpy(addrToCopy, (curr_page << PAGESHIFT), PAGESIZE);
        unReservePage();
    }


    activeQ =  (pcb*) p2;
    WriteRegister(REG_PTR0, (RCS421RegVal) ((pcb*)p2)->page_table0));
    return &((pcb*)p2)->ctx;
    
}
// MySwitchFunc(SavedContext * ctxp, void * p1, void * p2)


// static void idle_process() {
//     while(1) {
//         TracePrintf(0, "idle loop");
//         Pause();
//     }
// }

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
    // region1[PAGE_TABLE_LEN - 1].valid = 1;
    // region1[PAGE_TABLE_LEN - 1].pfn = ((uintptr_t)free_ll.head >> PAGESHIFT);
    // region1[PAGE_TABLE_LEN - 1].uprot = PROT_NONE;
    // region1[PAGE_TABLE_LEN - 1].kprot = PROT_READ|PROT_WRITE;

    // if(info->addr >= (void *)VMEM_1_BASE) {
    //     //create mapping
    //     struct physical_frame *head = (struct physical_frame *)((PAGE_TABLE_LEN - 1) << PAGESHIFT);
    //     TracePrintf(0, "head value: %p\n", *head);
    //     TracePrintf(0, "head next addr: %p\n", head->next);
    //     free_ll.head = head->next;
    //     free_ll.count--;
    //     region1[(uintptr_t)info->addr>>PAGESHIFT].valid = 1;
    //     region1[(uintptr_t)info->addr>>PAGESHIFT].pfn = head->pfn;
    //     region1[(uintptr_t)info->addr>>PAGESHIFT].uprot = PROT_NONE;
    //     region1[(uintptr_t)info->addr>>PAGESHIFT].kprot = PROT_READ|PROT_WRITE;
    // } else {
    //     struct physical_frame *head = free_ll.head;
    //     TracePrintf(0, "head addr: %p\n", head);
    //     free_ll.head = head->next;
    //     free_ll.count--;
    //     idle_region0[(uintptr_t)info->addr >> PAGESHIFT].valid = 1;
    //     idle_region0[(uintptr_t)info->addr >> PAGESHIFT].pfn = head->pfn;
    //     idle_region0[(uintptr_t)info->addr >> PAGESHIFT].uprot = PROT_READ|PROT_WRITE;
    //     idle_region0[(uintptr_t)info->addr >> PAGESHIFT].kprot = PROT_READ|PROT_WRITE;
    //}
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
    if(!vm_enabled) {
        if(addr > (void *)VMEM_1_LIMIT) {
            return -1;
        }
        currentBrk = addr;
    } else {
        int count = ((uintptr_t)(addr - UP_TO_PAGE(currentBrk)) >> PAGESHIFT) + 1;
        if (count > free_ll.count) {
           return -1;
        } 
        int i;
        int curr_page = (UP_TO_PAGE(currentBrk) >> PAGESHIFT) - VMEM_1_BASE;
        for (i = 0; i < count; i++) {
            region1[curr_page].valid = 1;
            region1[curr_page].kprot = PROT_READ | PROT_WRITE;
            region1[curr_page].uprot = PROT_NONE;
            region1[curr_page++].pfn  = getFreePage();
        }
    }
    return 0;
}

void freePage(struct pte* newPte) {
    addPage(newPte->pfn);  
    newPte->valid = 0;
}

void addInvalidPages() {
    int pfn;
    for (pfn = 0; pfn < MEM_INVALID_PAGES; pfn++) {
        addPage(pfn);
    }
}

void addPage(int pfn) {
    struct physical_frame* currFrame = (struct physical_frame *)reservePage(pfn);
    currFrame->next = free_ll.head;
    free_ll.head = (struct physical_frame *)(uintptr_t)(pfn << PAGESHIFT);
    free_ll.count++;
    unReservePage();  
}

unsigned int getFreePage() {
    unsigned int resultPfn = ((uintptr_t)free_ll.head >> PAGESHIFT);
    struct physical_frame* currFrame = (struct physical_frame *)reservePage(resultPfn);

    free_ll.head = currFrame->next; 
    free_ll.count--;
    unReservePage();
    return resultPfn;
}

static uintptr_t reservePage(int pfn) {
    region1[PAGE_TABLE_LEN - 1].valid = 1;
    region1[PAGE_TABLE_LEN - 1].pfn = pfn;
    region1[PAGE_TABLE_LEN - 1].uprot = PROT_NONE;
    region1[PAGE_TABLE_LEN - 1].kprot = PROT_READ|PROT_WRITE;
    return (uintptr_t)(VMEM_1_LIMIT - PAGESIZE);
}

static void unReservePage() {
    WriteRegister(REG_TLB_FLUSH, (RCS421RegVal)(VMEM_1_LIMIT - PAGESIZE));
    region1[PAGE_TABLE_LEN - 1].valid = 0;
}

/*
Return virtual address of a new page table
*/
static struct pte* getNewPageTable() {
    unsigned int vpn = PAGE_TABLE_LEN - 2;
    while(region1[vpn].valid) {
        vpn--;
    }
    //since we left while loop, then the pte is not valid and we can create a new mapping for a region 0 page table
    region1[vpn].valid = 1;
    region1[vpn].pfn = getFreePage();
    region1[vpn].kprot = PROT_READ|PROT_WRITE;
    region1[vpn].uprot = PROT_NONE; 
    return (struct pte *)(VMEM_1_BASE + (vpn << PAGESHIFT));
}