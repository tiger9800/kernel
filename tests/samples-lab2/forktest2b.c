#include <stdio.h>
#include <comp421/yalnix.h>

void
recurse(char *who, int i)
{
    printf("%s %d\n", who, i);
    Delay(1);
    if (i == 0)
    {
	printf("Done with recursion\n");
	return;
    }
    else
	recurse(who, i - 1);
}

int
main(int argc, char **argv)
{
    int pid;

    setbuf(stdout, NULL);

    printf("This program is a test primarily of Fork, Delay, and\n");
    printf("some user process stack growth\n");
    
    if ((pid = Fork()) == 0)
    {
	printf("CHILD\n");
	recurse("child", 20);
    }
    else
    {
	printf("PARENT: child pid = %d\n", pid);
	recurse("parent", 20);
    }

    printf("FORK2 done.\n");
    Exit(0);
}
