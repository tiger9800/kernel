#include <comp421/yalnix.h>
#include <comp421/hardware.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <stdlib.h>
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

pcb *activeQ;
pcb *readyQ;
pcb *blockedQ;

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
static struct pte* getNewPageTable();
SavedContext  *cloneContext(SavedContext * ctxp, void * p1, void * p2);
SavedContext *switchIdleToInit(SavedContext * ctxp, void * p1, void * p2);

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

    WriteRegister(REG_VECTOR_BASE, (RCS421RegVal)interruptHandlers);

    // Map pages for the kernel text (in region 1).
    // Is &etext on the boundary?
    unsigned int curr_page = (VMEM_1_BASE >> PAGESHIFT);
    while(curr_page < (uintptr_t)(&_etext) >> PAGESHIFT) {
        int ind = curr_page % PAGE_TABLE_LEN;
        region1[ind].pfn = curr_page;
        region1[ind].uprot = PROT_NONE;
        region1[ind].kprot = PROT_READ|PROT_EXEC;
        region1[ind].valid = 1;
        curr_page++;
    }
    
    // Map pages for the kernel data, bss, and heap (in region 1).
    // Is orig_brk on the boundary?
    while(curr_page < (UP_TO_PAGE(orig_brk) >> PAGESHIFT)) {
        int ind = curr_page % PAGE_TABLE_LEN;
        region1[ind].pfn = curr_page;
        region1[ind].uprot = PROT_NONE;
        region1[ind].kprot = PROT_READ|PROT_WRITE;
        region1[ind].valid = 1;
        //region1[curr_page%PAGE_TABLE_LEN] = new_entry;
        curr_page++;
    }

    // Make all remaining pages in region 1 invalid.
    while((curr_page%PAGE_TABLE_LEN) != 0) {
        region1[curr_page%PAGE_TABLE_LEN].valid = 0;
        curr_page++;
    }

    // Make all pages below kernel stack invalid (in region 0).
    curr_page = (uintptr_t)(PMEM_BASE) >> PAGESHIFT;
    while(curr_page < (uintptr_t)(KERNEL_STACK_BASE) >> PAGESHIFT) {
        idle_region0[curr_page].valid = 0;
        curr_page++;
    }

    // Map pages for the kernel stack (in region 0).
    while(curr_page < KERNEL_STACK_LIMIT >> PAGESHIFT) {
        idle_region0[curr_page].pfn = curr_page;
        idle_region0[curr_page].uprot = PROT_NONE;
        idle_region0[curr_page].kprot = PROT_READ|PROT_WRITE;
        idle_region0[curr_page].valid = 1;
        curr_page++;
    }



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

    TracePrintf(0, "I'm gonna malloc\n");
    pcb* initPCB = malloc(sizeof(pcb));

    TracePrintf(0, "I'm gonna getNewPageTable\n");
    struct pte *initPt0 = getNewPageTable();

    initPCB->pid = currPID++;
    initPCB->page_table0 = initPt0;
    initPCB->parent = NULL;
    initPCB->next = NULL;
    initPCB->delay_clock = 0;
    
    
    TracePrintf(0, "cloneContext\n");
    ContextSwitch(cloneContext, &initPCB->ctx, (void *)&idle_PCB, (void *)initPCB);
    TracePrintf(0, "after cloneContext PFN for init vpn 508: %u\n", idle_PCB.page_table0[508].pfn);
    TracePrintf(0, "after cloneContext PFN for idle vpn 508: %u\n", (initPCB)->page_table0[508].pfn);
    TracePrintf(0, "valid for init vpn 508: %u\n", idle_PCB.page_table0[508].valid);
    TracePrintf(0, "valid for idle vpn 508: %u\n", (initPCB)->page_table0[508].valid);

    TracePrintf(0, "idleToInit\n");
    ContextSwitch(switchIdleToInit, &idle_PCB.ctx, (void *)&idle_PCB, (void *)initPCB);
    TracePrintf(0, "after idleToInit PFN for init vpn 508: %u\n", idle_PCB.page_table0[508].pfn);
    TracePrintf(0, "after idleToInit PFN for idle vpn 508: %u\n", (initPCB)->page_table0[508].pfn);

    TracePrintf(0, "valid for init vpn 508: %u\n", idle_PCB.page_table0[508].valid);
    TracePrintf(0, "valid for idle vpn 508: %u\n", (initPCB)->page_table0[508].valid);

    LoadProgram("init", cmd_args, info, initPt0, free_ll);
    // create PCB for init using malloc
    // create page table for init region0
    // contextswitch to init
    // load init
}

SavedContext  *cloneContext(SavedContext * ctxp, void * p1, void * p2) {
    TracePrintf(0, "flush 1st time\n");
    WriteRegister(REG_TLB_FLUSH, TLB_FLUSH_0);
    TracePrintf(0, "flush done\n");
    (void)ctxp;  
    (void)p1;
    int curr_page;
    for(curr_page = KERNEL_STACK_BASE >> PAGESHIFT; curr_page < (KERNEL_STACK_LIMIT >> PAGESHIFT); curr_page++) {
        TracePrintf(0, "216 Page: %i\n", curr_page);
        ((pcb*)p2)->page_table0[curr_page].valid = 1;
        ((pcb*)p2)->page_table0[curr_page].kprot = PROT_READ | PROT_WRITE;
        ((pcb*)p2)->page_table0[curr_page].uprot = PROT_NONE;
        unsigned int pfn = getFreePage();
        if ((int)pfn == -1) {
            TracePrintf(0, "No enough free physical memory to complete operation\n");
            // What do we want to return here? SavedContext of the first process?
            //We do not change the page table or flush TLB
            return &((pcb*)p1)->ctx;
        }
        //make ptes up to kernel stack base equal to 0

        TracePrintf(0, "New pfn: %u\n", pfn);
        ((pcb*)p2)->page_table0[curr_page].pfn = pfn;
        uintptr_t addrToCopy = reservePage(pfn);
        memcpy((void *)addrToCopy, (void*)(uintptr_t)(curr_page << PAGESHIFT), PAGESIZE);
        unReservePage();
    }
    //make all other ptes invalid


    TracePrintf(0, "PFN for init vpn 508: %u\n", ((pcb*)p2)->page_table0[508].pfn);
    TracePrintf(0, "PFN for idle vpn 508: %u\n", ((pcb*)p1)->page_table0[508].pfn);
    TracePrintf(0, "valid for init vpn 508: %u\n", ((pcb*)p2)->page_table0[508].valid);
    TracePrintf(0, "valid for idle vpn 508: %u\n", ((pcb*)p1)->page_table0[508].valid);
    return &((pcb*)p2)->ctx;
}


SavedContext *switchIdleToInit(SavedContext * ctxp, void * p1, void * p2) {
    (void)ctxp;
    (void)p1;
    // TracePrintf(0, "flush 2nd time\n");
    // WriteRegister(REG_TLB_FLUSH, TLB_FLUSH_0);
    // TracePrintf(0, "flush done\n");
    activeQ =  (pcb*) p2;
    TracePrintf(0, "switchIdleToInit PFN for init vpn 508: %u\n", ((pcb*)p2)->page_table0[508].pfn);
    TracePrintf(0, "switchIdleToInit PFN for idle vpn 508: %u\n", ((pcb*)p1)->page_table0[508].pfn);
    TracePrintf(0, "valid for init vpn 508: %u\n", ((pcb*)p2)->page_table0[508].valid);
    TracePrintf(0, "valid for idle vpn 508: %u\n", ((pcb*)p1)->page_table0[508].valid);

    unsigned int pageToVpn = ((uintptr_t)(((pcb*)p2)->page_table0) >> PAGESHIFT) % PAGE_TABLE_LEN;
    void* physical_addr = (void *) ((uintptr_t)region1[pageToVpn].pfn << PAGESHIFT);
    
    
    WriteRegister(REG_PTR0, (RCS421RegVal) (physical_addr));
    WriteRegister(REG_TLB_FLUSH, TLB_FLUSH_0);

    TracePrintf(0, "switchIdleToInit PFN for init vpn 508: %u\n", ((pcb*)p2)->page_table0[508].pfn);
    TracePrintf(0, "switchIdleToInit PFN for idle vpn 508: %u\n", ((pcb*)p1)->page_table0[508].pfn);
    TracePrintf(0, "valid for init vpn 508: %u\n", ((pcb*)p2)->page_table0[508].valid);
    TracePrintf(0, "valid for idle vpn 508: %u\n", ((pcb*)p1)->page_table0[508].valid);
    
    TracePrintf(0, "switchIdleToInit PFN for init vpn 508: %u\n", ((pcb*)p2)->page_table0[508].pfn);
    TracePrintf(0, "switchIdleToInit PFN for idle vpn 508: %u\n", ((pcb*)p1)->page_table0[508].pfn);

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
    TracePrintf(0, "I'm inside setKernelBrk\n");
    if(!vm_enabled) {
        if(addr > (void *)VMEM_1_LIMIT) {
            return -1;
        }
        currentBrk = addr;
    } else {
        TracePrintf(0, "VM enabled\n");
        int count = ((uintptr_t)(addr - UP_TO_PAGE(currentBrk)) >> PAGESHIFT) + 1;
        if (count > free_ll.count) {
           return -1;
        } 
        int i;
        if(count > free_ll.count) {
            return -1;
        }
        unsigned int curr_page = (UP_TO_PAGE(currentBrk) - VMEM_1_BASE) >> PAGESHIFT;
        TracePrintf(0, "I'm going to allocate new pages\n");
        for (i = 0; i < count; i++) {
            TracePrintf(0, "I'm going to make bit valid of page %d\n", curr_page);
            region1[curr_page].valid = 1;
            region1[curr_page].kprot = PROT_READ | PROT_WRITE;
            region1[curr_page].uprot = PROT_NONE;
            unsigned int pfn = getFreePage();
            if ((int)pfn == -1) {
                TracePrintf(0, "No enough free physical memory to complete operation\n");
                return -1;
            }
            region1[curr_page++].pfn = pfn;
        }
        currentBrk = addr;
    }
    return 0;
}

void freePage(struct pte* newPte) {
    addPage(newPte->pfn);  
    newPte->valid = 0;
}

void addInvalidPages() {
    int pfn;
    for (pfn = PMEM_BASE; pfn < MEM_INVALID_PAGES; pfn++) {
        addPage(pfn);
    }
}

void addPage(int pfn) {
    TracePrintf(0, "I'm in addPage and pfn is %i\n", pfn);
    struct physical_frame* currFrame = (struct physical_frame *)reservePage(pfn);
    currFrame->next = free_ll.head;
    free_ll.head = (struct physical_frame *)(uintptr_t)(pfn << PAGESHIFT);
    TracePrintf(0, "free_ll.head is %p\n", free_ll.head);
    free_ll.count++;
    unReservePage();  
}

unsigned int getFreePage() {
    if (free_ll.count == 0) {
        return -1;
    }
    TracePrintf(0, "I'm in get free page and head is %p\n", free_ll.head);
    unsigned int resultPfn = ((uintptr_t)free_ll.head >> PAGESHIFT);
    struct physical_frame* currFrame = (struct physical_frame *)reservePage(resultPfn);
    TracePrintf(0, "I took a page and I'm going to deref it\n");
    free_ll.head = currFrame->next; 
    free_ll.count--;
    unReservePage();
    TracePrintf(0, "Took a new pfn from free list %i\n", resultPfn);
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
static struct pte*getNewPageTable() {
    unsigned int vpn = PAGE_TABLE_LEN - 2;
    while(region1[vpn].valid) {
        vpn--;
    }
    //since we left while loop, then the pte is not valid and we can create a new mapping for a region 0 page table
    region1[vpn].valid = 1;
    unsigned int pfn = getFreePage();
    if ((int)pfn == -1) {
        TracePrintf(0, "No enough free physical memory to complete operation\n");
        return (struct pte *)-1;
    }
    region1[vpn].pfn = pfn;
    region1[vpn].kprot = PROT_READ|PROT_WRITE;
    region1[vpn].uprot = PROT_NONE; 

    int curr_page;
    for(curr_page = 0; curr_page < (KERNEL_STACK_BASE >> PAGESHIFT); curr_page++) {
        ((struct pte *)(uintptr_t)(VMEM_1_BASE + (vpn << PAGESHIFT)))[curr_page].valid = 0;
    } 
    //set every valid bit in the page table to 0.
    //memset((void *)(uintptr_t)(VMEM_1_BASE + (vpn << PAGESHIFT)), 0, PAGE_TABLE_LEN * sizeof(struct pte));
    return (struct pte *)(uintptr_t)(VMEM_1_BASE + (vpn << PAGESHIFT));
}