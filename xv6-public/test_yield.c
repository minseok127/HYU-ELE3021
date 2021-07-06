#include "types.h"
#include "stat.h"
#include "user.h"

/* Create child process with fork(),
 * then print "Parent" in parent process and "Child" in child process
 * Also, switch each other with yield() system call */
int main(int argc, char* argv[])
{
	const int loopCount = 50;

	int pid = fork();
	
	if (pid < 0) {
		printf(1, "err : fork() return -1 at main\n");
	}
	else if (pid > 0) {
		for (int i = 0; i < loopCount; i++) {
			printf(1, "Parent\n");
			//yield(); // Release CPU
		}
	
		wait(); // Wait unitl child process finish
	}
	else {
		
		for (int i = 0; i < loopCount; i++) {
			printf(1, "Child\n");
			//yield(); // Release CPU
		}
	}

	exit();
}
