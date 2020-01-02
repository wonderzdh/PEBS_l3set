/* Dump simple PEBS data from kernel driver */
#include <unistd.h>
#include <sys/mman.h>
#include <sys/fcntl.h>
#include <sys/ioctl.h>
#include <stdio.h>
#include <poll.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <getopt.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>


#include "simple-pebs.h"
#include "dump-util.h"

#define err(x) perror(x), exit(1)

void dump_data(int cpunum, u64 *map, int num)
{
	int i;
	
	printf("dump %d\n", num);
	for (i = 0; i < num; i++)
		printf("%d: %lx\n", cpunum, map[i]);
}

static void usage(void)
{
	fprintf(stderr, "Usage: dumper [-b]\n"
		"-b binary dump\n");
	exit(1);
}

int main(int ac, char **av)
{
	int size = get_size();
	int ncpus = sysconf(_SC_NPROCESSORS_ONLN);
	void *map[ncpus];
	struct pollfd pfd[ncpus];
	int opt;
	int i;

	for (i = 0; i < ncpus; i++)
		open_cpu(&map[i], i, &pfd[i], size);
	
	int target = 7;
	FILE *outfile;
	char benchname[100];
	snprintf(benchname, 90, "%s_trace_len.txt",av[1]);
	printf("dumper filename: %s\n",benchname);
	outfile = fopen(benchname,"w");
	if(outfile == NULL){
		exit(-1);
	}
	int _count = 0;
	for(;;){
		usleep(200000);
		if(poll(pfd, ncpus, -1)<0)
			perror("poll");
		if(pfd[target].revents & POLLIN){
			int len=0;

			if(ioctl(pfd[target].fd, SIMPLE_PEBS_GET_OFFSET, &len) < 0){
				perror("SIMPLE_PEBS_GET_OFFSET");
				continue;
			}
			/*
  			printf("%d\n",len/8);			
			dump_data(target, map[target], len / sizeof(u64));
			*/
			if (ioctl(pfd[target].fd, SIMPLE_PEBS_RESET, 0) < 0) {
				perror("SIMPLE_PEBS_RESET");
				continue;
			}
			_count+=(len/8);
			fprintf(outfile,"%d %d\n",len/8,_count);	
		}
	}
	fclose(outfile);
	return 0;
}
