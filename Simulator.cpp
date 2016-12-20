#include <iostream>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include "framework.hpp"

extern "C" {
	#include "WK.h"
}


int num_cache = 3;
FILE *file;
long long mem_size = 8000;
int queue_size = 2000;
long long disk_time = 400000000;



int main(int argc, char *argv[]){
  
  if (argc != 2){
    printf("Invalid use of command. Include one input file.\n");
    return -1;
  }

  file = fopen(argv[1], "r");
  if(file == NULL){
    printf("Invalid file name.\n");
    return -2;
  }

  long long total_time = 0;
  long long total_size = 0;
  int count = 0;
  page_info current_page;

  while (fread(&current_page, sizeof(page_info), 1, file) == 1){
    total_time += current_page.comp_time + current_page.decomp_time;
    total_size += current_page.comp_size;
    count++;
  }

  printf("Total compression and decompression time is %llu and size is %llu. count: %d\n", total_time, total_size, count);
  
}
