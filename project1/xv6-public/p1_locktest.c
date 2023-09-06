#include "types.h"
#include "stat.h"
#include "user.h"

// to test 129, 130 lock interrupt

int main(int argc, char *argv[]){
	printProcessInfo();

	int pw = 2020028586;

	if(argc > 1){
		pw = atoi(argv[1]);
		printf(1, "if statement runs, input pw: %d\n", pw);
	}

	int pid1;
	pid1 = fork();

	if(pid1 == 0){
		schedulerLock(pw);
		printf(1, "\n@@ locked process id: %d\n", getpid());
		
		for(int iter = 0; iter < 103; iter++){
			printf(1, "@@ iteration in child lock... iter: %d\n", iter);
		}

		schedulerUnlock(pw);
		printf(1, "@@ scheduler unlocked!\n");
		for(int iter = 0; iter < 100; iter++){
			printf(1, "@@ loop in child, iter: %d\n", iter);
			sleep(1);
		}
		exit();
	} 

	else {
		printf(1, "\n** processs outside of locked process again: %d\n", getpid());
		printf(1, "** parent is going to run loop for 100 times)...\n");
		for(int iter = 0; iter < 10; iter++){
			printf(1, "** loop in parent, iter: %d\n", iter);
			sleep(10);
		}
		
		wait();
		printf(1, "** wait over, exit in parent process\n");
		printf(1, "** ");
		printProcessInfo();
	}

	printf(1, "[lock input] test done!\n");

	exit();
}
