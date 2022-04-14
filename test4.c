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
    int len;

    fprintf(stderr, "1. Doing TtyRead for process %i...\n", GetPid());
    len = TtyRead(0, line, sizeof(line));
    fprintf(stderr, "Done with TtyRead: len %d\n", len);
    fprintf(stderr, "line '");
    fwrite(line, len, 1, stderr);
    fprintf(stderr, "'\n");
    int status;
    TracePrintf(0, "About to wait!!!\n");
    int pid = Wait(&status);
    if (pid < 0) {
        fprintf(stderr, "Pid is bad\n");
    }
    
    // fprintf(stderr, "About to exit (pid=%i)!!\n", GetPid());
    Exit(0);
}
