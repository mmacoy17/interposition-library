#define _GNU_SOURCE
#include <stdio.h>
#include <dlfcn.h>
#include <stddef.h>
#include <string.h>
#include <sys/mman.h>
#include <signal.h>
#include <stdint.h>
#include <stdlib.h>

#define PAGE_SIZE 4096
#define OFFSET_MASK 0xfff
#define PAGEBASE_MASK ~OFFSET_MASK
#define PAGENUM(addr) (addr & PAGEBASE_MASK) >> 12
#define INBOUND_MASK 0x8000000000000000
#define INIT_NODE(node) Node node = {.pageNumber = 0, .next = NULL, .prev = NULL}

/*
 * Each queue (hot and cold) is designed to be made up of Nodes
 *
 * The next field holds the element added to the list immediately
 * AFTER the current Node
 *
 * The prev field holds the element added to the list immediately
 * BEFORE the currrent Node
 */
typedef struct Node {
	uintptr_t pageNumber;
	struct Node *next;
	struct Node *prev;
} Node;

// used to track the HOT queue
int queueSizeHOT;
Node leastRecentHOT;	        // oldest member of the queue
Node mostRecentHOT;		// newest member of the queue

// used to track the COLD queue
Node headCOLD; 			//haha


//============================ METHOD DECLARATIONS ============================


int dumbSearchAlgo(void *);
void movePage(void *, int);

//============================= MEMORY MANAGEMENT =============================

/*
 * Passthrough function for malloc which ultimately calls the original malloc
 * after setting up a new Node and adding it to the HOT queue if need be
 */
typedef void* (*orig_malloc)(size_t size); 

void *malloc(size_t size){
  
  orig_malloc original_malloc;
  original_malloc = (orig_malloc)dlsym(RTLD_NEXT, "malloc");
  void *location = original_malloc(size);

  int check = dumbSearchAlgo(location);
  return; // TODO remove after test
  if (check >= 0){
  	// Node was either in the HOT queue already, or just put there by mprotect()
  	return location;
  }
  else{
  	// New page. Must create node, and add to HOT queue
  	movePage(location, 1);

  }
  return location;
}

/*
 * Passthrough function for free which ultimately calls the original free
 */
typedef void (*orig_free)(void *ptr);

void free(void *ptr){
  printf("%s", "Calling free()\n");
  orig_free original_free;
  original_free = (orig_free)dlsym(RTLD_NEXT, "free");
  return original_free(ptr);
}


//============================== PAGE HANDLING ================================


/*
 * Takes the contents of a page being moved in or out of the hot
 * set and dumps it, preceeded by the page number and direction
 * of movement within the queues, out to a set file. (Use env var later for file?)
 *
 * 0 indicates moving out of the HOT queue, 1 indicates moving in
 */
void dumpPage(void *addr, int direction){
	uintptr_t pageInfo = (uintptr_t) PAGENUM((uintptr_t)addr);
	if (pageInfo == 0){
		return;	// Do not dump if it is a dummy Node
	}
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
// 0 indicates moving out of the HOT queue, 1 indicates moving in
void movePage(void *addr, int direction){
	if (direction == 1){
	
		// start by moving what is in the needed spot to the cold queue
		movePage(NULL, 0);

		// create new Node, insert it into the position of the 
		// current least recently added Node, and update
		Node n;
		n.pageNumber = (uintptr_t)addr >> 12;		// TODO make sure consistent use and non use of offset
		n.next = leastRecentHOT.next;
		n.prev = leastRecentHOT.prev;
		leastRecentHOT = *n.next;          // TODO this is the line that is causing the seg fault
		return; // TODO remove after test
		mostRecentHOT = n;
		
		if (dumbSearchAlgo(addr) == 0){
			Node currentNode = headCOLD;
			int found = 0;
			while (currentNode.next != NULL && !found){
				if (n.pageNumber == currentNode.pageNumber){
					found = 1;
				}
				else{
					currentNode = *currentNode.next;
				}
			}
			// once found, remove from COLD queue
			currentNode.prev->next = currentNode.next;
			currentNode.next->prev = currentNode.prev;
		}
	}
	// move a copy of leastRecentHOT to the front of the COLD queue
	else{
		Node n = leastRecentHOT;		
		n.next = headCOLD.next;
		if (headCOLD.next != NULL)
		  headCOLD.next->prev = &n;
		headCOLD.next = &n;
		n.prev = &headCOLD;
	}

}

//TODO check to make sure page number is not NULL

typedef int (*orig_mprotect)(void *addr, size_t len, int prot);

int mprotect(void *addr, size_t len, int prot){
	printf("Protecting page %lu", ((uintptr_t)addr >> 12));

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
 * Handles SIGSEGV signals by determing the page at fault, and
 * unprotecting it (which dumps and moves the page in the process)
 */
void SIGSEGV_handler (int signum, siginfo_t *info, void *context){
    uintptr_t mem_address = (uintptr_t)(info->si_addr);
	uintptr_t page_addr = (uintptr_t)(mem_address & PAGEBASE_MASK);
	//int page_num = PAGENUM(mem_address);

	mprotect((void *)page_addr, PAGE_SIZE, PROT_NONE);

}

/*
 * Slow algorith that searches all of the existing pages and returns 1 if
 * the Node exists in the HOT queue, 0 if it exists in the COLD queue, and
 * -1 if it does not exist at all.
 */
int dumbSearchAlgo(void *addr){
	uintptr_t pageNum = PAGENUM((uintptr_t)addr);
	int i;
	Node *currentNode = leastRecentHOT;
	for (i=0; i<queueSizeHOT; ++i){
		if (pageNum == currentNode->pageNumber){
			return 1;
		}
		else{
		  currentNode = currentNode.next;       // TODO also causing seg faults
		}
	}
	// here if the node does not exist in HOT
	currentNode = headCOLD;
	while (currentNode.next != NULL){
		if (pageNum == currentNode.pageNumber){
			return 0;
		}
		else{
			currentNode = *currentNode.next;
		}
	}
	return -1;
}


//============================== INITIALIZATIONS ==============================

/*
 * Initializes the HOT queue of Nodes of a specified size
 * Front of the queue is the least recently added Node
 */
void createQueue(int size){
	int i;
	for (i=0; i<size; ++i){
		printf("%s %d\n", "Created Node: ", i+1);
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
__attribute__((constructor))
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





