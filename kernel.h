#include <stdbool.h>

struct free_pages {
    int count;
    struct physical_frame *head;
};

struct physical_frame {
    struct physical_frame *next;
};

// typedef struct child child;
typedef struct queue queue;
typedef struct pcb pcb;
struct pcb {
    int pid;
    SavedContext ctx;
    struct pte *page_table0;
    int delay_offset;
    struct pcb *parent;
    struct pcb *next;
    struct pcb *nextChild;
    void *brk;
    void *min_sp;
    queue *statusQ;
    int status;
    queue *childrenQ;
    bool waiting;
    int numToRead;
};

// struct child {
//     struct child *next;
//     struct child *prev;
//     pcb *pcb;
// };

struct queue {
    pcb *head;
    pcb *tail;
    int count;
};

typedef struct line {
    char *init_ptr;
    char *content;
    int len;
    struct line *next;
} line;

typedef struct term {
    queue readQ;
    queue writeQ;
    line *read_data;
    line *write_data;
} term;

int LoadProgram(char *name, char **args, ExceptionInfo *info, struct pte *region0, struct free_pages free_pages, pcb *newPCB);
void freePage(struct pte *newPte, int region);
int getFreePage(); 