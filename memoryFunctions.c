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
 * Each queue (hot and cold) is designed to be made up of Nodes
 *
 * The next field holds the element added to the list immediately
 * AFTER the current Node
 *
 * The prev field holds the element added to the list immediately
 * BEFORE the currrent Node
 */
/*
typedef struct Node {
	uintptr_t pageNumber;
	struct Node *next;
	struct Node *prev;
} Node;
*/

int queueSizeHOT;

// map a region for the queues and then set the front of the HOT queue to the front
// of that region and the front of the COLD queue after the end of the HOT
int *mem;
int *queueHOTf;
int *queueCOLDf;
int *queueCOLDb;

/*
// used to track the HOT queue
Node *leastRecentHOT;	    // oldest member of the queue
Node *mostRecentHOT;		// newest member of the queue

// used to track the COLD queue
Node headCOLD; 			//haha
int queueSizeCOLD = 0;
*/

// used to tell malloc to start intervening
//int SETUP_FINISHED = 0;

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

  /*if (!SETUP_FINISHED){
    return location;
  }*/

  int check = dumbSearchAlgo(location);
  if (check >= 0){
  	// Node was either in the HOT queue already, or just put there by mprotect()
  	return location;
  }
  else{
  	// New page. Must add to HOT queue
        // TODO could lose track of this page if not in queue?
        //Node *node = original_malloc(sizeof(Node));
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
  printf("%s", "Calling free() end\n");
  return original_free(ptr);
}


//============================== PAGE HANDLING ================================


/*
 * Takes the contents of a page being moved in or out of the hot
 * set and dumps it, preceeded by the page number and direction
 * of movement within the queues, out to a set file. (Use env var later for file?)
 *
 * direction of 0 indicates moving out of the HOT queue, 1 indicates moving in
 * parameter addr is the address and not page number
 */
void dumpPage(void *addr, int direction){
  printf("%s %p, %d\n", "dumpPage addr and direction: ", addr, direction);
	uintptr_t pageNumber = (uintptr_t) PAGENUM((uintptr_t)addr);
	printf("%s %lu\n", "Inside dumpPage(), ", pageNumber);
	if (pageNumber == 0){
		return;	// Do not dump if it is an empty page
	}
	if (direction == 1) pageNumber = pageNumber & INBOUND_MASK;

	// write the page number, and direction of queue movement
	void *pageAddr = (void *)((uintptr_t)pageNumber << 12);
	write(file, pageAddr, (size_t)sizeof(void *));

	printf("%s\n", "Past first write");
	// write the contents of the page
	write(file, pageAddr, (size_t)PAGE_SIZE);


}

/*
// TODO create a hash map of elements that are in the cold queue
// 0 indicates moving out of the HOT queue, 1 indicates moving in
void movePage(void *addr, int direction, Node *node){
  printf("%s %p, %d\n", "movePage() called with the parameters: ", addr, direction);
	if (direction == 1){
     
		// start by moving what is in the needed spot to the cold queue
	    movePage(NULL, 0, NULL);

		// use new Node, insert it into the position of the 
		// current least recently added Node, and update
		node->pageNumber = (uintptr_t)addr >> 12;	  // TODO make sure consistent use and non use of offset
		node->next = leastRecentHOT->next;
		node->prev = leastRecentHOT->prev;
		leastRecentHOT = node->next;         
		mostRecentHOT = node;
		
		if (dumbSearchAlgo(addr) == 0){
			Node currentNode = headCOLD;
			int found = 0;
			while (currentNode.next != NULL && !found){
				if (node->pageNumber == currentNode.pageNumber){
					found = 1;
				}
				else{
					currentNode = *currentNode.next;
				}
			}
			// once found, remove from COLD queue
			currentNode.prev->next = currentNode.next;
			currentNode.next->prev = currentNode.prev;
			queueSizeCOLD--;
		}
	}
	// move a copy of leastRecentHOT to the front of the COLD queue
	// TODO is this really a copy?
	else{
		Node n = *leastRecentHOT;
		printf("%s %lu\n", "Page number moving to COLD is: ", n.pageNumber);
		if(n.pageNumber != 0){
		  n.next = headCOLD.next;
		  if (headCOLD.next != NULL)
		    headCOLD.next->prev = &n;
		  headCOLD.next = &n;
		  n.prev = &headCOLD;
		  queueSizeCOLD++;
		  
		  //sanitize to make sure addr is page aligned
		  uintptr_t page = (uintptr_t)addr >> 12;
		  page = page << 12;
		  //protect this page to induce a SIGSEGV signal when referenced
		  mprotect((void *)page, PAGE_SIZE, PROT_NONE);
		}
	}

}

*/

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
  printf("%s %p, %d\n", "movePage() called with the parameters: ", addr, direction);

	if (direction == 1){
		// start by clearing out the spot
		movePage(NULL, 0);

		// check where in the COLD queue the page was (if anywhere) and remove it
		int location = locateAndRemove((int)((uintptr_t)addr >> 12));
		//if (location != -1)
		//	addMemRef(location);

		// overwrite the front of the queue and increment
		*queueHOTf = (int)((uintptr_t)addr >> 12);
		queueHOTf = ((((int)(uintptr_t)&queueHOTf + sizeof(int)))%(sizeof(int)*queueSizeHOT))+mem;
	}
	else{
	  printf("%s %d\n", "The page number being evicted is: ", *queueHOTf);
		if(*queueHOTf != 0){
			//TODO handle mmap buffer overflow?
			bumpBackCold();			
			*queueCOLDf = *queueHOTf;
			uintptr_t addressOfPage = ((uintptr_t)*queueHOTf) << 12;
		      
			//protect this page to induce a SIGSEGV signal when referenced
			printf("%s %p\n", "Protecting: ", (void *)addressOfPage);
			mprotect((void *)addressOfPage, PAGE_SIZE, PROT_NONE);
			printf("%s\n", "Made it to the end of eviction");
		}
	}
}


//TODO update for new queue type

typedef int (*orig_mprotect)(void *addr, size_t len, int prot);

int mprotect(void *addr, size_t len, int prot){

  // 0 indicates moving out of the HOT queue, 1 indicates moving in
  int direction = 0;
  if (prot == (PROT_READ | PROT_WRITE)) direction = 1;
  printf("mprotect() on addr %lu, direction: %d \n", ((uintptr_t)addr /*>> 12*/), direction);

	/*Node *node = NULL;
	if (direction == 1){
	  node = malloc(sizeof(Node));
	}*/

	// dumps contents of page and moves within queues
	dumpPage(addr, direction);
	//movePage(addr, direction);
	
	printf("%s\n", "Done with dumpPage()");
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
  printf("%s %p,   %d\n", "SIGSEGV fault called on: ", info->si_addr, temp);   //TODO remove after testing
        uintptr_t mem_address = (uintptr_t)(info->si_addr);
	uintptr_t page_addr = (uintptr_t)(mem_address & PAGEBASE_MASK);

	movePage((void *)page_addr, 1);
	mprotect((void *)page_addr, PAGE_SIZE, (PROT_READ | PROT_WRITE));

}


/*
 * Slow algorith that searches all of the existing pages and returns 1 if
 * the Node exists in the HOT queue, 0 if it exists in the COLD queue, and
 * -1 if it does not exist at all.
 */
/*int dumbSearchAlgo(void *addr){
  printf("%s %p\n", "The location for search algo is: ", addr);
	uintptr_t pageNum = PAGENUM((uintptr_t)addr);
	printf("%s %lu\n", "The associated page number is: ", pageNum);
	int i;
	Node *currentNode = leastRecentHOT;
	for (i=0; i<queueSizeHOT; ++i){
	  printf("%s %lu\n", "Comparing page number", currentNode->pageNumber);
	  if (pageNum == currentNode->pageNumber){
	    printf("%s", "IT WORKED!");   	
	    return 1;
	  }
	  else{ 
	    currentNode = currentNode->next;
	    printf("%s\n", "Made it past next element");
         
	  }
	}
	// here if the node does not exist in HOT
	printf("%s\n", "Out of HOT search");
	currentNode = headCOLD.next;
	printf("%s %d\n", "Number of nodes in COLD: ", queueSizeCOLD);
        for(i=0; i<queueSizeCOLD; ++i){
		if (pageNum == currentNode->pageNumber){
		    return 0;
		}
		else{
		    currentNode = currentNode->next;
		}
	}
	printf("%s", "Returning -1 from search algo\n");
	return -1;
}*/

int dumbSearchAlgo(void *addr){
	int pageNum = (int)PAGENUM((uintptr_t)addr);
	int i;

	// return 1 if in HOT
	int *location = queueHOTf;
	for (i=0; i<queueSizeHOT; i++){
		if(*location == pageNum)
			return 1;
		location++;
	}

	// return 0 if in COLD
	location = queueCOLDf;
	while (location <= queueCOLDb){
		if(*location == pageNum)
			return 0;
		location++;
	}

	// not in HOT or COLD
	return -1;
}


//============================== INITIALIZATIONS ==============================

/*
 * Acts as a constructor for Nodes
 * Sets the page number to zero and the pointers to NULL
 */
/*
void initNode(Node *n){
  n->pageNumber = 0;
  n->prev = NULL;
  n->next = NULL;
}*/

/*
 * Initializes the HOT queue of Nodes of a specified size
 * Front of the queue is the least recently added Node
 */
/*
void createQueue(int size){
	int i;
	for (i=0; i<size; ++i){
	  Node *n = malloc(sizeof(Node));
	  initNode(n);
		if (i == 0){
			mostRecentHOT = n;
			leastRecentHOT = n;
			//TODO Need to edit .next and .prev fields to point to self?
		}
		else{
			n->prev = mostRecentHOT;
			mostRecentHOT->next = n;
			mostRecentHOT = n;
		}
	}
	mostRecentHOT->next = leastRecentHOT;
	leastRecentHOT->prev = mostRecentHOT;
}*/



/*
 * Runs when the library is linked and sets up the SIGSEGV handling and queues
 */
__attribute__((constructor))
void _init_(){
  //Do not use f prefix
  //doxygen
	// set up the SIGSEGV handling
	struct sigaction sigact;
	sigact.sa_flags = SA_SIGINFO;
	sigact.sa_sigaction = &SIGSEGV_handler;

	sigaction(SIGSEGV, &sigact, NULL);

	// set up the pointers to the HOT and COLD queues
	mem = (int *)mmap(NULL, sizeof(int)*1000000, (PROT_READ | PROT_WRITE), 
	(MAP_PRIVATE | MAP_ANONYMOUS), -1, 0);

	queueHOTf = mem;
	queueCOLDf = (queueHOTf + sizeof(int)*(queueSizeHOT+1));
	queueCOLDb = queueCOLDf;


	queueSizeHOT = strtol(getenv("QUEUE_SIZE"), NULL, 10);	
	//createQueue(queueSizeHOT);
	//initNode(&headCOLD);

	file = open("Page_Dump.txt", (O_RDWR | O_APPEND | O_CREAT)); //use open() and write(), returns int

	//SETUP_FINISHED = 1;
}

__attribute__((destructor))
void _atClose_(){
	//close the file
	close(file);
}





