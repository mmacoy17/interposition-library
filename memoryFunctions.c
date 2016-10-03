#define _GNU_SOURCE
#include <stdio.h>
#include <dlfcn.h>
#include <stddef.h>
#include <string.h>
#include <sys/mman.h>
#include <signal.h>

#define PAGE_SIZE 4096
#define OFFSET_MASK 0xfff
#define PAGEBASE_MASK ~OFFSET_MASK
#define PAGENUM(addr) (addr & PAGEBASE_MASK) >> 12



typedef void* (*orig_malloc)(size_t size); 

void *malloc(size_t size){
  
  orig_malloc original_malloc;
  original_malloc = (orig_malloc)dlsym(RTLD_NEXT, "malloc");
  return original_malloc(size);
}

typedef void (*orig_free)(void *ptr);

void free(void *ptr){
  printf("Passthrough of free()\n");
  
  orig_free original_free;
  original_free = (orig_free)dlsym(RTLD_NEXT, "free");
  return original_free(ptr);
}


typedef int (*orig_mprotect)(void *addr, size_t len, int prot);

int mprotect(void *addr, size_t len, int prot){
	printf("Protecting page %p", addr);

	//dump page contents to file
	//gets moved in or out of queue

	orig_mprotect original_mprotect;
	original_mprotect = (orig_mprotect)dlsym(RTLD_NEXT, "mprotect");
	return original_mprotect(addr, len, prot);
}

/* 
 * Handles SIGSEGV signals by determing the page number at fault,
 * unprotecting it, and dumping the contents into a file, then adding
 * that page to the hot set
 */
void SIGSEGV_handler (int signum, siginfo_t *info, void *context){
	void *mem_address = info->si_addr;
	void *page_addr = (void *)((int)mem_address & PAGEBASE_MASK);
	int page_num = PAGENUM((int)mem_address);

	mprotect(page_addr, PAGE_SIZE, PROT_NONE);

}


// Runs when the library is linked and sets up the SIGSEGV handling
void _init_(){

	struct sigaction sigact;
	sigact.sa_flags = SA_SIGINFO;
	sigact.sa_sigaction = &SIGSEGV_handler;

	sigaction(SIGSEGV, &sigact, NULL);
}





