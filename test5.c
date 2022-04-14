#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>

#include <comp421/yalnix.h>
#include <comp421/hardware.h>

char line[TERMINAL_MAX_LINE];

int
main()
{
    int i;
    int pid;

    if ((pid = Fork()) < 0) {
	fprintf(stderr, "Can't Fork!\n");
	Exit(1);
    }

    if (pid != 0) {
	for (i = 0; i < 3; i++) {
	    sprintf(line, "1. Process %i line (it's a parent)\n", GetPid());
	    TtyWrite(0, line, strlen(line));
	}
    } else {
	for (i = 0; i < 3; i++) {
        sprintf(line, "1. Process %i line (it's a child)\n", GetPid());
	    TtyWrite(1, line, strlen(line));
	}
    }

    if ((pid = Fork()) < 0) {
	fprintf(stderr, "Can't Fork!\n");
	Exit(1);
    }

    if (pid != 0) {
	for (i = 0; i < 3; i++) {
	    sprintf(line, "2. Process %i line (it's a parent)\n", GetPid());
	    TtyWrite(1, line, strlen(line));
	}
    } else {
	for (i = 0; i < 3; i++) {
	    sprintf(line, "2. Process %i line (it's a child)\n", GetPid());
	    TtyWrite(3, line, strlen(line));
	}
    }

    // if ((pid = Fork()) < 0) {
	// fprintf(stderr, "Can't Fork!\n");
	// Exit(1);
    // }

    // if (pid != 0) {
	// for (i = 0; i < 3; i++) {
	//     sprintf(line, "3. Process %i line (it's a parent)\n", GetPid());
	//     TtyWrite(0, line, strlen(line));
	// }
    // } else {
	// for (i = 0; i < 3; i++) {
	//     sprintf(line, "3. Process %i line (it's a child)\n", GetPid());
	//     TtyWrite(0, line, strlen(line));
	// }
    // }
    sprintf(line, "Process %i is about to exit!!!\n", GetPid());
    TtyWrite(0, line, strlen(line));
    Exit(0);
}
