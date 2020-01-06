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

unsigned long long TARGETSET=705;

int slice_set_count[2048][8]={0};
int single_set_count[8]={0};

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
	int x=0;
	int _count=0;
	unsigned long long vaddr_filter = (TARGETSET & 0x3f)<<6;

	for(; x<1000; x++){
		usleep(200000);
		if(poll(pfd, ncpus, -1)<0)
			perror("poll");
		if(pfd[target].revents & POLLIN){
			printf("ok\n");		
			if(x==0){	
				if(ioctl(pfd[target].fd, GET_PID, &pid) < 0){
					perror("GET_PID");
					continue;
				}
				pagemap_fd = open_pagemap(pid);
			}
			int len=0;
			if(ioctl(pfd[target].fd, SIMPLE_PEBS_GET_OFFSET, &len) < 0){
				perror("SIMPLE_PEBS_GET_OFFSET");
				continue;
			}
			printf("len:%d\n",len/8);
			/*get physical address and count*/
			unsigned long long paddr;
			unsigned long long *vaddrset=(u64*) map[target];
			int j;
			for(j=0; j<len/8; j++){
	/*			if((vaddrset[j] & 0xfc0)- vaddr_filter == 0){
				virt_to_phys_user(&paddr, pagemap_fd, vaddrset[j]);
					if(((paddr >> 6) & 0x7ff) - TARGETSET == 0){
						single_set_count[calculateSlice(paddr)]++;
					}
	*/
				virt_to_phys_user(&paddr, pagemap_fd, vaddrset[j]);
				slice_set_count[(paddr >> 6) & 0x7ff][calculateSlice(paddr)]++;
	
				}
			}	
			if (ioctl(pfd[target].fd, SIMPLE_PEBS_RESET, 0) < 0) {
				perror("SIMPLE_PEBS_RESET");
				continue;
			}
		}
/*		if(x%100==0){
			printf("dump time:%d\n",x);
			int y=0;
			for(y=0;y<2048;y++){
				printf("set%d ",y);
				for(i=0;i<8;i++){
					if(i!=7)printf("%d ",slice_set_count[y][i]);
					else	printf("%d\n",slice_set_count[y][i]);
				}
			}
			memset(slice_set_count,0,sizeof(int)*2048*8);
		}
*/
	
	close(pagemap_fd);
/*
	for(x=0;x<8;x++) printf("%d\n",single_set_count[x]);
*/

	FILE *result;
	result = fopen("result.txt","w");
	for(x=0;x<2048;x++){
		fprintf(result,"set%d ",x);
		for(i=0;i<8;i++){
			if(i!=7)fprintf(result,"%d ",slice_set_count[x][i]);
			else	fprintf(result,"%d\n",slice_set_count[x][i]);
		}
	}
	fclose(result);

	return 0;
}
