#include <comp421/hardware.h>
#include <comp421/yalnix.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
int x = 5;
int y = 34;
char s2[10] = "Help!";
int main(int argc, char **argv) {

    (void)argc;
    (void)argv;
    // int i;

    // for (i = 0; i < argc; i++) {
	// fprintf(stderr, "argv[%d] = %p", i, argv[i]);
	// fprintf(stderr, " = '%s'\n", argv[i]);
    //}
        // TracePrintf(0, "Init is working\n");
        // int z = y - x;
        // TracePrintf(0, "Result is %i\n", z);

        // //Test Brk()
        // char *s = malloc(sizeof(char)*20000);
        // if (s == NULL) {
        //     TracePrintf(0, "Pointer is null!\n");
        // } else {
        //     strcpy(s, s2);
        //     TracePrintf(0, "Malloced string: %s\n", s);
        // }

        //Test GetPid()
        (void)argc;
        TracePrintf(0, "PID of the current process: %i\n", GetPid());

        // Test Delay()
        // TracePrintf(0, "1st delay return value is %i pid=%i\n", Delay(5), GetPid());
        // int pid1 = Fork();
        // if(pid1 == 0) {
        //     TracePrintf(0, "Fork1: I'm child: %i\n", GetPid());
        //     TracePrintf(0, "I'm starting a new program\n");
        //     // Exec(argv[1], argv + 1);
        // } else if (pid1 > 0) {
        //     TracePrintf(0, "Fork1: I'm parent: %i\n", GetPid());
        //     int pid2 = Fork();
        //     if(pid2 == 0) {
        //         TracePrintf(0, "Fork2: I'm child: %i\n", GetPid());
        //         // Exec(argv[1], argv + 1);
        //     } else if (pid2 > 0) {
        //         TracePrintf(0, "Fork2: I'm parent: %i\n", GetPid());
        //         Delay(8);
        //         TracePrintf(0, "Delay ended for pid=%i\n", GetPid());
        //     } else {
        //         TracePrintf(0, "Fork failed\n");
        //     }
        // } else {
        //     TracePrintf(0, "Fork failed\n");
        // }
        int p1 = Fork();
        if(p1 == 0) {
            TracePrintf(0, "Fork1: I'm child: %i\n", GetPid());
            TracePrintf(0, "I'm starting a new program\n");
            // Exec(argv[1], argv + 1);
        } else if (p1 > 0) {
            TracePrintf(0, "Fork1: I'm parent: %i\n", GetPid());
        } else {
            TracePrintf(0, "Fork failed\n");
        }
        p1 = Fork();
        if(p1 == 0) {
            TracePrintf(0, "Fork1: I'm child: %i\n", GetPid());
            TracePrintf(0, "I'm starting a new program\n");
            // Exec(argv[1], argv + 1);
        } else if (p1 > 0) {
            TracePrintf(0, "Fork1: I'm parent: %i\n", GetPid());
        } else {
            TracePrintf(0, "Fork failed\n");
        }
        p1 = Fork();
        if(p1 == 0) {
            TracePrintf(0, "Fork1: I'm child: %i\n", GetPid());
            TracePrintf(0, "I'm starting a new program\n");
            // Exec(argv[1], argv + 1);
        } else if (p1 > 0) {
            TracePrintf(0, "Fork1: I'm parent: %i\n", GetPid());
        } else {
            TracePrintf(0, "Fork failed\n");
        }
        p1 = Fork();
        if(p1 == 0) {
            TracePrintf(0, "Fork1: I'm child: %i\n", GetPid());
            TracePrintf(0, "I'm starting a new program\n");
            // Exec(argv[1], argv + 1);
        } else if (p1 > 0) {
            TracePrintf(0, "Fork1: I'm parent: %i\n", GetPid());
        } else {
            TracePrintf(0, "Fork failed\n");
        }

        p1 = Fork();
        if(p1 == 0) {
            TracePrintf(0, "Fork1: I'm child: %i\n", GetPid());
            TracePrintf(0, "I'm starting a new program\n");
            // Exec(argv[1], argv + 1);
        } else if (p1 > 0) {
            TracePrintf(0, "Fork1: I'm parent: %i\n", GetPid());
        } else {
            TracePrintf(0, "Fork failed\n");
        }


        // p1 = Fork();
        // if(p1 == 0) {
        //     TracePrintf(0, "Fork1: I'm child: %i\n", GetPid());
        //     TracePrintf(0, "I'm starting a new program\n");
        //     // Exec(argv[1], argv + 1);
        // } else if (p1 > 0) {
        //     TracePrintf(0, "Fork1: I'm parent: %i\n", GetPid());
        // } else {
        //     TracePrintf(0, "Fork failed\n");
        // }

        // if(Fork() == 0) {
        //     TracePrintf(0, "Fork2: I'm child: %i\n", GetPid());
        //     TracePrintf(0, "I'm starting a new program\n");
        //     // Exec(argv[1], argv + 1);
        // } else {
        //     TracePrintf(0, "Fork2: I'm parent: %i\n", GetPid());
        // }

        // // Won't work if uncomment this!!
        // if(Fork() == 0) {
        //     TracePrintf(0, "Fork3: I'm child: %i\n", GetPid());
        //     TracePrintf(0, "I'm starting a new program\n");
        //     // Exec(argv[1], argv + 1);
        // } else {
        //     TracePrintf(0, "Fork3: I'm parent: %i\n", GetPid());
        // }

        // // Won't work if uncomment this!!
        // if(Fork() == 0) {
        //     TracePrintf(0, "Fork4: I'm child: %i\n", GetPid());
        //     TracePrintf(0, "I'm starting a new program\n");
        //     // Exec(argv[1], argv + 1);
        // } else {
        //     TracePrintf(0, "Fork4: I'm parent: %i\n", GetPid());
        // }
        TracePrintf(0, "process %i is about to exit\n", GetPid());
        // TracePrintf(0, "2nd delay return value is %i for pid=%i\n", Delay(2), GetPid());
        // Pause();
        Exit(0);
}