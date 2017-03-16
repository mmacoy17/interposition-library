#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <time.h>

int main(int argc, char *argv[]){
	int *array = (int *) malloc(sizeof(int)*4000000);
	int i;
	for (i=0; i<1000000; i++){
		srand(time(NULL));
		int index = (rand()*100000)%(4000000/sizeof(int));
		array[index] = array[index]*2+1;
	}
}