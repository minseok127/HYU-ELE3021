#include "types.h"
#include "stat.h"
#include "user.h"
#include "fcntl.h"

#define NUM (4218368)

int main(void) {
	printf(1, "start : %d\n", uptime());
	int c = 1;
	//int c[256];
	//int pid;
	int fd;

	/*if ((pid = fork()) == -1) {
		exit();
	}
	else if (pid > 0) {
		fd = open("sync_test1.bin", O_RDWR | O_CREATE);
		
		for (int i = 0; i < NUM; i++) {
			write(fd, c, sizeof(int) * 256);
			//sync();
			//printf(1, "parent log num : %d\n", get_log_num());
			//c++;
		}
		
		wait();
	}
	else {
		fd = open("sync_test2.bin", O_RDWR | O_CREATE);

		for (int i = 0; i < NUM; i++) {
			write(fd, c, sizeof(int) * 256);
			//sync();
			//printf(1, "child log num : %d\n", get_log_num());
			//c++;
		}

		exit();
	}*/

	fd = open("sync_test.bin", O_RDWR | O_CREATE);

	for (int i = 0; i < NUM; i++) {
		write(fd, &c, sizeof(int));
		c++;
	}

	printf(1, "end : %d\n", uptime());

	exit();
}
