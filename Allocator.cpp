#include <iostream>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include "Allocator.h"

Allocator::Allocator(void){
  insertions = 0;
}

void Allocator::add(unsigned int size){
	if (size < MINIMUM || size > MAXIMUM){
		printf("Impossible page size!!\n");
		return;
	}
	
	histogram[insertions] = size;
	
	int block_size = LOW;
	while (block_size < size && block_size < HIGH){
	  block_size += 128;
	}
	while (block_size < size){
	  int mid_size = block_size + block_size/2;
	  if (mid_size < size)
	    mid_size = block_size*2;
	  block_size = mid_size;
	}

	total_frag += (block_size - size);
	insertions++;
}

unsigned int Allocator::getInsert(){
  return insertions;
}

unsigned int * Allocator::getHistogram(){
  return histogram;
}

unsigned int Allocator::getFrag(){
	return total_frag;
}

double Allocator::getAverage(){
  return ((double)total_frag/(double)insertions);
}
