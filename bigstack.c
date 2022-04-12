#include <comp421/yalnix.h>

void
force(char *addr)
{
    *addr = 42;
}

int
main()
{
    char big_buffer[20*1024];
    int foo;
    int i;

    (void)foo;
    foo = 42;
    for (i = 0; i < (int)sizeof(big_buffer); i++) 
	force(big_buffer + i);

    Exit(0);
}
