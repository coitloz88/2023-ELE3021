#include "types.h"
#include "stat.h"
#include "user.h"

// to test 129, 130 lock interrupt

int main(int argc, char *argv[]){
	printProcessInfo();

	int pid1;
	pid1 = fork();

	if(pid1 == 0){
		// child process
		printf(1, "@@ yield to other process\n");
		yield();
		
		printf(1, "@@ before changing priority");	
		printProcessInfo();
			
		setPriority(getpid(), 1);
		
		printf(1, "@@ after changing priority to 1, priority");
		printProcessInfo();
		

		printf(1, "@@ exit in child process\n");
		exit();
	} 

	else {
		// parent process
		for(int i = 0; i < 1000; i++){
			if(i % 100 == 0)
				printf(1, "** parent iteration...\n");
		}
		wait();
		printf(1, "** wait over\n");
		printf(1, "** print process info ");
		printProcessInfo();
		printf(1, "** getLevel: %d\n", getLevel());
	}

	printf(1, "[syscall test] test done!\n");

	exit();
}
