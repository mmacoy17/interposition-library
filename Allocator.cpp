#include <iostream>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include "Allocator.h"

void Allocator::add(unsigned int size){
	if (size < MINIMUM || size > MAXIMUM){
		printf("Impossible page size!!\n");
		return;
	}

	int block_size = MINIMUM;
	while (block_size < size){
		block_size *= 2;
	}

	total_frag += (block_size - size);
}

unsigned int Allocator::getFrag(){
	return total_frag;
}
