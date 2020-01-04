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

/*LLC slice selection hash function of Intel 8-core processor*/
#define hash_0 0x1B5F575440
#define hash_1 0x2EB5FAA880
#define hash_2 0x3CCCC93100

uint64_t rte_xorall64(uint64_t ma) {
        return __builtin_parityll(ma);
}

uint8_t calculateSlice(uint64_t pa) {
        uint8_t sliceNum=0;
        sliceNum= (sliceNum << 1) | (rte_xorall64(pa&hash_2));
        sliceNum= (sliceNum << 1) | (rte_xorall64(pa&hash_1));
        sliceNum= (sliceNum << 1) | (rte_xorall64(pa&hash_0));
        return sliceNum;
}

void dump_data(int cpunum, u64 *map, int num)
{
	int i;
	
	printf("dump %d\n", num);
	for (i = 0; i < num; i++)
		printf("%d: %lx\n", cpunum, map[i]);
}

int open_pagemap(pid_t pid)
{
	char pagemap_file[200];
	int pagemap_fd;

	snprintf(pagemap_file, sizeof(pagemap_file), "/proc/%ju/pagemap", (uintmax_t)pid);
	pagemap_fd = open(pagemap_file, O_RDONLY);
	if (pagemap_fd < 0) {
		return -1;
	}
	else return pagemap_fd;
}

int virt_to_phys_user(uintptr_t *paddr, int fd, uintptr_t vaddr){
	uint64_t data;
	uint64_t vpn;

	vpn = vaddr / 4096;
	pread(fd, &data, sizeof(data), vpn * sizeof(data));

	*paddr = ((data & (((uint64_t)1 << 54) - 1)) * 4096) + (vaddr % 4096);
	return 0;
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
	pid_t pid;
	int pagemap_fd;

	for(;;){
		usleep(2000000);
		if(poll(pfd, ncpus, -1)<0)
			perror("poll");
		if(pfd[target].revents & POLLIN){
	
			if(ioctl(pfd[target].fd, GET_PID, &pid) < 0){
				perror("GET_PID");
				continue;
			}
			printf("pid:%d\n",pid);
			pagemap_fd = open_pagemap(pid);
			
			int len=0;
			if(ioctl(pfd[target].fd, SIMPLE_PEBS_GET_OFFSET, &len) < 0){
				perror("SIMPLE_PEBS_GET_OFFSET");
				continue;
			}
			/*get physical address and count*/
			unsigned long long paddr;
			unsigned long long *vaddrset=(u64*) map[target];
			int j;
			for( j=0; j<len/8; j++){
				virt_to_phys_user(&paddr, pagemap_fd, vaddrset[j]);
				printf("%lx\n",paddr);
			}

			if (ioctl(pfd[target].fd, SIMPLE_PEBS_RESET, 0) < 0) {
				perror("SIMPLE_PEBS_RESET");
				continue;
			}
		}
	}
	return 0;
}
