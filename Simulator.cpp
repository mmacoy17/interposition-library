#include <iostream>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include "framework.hpp"

extern "C" {
	#include "WK.h"
}


int num_cache = 1;
FILE *file;
long long mem_size = 10485760; // 100MB of RAM
int queue_size = 2000;           // number of pages
long long trace_mem_size;
long long disk_time = 400000000; // in ms

page_info *queueF;// = (page_info *)malloc(sizeof(page_info)*25000);
page_info *queueB;// = queueF;

long long mem_used = 0;

void pushBackQueue(page_info move, int index){
  if (index == -1 && (queueB+1)>=queueF+500000) printf("END OF QUEUE REACHED****\n");
  page_info holder = move;
  page_info *location = queueF + index -1;
  if (index == -1){
    location = queueB;
    queueB++;
  }
  while(location >= queueF){
    *(location+1) = *location;
    location--;
  }
  *queueF = holder;
}

int searchQueue(WK_word address){
  int count = 0;
  page_info *location = queueF;
  while((location+count) < queueB && count<=(mem_used/4096)){
    if(((((location+count)->address)<<1)>>1) == address)
      return count;
    count++;
  }
  return -1;
}




int main(int argc, char *argv[]){
  
  // ensuring proper use
  if (argc != 2){
    printf("Invalid use of command. Include one input file.\n");
    return -1;
  }

  file = fopen(argv[1], "r");
  if(file == NULL){
    printf("Invalid file name.\n");
    return -2;
  }
  
  trace_mem_size = queue_size*4096;
  if (trace_mem_size >= mem_size){
    printf("Memory size must be greater than QUEUE_SIZE*4096");
    return -3;
  }

  FILE *tester = fopen("address_file.txt", "w+");
  FILE *problems = fopen("problem_file.txt", "w+");

  queueF = (page_info *)malloc(sizeof(page_info)*500000);
  queueB = queueF;

  long long time_used = 0;
  long long comp_time_used = 0;
  page_info current_page;


  printf("%llu\n", mem_size/4096);
  //actual meat of processing
  while (fread(&current_page, sizeof(page_info), 1, file) == 1){
    fprintf(tester, "%lu\n", ((current_page.address<<1)>>1));
    //printf("MADE IT\n");
    //printf("%p\n", (void *)(((current_page.address)<<1)>>1));
    int index = searchQueue((((current_page.address)<<1)>>1));
    //if (index != -1) printf("NON-NEGATIVE: %d\n", index);
    //printf("ONE %d\n", index);
    pushBackQueue(current_page, index);
    //printf("TWO\n");
    if((((current_page.address)<<1)>>1) != current_page.address){
      if((index == -1 && mem_used+4096 > mem_size) /*|| index > ((mem_size/4096)*1.10)*/){
	fprintf(problems, "%lu\n", ((current_page.address<<1)>>1));
	printf("Address: %lu      %lu\n", current_page.address, ((current_page.address)<<1)>>1);
	printf("Index: %d\n", index);
        comp_time_used += disk_time;
      }
      if((index == -1 && mem_used+4096 > mem_size) || (index > mem_size/4096)){
        time_used += disk_time;
      }
    }
    if (index == -1){
      mem_used += 4096;
    }
  }
  printf("Total time spent on swaps is %llu ns\n", time_used);
  printf("With \"compression\" total time spent on swaps is %llu ns\n", comp_time_used);
}
