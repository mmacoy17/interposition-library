#include <stdio.h>
#include <stdlib.h>
#include <math.h>

void method(){
  int *array = (int *) malloc(sizeof(int)*40000);
  int i;
  for (i=0; i<40000; i++){
    array[i] = rand();
  }
}


int main(int argc, char *argv[]){
  method();
  return 0;
}


