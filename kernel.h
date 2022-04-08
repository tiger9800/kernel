struct free_pages {
    int count;
    struct physical_frame *head;
};

struct physical_frame {
    struct physical_frame *next;
};

typedef struct pcb {
    pid_t pid;
    SavedContext *ctx;
    struct pte *page_table0;
    int delay_clock;
    struct pcb *parent;
    struct pcb *next;
    // unsigned long brk;
    // int n_child;
    // StatusQueue *statusQ;
} pcb;

int LoadProgram(char *name, char **args, ExceptionInfo* info, struct pte* region0, struct free_pages free_pages);
void freePage(struct pte* newPte);
unsigned int getFreePage(); 