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
#define INBOUND_MASK 0x8000000000000000
#define INIT_NODE(node) Node node = {.next = NULL, .prev = NULL}

typedef struct Node {
	int pageNumber;
	struct Node *next;
	struct Node *prev;
} Node;

// used to track the HOT queue
int queueSizeHOT;
Node leastRecentHOT;
Node mostRecentHOT;

// used to track the COLD queue
Node headCOLD; //haha


//=============================================================================


typedef void* (*orig_malloc)(size_t size); 

void *malloc(size_t size){
  
  orig_malloc original_malloc;
  original_malloc = (orig_malloc)dlsym(RTLD_NEXT, "malloc");
  return original_malloc(size);
}

typedef void (*orig_free)(void *ptr);

void free(void *ptr){

  orig_free original_free;
  original_free = (orig_free)dlsym(RTLD_NEXT, "free");
  return original_free(ptr);
}


//=============================================================================

//TODO check to make sure page number is not NULL

/*
 * Takes the contents of a page being moved in or out of the hot
 * set and dumps it, preceeded by the page number and direction
 * of movement within the sets, out to a set file. (Use env var later?)
 */
void dumpPage(void *addr, int direction){

	int pageInfo = (int) addr;
	if (direction == 1) pageInfo = pageInfo & INBOUND_MASK;

	FILE *file = fopen("Page_Dump.txt", "a");

	// write the page number, and direction of queue movement
	fwrite((void *)pageInfo, 1, 8, file);

	// write the contents of the page
	fwrite(addr, 1, PAGE_SIZE, file);

	//close the file
	fclose(file);

}

// TODO create a hash map of elements that are in the cold queue
void movePage(void *addr, int direction){
	if (direction == 1){
		// start by moving what is in the needed spot to the cold queue
		movePage(NULL, 0);

		// create new Node, insert it into the position of the 
		// current least recently added Node, and update
		Node n;
		n.pageNumber = (int)addr;
		n.next = leastRecentHOT.next;
		n.prev = leastRecentHOT.prev;
		leastRecentHOT = *n.next;
		mostRecentHOT = n;
	}
	else{
		Node n = leastRecentHOT;
		n.next = headCOLD.next;
		headCOLD.next->prev = &n;
		headCOLD.next = &n;
		n.prev = &headCOLD;
	}

}

//TODO check to make sure page number is not NULL

typedef int (*orig_mprotect)(void *addr, size_t len, int prot);

int mprotect(void *addr, size_t len, int prot){
	printf("Protecting page %p", addr);

	// 0 indicates moving out of the HOT queue, 1 indicates moving in
	int direction = 0;
	if (prot == PROT_NONE) direction = 1;

	// dumps contents of page and moves within queues
	dumpPage(addr, direction);
	movePage(addr, direction);
	

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


//=============================================================================

/*
 * Initializes a queue of Nodes of a specified size
 * Front of the queue is the least recently added Node
 */
void createQueue(int size){
	int i = 0;
	for (int i; i<size-1; ++i){
		INIT_NODE(n);
		if (leastRecentHOT.pageNumber == 0){
			mostRecentHOT = n;
			leastRecentHOT = n;
			//TODO Need to edit .next and .prev fields to point to self?
		}
		else{
			n.prev = &mostRecentHOT;
			mostRecentHOT.next = &n;
			mostRecentHOT = n;
		}
	}
	mostRecentHOT.next = &leastRecentHOT;
	leastRecentHOT.prev = &mostRecentHOT;
}

/*
 * Runs when the library is linked and sets up the SIGSEGV handling and queue
 */
void _init_(){

	struct sigaction sigact;
	sigact.sa_flags = SA_SIGINFO;
	sigact.sa_sigaction = &SIGSEGV_handler;

	sigaction(SIGSEGV, &sigact, NULL);

	queueSizeHOT = strtol(getenv("QUEUE_SIZE"), NULL, 10);
	leastRecentHOT.pageNumber = 0;
	mostRecentHOT.pageNumber = 0;
	createQueue(queueSizeHOT);
	INIT_NODE(headCOLD);
}





