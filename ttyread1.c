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

    TracePrintf(0, "Running ttyread1\n");
    fprintf(stderr, "Doing TtyRead...\n");
    if(Fork() != 0) {
        len = TtyRead(0, line, sizeof(line));
    }
    fprintf(stderr, "Done with TtyRead: len %d\n", len);
    fprintf(stderr, "line '");
    fwrite(line, len, 1, stderr);
    fprintf(stderr, "'\n");

    Exit(0);
}
