#include <unistd.h>
#include <comp421/yalnix.h>
#include <stdio.h>
#include <comp421/hardware.h>

int
main()
{
    int i = write(2, "init!\n", 6);
    TtyPrintf(TTY_CONSOLE, "Output is %i\n", i);
    Exit(0);
}