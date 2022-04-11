#include <stdio.h>
#include <stdlib.h>
#include <comp421/yalnix.h>
#include <comp421/hardware.h>

void
foo(int depth)
{
    char array[65532];
    int i;

    TracePrintf(0, "blowstack foo: depth %d\n", depth);

    fprintf(stderr,"%d ",depth);
    fflush(stderr);
    TracePrintf(0, "%d ========================\n", depth);
    for (i = 0; i < (int)sizeof(array); i++) {
        array[i] = 'a';
        // TracePrintf(0, "%i/%i %c \n", i, (int)sizeof(array), array[i]);
    }
    TracePrintf(0, "%d ======================== done!\n", depth);
    if (depth == 1) return;

    foo(depth-1);
}

int
main(int argc, char **argv)
{
    (void)argc;
    int num = atoi(argv[1]);

    TracePrintf(0, "blowstack initially calling foo depth %d\n", num);
    foo(num);
    TracePrintf(0, "About to exit!\n");
    Exit(0);
}
