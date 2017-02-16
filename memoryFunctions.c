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
#include <errno.h>

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

// Used to determine if this is actually running a SPEC benchmark
int VALID;
 
int queueSizeHOT;

// map a region for the queues and then set the front of the HOT queue to the front
// of that region and the front of the COLD queue after the end of the HOT
int *mem;
int *queueHOTf;
int *queueCOLDf;
int *queueCOLDb;


//File for page dumps
int file;
int add_file;

//tracker for empties
int empties = 0;
static int faults = 0;
static int prot_in = 0;


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

  if(!VALID){
    return location;
  }
 

  uintptr_t location_copy = (uintptr_t)location;
  uintptr_t end = location_copy + size;

  // if the memory is allocated over multiple pages

  while((location_copy / 4096) <= (end/4096)){
    int check = dumbSearchAlgo(location);
    if (check >= 0){
      // Page was either in the HOT queue already, or just put there by mprotect()
      //return location;
    }
    else{
      // New page. Must add to HOT queue
      movePage(location, 1);
    }
    location_copy += 4096;
    }

  return location;
  }


/*
 * Passthrough function for calloc which ultimately calls the original calloc
 * after adding a new page number to the HOT queue if need be
 */
typedef void* (*orig_calloc)(size_t nmeb, size_t size); 

void *calloc(size_t nmeb, size_t size){
  
  orig_calloc original_calloc;
  original_calloc = (orig_calloc)dlsym(RTLD_NEXT, "calloc");
  void *location = original_calloc(nmeb, size);

  if (!VALID){
    return location;
  }

  uintptr_t location_copy = (uintptr_t)location;
  uintptr_t end = location_copy + nmeb*size;

  // if the memory is allocated over multiple pages

  while((location_copy / 4096) <= (end/4096)){
    int check = dumbSearchAlgo((void *)location_copy);
    if (check >= 0){
      // Page was either in the HOT queue already, or just put there by mprotect()
      //return location;
    }
    else{
      // New page. Must add to HOT queue
      movePage((void *)location_copy, 1);
    }
    location_copy += 4096;
    }

  return location;
  }


/*
 * Passthrough function for realloc which ultimately calls the original realloc
 * after adding a new page number to the HOT queue if need be
 */
typedef void* (*orig_realloc)(void *ptr, size_t size); 

void *realloc(void *ptr, size_t size){
  
  orig_realloc original_realloc;
  original_realloc = (orig_realloc)dlsym(RTLD_NEXT, "realloc");
  void *location = original_realloc(ptr, size);

  if (location == NULL || !VALID) return location;

  uintptr_t location_copy = (uintptr_t)location;
  uintptr_t end = location_copy + size;

  // if the memory is allocated over multiple pages

  while ((location_copy / 4096) <= (end/4096)){
    int check = dumbSearchAlgo((void *)location_copy);
    if (check >= 0){
      // Page was either in the HOT queue already, or just put there by mprotect()
      //return location;
    }
    else{
      // New page. Must add to HOT queue
      movePage((void *)location_copy, 1);
    }
    location_copy += 4096;
    }

  return location;
  }


/*
 * Passthrough function for free which ultimately calls the original free
 */
/*typedef void (*orig_free)(void *ptr);

void free(void *ptr){
  orig_free original_free;
  original_free = (orig_free)dlsym(RTLD_NEXT, "free");
  return original_free(ptr);
  }*/


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
	if (direction == 1) pageNumber = (pageNumber | INBOUND_MASK);

	// write the page number, and direction of queue movement
	void *pageAddr = (void *)((uintptr_t)pageNumber << 12);
	void *pageNumPtr = &pageNumber;
	void *pageAddrPtr = &pageAddr;

	//if (direction == 1) printf("%d  %p\n", prot_in, (void *)pageNumber);
	int err = write(file, pageNumPtr, sizeof(pageNumPtr));
	//write(add_file, pageNumPtr, sizeof(pageNumPtr));
	if (err != 8){
	  printf("\n number error was number: %d **********\n", errno);
	  file = open("/home/class17/mmacoy17/ThesisTestCode/interposition-library/SPEC_Dump.txt", (O_RDWR | O_CREAT | O_APPEND), (S_IRUSR | S_IWUSR));
	  err = write(file, pageNumPtr, sizeof(pageNumPtr));
	  printf("\n********* response to error in # was writing %d bytes %d **********\n", err, errno);
	}

	// write the contents of the page
	err = write(file, pageAddrPtr, (size_t)PAGE_SIZE);
	if (err != 4096){
	  printf("\n page error was number: %d **********\n", errno);
	  file = open("/home/class17/mmacoy17/ThesisTestCode/interposition-library/SPEC_Dump.txt", (O_RDWR | O_CREAT | O_APPEND), (S_IRUSR | S_IWUSR));
	  err = write(file, pageAddrPtr, (size_t)PAGE_SIZE);
	  printf("\n********* response to error in page was writing %d bytes %d **********\n", err, errno);
	}

}

int bumpBackCold(){
  /*int *addr = queueCOLDb;

	// NOTE HARDCODE
	if ((queueCOLDb+sizeof(int)) > (mem+sizeof(int)*1000000))
		return -1;

	while (addr >= queueCOLDf){
		*(addr + sizeof(int)) = *(addr);
		addr--;
	}
        
	queueCOLDb++;*/
	return 0;
}

/*
 * checks the COLD queue to determine what the index of the given page number is
 * if it is in the queue. Returns either the index or -1 if it is not present
 *
 * parameter number is the page number not the address
 */
int locateAndRemove(int number){
  return -1;
  /*int *location = queueCOLDf;
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
	return -1;*/
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
		else{
		  empties++;
		}
	}
}


typedef int (*orig_mprotect)(void *addr, size_t len, int prot);

int mprotect(void *addr, size_t len, int prot){

  // 0 indicates moving out of the HOT queue, 1 indicates moving in
  int direction = 0;
  if (prot == (PROT_READ | PROT_WRITE)) direction = 1;

  orig_mprotect original_mprotect;
  original_mprotect = (orig_mprotect)dlsym(RTLD_NEXT, "mprotect");
  int ret_value = original_mprotect(addr, len, prot);

  if (!VALID){
    return ret_value;
  }

  // dumps contents of page and moves within queues
  dumpPage(addr, direction);
  if (direction == 1) prot_in++;
  return ret_value;
}

/* 
 * Handles SIGSEGV signals by determing the page at fault, and
 * unprotecting it (which dumps and moves the page in the process)
 */
void SIGSEGV_handler (int signum, siginfo_t *info, void *context){
  
  int temp = 0;
  if (info->si_code == SEGV_MAPERR) temp = -1;
  if (temp == -1) printf("**********************************************************************************************\n");
  uintptr_t mem_address = (uintptr_t)(info->si_addr);
  uintptr_t page_addr = (uintptr_t)(mem_address & PAGEBASE_MASK);

  if (VALID){
    movePage((void *)page_addr, 1);
  }
  mprotect((void *)page_addr, PAGE_SIZE, (PROT_READ | PROT_WRITE));
  faults++;

}



int dumbSearchAlgo(void *addr){
	int pageNum = (int)PAGENUM((uintptr_t)addr);
	int i;

	// return 1 if in HOT
	int *location = queueHOTf-1;
	if (location < mem) location = mem + queueSizeHOT-1;
	for (i=0; i<queueSizeHOT; i++){
	  if(*location == pageNum){
	    return 1;
	  }
	  //location = ((((int)(uintptr_t)location - (int)(uintptr_t)mem) / sizeof(int) + 1)%(queueSizeHOT))+mem;
	  location = location - 1;
	  if (location < mem) location = mem + queueSizeHOT-1;
	}

	// return 0 if in COLD
	//location = queueCOLDf;
	//while (location <= queueCOLDb){
	//  if(*location == pageNum){
	//    return 0;
	//  }
	//  location++;
	//}

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

	pid_t idn = getpid();
	char id[sizeof(idn)];
	int i = 0;
	while (idn != 0){
	  int temp = idn%10;
	  id[i++] = temp+48;
	  idn = idn/10;
	}
	extern char *program_invocation_short_name;
	
	//printf("\n%s\n", program_invocation_short_name);
	char fileName[73+35+1] = {'/','h','o','m','e','/','c','l','a','s','s','1','7','/','m','m','a','c','o','y','1','7','/','T','h','e','s','i','s','T','e','s','t','C','o','d','e',
				 '/','i','n','t','e','r','p','o','s','i','t','i','o','n','-','l','i','b','r','a','r','y','/','S','P','E','C','_','D','u','m','p','.','t','x','t'};
	/*int j;
	for (j=0; j<i; j++){
	  fileName[73+j] = id[j];
	  }*/
	int j=0;
	while (program_invocation_short_name[j] != '\0'){
	  fileName[73+j] = program_invocation_short_name[j];
	  j++;
	}
	fileName[73+j] = '\0';
	
	if (j>=25){
	  VALID = 1;
	  file = open(&fileName, (O_RDWR | O_CREAT | O_APPEND), (S_IRUSR | S_IWUSR));
	}
	else{
	  VALID = 0;
	  //file = open("/home/class17/mmacoy17/ThesisTestCode/interposition-library/SPEC_Dump.txt", (O_RDWR | O_CREAT | O_APPEND), (S_IRUSR | S_IWUSR)); //74 chars
	//add_file = open("/home/class17/mmacoy17/ThesisTestCode/interposition-library/mem_address_Dump.txt", (O_RDWR | O_CREAT), (S_IRUSR | S_IWUSR));

	}
}

__attribute__((destructor))
void _atClose_(){
	//close the file
	close(file);
	close(add_file);
	//printf("Number of empty pages is: %d\n", empties);
	//printf("Number of faults is: %d    prot_in: %d\n", faults, prot_in);
	}
