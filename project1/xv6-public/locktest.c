#include "types.h"
#include "stat.h"
#include "user.h"

// for int 129, 130 instruction

int main(int argc, char *argv[]){
	printf(1, "Creating process id: %d\n", getpid());
    
	int pw = 2020028586;

	if(argc > 1){
		pw = atoi(argv[1]);
		printf(1, "if statement runs, input pw: %d\n", pw);
	}

	int pid1;
	pid1 = fork();

	if(pid1 == 0){
		// schedulerLock(pw);
		printf(1, "** locked process id: %d\n", getpid());
		sleep(50);
		printf(1, "slept for 100 in child locked process...\n");
		// schedulerUnlock(pw);
		printf(1, "** scheduler unlocked!\n");
		exit();
	} 

	else {
		printf(1, "\n** processs outside of locked process again: %d\n", getpid());
		sleep(300);
		printf(1, "slept in parent, exit in parent process\n");
		exit();
	}

	printf(1, "[lock input] test done!");

	exit();
}
