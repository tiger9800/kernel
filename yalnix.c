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
// Structs containing reading/writing queues and linked lists of data.
term terminals[NUM_TERMINALS];
// Create input buffer (for more efficiency).
char input_buf[TERMINAL_MAX_LINE];

// PID of the next process to be created.
int currPID = 1;

// Active process during the previous TRAP_CLOCK.
pcb *prevActive = NULL;

// Create queues of blocked readers and writers.




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
static int copyPTE(struct pte* dest, struct pte* src, int curr_page);
static int copyMemoryImage(pcb *destProc, pcb *srcProc);
void *find_PT0_physical_addr(pcb *proc);

// MySwitch functions.
SavedContext  *cloneContext(SavedContext *ctxp, void *p1, void *p2);
SavedContext *switchProcesses(SavedContext *ctxp, void *p1, void *p2);
SavedContext *terminateSwitch(SavedContext *ctxp, void *p1, void *p2);

// Functions for working with queues. 
static void enqueue(pcb* proc, queue* queue);
static pcb *dequeue(queue* queue);
static void delayProcess(pcb* proc, int delay);
static void runNextProcess();
//static void printQueue(queue q);
static void printChildren(queue q);
static pcb *init_pcb();
static void addChild(pcb* proc, queue* queue);
// static int removeChild(pcb *parent, pcb *childPCB);
static int removeChild(queue* queue, pcb* elem);
static line *addData(line *elem, line *head);
static line *removeReadData(term *t);
static line *removeWriteData(term *t);

// Kernel call helpers.
static int KernelGetPid();
static int KernelDelay(int clock_ticks);
static int KernelUserBrk(ExceptionInfo *info);
static int KernelFork();
static int KernelExec(ExceptionInfo *info);
static void KernelExit(int status);
static int KernelWait(int *status);
static int KernelRead(ExceptionInfo *info);
static int KernelWrite(ExceptionInfo *info);

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

    //Initialize a PCB of init process (first process to run).
    pcb *initPCB = init_pcb();

    if (initPCB == NULL) {
        TracePrintf(0, "No enough physical or virtual memory to create a process\n");
        Halt();
    }

    // Save current context to the saved context of process to be run after KernelStart()
    ContextSwitch(cloneContext, &initPCB->ctx, (void *)&idle_PCB, (void *)initPCB);
    // Switch from idle process to init.
    if (active->pid == idle_PCB.pid) {
        // Where the idle process will resume after cloneContext.
        // Idle process performs switch with init process.
        ContextSwitch(switchProcesses, &idle_PCB.ctx, (void *)&idle_PCB, (void *)initPCB);
    } else {
        // Where init process will resume after switchProcesses.
        // Init loads its code.
        if (cmd_args[0] == NULL) {
            LoadProgram("init", cmd_args, info, initPCB->page_table0, free_ll, initPCB);
        } else {
            LoadProgram(cmd_args[0], cmd_args, info, initPCB->page_table0, free_ll, initPCB);
        }
    }
}


/*
 *  MySwitchFunc functions.
 */

SavedContext  *cloneContext(SavedContext *ctxp, void *p1, void *p2) {
    (void)ctxp;  
    (void)p1;
    int curr_page;
    // Make a "deep" copy of the kernel stack.
    for (curr_page = (DOWN_TO_PAGE(KERNEL_STACK_BASE) >> PAGESHIFT); curr_page < (UP_TO_PAGE(KERNEL_STACK_LIMIT) >> PAGESHIFT); curr_page++) {
        // Get a free pfn for this page.
        struct pte *dest = &((pcb*)p2)->page_table0[curr_page];
        struct pte *src = &((pcb*)p1)->page_table0[curr_page];
        if (copyPTE(dest, src, curr_page) == ERROR) {
            TracePrintf(0, "No enough free physical memory to complete operation\n");
            // Call "terminate process" that will:
            
            // 1) "unmap" page_table0 address
            // 2) free a physical page for this process's page table0
            // 3) free PCB
            // 3) add all valid pages in this page table to a free list.
            // 4) save exit status and add process to the statusQueue of its parent.
            // Return context of the 1st process since switch wasn't successful.
            return &((pcb*)p2)->ctx;
        }
    }

    return &((pcb*)p2)->ctx;
}


SavedContext *switchProcesses(SavedContext *ctxp, void *p1, void *p2) {
    (void)ctxp;
    (void)p1;
    TracePrintf(0, "Doing a context switch from process %i to process %i!\n", ((pcb *)p1)->pid, ((pcb *)p2)->pid);
    void *pt0_physical_addr = find_PT0_physical_addr((pcb *)p2);
    // Let hardware know a physical address of a new page table 0.
    WriteRegister(REG_PTR0, (RCS421RegVal)pt0_physical_addr);
    // Flush TLB for an entire region 0.
    WriteRegister(REG_TLB_FLUSH, (RCS421RegVal)TLB_FLUSH_0);
    // Make process 2 active.
    active = (pcb *)p2;
    return &((pcb*)p2)->ctx;
}

SavedContext *terminateSwitch(SavedContext *ctxp, void *p1, void *p2) {
    //Terminate p1 and switch to p2
    (void)ctxp;
    (void)p1;
    TracePrintf(0, "Terminating %i and running %i!\n", ((pcb *)p1)->pid, ((pcb *)p2)->pid);
    void *pt0_physical_addr = find_PT0_physical_addr((pcb *)p2);
    // Let hardware know a physical address of a new page table 0.
    WriteRegister(REG_PTR0, (RCS421RegVal)pt0_physical_addr);
    // Flush TLB for an entire region 0.
    WriteRegister(REG_TLB_FLUSH, (RCS421RegVal)TLB_FLUSH_0);
    //do all the free memory stuff
    int currPage;
    for(currPage = MEM_INVALID_PAGES; currPage < PAGE_TABLE_LEN; currPage++) {
        if(active->page_table0[currPage].valid) {
            freePage(active->page_table0 + currPage, 0);
        }
    } 
    struct pte *region0_addr = active->page_table0;
    unsigned int idx = ((uintptr_t)region0_addr >> PAGESHIFT) % PAGE_TABLE_LEN;
    freePage(&region1[idx], 1);

    // Working with status queue:
    // Free pcbs of children that was not reaped by wait.
    if(active->statusQ != NULL && active->statusQ->count>0) {
        pcb *currChild = dequeue(active->statusQ);
        while(currChild != NULL) {
            free(currChild);
            currChild = dequeue(active->statusQ);
        }
    }
    // Free a status queue.
    if (active->statusQ != NULL) {
        free(active->statusQ);
    }

    // Working with a children queue.
    // Tell every alive child that the parent exited, so they can free their own pcb.
    if(active->childrenQ != NULL) {
        while(active->childrenQ->head != NULL) {
            active->childrenQ->head->parent = NULL;
            removeChild(active->childrenQ, active->childrenQ->head);
        }
    }
    // Free a children queue.
    if (active->childrenQ != NULL) {
        free(active->childrenQ);
    }

    if(active->parent == NULL) {
        // If process does not have a parent, process can free its pcb.
        free(active);
    } else {
        // If process has a parent, we should remove its pcb from parent's children queue.
        if (removeChild(active->parent->childrenQ, active) == -1) {
            TracePrintf(0, "Parent doesn't have a child with pid=%i in its children queue!\n", active->pid);
        }
        // Now we should add its pcb to its parent's status queue.
        enqueue(active, active->parent->statusQ);
        
        // Print a status queue.
        TracePrintf(0, "Print a status queue of the parent after process %i exited:\n", active->pid);
        //printQueue(*(active->parent->statusQ));
        // Print a children queue.
        TracePrintf(0, "Print a children queue of the parent after process %i exited:\n", active->pid);
        printChildren(*(active->parent->childrenQ));
    }

    // Make process 2 active.
    active = (pcb *)p2;

    return &((pcb*)p2)->ctx;
}

/*
 *  Trap handlers (located in the interrupt vector table).
 */

void trap_kernel_handler(ExceptionInfo *info) {
    // args for this function are in info->regs[1:8]
    // put return value in info->regs[0] (don't put anything there if Exit is called)
    switch (info->code) {
        case YALNIX_FORK:
            info->regs[0] = KernelFork();
            break;
        case YALNIX_EXEC:
            info->regs[0] = KernelExec(info);
            break;
        case YALNIX_EXIT:
            KernelExit(info->regs[1]);
            break;
        case YALNIX_WAIT:
            info->regs[0] = KernelWait((int*) info->regs[1]);
            break;
        case YALNIX_GETPID:
            info->regs[0] = KernelGetPid();
            break;
        case YALNIX_BRK:
            info->regs[0] = KernelUserBrk(info);
            break;
        case YALNIX_DELAY:
            info->regs[0] = KernelDelay(info->regs[1]);
            break;
        case YALNIX_TTY_READ:
            info->regs[0] = KernelRead(info);
            break;
        case YALNIX_TTY_WRITE:
            info->regs[0] = KernelWrite(info);
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
       TracePrintf(0, "Pid %i left to wait %i\n",blockedQ.head->pid, blockedQ.head->delay_offset);
    }
    // Put all processes whose delay is 0 to a ready queue.
    pcb *curr_process = blockedQ.head;
    while(curr_process != NULL && curr_process->delay_offset <= 0) {
        // Remove this process from the blocked queue.
        dequeue(&blockedQ);
        pcb *next_process = curr_process->next;
        // Put this process to a ready queue.
        enqueue(curr_process, &readyQ);
        // Go to the next process in a blocked queue.
        curr_process = next_process;
    }
    
    if (active->pid == 0 || (prevActive != NULL && prevActive->pid == active->pid)) {
        // If idle is running or if some process is running for at least 2 clock ticks: ContextSwitch!
        // Take the next process from the ready queue.
        pcb *nextReady = dequeue(&readyQ);
        // Context switch to this process.
        if (nextReady != NULL) {
            // Put a current non-idle process in the ready queue.
            if (active->pid != 0) {
                enqueue(active, &readyQ);
            }
            prevActive = active;
            // TracePrintf(0, "Print a blocked queue before context switch:\n");
            //printQueue(blockedQ);
            // TracePrintf(0, "Print a ready queue before context switch:\n");
            //printQueue(readyQ);
            ContextSwitch(switchProcesses, &active->ctx, (void *)active, (void *)nextReady);
        }
    } else {
        prevActive = active;
        // TracePrintf(0, "Print a blocked queue:\n");
        //printQueue(blockedQ);
        // TracePrintf(0, "Print a ready queue:\n");
        //printQueue(readyQ);
    }
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
    KernelExit(ERROR);
}

void trap_memory_handler(ExceptionInfo *info) {
    TracePrintf(0, "trap_memory_handler!\n");
    TracePrintf(0, "Requested address: %p\n", info->addr);
    TracePrintf(0, "Current lowest sp: %p\n", active->min_sp);
    TracePrintf(0, "Current break: %u\n", (uintptr_t)(active->brk) >> PAGESHIFT);
    TracePrintf(0, "Requested vpn: %u\n", (DOWN_TO_PAGE(info->addr)) >> PAGESHIFT);
    TracePrintf(0, "Current min_sp vpn: %u\n", (uintptr_t)(active->min_sp) >> PAGESHIFT);
    TracePrintf(0, "Current brk vpn: %u\n", (uintptr_t)(active->brk) >> PAGESHIFT);


    // TracePrintf(0, "trap_memory_handler, code:%i\n", info->code);
    // TracePrintf(0, "trap_memory_handler, pc:%i\n", info->pc);
    // TracePrintf(0, "trap_memory_handler, sp:%i\n", info->sp);


    unsigned int curr_page = (DOWN_TO_PAGE(info->addr)) >> PAGESHIFT;
    if (DOWN_TO_PAGE(info->addr) >= (intptr_t)active->min_sp) {
        // Just set a stack pointer to this address.
        // info->sp = info->addr;
    } else if (info->addr == NULL || info->addr <= active->brk || info->addr >= active->min_sp) {
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
        KernelExit(ERROR);
    } else {
        // info->addr is between the process's break and stack pointer.
        // This is a request to enlarge process's stack to "cover" info->addr.
        int new_gap = ((uintptr_t)(DOWN_TO_PAGE(info->addr) - UP_TO_PAGE(active->brk)) >> PAGESHIFT);
        // Check that there is more than one page between process's break and a new stack pointer.
        if (new_gap < 1) {
            // TODO: terminate process 
        }
        int count = ((uintptr_t)(DOWN_TO_PAGE(active->min_sp) - DOWN_TO_PAGE(info->addr)) >> PAGESHIFT);
        // Check that we have enough physical memory for enlarging stack.
        if (count > free_ll.count) {
           // TODO: terminate process 
        } 
        int i;
        for (i = 0; i < count; i++) {
            active->page_table0[curr_page].valid = 1;
            active->page_table0[curr_page].kprot = PROT_READ | PROT_WRITE;
            active->page_table0[curr_page].uprot = PROT_READ | PROT_WRITE;
            active->page_table0[curr_page++].pfn = getFreePage();
        }
        // Lower a stack pointer.
        // info->sp = info->addr;
        // Lower a pointer to the lowest allocated page for the user stack.
        active->min_sp = (void *)DOWN_TO_PAGE(info->addr);
        TracePrintf(0, "Process's stack was expanded.\n");
    }
}

void trap_math_handler(ExceptionInfo *info) {
    // same as in trap_illegal (look at info->code for better description of error)
    (void)info;
    TracePrintf(0, "trap_math_handler");
    KernelExit(ERROR);
}

void trap_tty_transmit_handler(ExceptionInfo *info) {
    TracePrintf(0, "trap_tty_transmit_handler");
    // The previous TtyTransmit hardware operation on info->code terminal has completed
    int term = info->code;
    // Unblock the blocked process that started this TtyWrite kernel call that started this output (head of writeQ)
    pcb *proc = dequeue(&terminals[term].writeQ);
    // Add this process to a ready queue.
    enqueue(proc, &readyQ);
    // Check if other TtyWrite calls are pending for this terminal
    if (terminals[term].writeQ.count > 0) {
        // Start the next TtyTransmit
        line *nextLine = removeWriteData(&terminals[term]);
        TtyTransmit(term, nextLine->content, nextLine->len);
    }
}

void trap_tty_receive_handler(ExceptionInfo *info) {
    TracePrintf(0, "trap_tty_receive_handler");
    // a new line of input is available from the terminal of number info->code
    int term = info->code;
    int len = TtyReceive(term, input_buf, TERMINAL_MAX_LINE);
    line *nextLine = malloc(sizeof(line));
    nextLine->content = malloc(sizeof(char)*len);
    memcpy(nextLine->content, input_buf, len);
    nextLine->len = len;
    terminals[term].read_data = addData(nextLine, terminals[term].read_data);
}   

// Called when malloc is called by the kernel.
int SetKernelBrk(void *addr) { 
    if(!vm_enabled) {
        if(addr > (void *)VMEM_LIMIT) {
           return -1;//before exiting kernelstart we request invalid memory
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
            region1[curr_page++].pfn = getFreePage();
        }
        /// Note: break is not a part of heap.
        currentBrk = addr;
    }
    return 0;
}

/*
 *  Helper functions for memory manipulation.
 */

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
void freePage(struct pte* newPte, int region) {
    
    addPage(newPte->pfn);  
    newPte->valid = 0;
    // Do TLB flush for this specific pte.
    int vpn = (((uintptr_t)newPte & PAGEOFFSET) / sizeof(struct pte)) + region*PAGE_TABLE_LEN;
    
    WriteRegister(REG_TLB_FLUSH, (RCS421RegVal) (vpn << PAGESHIFT));
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
int getFreePage() {
    if (free_ll.count == 0) {
        TracePrintf(0, "Not enough free physical pages in getFreePage()\n");
        return -1;
    }
    // TracePrintf(0, "I'm in get free page and head is %p\n", free_ll.head);
    int resultPfn = ((uintptr_t)free_ll.head >> PAGESHIFT);
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
        TracePrintf(0, "vpn %i is already used\n", vpn);
        vpn--;
    }
    if (vpn < 0) {
        // No enough free virtual pages in region 1.
        return NULL;
    }
    TracePrintf(0, "Print vpn of new page table %i\n", vpn);
    // Otherwise, pte is not valid and we can create a new mapping for a region 0 page table.
    region1[vpn].valid = 1;
    // Get a physical page for this page table.
    int pfn = getFreePage();
    if (pfn == -1) {
        TracePrintf(0, "No enough free physical memory to complete operation\n");
        return NULL;
    }
    region1[vpn].pfn = pfn;
    TracePrintf(0, "Print pfn of new page table %i\n", pfn);
    TracePrintf(0, "Phys addr for new page table is %p\n", (uintptr_t)pfn << PAGESHIFT);
    region1[vpn].kprot = PROT_READ|PROT_WRITE;
    region1[vpn].uprot = PROT_NONE; 

    // We set every valid bit in the page table to 0 in MySwitchFunc for "consistency."
    struct pte *pt0_vrt_addr = (struct pte *)(uintptr_t)(VMEM_1_BASE + (vpn << PAGESHIFT));
    WriteRegister(REG_TLB_FLUSH, (RCS421RegVal)pt0_vrt_addr);

    // "Clear" this chunk of memory to avoid weird bugs.
    // memset((void *)pt0_vrt_addr, '\0', PAGE_TABLE_LEN * sizeof(struct pte));
    int curr_page = 0;
    while (curr_page < PAGE_TABLE_LEN) {
        pt0_vrt_addr[curr_page].valid = 0;
        curr_page++;
    }
    return pt0_vrt_addr;
}

static int copyPTE(struct pte* dest, struct pte* src, int curr_page) {
    
    if (curr_page == 508) {
        if (src->valid == 0) {
            TracePrintf(0, "Src: 508 is invalid\n");
        } else {
            TracePrintf(0, "Src: 508 is valid\n");
        }
    }
    if(!src->valid) {
        dest->valid = 0;
    } else {
        dest->valid = 1;
        dest->kprot = src->kprot;
        dest->uprot = src->uprot;
        int pfn = getFreePage();
        if (pfn == -1) {
            TracePrintf(0, "No enough free physical memory to complete operation\n");
            return ERROR;
        }
        dest->pfn = pfn;
        // Map pfn to some virtual address.
        uintptr_t addrToCopy = reservePage(pfn);
        // Copy contents of the whole page to a new physical page. 
        memcpy((void *)addrToCopy, (void*)(uintptr_t)(curr_page << PAGESHIFT), PAGESIZE);
        // Unmap this pfn since we won't use it anymore.
        unReservePage();
    }
    if (curr_page == 508) {
        if (dest->valid == 0) {
            TracePrintf(0, "Des: 508 is invalid\n");
        } else {
            TracePrintf(0, "Des: 508 is valid\n");
        }
    }
    return 0;
}

static int copyMemoryImage(pcb *destProc, pcb *srcProc) {
    int curr_page;
    // Make a "deep" copy of the user memory.
    for (curr_page = MEM_INVALID_PAGES; curr_page < (DOWN_TO_PAGE(KERNEL_STACK_BASE) >> PAGESHIFT); curr_page++) {
        // Get a free pfn for this page if needed.
        struct pte *dest = &destProc->page_table0[curr_page];
        struct pte *src = &srcProc->page_table0[curr_page];
        if (copyPTE(dest, src, curr_page) == ERROR) {
            TracePrintf(0, "No enough free physical memory to complete operation\n");
            // Call "terminate process" that will:
            // 1) "unmap" page_table0 address
            // 2) free a physical page for this process's page table0
            // 3) add all valid pages in this page table to a free list.
            // 4) save exit status and add process to the statusQueue of its parent.
            // Return context of the 1st process since switch wasn't successful.
            return -1;
        }
    }
    return 0;
}


void * find_PT0_physical_addr(pcb *proc) {
    struct pte *pt0_virtual_addr = proc->page_table0;
    if (proc->pid == 0) {
        // Idle is a special process. Its PT0 is not on the boundary!!!
        // But we know for sure that it's virtual address == physical address.
        return pt0_virtual_addr;
    } else {
        // Otherwise, we need to find a physical address.
        // Convert a virtual address of page table 0 to its vpn.
        unsigned int pt0_vpn = (((uintptr_t)pt0_virtual_addr) >> PAGESHIFT) % PAGE_TABLE_LEN;
        // Find a corresponding physical address of page table 0.
        void* pt0_physical_addr = (void *)((uintptr_t)region1[pt0_vpn].pfn << PAGESHIFT);
        // Let hardware know a physical address of a new page table 0.
        return pt0_physical_addr;
    }
}

/*
 *  Queue functions.
 */
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
        proc->delay_offset = delay;
        // No need to care about specific order.
        enqueue(proc, &blockedQ);
    } else {
        // Blocked queue already has some processes.
        // Insert processes according to its delay time.
        pcb* prev_process = NULL;
        pcb* curr_process = blockedQ.head;
        int sum = 0;
        while (curr_process != NULL && (sum + curr_process->delay_offset) < delay) {
            sum += curr_process->delay_offset;
            prev_process = curr_process;
            curr_process = curr_process->next;
        }
        // Adjust delay offsets.
        proc->delay_offset = delay - sum;
        if (curr_process != NULL) {
            curr_process->delay_offset = curr_process->delay_offset - proc->delay_offset;
        }
        // Three cases are possible.
        if (curr_process == blockedQ.head) {
            // 1. Insert this process at the beginning of the queue.
            proc->next = curr_process;
            blockedQ.head = proc;  
        } else if (prev_process == blockedQ.tail) {
            // 2. Insert this process at the end of the queue.
            proc->next = NULL;
            blockedQ.tail->next = proc;
            blockedQ.tail = proc;
        } else {
            // 3. Insert this process between prev_process and curr_process.
            prev_process->next = proc;
            proc->next = curr_process;
        }
        blockedQ.count++;
    }
    TracePrintf(0, "Printing the elements of blocked queue:\n");
    //printQueue(blockedQ);
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

// static void printQueue(queue q) {
//     int i = 1;
//     pcb *cur = q.head;
//     if (q.count == 0){
//         TracePrintf(0, "Queue is empty\n");
//     }
//     while(cur != NULL) {
//         TracePrintf(0, "%i. Process with pid=%i and delayed time %i\n", i++, cur->pid, cur->delay_offset);
//         cur = cur->next;
//     }
// }

static void printChildren(queue q) {
    int i = 1;
    pcb *cur = q.head;
    if (q.count == 0){
        TracePrintf(0, "Queue is empty\n");
    }
    while(cur != NULL) {
        TracePrintf(0, "%i. Process with pid=%i and delayed time %i\n", i++, cur->pid, cur->delay_offset);
        cur = cur->nextChild;
    }
}

static pcb *init_pcb() {
    pcb *newPCB = malloc(sizeof(pcb));
    struct pte *newPt0 = getNewPageTable();
    if(newPCB == NULL || newPt0 == NULL) {
        return NULL;
    }
    // Init some PCB values for this process.
    newPCB->pid = currPID++;
    newPCB->page_table0 = newPt0;
    newPCB->parent = NULL;
    newPCB->next = NULL;
    newPCB->nextChild = NULL;
    newPCB->childrenQ = NULL;
    newPCB->statusQ = NULL;
    newPCB->waiting = false;
    return newPCB;
}

/*
 *  Helper functions for kernel calls.
 */

static int KernelGetPid() {
    return active->pid;
}

static int KernelDelay(int clock_ticks) {
    if (clock_ticks < 0) {
        return ERROR;
    } else if (clock_ticks == 0) {
        return 0;
    } else {
        active->delay_offset = clock_ticks;
        // Put the process in the blocked queue.
        delayProcess(active, clock_ticks);
        // Context switch to a ready process.
        runNextProcess();
        return 0;
    }
}

static int KernelUserBrk(ExceptionInfo *info) {
    (void)info;
    void *addr = (void *)info->regs[1];
    if (addr == NULL || addr <= active->brk || addr >= info->sp) {
        return ERROR;
    }
    // addr must be between the process's break and stack pointer.
    // This is a request to enlarge process's heap to "cover" addr.
    int new_gap = ((uintptr_t)(DOWN_TO_PAGE(info->sp) - UP_TO_PAGE(addr)) >> PAGESHIFT);
    TracePrintf(0, "new gap when malloc was done: %i\n", new_gap);
    // Check that there is more than one page between process's break and a new stack pointer.
    if (new_gap < 1) {
        return ERROR; 
    }
    int count = ((uintptr_t)(UP_TO_PAGE(addr) - UP_TO_PAGE(active->brk)) >> PAGESHIFT);
    // Check whether there is enough physical memory.
    if (count > free_ll.count) {
        return ERROR;
    } 
    int i;
    unsigned int curr_page = (UP_TO_PAGE(active->brk) >> PAGESHIFT) % PAGE_TABLE_LEN;
    for (i = 0; i < count; i++) {
        TracePrintf(0, "VPN we use for malloc: %i\n", curr_page);
        (active->page_table0)[curr_page].valid = 1;
        (active->page_table0)[curr_page].kprot = PROT_READ|PROT_WRITE;
        (active->page_table0)[curr_page].uprot = PROT_READ|PROT_WRITE;
        (active->page_table0)[curr_page++].pfn = getFreePage();
    }
    // Note: break is not a part of heap.
    active->brk = (void *)UP_TO_PAGE(addr);
    return 0;
}

/**
 * @brief Kernel call for fork.
 * 
 */
static int KernelFork() {
    // Create a new process: pcb, page table
    // Make an exact copy of the page table of the currently active process's page table 0
    // ContextSwitch will return 2 times and we should return 0 for child and child's pcb for parent
    pcb *childPCB = init_pcb();
    childPCB->brk = active->brk;
    childPCB->min_sp = active->min_sp;

    // Update some values in parent's pcb.
    if (active->statusQ == NULL) {
        active->statusQ = malloc(sizeof(queue));
    }

    if (active->childrenQ == NULL) {
        active->childrenQ = malloc(sizeof(queue));
    }


    TracePrintf(0, "Created a child with pid %i\n", childPCB->pid);
    if (copyMemoryImage(childPCB, active) == -1) {
        TracePrintf(0, "Copy memory image failed");
        return ERROR;
    }
    ContextSwitch(cloneContext, &childPCB->ctx, (void *)active, (void *)childPCB);
    if(active->pid == childPCB->pid) {
        // TracePrintf(0, "Print a blocked queue when a child is running (pid = %i):\n", active->pid);
        //printQueue(blockedQ);
        // TracePrintf(0, "Print a ready queue when a child is running (pid = %i):\n", active->pid);
        //printQueue(readyQ);
        return 0;
    } else {
        // Add a child to the ready queue.
        enqueue(childPCB, &readyQ);
        // Add a child to the children queue
        childPCB->parent = active;
        addChild(childPCB, active->childrenQ);
        // TracePrintf(0, "Print a blocked queue when a parent is running (pid = %i):\n", active->pid);
        // //printQueue(blockedQ);
        // TracePrintf(0, "Print a ready queue when a parent is running (pid = %i):\n", active->pid);
        //printQueue(readyQ);
        return childPCB->pid;
    }
}

static int KernelExec(ExceptionInfo *info) {
    // active already exists and has a page table and other stuff

    int ret_val = LoadProgram((char *)info->regs[1], (char **)info->regs[2], info, active->page_table0, free_ll, active);
    if (ret_val == -1) {
        return ERROR;
    } else if (ret_val == -2) {
        // terminate process
        KernelExit(ERROR);
        return ERROR;
    } else {
        return 0;
    }

}

static void KernelExit(int status) {
    // Call "terminate process" that will:
    // 1) add all valid pages in this page table to a free list.
    // 2) "unmap" page_table0 address 
    // 3) free a physical page for this process's page table0
    // 4) if statusQ != NULL, iterate over all children in StatusQ and free their PCB's
    // 5) save exit status to this process's PCB (don't free it yet!) 
    // 6) Go to its parent pcb, and enqueue process's pcb to the statusQueue of its parent.
    // 7) Context switch (use a terminateSwitch function) to the next ready process in ready queue or idle otherwise
    // 8) check counts of all queues, if there are no processes left, halt the kernel!!!

    active->status = status;
    if(active->parent != NULL && active->parent->waiting) {
            TracePrintf(0, "Parent is done waiting. It's on readyQ now\n");
            enqueue(active->parent, &readyQ);
            active->parent->waiting = false;
    }
    // Take the next process from the ready queue.
    pcb *nextReady = dequeue(&readyQ);
    // Context switch to this process.
    if (nextReady != NULL) {
        // There is the next non-idle procees that we can switch to!
        // TracePrintf(0, "Print a blocked queue before context switch:\n");
        //printQueue(blockedQ);
        // TracePrintf(0, "Print a ready queue before context switch:\n");
        //printQueue(readyQ);
        ContextSwitch(terminateSwitch, &active->ctx, (void *)active, (void *)nextReady);
    } else if (blockedQ.count != 0) {
        // If there are some blocked processes, switch to an idle process!
        ContextSwitch(terminateSwitch, &active->ctx, (void *)active, (void *)&idle_PCB);
    } else {
        // There no more ready/blocked/I/O processes waiting, so we can Halt() an entire kernel.
        TracePrintf(0, "Idle is the last process running. I am going to halt\n");
        Halt();
    }
    // TODO: check queue for I/O and waiting for children!!!!!!!!!!!!!
    
}

static void addChild(pcb* proc, queue* queue) {
    proc->nextChild = NULL;
    if(queue->count == 0) {
        // This is the first element in the queue.
        queue->head = proc;
        queue->tail = proc;
    }
    else {
        // This is not the first element in the queue.
        queue->tail->nextChild = proc;
        queue->tail = proc;
    }
    queue->count++;
    TracePrintf(0, "Print all children after adding (child pid = %i):\n", proc->pid);
    printChildren(*(active->childrenQ));
}

// Renamed for consistency.
static int removeChild(queue* queue, pcb* elem) {
    pcb* currElem = queue->head;
    pcb* prevElem = NULL;
    while(currElem != NULL && currElem->pid != elem->pid) {
        prevElem = currElem;
        currElem = currElem->nextChild;
    }
    if(currElem == NULL) {
        return -1;
    }
    else {
        if(queue->count == 1) {
            queue->head = NULL;
            queue->tail = NULL;
        }
        else if (currElem == queue->head) {
            queue->head = currElem->nextChild;
        }
        else if(currElem == queue->tail) {
            queue->tail = prevElem; 
            prevElem->nextChild = NULL;
        } else {
            prevElem->nextChild = currElem->nextChild;
        }
        
        queue->count--;
        
        return 0;
    }
}

/**
 * @brief 
 * 
 * @param status 
 * @return On success returns pid and puts status into status
 */
static int KernelWait(int *status) {
    if(active->statusQ->count == 0 && active->childrenQ->count == 0) {
        return ERROR;
    }
    else if(active->statusQ->count > 0) {
        pcb *deadChild = dequeue(active->statusQ);
        *status = deadChild->status;
        int childPid = deadChild->pid;
        free(deadChild);
        return childPid;
    }
    else {
        active->waiting = true;
        TracePrintf(0, "Process %i is waiting for a child to terminate\n", active->pid);
        runNextProcess();//we will come back once our child is free
        //here, we know there is a child to reap
        pcb *deadChild = dequeue(active->statusQ);
        *status = deadChild->status;
        int childPid = deadChild->pid;
        free(deadChild);
        return childPid;
    }

}

static int KernelRead(ExceptionInfo *info) {
    // Check all params (create separate function)
    int term = info->regs[1];
    if (term < 0 || term >= NUM_TERMINALS) {
        return ERROR;
    }
    // Check buf!!!
    void *buf = (void *)info->regs[2];
    int len = info->regs[3];
    if (len < 0) return ERROR;
    if (len == 0) return 0;
    if (terminals[term].read_data->contents != NULL) {
        int ret_len = len;
        memcpy(buf, terminals[term].read_data->contents, len);
        int ret_len = MIN(len, terminals[term].read_data->len);
        if (ret_len == terminals[term].read_data->len) {
            // Can free this line struct
            free(removeReadData(&terminals[term]));
        } else {
            // Move the pointer for contents
            terminals[term].read_data->contents += ret_len;
            // Decrement available length
            terminals[term].read_data->len -= ret_len;
        }
    } else {
        // Block a reader until TtyReceive interrupt happens.
        
    }
    return 0;
}

static int KernelWrite(ExceptionInfo *info) {
    (void)info;
    return 0;
}
// Returns a new/old head.
static line *addData(line *elem, line *head) {
    elem->next = NULL;
    if (head == NULL) {
        return elem;
    } else {
        line *cur = head;
        while (cur->next != NULL) {
            cur = cur->next;
        }
        cur->next = elem;
        return head;
    }
}
// Return the read data.
static line *removeReadData(term *t) {
    if (t->read_data == NULL) {
        return NULL;
    }
    line *ret_val = t->read_data;
    t->read_data = t->read_data->next;
    return ret_val;
}
// Return the write data.
static line *removeWriteData(term *t) {
    if (t->write_data == NULL) {
        return NULL;
    }
    line *ret_val = t->write_data;
    t->write_data = t->write_data->next;
    return ret_val;
}


