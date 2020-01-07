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
#include <sys/wait.h>
#include <time.h>

#include "simple-pebs.h"
#include "dump-util.h"

#define err(x) perror(x), exit(1)

/*LLC slice selection hash function of Intel 8-core processor*/
#define hash_0 0x1B5F575440
#define hash_1 0x2EB5FAA880
#define hash_2 0x3CCCC93100

unsigned long long TARGETSET=705;

int quit_flag=0;

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
	fprintf(stderr, "Usage: bubble COMMAND [output path]\n-a\tCount all LLC slices/sets\n");
	exit(1);
}

void handler(int sig){
	quit_flag = 1;
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
	int phase_count=0;
	unsigned long long vaddr_filter = (TARGETSET & 0x3f)<<6;
	
	int len = 0;
	char path[500];
	char cpath[500];

	int mode=0;
	while ((opt = getopt(ac, av, "a:b:c:")) != -1) {
		switch(opt){
			case 'a':
				mode=0;
				snprintf(path,300,"%s",optarg);
				break;
			case 'b':
				mode=1;
				snprintf(path,300,"%s",optarg);
				break;
			case 'c':
				mode=2;
				snprintf(path,300,"%s",optarg);
				break;

			default:
				usage();
				exit(1);
				break;
		}
	}

	signal(SIGINT, handler);

	if(mode==0){
		struct timespec start_time,cur_time;
		long long int time_diff=0;
		clock_gettime(CLOCK_MONOTONIC,&start_time);

		while(!quit_flag){
			usleep(200000);
			if(poll(pfd, ncpus, -1)<0)
				perror("poll");
			if(pfd[target].revents & POLLIN){
				if(ioctl(pfd[target].fd, SIMPLE_PEBS_GET_OFFSET, &len) < 0){
					perror("SIMPLE_PEBS_GET_OFFSET");
					continue;
				}
				printf("len:%d\n",len/8);
				if(len>80000){
					if(ioctl(pfd[target].fd, GET_PID, &pid) < 0){
						perror("GET_PID");
						continue;
					}
					pagemap_fd = open_pagemap(pid);
					unsigned long long paddr;
					unsigned long long *vaddrset=(u64*) map[target];
					int j;

					for(j=0; j<len/8; j++){
						virt_to_phys_user(&paddr, pagemap_fd, vaddrset[j]);
						slice_set_count[(paddr >> 6) & 0x7ff][calculateSlice(paddr)]++;
	
					}
					close(pagemap_fd);
				}
			}	
			if (ioctl(pfd[target].fd, SIMPLE_PEBS_RESET, 0) < 0) {
				perror("SIMPLE_PEBS_RESET");
				continue;
			}

			clock_gettime(CLOCK_MONOTONIC,&cur_time);
			time_diff =  (cur_time.tv_nsec - start_time.tv_nsec)+ (cur_time.tv_sec-start_time.tv_sec)* 1000000000;
			if (time_diff >= 1000000000){
				
				FILE *result;
				snprintf(cpath,499,"%s_%d_%d.txt",path,phase_count,time_diff/1000000);
				result = fopen(cpath,"w");
				for(x=0;x<2048;x++){
					fprintf(result,"set%d ",x);
					for(i=0;i<8;i++){
						if(i!=7)fprintf(result,"%d ",slice_set_count[x][i]);
						else	fprintf(result,"%d\n",slice_set_count[x][i]);
					}
				}
				fclose(result);
				memset(slice_set_count,0,sizeof(int)*2048*8);
				phase_count++;
				clock_gettime(CLOCK_MONOTONIC,&start_time);
			}
		}
	}

	if(mode==1){/* print result per phase (Billions of instructions).*/
		long long int ins_num_pre=0,ins_num_cur=0,ins_diff=0;
		while(!quit_flag){
			usleep(200000);
			if(poll(pfd, ncpus, -1)<0)
				perror("poll");
			if(pfd[target].revents & POLLIN){
				if(ioctl(pfd[target].fd, SIMPLE_PEBS_GET_OFFSET, &len) < 0){
					perror("SIMPLE_PEBS_GET_OFFSET");
					continue;
				}
				printf("len:%d\n",len/8);
				if(len>160000){
					if(ioctl(pfd[target].fd, GET_PID, &pid) < 0){
						perror("GET_PID");
						continue;
					}
					pagemap_fd = open_pagemap(pid);
					unsigned long long paddr;
					unsigned long long *vaddrset=(u64*) map[target];
					int j;

					for(j=0; j<len/8; j++){
						virt_to_phys_user(&paddr, pagemap_fd, vaddrset[j]);
						slice_set_count[(paddr >> 6) & 0x7ff][calculateSlice(paddr)]++;
	
					}
					close(pagemap_fd);
					
					if (ioctl(pfd[target].fd, SIMPLE_PEBS_RESET, 0) < 0) {
						perror("SIMPLE_PEBS_RESET");
						continue;
					}
				}
			}	
			
			if(ioctl(pfd[target].fd, GET_CURRENT_INSTR, &ins_num_cur) < 0){
				perror("GET_CURRENT_INSTR");
				continue;
			}
		
			ins_diff = ins_num_cur - ins_num_pre;	
			if (ins_diff >= 80000000){
				FILE *result;
				snprintf(cpath,499,"%s_%d_%dm.txt",path,phase_count,ins_diff/1000000);
				result = fopen(cpath,"w");
				for(x=0;x<2048;x++){
					fprintf(result,"set%d ",x);
					for(i=0;i<8;i++){
						if(i!=7)fprintf(result,"%d ",slice_set_count[x][i]);
						else	fprintf(result,"%d\n",slice_set_count[x][i]);
					}
				}
				fclose(result);
				memset(slice_set_count,0,sizeof(int)*2048*8);
				phase_count++;
				ins_num_pre=ins_num_cur;
			}
		}
	}

	if(mode==2){/* print result when target program finishs*/
		while(!quit_flag){
			usleep(200000);
			if(poll(pfd, ncpus, -1)<0)
				perror("poll");
			if(pfd[target].revents & POLLIN){
				if(ioctl(pfd[target].fd, SIMPLE_PEBS_GET_OFFSET, &len) < 0){
					perror("SIMPLE_PEBS_GET_OFFSET");
					continue;
				}
				printf("len:%d\n",len/8);
				if(len>1600000){
					if(ioctl(pfd[target].fd, GET_PID, &pid) < 0){
						perror("GET_PID");
						continue;
					}
					pagemap_fd = open_pagemap(pid);
					unsigned long long paddr;
					unsigned long long *vaddrset=(u64*) map[target];
					int j;

					for(j=0; j<len/8; j++){
						virt_to_phys_user(&paddr, pagemap_fd, vaddrset[j]);
						slice_set_count[(paddr >> 6) & 0x7ff][calculateSlice(paddr)]++;
	
					}
					close(pagemap_fd);
					
					if (ioctl(pfd[target].fd, SIMPLE_PEBS_RESET, 0) < 0) {
						perror("SIMPLE_PEBS_RESET");
						continue;
					}
				}
			}	
		}
		FILE *result;
		snprintf(cpath,499,"%s.txt",path);
		result = fopen(cpath,"w");
		for(x=0;x<2048;x++){
			fprintf(result,"set%d ",x);
			for(i=0;i<8;i++){
				if(i!=7)fprintf(result,"%d ",slice_set_count[x][i]);
				else	fprintf(result,"%d\n",slice_set_count[x][i]);
			}
		}
		fclose(result);	
	}
	return 0;
}
