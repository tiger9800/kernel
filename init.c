#include <comp421/hardware.h>
#include <stdlib.h>
#include <string.h>
int x = 5;
int y = 34;
char s2[10] = "Hello";
int main() {
    while(1) {
        TracePrintf(0, "Init is working\n");
        int z = y - x;
        TracePrintf(0, "Result is %i\n", z);
        char *s = malloc(sizeof(char)*5);
        strcpy(s, s2);
        TracePrintf(0, "Malloced string: %s\n", s);
        Pause();
    }
    return 0;
}