#include <iostream>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string>
#include <math.h>
#include "framework.hpp"
#include "Allocator.h"

extern "C" {
	#include "WK.h" 
}


//compile with -O3
//tool that reads through the traces and prints out the page numbers
//ratio of hits to pre-fetching since last dump
//what-if on multiples 

//g++ -O1 -fthread-jumps -falign-functions -falign-loops -falign-jumps -falign-labels -fcaller-saves -fcrossjumping -fcse-follow-jumps -fcse-skip-blocks -fdelete-null-pointer-checks -fdevirtualize -fexpensive-optimizations -fgcse -finline-small-functions -findirect-inlining -fipa-cp -foptimize-sibling-calls -foptimize-strlen -fpartial-inlining -fpeephole2 -freorder-blocks-and-partition -freorder-functions -frerun-cse-after-loop -fsched-interblock -fsched-spec -fschedule-insns -fschedule-insns2 -fstrict-aliasing -fstrict-overflow -ftree-builtin-call-dce -ftree-switch-conversion -ftree-tail-merge -ftree-pre Simulator.cpp -o Simulator


// constansts
const int num_cache = 11;
const long long disk_time = 4000000; // in ns
//const double multiple = 2.0;
const int pages_per_fetch = 128;
const int pre_fetch_queue_length = 3;
const int pre_fetch_size = 4096*pages_per_fetch*pre_fetch_queue_length; //page_size*number
// use array to define different compression levels
const double comp_perc_level[] = {0.0, .10, .20, .30, .40, .50, .60, .70, .80, .90, 1.0};
page_info fetched[num_cache][pre_fetch_queue_length][pages_per_fetch];
const double alpha = .99;

// determined from command line inputs
FILE *file;
long long mem_size;// = 20971520*3; // 20MB*3 of RAM
int queue_size;// = 2000;           // number of pages
long long trace_mem_size;

// tracked while running
double perc_size_post_comp = 1.0;
long long count = 0;
long long mem_used = 0;
int pre_fetch_front[num_cache];
long long num_fetch_hits[num_cache];
long long num_fetch_possible[num_cache];
double prefetch_hit_rates[num_cache];
double locality = 0;

int temp_pre_possible[num_cache];
int temp_pre_hits[num_cache];

page_info *queueF;// = (page_info *)malloc(sizeof(page_info)*25000);
int queueB = 0; //max index into queue + 1

//updated
void pushBackQueue(page_info move, int index){
  if (index == -1 && (queueB+1)>=500000) printf("END OF QUEUE REACHED****\n");
  page_info holder = move;
  int location = index -1;
  if (index == -1){
    location = queueB;
    queueB++;
  }
  while(location >= 0){
    queueF[location+1] = queueF[location];
    location--;
  }
  queueF[0] = holder;
}

//updated
unsigned int searchQueue(WK_word address){
  unsigned int count = 0;
  while(count < queueB && count<=(mem_used/4096)){
    if((((queueF[count].address)<<1)>>1) == address)
      return count;
    count++;
  }
  return -1;
}

int searchPreFetch(WK_word address, int cache){
  int i;
  int j;
  // int num_safe = (int)fmin(pre_fetch_queue_length, fetch_safe[cache]);
  //printf("\n");
  for (i=0; i<pre_fetch_queue_length; i++){
    for(j=0; j<pages_per_fetch; j++){
      //printf("%lu\n", (((fetched[cache][i][j].address)<<1)<<1));
      if ((((fetched[cache][i][j].address)<<1)>>1) == address)
	return 1;
    }
  }
  //printf("\n");
  return 0;
}

//updated
void preFetch(int index, int comp_level){
  // front and back are the number elements around the index to prefetch
  int front = 0;
  int i = pages_per_fetch/2;
  while((index+queue_size-front-1 > (int)((mem_size/4096)*(1-comp_perc_level[comp_level]))) && i > 0 && (index-front-1)>0){
    front++;
    i--;
  }
  //printf("front: %d\n", front);
  int back = 0;
  int j = pages_per_fetch-front;
  while((index+queue_size+back+1 <= (int)((mem_size/4096)*(1-comp_perc_level[comp_level]))
    +(comp_perc_level[comp_level]*mem_size/(perc_size_post_comp*4096))) && j > 0){
    back++;
    j--;
  }
  front = pages_per_fetch-back;
  
  j=0;
  int location = index;
  //printf("Index: %d front: %d back: %d\n", index, front, back);
  for(i=1; i<=front; i++){
    fetched[comp_level][pre_fetch_front[comp_level]][j++] = queueF[location-i];
  }
  //printf("THIS FAR?\n");
  for (i=1; i<=back; i++){
    fetched[comp_level][pre_fetch_front[comp_level]][j++] = queueF[location+i];
  }
  pre_fetch_front[comp_level] = (pre_fetch_front[comp_level] + 1) % pre_fetch_queue_length;
}

/*void spatialPreFetch(int index, int comp_level, int offset){
  page_info *location = queueF + offset;
  
  while (location < queueB){
    int i = 0;
    int dif = (((location->address)<<1)>>1) - ((((queueF + index)->address)<<1)>>1);
    if (dif<=pages_per_fetch/2 && dif >=-pages_per_fetch/2 && dif != 0){
      //printf("%d\n", i);
      fetched[comp_level][pre_fetch_front[comp_level]][i++] = *location;
    }
    
    location++;
  }
  //printf("\n");
  pre_fetch_front[comp_level] = (pre_fetch_front[comp_level] + 1) % pre_fetch_queue_length;
  }*/

/*void printHistograms(char *in_file, Allocator *frags){
  unsigned int* histograms[num_cache];

  std::string input_file(in_file);
  std::string fileName = "hist_" + input_file + ".csv";
  const char *name = fileName.c_str();
  FILE *out_file = fopen(name, "a+");
  fprintf(out_file, "\n%llu\n", mem_size);

  int i;
  for (i=0; i<num_cache; i++){
    histograms[i] = frags[i].getHistogram();
  }


  int max_length = 0;
  int tracker;
  for (i=0; i<num_cache; i++){
    tracker = 0;
    while (histograms[i][tracker] != 0){
      tracker++;
    }
    if (tracker > max_length)
      max_length = tracker;
  }
  
  for (i=0; i<max_length; i++){
    int j;
    for (j=0; j<num_cache; j++){
      if(histograms[j][i] != 0)
	fprintf(out_file, "%d,", histograms[j][i]);
      else
	fprintf(out_file, " ,");
    }
    fprintf(out_file, "\n");
  }
  }*/




int main(int argc, char *argv[]){
  
  // ensuring proper use
  if (argc != 5){
    printf("Invalid use of command. Include one input file, memory size, queue size and multiple.\n");
    return -1;
  }

  file = fopen(argv[1], "r");
  if(file == NULL){
    printf("Invalid file name.\n");
    return -2;
  }
  
  // account for prefetch and compression hiding
  mem_size = strtoll(argv[2], NULL, 10) - pre_fetch_size - 4096; 
  queue_size = strtol(argv[3], NULL, 10);
  trace_mem_size = queue_size*4096;
  if (trace_mem_size >= mem_size){
    printf("Memory size must be greater than QUEUE_SIZE*4096\n");
    return -3;
  }

  double multiple = strtod(argv[4], NULL);

  FILE *output = fopen(/*"Final_Output.txt"*/ "Output_file.txt", "a+");
  
  std::string input_file(argv[1]);
  std::string memory(argv[2]);
  std::string fileName = "miss_" + input_file + memory + "_.99";
  const char *name = fileName.c_str();
  FILE *miss_file = fopen(name, "w+");
  //double *miss_curve = (double *)malloc(sizeof(double)*2500000;
  long long curve_count = 0;
  

  // array of Allocator trackers
  //Allocator *fragmentation = new Allocator[num_cache];

  // use array to store total swap times
  long long total_times[num_cache];
  long long comp_times[num_cache];
  int clean;
  for (clean = 0; clean < num_cache; clean++){
    total_times[clean] = 0;
    comp_times[clean] = 0;
  }

  queueF = (page_info *)malloc(sizeof(page_info)*500000);
  if(queueF == NULL)
    printf("MALLOC FAILED!!\n");

  long long time_used = 0;
  long long comp10_time_used = 0;
  long long comp100_time_used = 0;
  page_info current_page;

  long long comp_decomp = 0;
  long long comp_count = 0;


  printf("%llu\n", mem_size/4096);
  
  //actual meat of processing
  while (fread(&current_page, sizeof(page_info), 1, file) == 1){
    //printf("Break 0, ");
    //update the average compression
    perc_size_post_comp = ((perc_size_post_comp*count) + (((double)current_page.comp_size/multiple)/4096))/(count+1);
    count++;

    //printf("1, ");
    
    //if (count%10000 == 0)
    //printf("%llu\n", count);

    //fprintf(tester, "%lu\n", ((current_page.address<<1)>>1));
    int index = searchQueue((((current_page.address)<<1)>>1));
    
    if (index == -1){
      pushBackQueue(current_page, index);
    }

    //printf("2, ");

    if((((current_page.address)<<1)>>1) != current_page.address){
      if (index == -1){
          printf("***ERROR: Page being re-inserted without ever leaving\n");
          return -4;
      }
      

      else{
	pushBackQueue(current_page, index);
	//printf("3, ");
	
	curve_count++;
	locality = locality*alpha + index*(1-alpha);
	if (curve_count%10000 == 0){
	  fprintf(miss_file, "%f,", locality);
	  int i;
	  for(i=0; i<num_cache; i++){
	    if(temp_pre_possible[i] == 0)
	      fprintf(miss_file, " %f,", prefetch_hit_rates[i]);
	    else{
	      prefetch_hit_rates[i] = prefetch_hit_rates[i]*alpha + ((double)temp_pre_hits[i]/(double)temp_pre_possible[i])*(1-alpha);
	      fprintf(miss_file, " %f,", prefetch_hit_rates[i]);
	    }
      
	    temp_pre_hits[i] = 0;
	    temp_pre_possible[i] = 0;
	  }
	  fprintf(miss_file, "\n");
	  }

        // REQUIRES LIST OF COMPRESSION PERCENTAGES IS LEAST TO GREATEST
        int i=num_cache-1; 
        // while the page is past the uncompressed pages in memory 

	//printf("4, ");

        while((index+queue_size > (int)((mem_size/4096)*(1-comp_perc_level[i]))) && i >= 0){
	  //printf("5, ");

          int in_pre_fetch = searchPreFetch((((current_page.address)<<1)>>1), i);
	  temp_pre_possible[i]++;
	  num_fetch_possible[i]++;
          if(in_pre_fetch){
            num_fetch_hits[i]++;
	    temp_pre_hits[i]++;
          }

          // if the page is in the compressed region
          else if (index+queue_size <= (int)((mem_size/4096)*(1-comp_perc_level[i]))+(comp_perc_level[i]*mem_size/(perc_size_post_comp*4096))){

	    //printf("6, ");

            total_times[i] += current_page.decomp_time;
            comp_times[i] += current_page.comp_time;
            comp_decomp += current_page.comp_time+current_page.decomp_time;
            comp_count++;
	    preFetch(index, i);
	    //int offset = (int)((mem_size/4096)*(1-comp_perc_level[i]))+(comp_perc_level[i]*mem_size/(perc_size_post_comp*4096));
	    //spatialPreFetch(index, i, offset);

	    //printf("7, ");
	    
          }
          // if the page is on the disk
          // add allocator stuff here?
          else{
	    //printf("8, ");
            total_times[i] += disk_time;
            //fragmentation[i].add(current_page.comp_size);
          }
          
          i--;

	  //printf("9, ");
        }
      }
    }
    if (index == -1){
      mem_used += 4096;
    }

    //printf("10\n");
  }
  
  printf("MADE IT THROUGH THE MAIN LOOP OF ALL PAGES! *******************************************************\n");
  int i;
  int index = 0;
  double min_percent = 1.0;
  double fetch_hit_rate = 0.0;
  for (i=0; i<num_cache; i++){
    printf("Total time: %f, Comp time saved: %f\n", (double)total_times[i]/1000000000, (double)comp_times[i]/1000000000);
    //printf("Frag average: %f,  %d\n", fragmentation[i].getAverage(), fragmentation[i].getInsert());

    fetch_hit_rate = (double)num_fetch_hits[i]/(double)num_fetch_possible[i];
    printf("Pre-fetching hit rate: %f\n", fetch_hit_rate);

    if(((double)total_times[i]/(double)total_times[0]) < min_percent){
      index = i;
      min_percent = (double)total_times[i]/total_times[0];
    }
  }
  //fflush
  //printHistograms(argv[1], fragmentation);
  
  printf("%s %s %f %f %f %d %f %f\n", argv[1], argv[2], comp_perc_level[index], min_percent, (double)num_fetch_hits[index]/(double)num_fetch_possible[index], pages_per_fetch*pre_fetch_queue_length, 1.0/perc_size_post_comp, multiple);
  fprintf(output, "%s %s %f %f %f %d %f %f\n", argv[1], argv[2], comp_perc_level[index], min_percent, (double)num_fetch_hits[index]/(double)num_fetch_possible[index], pages_per_fetch*pre_fetch_queue_length, 1.0/perc_size_post_comp, multiple);

  printf("Average comp and decomp is: %f\n", (double)(comp_decomp/comp_count)/1000000000);

  //while (miss_curve[i]!= 0){
  //  fprintf(miss_file, "%f\n", miss_curve[i++]);
  //}
 
}
