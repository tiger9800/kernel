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
    int len2;
    int len3;

    fprintf(stderr, "1. Doing TtyRead for process %i...\n", GetPid());
    len = TtyRead(0, line, sizeof(line));
    fprintf(stderr, "Done with TtyRead: len %d\n", len);
    fprintf(stderr, "line '");
    fwrite(line, len, 1, stderr);
    fprintf(stderr, "'\n");
    Fork();
    char *line2 = malloc(TERMINAL_MAX_LINE);
    fprintf(stderr, "2. Doing TtyRead for process %i...\n", GetPid());
    len2 = TtyRead(0, line2, sizeof(line2));
    fprintf(stderr, "2. Done with TtyRead: len %d for process %i...\n", len2, GetPid());
    fprintf(stderr, "line '");
    fwrite(line2, len2, 1, stderr);
    fprintf(stderr, "'\n");
    free(line2);
    Fork();
    char *line3 = malloc(TERMINAL_MAX_LINE);
    fprintf(stderr, "3. Doing TtyRead for process %i...\n", GetPid());
    len3 = TtyRead(0, line3, sizeof(line3));
    fprintf(stderr, "3. Done with TtyRead: len %dfor process %i...\n", len3, GetPid());
    fprintf(stderr, "line '");
    fwrite(line3, len3, 1, stderr);
    fprintf(stderr, "'\n");
    free(line3);

    
    fprintf(stderr, "About to exit!!\n");
    Exit(0);
}
