#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>

#define IOSETCONT 26625

int main(int argc, char *argv[])
{
	int fd;
	int cont;

	cont = atoi(argv[2]);
	fd = open(argv[1], O_RDWR);
	if(fd == -1)
	{
		printf("can't open device\n");
		return 1;
	}
	ioctl(fd, IOSETCONT, cont);
	close(fd);
	return 0;
}
