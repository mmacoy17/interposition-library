#include <iostream>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include "framework.hpp"

extern "C" {
	#include "WK.h"
}


int num_cache = 11;
FILE *file;
long long mem_size = 104857600/5; // 100MB of RAM
int queue_size = 4000;           // number of pages
long long trace_mem_size;
long long disk_time = 400000000; // in ms
double perc_size_post_comp = 1.0;
int count = 0;

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
    printf("Memory size must be greater than QUEUE_SIZE*4096\n");
    return -3;
  }

  // use array to define different compression levels
  double comp_perc_level[] = {0.0, .10, .20, .30, .40, .50, .60, .70, .80, .90, 1.0};

  // use array to store total swap times
  long long total_times[num_cache];
  int clean;
  for (clean = 0; clean < num_cache; clean++) total_times[clean] = 0;

  queueF = (page_info *)malloc(sizeof(page_info)*500000);
  queueB = queueF;

  long long time_used = 0;
  long long comp10_time_used = 0;
  long long comp100_time_used = 0;
  page_info current_page;

  long long comp_decomp = 0;
  int comp_count = 0;


  printf("%llu\n", mem_size/4096);
  
  //actual meat of processing
  while (fread(&current_page, sizeof(page_info), 1, file) == 1){
    perc_size_post_comp = ((perc_size_post_comp*count) + ((double)current_page.comp_size/4096))/(count+1);
    count++;

    //fprintf(tester, "%lu\n", ((current_page.address<<1)>>1));
    int index = searchQueue((((current_page.address)<<1)>>1));
    pushBackQueue(current_page, index);

    if((((current_page.address)<<1)>>1) != current_page.address){
      if (index == -1){
          printf("***ERROR: Page being re-inserted without ever leaving\n");
          return -4;
      }  
      else{

        // REQUIRES LIST OF COMPRESSION PERCENTAGES IS LEAST TO GREATEST
        int i=num_cache-1; 
        while((index+queue_size > (int)((mem_size/4096)*(1-comp_perc_level[i]))) && i >= 0){
          if (index+queue_size <= (int)((mem_size/4096)*(1-comp_perc_level[i]))+(comp_perc_level[i]*mem_size/(perc_size_post_comp*4096))){
            total_times[i] += current_page.decomp_time;
            total_times[i] += current_page.comp_time;
            comp_decomp += current_page.comp_time+current_page.decomp_time;
            comp_count++;
          }
          else total_times[i] += disk_time;
          
          i--;
        }
      }
    }
    if (index == -1){
      mem_used += 4096;
    }
  }
  int i;
  for (i=0; i<num_cache; i++){
    printf("Total time spent on swaps with %d percent additional memory is: %f s\n", (int)(comp_perc_level[i]*100), (double)total_times[i]/1000000000);
  }
  printf("Average comp and decomp is: %f\n", (double)(comp_decomp/comp_count)/1000000000);
}
