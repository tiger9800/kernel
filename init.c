#include <comp421/hardware.h>
#include <comp421/yalnix.h>
#include <stdlib.h>
#include <string.h>
// int x = 5;
// int y = 34;
// char s2[10] = "Help!";
int main() {
    while(1) {
        // TracePrintf(0, "Init is working\n");
        // int z = y - x;
        // TracePrintf(0, "Result is %i\n", z);

        // // Test Brk()
        // char *s = malloc(sizeof(char)*20000);
        // if (s == NULL) {
        //     TracePrintf(0, "Pointer is null!\n");
        // } else {
        //     strcpy(s, s2);
        //     TracePrintf(0, "Malloced string: %s\n", s);
        // }

        // // Test GetPid()
        // TracePrintf(0, "PID of the current process: %i\n", GetPid());


        // Test Delay()
        // TracePrintf(0, "1st delay return value is %i\n", Delay(5));
        // TracePrintf(0, "2nd delay return value is %i\n", Delay(-1));
        // TracePrintf(0, "3rd delay return value is %i\n", Delay(2));
        if(Fork() == 0) {
            TracePrintf(0, "I'm child: %i\n", GetPid());
        } else {
            TracePrintf(0, "I'm parent: %i\n", GetPid());
        }
        Pause();
    }
    return 0;
}