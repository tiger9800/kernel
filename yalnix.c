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
// Interrupt vector.
void (*interruptHandlers[TRAP_VECTOR_SIZE])(ExceptionInfo *);
// Page table for region 1 (shared between all the processes).
struct pte region1[PAGE_TABLE_LEN];
// Page table for region 0 of the idle process.
struct pte idle_region0[PAGE_TABLE_LEN]; 
// PCB of the idle process.
pcb idle_PCB;

// Current kernel break address.
void *currentBrk;
// Boolean saying whether virtual memory has been enabled.
bool vm_enabled = false;
// Initialized linked list of free physical pages.
struct free_pages free_ll = {0, NULL};
// Pointer to PCB of currently active processs.
pcb *active;
// FIFO queues of ready and blocked processes.
queue readyQ = {NULL, NULL, 0};
queue blockedQ = {NULL, NULL, 0};
// PID of the next process to be created.
int currPID = 1;

// Active process during the previous TRAP_CLOCK.
pcb *prevActive = NULL;

// Interrupt handler routines.
void trap_kernel_handler(ExceptionInfo *info);
void trap_clock_handler(ExceptionInfo *info);
void trap_illegal_handler(ExceptionInfo *info);
void trap_memory_handler(ExceptionInfo *info);
void trap_math_handler(ExceptionInfo *info);
void trap_tty_transmit_handler(ExceptionInfo *info);
void trap_tty_receive_handler(ExceptionInfo *info);
// Routines for pages manipulation.
static void initPageTables();
static void initFreePages(int start_addr, int end_addr);
static uintptr_t reservePage(int pfn);
static void unReservePage();
void addPage(int pfn);
void addInvalidPages();
static struct pte *getNewPageTable();
// MySwitch functions.
SavedContext  *cloneContext(SavedContext *ctxp, void *p1, void *p2);
SavedContext *switchProcesses(SavedContext *ctxp, void *p1, void *p2);

// Functions for working with queues. 
static void enqueue(pcb* proc, queue* queue);
static pcb *dequeue(queue* queue);
static void delayProcess(pcb* proc, int delay);
static void runNextProcess();
static void printQueue(queue q);


// Kernel call helpers.
static int KernelGetPid();
static int KernelDelay(int clock_ticks);



void KernelStart(ExceptionInfo * info, unsigned int pmem_size, void * orig_brk, char ** cmd_args) {
    (void)cmd_args;

    // Create an interrupt vector.
    interruptHandlers[TRAP_KERNEL] = trap_kernel_handler;
    interruptHandlers[TRAP_CLOCK] = trap_clock_handler;
    interruptHandlers[TRAP_ILLEGAL] = trap_illegal_handler;
    interruptHandlers[TRAP_MEMORY] = trap_memory_handler;
    interruptHandlers[TRAP_MATH] = trap_math_handler;
    interruptHandlers[TRAP_TTY_TRANSMIT] = trap_tty_transmit_handler;
    interruptHandlers[TRAP_TTY_RECEIVE] = trap_tty_transmit_handler;
    // Init all other handlers to NULL.
    int i;
    for (i = 7; i < TRAP_VECTOR_SIZE; i++) {
        interruptHandlers[i] = NULL;
    }
    // Set a current break.
    currentBrk = orig_brk;

    // Put all pages from region 0 to a free list (except for invalid pages and kernel stack).
    initFreePages(MEM_INVALID_SIZE, DOWN_TO_PAGE(KERNEL_STACK_BASE));
    // Put all pages above kernel heap to a free list.
    initFreePages(UP_TO_PAGE(orig_brk), DOWN_TO_PAGE(pmem_size));

    // Let hardware know where interrupt vector is located. 
    WriteRegister(REG_VECTOR_BASE, (RCS421RegVal)interruptHandlers);

    // Create an initial mapping between physical and virtual addresses.
    initPageTables();

    // Let hardware know where page tables are located.
    WriteRegister(REG_PTR0, (RCS421RegVal)idle_region0);
    WriteRegister(REG_PTR1, (RCS421RegVal)region1);
    
    // Enable virtual memory.
    WriteRegister(REG_VM_ENABLE, (RCS421RegVal)1);
    vm_enabled = true;

    // Add "invalid" pages to a list of free pages.
    addInvalidPages();

    // Create an array of arguments for loading idle.c
    char* arg[1] = {NULL};

    // Initailize some PCB values for the idle process.
    idle_PCB.pid = 0;
    idle_PCB.page_table0 = idle_region0;
    active = &idle_PCB;
    // Load an idle process.
    LoadProgram("idle", arg, info, idle_region0, free_ll, &idle_PCB);

    // Allocate memory for PCB of init process (first process to run).
    pcb *initPCB = malloc(sizeof(pcb));

    // Create a page table for this process.
    struct pte *initPt0 = getNewPageTable();
    // Init some PCB values for this process.
    initPCB->pid = currPID++;
    initPCB->page_table0 = initPt0;
    TracePrintf(0, "Clone context from idle to init in KernelStart (active pid=%i)\n", active->pid);
    // Save current context to the saved context of process to be run after KernelStart()
    ContextSwitch(cloneContext, &initPCB->ctx, (void *)&idle_PCB, (void *)initPCB);
    // Switch from idle process to init.
    if (active->pid == idle_PCB.pid) {
        TracePrintf(0, "Switch from idle to init in KernelStart (active pid=%i)\n", active->pid);
        ContextSwitch(switchProcesses, &idle_PCB.ctx, (void *)&idle_PCB, (void *)initPCB);
    } else {
        // Can uncomment this to see that it runs twice (first time with pid = 0, second time with pid=1)!
        // TracePrintf(0, "Switch from idle to init in KernelStart (active pid=%i)\n", active->pid);
        // ContextSwitch(switchProcesses, &idle_PCB.ctx, (void *)&idle_PCB, (void *)initPCB);
    }

    // Check cmd_args, run the specified process or init otherwise
    if (cmd_args[0] == NULL) {
        LoadProgram("init", cmd_args, info, initPt0, free_ll, initPCB);
    } else {
        LoadProgram(cmd_args[0], cmd_args, info, initPt0, free_ll, initPCB);
    }
}

static void initPageTables() {
    // Map pages for the kernel text (in region 1).
    unsigned int curr_page = PAGE_TABLE_LEN;
    while(curr_page < (uintptr_t)(&_etext) >> PAGESHIFT) {
        int ind = curr_page % PAGE_TABLE_LEN;
        region1[ind].pfn = curr_page;
        region1[ind].uprot = PROT_NONE;
        region1[ind].kprot = PROT_READ|PROT_EXEC;
        region1[ind].valid = 1;
        curr_page++;
    }
    
    // Map pages for the kernel data, bss, and heap (in region 1).
    // currentBrk is set to orig_break when we call this function.
    while(curr_page < (UP_TO_PAGE(currentBrk) >> PAGESHIFT)) {
        int ind = curr_page % PAGE_TABLE_LEN;
        region1[ind].pfn = curr_page;
        region1[ind].uprot = PROT_NONE;
        region1[ind].kprot = PROT_READ|PROT_WRITE;
        region1[ind].valid = 1;
        curr_page++;
    }

    // Make all remaining pages in region 1 invalid.
    while((curr_page % PAGE_TABLE_LEN) != 0) {
        region1[curr_page % PAGE_TABLE_LEN].valid = 0;
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
}

SavedContext  *cloneContext(SavedContext *ctxp, void *p1, void *p2) {
    (void)ctxp;  
    // ASK: What to do with the ptes below kernel stack? 
    // For now, set them all to 0.
    int curr_page = 0;
    while (curr_page < (DOWN_TO_PAGE(KERNEL_STACK_BASE) >> PAGESHIFT)) {
        ((pcb*)p2)->page_table0[curr_page].valid = 0;
        curr_page++;
    }
    // Make a "deep" copy of the kernel stack.
    while(curr_page < (UP_TO_PAGE(KERNEL_STACK_LIMIT) >> PAGESHIFT)) {
        ((pcb*)p2)->page_table0[curr_page].valid = 1;
        ((pcb*)p2)->page_table0[curr_page].kprot = PROT_READ|PROT_WRITE;
        ((pcb*)p2)->page_table0[curr_page].uprot = PROT_NONE;
        // Get a free pfn for this page.
        unsigned int pfn = getFreePage();
        if ((int)pfn == -1) {
            TracePrintf(0, "No enough free physical memory to complete operation\n");
            // Call "terminate process" that will:
            // 1) "unmap" page_table0 address
            // 2) free a physical page for this process's page table0
            // 3) add all valid pages in this page table to a free list.
            // 4) save exit status and add process to the statusQueue of its parent.
            // Return context of the 1st process since switch wasn't successful.
            return &((pcb*)p1)->ctx;
        }
        ((pcb*)p2)->page_table0[curr_page].pfn = pfn;
        // Map pfn to some virtual address.
        uintptr_t addrToCopy = reservePage(pfn);
        // Copy contents of the whole page to a new physical page.
        memcpy((void *)addrToCopy, (void*)(uintptr_t)(curr_page << PAGESHIFT), PAGESIZE);
        // Unmap this pfn since we won't use it anymore.
        unReservePage();
        curr_page++;
    }
    TracePrintf(0, "When copying kernel stack from process %i to process %i:\n", ((pcb*)p1)->pid, ((pcb*)p2)->pid);
    TracePrintf(0, "In process %i:\n", ((pcb*)p1)->pid);
    TracePrintf(0, "Valid bit = %i for vpn 508 in cloneContext\n", ((pcb*)p1)->page_table0[508].valid);
    TracePrintf(0, "PFN = %i for vpn 508 in cloneContext\n", ((pcb*)p1)->page_table0[508].pfn);
    TracePrintf(0, "PFN = %i for vpn 509 in cloneContext\n", ((pcb*)p1)->page_table0[509].pfn);
    TracePrintf(0, "PFN = %i for vpn 510 in cloneContext\n", ((pcb*)p1)->page_table0[510].pfn);

    TracePrintf(0, "In process %i:\n", ((pcb*)p2)->pid);
    TracePrintf(0, "Valid bit = %i for vpn 508 in cloneContext\n", ((pcb*)p2)->page_table0[508].valid);
    TracePrintf(0, "PFN = %i for vpn 508 in cloneContext\n", ((pcb*)p2)->page_table0[508].pfn);
    TracePrintf(0, "PFN = %i for vpn 509 in cloneContext\n", ((pcb*)p2)->page_table0[509].pfn);
    TracePrintf(0, "PFN = %i for vpn 510 in cloneContext\n", ((pcb*)p2)->page_table0[510].pfn);
    return &((pcb*)p2)->ctx;
}


SavedContext *switchProcesses(SavedContext *ctxp, void *p1, void *p2) {
    (void)ctxp;
    (void)p1;
    TracePrintf(0, "Doing a context switch from process %i to process %i!\n", ((pcb *)p1)->pid, ((pcb *)p2)->pid);
    struct pte *pt0_virtual_addr = ((pcb *)p2)->page_table0;
    TracePrintf(0, "Find a virtual address for pt0: %p\n", pt0_virtual_addr);
    if (((pcb*)p2)->pid == 0) {
        // Idle is a special process. Its PT0 is not on the boundary!!!
        // But we know for sure that it's virtual address == physical address.
        // Thus we can just write a virtual address in the register.
        WriteRegister(REG_PTR0, (RCS421RegVal)pt0_virtual_addr);
    } else {
        // Otherwise, we need to find a physical address.
        // Convert a virtual address of page table 0 to its vpn.
        unsigned int pt0_vpn = (((uintptr_t)pt0_virtual_addr) >> PAGESHIFT) % PAGE_TABLE_LEN;
        TracePrintf(0, "Find a vpn for pt0: %u\n", pt0_vpn);
        // Find a corresponding physical address of page table 0.
        void* pt0_physical_addr = (void *)((uintptr_t)region1[pt0_vpn].pfn << PAGESHIFT);
        TracePrintf(0, "Physical address of pt0: %p\n", pt0_physical_addr);
        // Let hardware know a physical address of a new page table 0.
        WriteRegister(REG_PTR0, (RCS421RegVal)pt0_physical_addr);
    }
    // Flush TLB for an entire region 0.
    WriteRegister(REG_TLB_FLUSH, TLB_FLUSH_0);

    // Make process 2 active.
    active = (pcb *)p2;
    TracePrintf(0, "Done doing a context switch from process %i to process %i!\n", ((pcb *)p1)->pid, ((pcb *)p2)->pid);
    return &((pcb*)p2)->ctx;
}

void trap_kernel_handler(ExceptionInfo *info) {
    // args for this function are in info->regs[1:8]
    // put return value in info->regs[0] (don't put anything there if Exit is called)
    switch (info->code) {
        case YALNIX_FORK:
            break;
        case YALNIX_EXEC:
            break;
        case YALNIX_EXIT:
            break;
        case YALNIX_WAIT:
            break;
        case YALNIX_GETPID:
            TracePrintf(0, "in getpid_handler\n"); 
            info->regs[0] = KernelGetPid();
            TracePrintf(0, "return value %i\n", info->regs[0]);
            break;
        case YALNIX_BRK:
            // check that there is >1 pages between stack and new break
            break;
        case YALNIX_DELAY:
            info->regs[0] = KernelDelay(info->regs[1]);
            break;
        case YALNIX_TTY_READ:
            break;
        case YALNIX_TTY_WRITE:
            break;
        default:
            TracePrintf(0, "Invalid code in the trap_kernel_handler\n");
    }
}

void trap_clock_handler(ExceptionInfo *info) {
    (void)info;
    TracePrintf(0, "trap_clock_handler\n");
    // Decrement a delay_offset of the first blocked process.
    if (blockedQ.head != NULL) {
       blockedQ.head->delay_offset--; 
    }
    // Put all processes whose delay is 0 to a ready queue.
    pcb *curr_process = blockedQ.head;
    while(curr_process != NULL && curr_process->delay_offset == 0) {
        // Remove this process from the blocked queue.
        dequeue(&blockedQ);
        // Put this process to a ready queue.
        enqueue(curr_process, &readyQ);
        // Go to the next process in a blocked queue.
        curr_process = curr_process->next;
    }

    if (prevActive != NULL && prevActive->pid == active->pid) {
        // The process is running for at least 2 clock ticks.
        // Take the next process from the ready queue.
        pcb *nextReady = dequeue(&readyQ);
        // Put a current process in the ready queue.
        enqueue(active, &readyQ);
        // Context switch between them.
        if (nextReady != NULL) {
            prevActive = active;
            ContextSwitch(switchProcesses, &active->ctx, (void *)active, (void *)nextReady);
        }
    }
    TracePrintf(0, "Print a blocked queue:\n");
    printQueue(blockedQ);
    TracePrintf(0, "Print a ready queue:\n");
    printQueue(readyQ);
}


void trap_illegal_handler(ExceptionInfo *info) {
    // terminate current process
    // print message with the current process id and problem that coused it (look at info->code)
    // can print with printf/fprintf or (better) TtyTransmit
    // continue running other processes (context switch to the next runnable)
    // exit status reported to the parent process of the terminated process when the parent calls the Wait
    // kernel call should be the value ERROR; same as if child called Exit(ERROR)
    (void)info;
    TracePrintf(0, "trap_illegal_handler");
    Halt();
}

void trap_memory_handler(ExceptionInfo *info) {

    TracePrintf(0, "trap_memory_handler, addr:%p\n", info->addr);
    TracePrintf(0, "trap_memory_handler, code:%i\n", info->code);
    TracePrintf(0, "trap_memory_handler, pc:%i\n", info->pc);
    TracePrintf(0, "trap_memory_handler, sp:%i\n", info->sp);
    // Note: the last page of the user stack *ends* at virtual address info->sp.
    if (info->addr == NULL || info->addr <= active->brk || info->addr >= info->sp) {
        // TODO: terminate process (context switch to next ready process)
        TracePrintf(0, "ERROR: disallowed memory access for process %i:\n", KernelGetPid());
        switch (info->code) {
            case TRAP_MEMORY_MAPERR:
                TracePrintf(0, "No mapping at addr %p\n", info->addr);
                break;
            case TRAP_MEMORY_ACCERR:
                TracePrintf(0, "Protection violation at addr %p\n", info->addr);
                break;
            case TRAP_MEMORY_KERNEL:
                TracePrintf(0, "Linux kernel sent SIGSEGV at addr %p\n", info->addr);
                break;
            case TRAP_MEMORY_USER:
                TracePrintf(0, "Received SIGSEGV from user at addr %p\n", info->addr);
                break;
            default:
                TracePrintf(0, "Unidentified error type at address %p:\n", info->addr);
        }
    } else {
        // info->addr is between the process's break and stack pointer.
        // This is a request to enlarge process's stack to "cover" info->addr.
        int new_gap = ((uintptr_t)(DOWN_TO_PAGE(info->addr) - UP_TO_PAGE(active->brk)) >> PAGESHIFT);
        // Check that there is more than one page between process's break and a new stack pointer.
        if (new_gap < 1) {
            // TODO: terminate process 
        }
        int count = ((uintptr_t)(DOWN_TO_PAGE(info->sp) - DOWN_TO_PAGE(info->addr)) >> PAGESHIFT);
        // Check that we have enough physical memory for enlarging stack.
        if (count > free_ll.count) {
           // TODO: terminate process 
        } 
        int i;
        unsigned int curr_page = (DOWN_TO_PAGE(info->addr)) >> PAGESHIFT;
        for (i = 0; i < count; i++) {
            active->page_table0[curr_page].valid = 1;
            active->page_table0[curr_page].kprot = PROT_READ | PROT_WRITE;
            active->page_table0[curr_page].uprot = PROT_READ | PROT_WRITE;
            unsigned int pfn = getFreePage();
            if ((int)pfn == -1) {
                TracePrintf(0, "No enough free physical memory to complete operation\n");
                // TODO: terminate process
            }
            active->page_table0[curr_page++].pfn = pfn;
        }
        // Lower a stack pointer.
        info->sp = info->addr;
        TracePrintf(0, "Process's stack was expanded.\n");
    }
}

void trap_math_handler(ExceptionInfo *info) {
    // same as in trap_illegal (look at info->code for better description of error)
    (void)info;
    TracePrintf(0, "trap_math_handler");
    Halt();
}

void trap_tty_transmit_handler(ExceptionInfo *info) {
    // the previous TtyTransmit hardware operation on info->code terminal has completed
    // unblock the blocked process that started this TtyWrite kernel call that started this output
    // if other TtyWrite calls are pending for info->code terminal, start the next one 
    // by calling TtyTransmit(info->code, void *buf, int len)
    // buf must ne in region 1
    (void)info;
    TracePrintf(0, "trap_tty_transmit_handler");
    Halt();
}

void trap_tty_receive_handler(ExceptionInfo *info) {
    // a new line of input is available from the terminal of number info->code
    // "allocate" some memory for buf (should lie in Region1!)
    // call TtyReceive(info->code, void *buf, TERMINAL_MAX_LINE)
    // if necessary should buffer the input line for a subsequent TtyRead kernel call by some user process.
    (void)info;
    TracePrintf(0, "trap_tty_receive_handler");
    Halt();
}   

// Called when malloc is called by the kernel.
int SetKernelBrk(void *addr) { 
    if(!vm_enabled) {
        if(addr > (void *)VMEM_LIMIT) {
            return -1;
        }
        currentBrk = addr;
    } else {
        int count = ((uintptr_t)(UP_TO_PAGE(addr) - UP_TO_PAGE(currentBrk)) >> PAGESHIFT);
        if (count > free_ll.count) {
           return -1;
        } 
        int i;
        unsigned int curr_page = (UP_TO_PAGE(currentBrk) >> PAGESHIFT) % PAGE_TABLE_LEN;
        for (i = 0; i < count; i++) {
            region1[curr_page].valid = 1;
            region1[curr_page].kprot = PROT_READ|PROT_WRITE;
            region1[curr_page].uprot = PROT_NONE;
            unsigned int pfn = getFreePage();
            if ((int)pfn == -1) {
                TracePrintf(0, "No enough free physical memory to complete operation\n");
                return -1;
            }
            region1[curr_page++].pfn = pfn;
        }
        /// Note: break is not a part of heap.
        currentBrk = addr;
    }
    return 0;
}

// Don't include page for end_addr.
static void initFreePages(int start_addr, int end_addr) {
    int curr_addr;
    for (curr_addr = start_addr; curr_addr < end_addr; curr_addr+=PAGESIZE) {
        struct physical_frame* currFrame = (struct physical_frame *)(uintptr_t)curr_addr;
        currFrame->next = free_ll.head;
        free_ll.head = currFrame;
        free_ll.count++;
    } 
}

// Frees the pte of the page table.
void freePage(struct pte* newPte) {
    addPage(newPte->pfn);  
    newPte->valid = 0;
    // Do TLB flush for this specific pte.
    int vpn = (uintptr_t)newPte & PAGEOFFSET;
    WriteRegister(REG_TLB_FLUSH, vpn << PAGESHIFT);
}

// Adds all invalid pages to a free list of pages.
void addInvalidPages() {
    int pfn;
    for (pfn = PMEM_BASE; pfn < MEM_INVALID_PAGES; pfn++) {
        addPage(pfn);
    }
}

// Adds a physical page to a free list of pages.
void addPage(int pfn) {
    // TracePrintf(0, "I'm in addPage and pfn is %i\n", pfn);
    // Get a virtual address that maps to desired pfn.
    struct physical_frame* currFrame = (struct physical_frame *)reservePage(pfn);
    currFrame->next = free_ll.head;
    // Set a head to a physical address of this page.
    free_ll.head = (struct physical_frame *)(uintptr_t)(pfn << PAGESHIFT);
    // TracePrintf(0, "free_ll.head is %p\n", free_ll.head);
    free_ll.count++;
    unReservePage();  
}

// Removes a free page from the list of fre pages.
unsigned int getFreePage() {
    if (free_ll.count == 0) {
        return -1;
    }
    // TracePrintf(0, "I'm in get free page and head is %p\n", free_ll.head);
    unsigned int resultPfn = ((uintptr_t)free_ll.head >> PAGESHIFT);
    // Map a virtual address to resultPfn.
    struct physical_frame* currFrame = (struct physical_frame *)reservePage(resultPfn);
    free_ll.head = currFrame->next; 
    free_ll.count--;
    unReservePage();
    // TracePrintf(0, "Took a new pfn from free list %i\n", resultPfn);
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
static struct pte *getNewPageTable() {
    // Reserve a vpn in page table 1 for this purpose.
    int vpn = PAGE_TABLE_LEN - 2;
    while(vpn >= 0 && region1[vpn].valid) {
        vpn--;
    }
    if (vpn < 0) {
        // No enough free virtual pages in region 1.
        return NULL;
    }
    // Otherwise, pte is not valid and we can create a new mapping for a region 0 page table.
    region1[vpn].valid = 1;
    // Get a physical page for this page table.
    unsigned int pfn = getFreePage();
    if ((int)pfn == -1) {
        TracePrintf(0, "No enough free physical memory to complete operation\n");
        return NULL;
    }
    region1[vpn].pfn = pfn;
    region1[vpn].kprot = PROT_READ|PROT_WRITE;
    region1[vpn].uprot = PROT_NONE; 

    // We set every valid bit in the page table to 0 in MySwitchFunc for "consistency."
    uintptr_t pt0_vrt_addr = (uintptr_t)(VMEM_1_BASE + (vpn << PAGESHIFT));
    // "Clear" this chunk of memory to avoid weird bugs.
    memset((void *)pt0_vrt_addr, '\0', PAGE_TABLE_LEN * sizeof(struct pte));
    return (struct pte *)pt0_vrt_addr;
}

/**
 * @brief Adds proc to the specified queue
 * 
 * @param proc element to put on the queue
 * @param queue queue to add the element to
 */
static void enqueue(pcb* proc, queue* queue) {
    proc->next = NULL;
    if(queue->count == 0) {
        // This is the first element in the queue.
        queue->head = proc;
        queue->tail = proc;
    }
    else {
        // This is not the first element in the queue.
        queue->tail->next = proc;
        queue->tail = proc;
    }
    queue->count++;
}

/**
 * @brief Removes the first element from the queue.
 *
 * @param queue queue to remove the element from
 */
static pcb *dequeue(queue* queue) {
    if(queue->count == 0) {
        // Not enough elements in the queue.
        return NULL;
    } 
    pcb *removedPCB = queue->head;
    queue->head = queue->head->next;
    if (queue->count == 1) {
        // This was the last process in the queue.
        queue->tail = NULL;
    }
    queue->count--;
    return removedPCB;
}

/**
 * @brief Adds the PCB of the delayed process in the blocking queue. The PCBs are sorted according to their delay times. 
 * 
 * @param proc element to put on the blocked queue
 * @param delay clock_ticks to be delayed by.
 */
static void delayProcess(pcb* proc, int delay) {
    if(blockedQ.count == 0) {
        // This is the first process in the blocked queue.
        proc->delay_offset = delay;
        // No need to care about specific order.
        enqueue(proc, &blockedQ);
    } else {
        // Blocked queue already has some processes.
        // Insert processes according to its delay time.
        pcb* prev_process = NULL;
        pcb* curr_process = blockedQ.head;
        while (curr_process != NULL && curr_process->delay_offset < delay) {
            prev_process = curr_process;
            curr_process = curr_process->next;
        }
        // Three cases are possible.
        if (curr_process == blockedQ.head) {
            // 1. Insert this process at the beginning of the queue.
            // Adjust delay offsets.
            proc->delay_offset = delay;
            curr_process -= delay;
            // Adjust pointers.
            proc->next = curr_process;
            blockedQ.head = proc;  
        } else if (prev_process == blockedQ.tail) {
            // 2. Insert this process at the end of the queue.
            // Adjust delay offsets.
            proc->delay_offset = delay - (blockedQ.tail->delay_offset);
            // Adjust pointers.
            proc->next = NULL;
            blockedQ.tail->next = proc;
            blockedQ.tail = proc;
        } else {
            // 3. Insert this process between prev_process and curr_process.
            // Need to adjust delay offsets.
            proc->delay_offset = delay - prev_process->delay_offset;
            curr_process->delay_offset -= delay;
            // Adjust pointers.
            prev_process->next = proc;
            proc->next = curr_process;
        }
        blockedQ.count++;
    }
    TracePrintf(0, "Printing the elements of blocked queue:\n");
    printQueue(blockedQ);
}

/**
 * @brief Runs the next process from the ready queue or idle if queue is empty.
 */
static void runNextProcess() {
    pcb *nextReady = dequeue(&readyQ);
    // Run an idle process if the ready queue is empty.
    if (nextReady == NULL) {
        nextReady = &idle_PCB;
    }
    // Perform context switch.
    ContextSwitch(switchProcesses, &active->ctx, (void *)active, (void *)nextReady);
}


// Helpers for kernel calls.

static int KernelGetPid() {
    return active->pid;
}

static int KernelDelay(int clock_ticks) {
    TracePrintf(0, "Inside KernelDelay: number of ticks is %i\n", clock_ticks);
    if (clock_ticks < 0) {
        return ERROR;
    } else if (clock_ticks == 0) {
        return 0;
    } else {
        TracePrintf(0, "Call delayProcess\n");
        // Put the process in the blocked queue.
        delayProcess(active, clock_ticks);
        TracePrintf(0, "Call runNextProcess\n");
        // Context switch to a ready process.
        runNextProcess();
        TracePrintf(0, "KernelDelay is done\n");
        return 0;
    }
}

static void printQueue(queue q) {
    int i = 1;
    pcb *cur = q.head;
    while(cur != NULL) {
        TracePrintf(0, "%i. Process with pid=%i and delayed time %i\n", i++, cur->pid, cur->delay_offset);
        cur = cur->next;
    }
}