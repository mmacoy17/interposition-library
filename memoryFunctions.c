#define _GNU_SOURCE
#include <stdio.h>
#include <dlfcn.h>
#include <stddef.h>
#include <string.h>
#include <sys/mman.h>
#include <signal.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <fcntl.h>

#define PAGE_SIZE 4096
#define OFFSET_MASK 0xfff
#define PAGEBASE_MASK ~OFFSET_MASK
#define PAGENUM(addr) (addr & PAGEBASE_MASK) >> 12
#define INBOUND_MASK 0x8000000000000000

/*
 * To use:
 * gcc -shared -fPIC memoryFunctions.c -o memoryFunctions.so -ldl
 * bash
 * export LD_PRELOAD = ./memoryFunctions.so
 * export QUEUE_SIZE = ""
 */



int queueSizeHOT;

// map a region for the queues and then set the front of the HOT queue to the front
// of that region and the front of the COLD queue after the end of the HOT
int *mem;
int *queueHOTf;
int *queueCOLDf;
int *queueCOLDb;


//File for page dumps
int file;


//============================ METHOD DECLARATIONS ============================


int dumbSearchAlgo(void *);
void movePage(void *, int);

//============================= MEMORY MANAGEMENT =============================

/*
 * Passthrough function for malloc which ultimately calls the original malloc
 * after adding a new page number to the HOT queue if need be
 */
typedef void* (*orig_malloc)(size_t size); 

void *malloc(size_t size){
  
  orig_malloc original_malloc;
  original_malloc = (orig_malloc)dlsym(RTLD_NEXT, "malloc");
  void *location = original_malloc(size);


  int check = dumbSearchAlgo(location);
  if (check >= 0){
  	// Page was either in the HOT queue already, or just put there by mprotect()
  	return location;
  }
  else{
  	// New page. Must add to HOT queue
  	movePage(location, 1);
  }
  return location;
}

/*
 * Passthrough function for free which ultimately calls the original free
 */
typedef void (*orig_free)(void *ptr);

void free(void *ptr){
  orig_free original_free;
  original_free = (orig_free)dlsym(RTLD_NEXT, "free");
  return original_free(ptr);
}


//============================== PAGE HANDLING ================================


/*
 * Takes the contents of a page being moved in or out of the hot
 * queue and dumps it, preceeded by the page number and direction
 * of movement within the queues, out to a set file. (Use env var later for file?)
 *
 * direction of 0 indicates moving out of the HOT queue, 1 indicates moving in
 * parameter addr is the address and not page number
 */
void dumpPage(void *addr, int direction){
  //printf("%s %p, %d\n", "dumpPage addr and direction: ", addr, direction);
	uintptr_t pageNumber = (uintptr_t) PAGENUM((uintptr_t)addr);
	if (pageNumber == 0){
		return;	// Do not dump if it is an empty page
	}
	if (direction == 1) pageNumber = pageNumber | INBOUND_MASK;

	// write the page number, and direction of queue movement
	void *pageAddr = (void *)((uintptr_t)pageNumber << 12);
	void *pageNumPtr = &pageNumber;
	void *pageAddrPtr = &pageAddr;

	write(file, pageNumPtr, sizeof(pageNumPtr));

	// write the contents of the page
	write(file, pageAddrPtr, (size_t)PAGE_SIZE);


}

int bumpBackCold(){
	int *addr = queueCOLDb;

	// NOTE HARDCODE
	if ((queueCOLDb+sizeof(int)) > (mem+sizeof(int)*1000000))
		return -1;

	while (addr >= queueCOLDf){
		*(addr + sizeof(int)) = *(addr);
		addr--;
	}
        
	queueCOLDb++;
	return 0;
}

/*
 * checks the COLD queue to determine what the index of the given page number is
 * if it is in the queue. Returns either the index or -1 if it is not present
 *
 * parameter number is the page number not the address
 */
int locateAndRemove(int number){
	int *location = queueCOLDf;
	int position = 0;
	// move through the full COLD queue until found or at the end
	while (location <= queueCOLDb){
		// if found, remove and return the position in the queue
		if (*location == number){
			// iterate through rest of the queue and pull forward, filling the empty space
			while ((location + sizeof(int)) <= queueCOLDb){
				*location = *(location + sizeof(int));
				location++;
			}
			return position;
		}
		location++;
		position++;
	}
	return -1;
}

/*
 * Moves page either in or out of the HOT queue. A direction of 1 indicates
 * moving into the HOT queue, a direction of 0 indicates moving out.
 *
 * parameter addr is the address of the page not the page number
 */
void movePage(void *addr, int direction){
  //printf("%s %p, %d\n", "movePage() called with the parameters: ", (void *)((((uintptr_t)addr)>>12)<<12), direction);

	if (direction == 1){
		// start by clearing out the spot
		movePage(NULL, 0);

		// check where in the COLD queue the page was (if anywhere) and remove it
		int location = locateAndRemove((int)((uintptr_t)addr >> 12));
		//if (location != -1)
		//	addMemRef(location);

		// overwrite the front of the queue and increment
		*queueHOTf = (int)((uintptr_t)addr >> 12);


		// TODO is this overwriting in the statics area?
		queueHOTf = ((((int)(uintptr_t)queueHOTf - (int)(uintptr_t)mem) / sizeof(int) + 1)%(queueSizeHOT))+mem;
	}
	else{
		if(*queueHOTf != 0){
			bumpBackCold();
			*queueCOLDf = *queueHOTf;
			uintptr_t addressOfPage = ((uintptr_t)*queueHOTf) << 12;

			//protect this page to induce a SIGSEGV signal when referenced
			mprotect((void *)addressOfPage, PAGE_SIZE, PROT_NONE);
		}
	}
}


typedef int (*orig_mprotect)(void *addr, size_t len, int prot);

int mprotect(void *addr, size_t len, int prot){

  // 0 indicates moving out of the HOT queue, 1 indicates moving in
  int direction = 0;
  if (prot == (PROT_READ | PROT_WRITE)) direction = 1;


  // dumps contents of page and moves within queues
  dumpPage(addr, direction);
        
  orig_mprotect original_mprotect;
  original_mprotect = (orig_mprotect)dlsym(RTLD_NEXT, "mprotect");
  return original_mprotect(addr, len, prot);
}

/* 
 * Handles SIGSEGV signals by determing the page at fault, and
 * unprotecting it (which dumps and moves the page in the process)
 */
void SIGSEGV_handler (int signum, siginfo_t *info, void *context){
  int temp = 0;
  if (info->si_code == SEGV_MAPERR) temp = -1;
  uintptr_t mem_address = (uintptr_t)(info->si_addr);
  uintptr_t page_addr = (uintptr_t)(mem_address & PAGEBASE_MASK);

  movePage((void *)page_addr, 1);
  mprotect((void *)page_addr, PAGE_SIZE, (PROT_READ | PROT_WRITE));

}



int dumbSearchAlgo(void *addr){
	int pageNum = (int)PAGENUM((uintptr_t)addr);
	int i;

	// return 1 if in HOT
	int *location = queueHOTf;
	for (i=0; i<queueSizeHOT; i++){
	  if(*location == pageNum){
			return 1;
	  }
		location = ((((int)(uintptr_t)location - (int)(uintptr_t)mem) / sizeof(int) + 1)%(queueSizeHOT))+mem;
	}

	// return 0 if in COLD
	location = queueCOLDf;
	while (location <= queueCOLDb){
	  if(*location == pageNum){
			return 0;
	  }
		location++;
	}

	// not in HOT or COLD
	return -1;
}


//============================== INITIALIZATIONS ==============================


/*
 * Runs when the library is linked and sets up the SIGSEGV handling and queues
 */
__attribute__((constructor))
void _init_(){
  //TODO doxygen
	// set up the SIGSEGV handling
	struct sigaction sigact;
	sigact.sa_flags = SA_SIGINFO;
	sigact.sa_sigaction = &SIGSEGV_handler;

	sigaction(SIGSEGV, &sigact, NULL);

	// set up the pointers to the HOT and COLD queues
	mem = (int *)mmap(NULL, sizeof(int)*1000000, (PROT_READ | PROT_WRITE), 
	(MAP_PRIVATE | MAP_ANONYMOUS), -1, 0);

	queueSizeHOT = strtol(getenv("QUEUE_SIZE"), NULL, 10);

	queueHOTf = mem;
	queueCOLDf = (queueHOTf + sizeof(int)*(queueSizeHOT+1));
	queueCOLDb = queueCOLDf;




	file = open("../SPEC_Dump.txt", (O_RDWR | O_CREAT), (S_IRUSR | S_IWUSR)); // took out the appending
}

__attribute__((destructor))
void _atClose_(){
	//close the file
	close(file);
}
