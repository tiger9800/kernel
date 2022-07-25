# kernel

Completed by:
Yuliia Suprun (ys70)
Davyd Fridman (df21)
## DESCRIPTION

This is a program that implements a Yalnix operating system.

## DESIGN NOTES
- We created a special data structure for each process's pcb, which contains its pid, saved context, the virtual address of its page table 0, a pointer to the next element in the queue for this process, etc.
- Each process can be in 4 possible states:
1) Active
We keep a global pointer to the active process's pcb.
2) Ready: keep a separate queue.
3) Blocked by Delay(): keep a separate queue.
4) Blocked by I/O operations: keep 2 separate queues for readers and writers for each of the terminals.
5) Waiting: such processes have a field "waiting"=1 in their pcbs.

- Our FIFO queue data structure have three useful fields: head, tail, and count.

- We also created a linked list of children and put a link to this list in parent's pcb.

- We created a linked list of "lines" that are read or are written to the terminal. Each "line" contains a buffer, a length, and a pointer to the next line in teh linked list.

- We also created a data structure called "term" that contains all important data about terminal: its queues of writers and readers as well as linked lists of read-lines or write_lines. 

NOTE: We decided to not free our malloced data structures before Halt() operations because Halt() automatically "releases" all the memory.

## TESTING

- We did a thorough testing of our kernel. In particular, this folder includes a lot of tests that show that our kernel has an expected behavior when user processes perform kernel calls.


## TO BUILD 

To build Yalnix

- cd to our folder and type "make"

To clean up

- cd to our folder and type "make clean"

## TO RUN

- ./yalnix <program_name> <params>
- All see traces use "-lk 1" after ./yalnix.