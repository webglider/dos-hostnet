#include <stdio.h>				// printf, etc
#include <stdint.h>				// standard integer types, e.g., uint32_t
#include <signal.h>				// for signal handler
#include <stdlib.h>				// exit() and EXIT_FAILURE
#include <string.h>				// strerror() function converts errno to a text string for printing
#include <fcntl.h>				// for open()
#include <errno.h>				// errno support
#include <assert.h>				// assert() function
#include <unistd.h>				// sysconf() function, sleep() function
#define _GNU_SOURCE
#include <sys/mman.h>			// support for mmap() function
#include <math.h>				// for pow() function used in RAPL computations
#include <time.h>
#include <stdbool.h>
#include <sys/time.h>			// for gettimeofday
#include <sys/ipc.h>
#include <sys/shm.h>
#include <immintrin.h>
#include <fcntl.h>
#include "SKX_IMC_BDF_Offset.h"

#define SOCKET 3
int active_channels[] = {0, 3};
#define NUM_CHANNELS 2
#define NUM_RANKS 2 // Number of ranks per-channel
#define NUM_BANKS 16 // Number of banks per-rank
#define NUM_IMC_CTRS 4
#define NUM_REQUESTS 1000
#define REQ_THRESH 0.8
#define MIN_REQ_FRAC 0.1

#define MAP_HUGE_1GB (30 << MAP_HUGE_SHIFT)
#define BUF_SIZE 1024UL*1024UL*1024UL // should be less than or equal to 1G
#define CL_SIZE 64

#define WR_CAS_RANK_BASE 0x004000b8 // rank 0: b8, rank 1: b9
#define CAS_COUNT_WR 0x00400c04

volatile unsigned int *mmconfig_ptr; // PCIe CFG space
uint64_t imc_counts[NUM_CHANNELS][NUM_IMC_CTRS];
uint64_t prev_imc_counts[NUM_CHANNELS][NUM_IMC_CTRS];

// Convert PCI(bus:device.function,offset) to uint32_t array index
uint32_t PCI_cfg_index(unsigned int Bus, unsigned int Device, unsigned int Function, unsigned int Offset)
{
    uint32_t byteaddress;
    uint32_t index;
    // assert (Bus == BUS);
    assert (Device >= 0);
    assert (Function >= 0);
    assert (Offset >= 0);
    assert (Device < (1<<5));
    assert (Function < (1<<3));
    assert (Offset < (1<<12));

    // fprintf(log_file,"Bus,(Bus<<20)=%x\n",Bus,(Bus<<20));
    // fprintf(log_file,"Device,(Device<<15)=%x\n",Device,(Device<<15));
    // fprintf(log_file,"Function,(Function<<12)=%x\n",Function,(Function<<12));
    // fprintf(log_file,"Offset,(Offset)=%x\n",Offset,Offset);

    byteaddress = (Bus<<20) | (Device<<15) | (Function<<12) | Offset;
    index = byteaddress / 4;
    return ( index );
}

void init_mmconfig() {
    int mem_fd;
    char filename[100];
    unsigned long mmconfig_base=0x80000000;		// DOUBLE-CHECK THIS ON NEW SYSTEMS!!!!!   grep MMCONFIG /proc/iomem | awk -F- '{print $1}'
    unsigned long mmconfig_size=0x10000000;
    sprintf(filename,"/dev/mem");
	mem_fd = open(filename, O_RDWR);
	// fprintf(log_file,"   open command returns %d\n",mem_fd);
	if (mem_fd == -1) {
		fprintf(stderr ,"ERROR %s when trying to open %s\n", strerror(errno), filename);
		exit(EXIT_FAILURE);
	}
	int map_prot = PROT_READ | PROT_WRITE;
	mmconfig_ptr = mmap(NULL, mmconfig_size, map_prot, MAP_SHARED, mem_fd, mmconfig_base);
    if (mmconfig_ptr == MAP_FAILED) {
        fprintf(stderr, "cannot mmap base of PCI configuration space from /dev/mem: address %lx\n", mmconfig_base);
        exit(EXIT_FAILURE);
    }
    close(mem_fd);      // OK to close file after mmap() -- the mapping persists until unmap() or program exit
}

void prog_imc_ctrs(int rank, int bg) {
    int bus, device, function, offset, imc, channel, subchannel, socket, rc, c, counter;
    int banks_per_bg;
    uint32_t index, value;
    socket = SOCKET;
    for(c = 0; c < NUM_CHANNELS; c++) {
        for(counter = 0; counter < NUM_IMC_CTRS; counter++) {
            // event code is determined by rank number; umask is bank number
            value = ((WR_CAS_RANK_BASE + rank) | ((bg*NUM_IMC_CTRS + counter) << 8));

            channel = active_channels[c];
            bus = IMC_BUS_Socket[socket];
            device = IMC_Device_Channel[channel];
            function = IMC_Function_Channel[channel];
            offset = IMC_PmonCtl_Offset[counter];
            index = PCI_cfg_index(bus, device, function, offset);
            mmconfig_ptr[index] = value;
        }
    }
}

void prog_imc_ctrs_debug() {
    int bus, device, function, offset, imc, channel, subchannel, socket, rc, c, counter;
    int banks_per_bg;
    uint32_t index, value;
    socket = SOCKET;
    for(c = 0; c < NUM_CHANNELS; c++) {
        for(counter = 0; counter < 1; counter++) {
            value = CAS_COUNT_WR;

            channel = active_channels[c];
            bus = IMC_BUS_Socket[socket];
            device = IMC_Device_Channel[channel];
            function = IMC_Function_Channel[channel];
            offset = IMC_PmonCtl_Offset[counter];
            index = PCI_cfg_index(bus, device, function, offset);
            mmconfig_ptr[index] = value;
        }
    }
}

void sample_imc_ctrs() {
    int bus, device, function, offset, imc, channel, subchannel, counter, c;
    uint32_t index, low, high;
    uint64_t count;

    bus = IMC_BUS_Socket[SOCKET];

    for(c = 0; c < NUM_CHANNELS; c++) {
        channel = active_channels[c];
        device = IMC_Device_Channel[channel];
        function = IMC_Function_Channel[channel];
        for(counter = 0; counter < NUM_IMC_CTRS; counter++) {
            offset = IMC_PmonCtr_Offset[counter];
            index = PCI_cfg_index(bus, device, function, offset);
            low = mmconfig_ptr[index];
            high = mmconfig_ptr[index+1];
            count = ((uint64_t) high) << 32 | (uint64_t) low;
            prev_imc_counts[c][counter] = imc_counts[c][counter];
            imc_counts[c][counter] = count;
        }
    }
}

void get_max_imc_ctr(int *max_channel, int *max_ctr, uint64_t* max_val) {
    int c, counter;
    int mchannel = 0, mctr = 0;
    uint64_t mval = 0;
    for(c = 0; c < NUM_CHANNELS; c++) {
        for(counter = 0; counter < NUM_IMC_CTRS; counter++) {
            if(imc_counts[c][counter] - prev_imc_counts[c][counter] >= mval) {
                mchannel = c;
                mctr = counter;
                mval = imc_counts[c][counter] - prev_imc_counts[c][counter];
            }
        }
    }
    *max_channel = mchannel;
    *max_ctr = mctr;
    *max_val = mval;
}

uint64_t get_sum_imc_ctrs() {
    int c, counter;
    uint64_t res = 0;
    for(c = 0; c < NUM_CHANNELS; c++) {
        for(counter = 0; counter < NUM_IMC_CTRS; counter++) {
            res += imc_counts[c][counter] - prev_imc_counts[c][counter];
        }
    }
    return res;
}

void log_imc_ctrs() {
    int c, counter;
    for(c = 0; c < NUM_CHANNELS; c++) {
        for(counter = 0; counter < NUM_IMC_CTRS; counter++) {
            fprintf(stderr, "channel:%d, counter:%d, value:%lu\n", c, counter, imc_counts[c][counter] - prev_imc_counts[c][counter]);
        }
    }
}

void issue_requests(volatile char *p, int num_requests) {
    int i;
    __m512i val = _mm512_set_epi32(1995, 1995, 2002, 2002, 1995, 1995, 2002, 2002, 1995, 1995, 2002, 2002, 1995, 1995, 2002, 2002);
	for (i=0; i < num_requests; i++) {
		__asm__ volatile ("vmovntdq %1, (%0)" : : "r"(p), "x"(val) : "memory"); // non-temporal store instruction
	}
}

int main() {

    // Init mmconfig_ptr
    init_mmconfig();

    // mmap and initialize 1GB huge page
    char *a;
    a = mmap(0, 1024UL*1024UL*1024UL, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_HUGETLB | MAP_ANONYMOUS | MAP_HUGE_1GB, -1, 0);
    if(a == MAP_FAILED) {
        fprintf(stderr, "Failed to mmap 1G huge page. Make sure 1G hugetlb pages are populated\n");
        exit(EXIT_FAILURE);
    }
    memset(a, 'm', BUF_SIZE);

    asm volatile("" : : : "memory");

    // execute passes
    // each pass: program imc ctrs for a group of banks on each channel
        // for each address: generate accesses; 
            // check if count > threshold fraction; 
            // then output bank mapping
    int channel, rank, bank, bg, num_bgs;
    int max_channel, max_ctr;
    uint64_t max_val, num_hits, sum_val;
    char *p;
    num_bgs = NUM_BANKS/NUM_IMC_CTRS;
    for(rank = 0; rank < NUM_RANKS; rank++) {
        for(bg = 0; bg < num_bgs; bg++) {
            // Execute a pass
            fprintf(stderr, "Executing pass: rank=%d, bg=%d\n", rank, bg);
            num_hits = 0;

            // program imc ctrs for a group of banks on each channel
            prog_imc_ctrs(rank, bg);
            // prog_imc_ctrs_debug(rank, bg);
            for(p = a; p < a + BUF_SIZE; p += CL_SIZE) {
                sample_imc_ctrs();
                // Generate accesses
                issue_requests(p, NUM_REQUESTS);
                // Measure bank counts
                sample_imc_ctrs();

                // debugging
                // fprintf(stderr, "generated requests to address: 0x%08X\n", (unsigned int)((uintptr_t)p & 0x3FFFFFFF));
                // log_imc_ctrs();

                get_max_imc_ctr(&max_channel, &max_ctr, &max_val);
                sum_val = get_sum_imc_ctrs(); 
                if((((double)sum_val) >= MIN_REQ_FRAC * NUM_REQUESTS) 
                    && (((double) max_val) >= (REQ_THRESH * sum_val))) {
                    num_hits++;
                    uintptr_t paddr = (uintptr_t)p & 0x3FFFFFFF;
                    fprintf(stdout, "0x%08X,%d,%d,%d,%d,%lu,%lu\n", (unsigned int) paddr, max_channel, rank, bg, max_ctr, sum_val, max_val);
                }
            }

            fflush(stdout);
            fprintf(stderr, "Pass completed: rank=%d, bg=%d, hits=%lu\n", rank, bg, num_hits);
        }
    }

    return 0;
}

