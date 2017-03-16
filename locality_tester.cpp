#include <stdio.h>
#include <stdlib.h>
#include <math.h>

void method(){
  int *array = (int *) malloc(sizeof(int)*40000);
  printf("HERE\n");
  int i;
  for (i=0; i<38; i++){
    *(array+(i*1024)) = rand();
    printf("%d\n",*(array+(i*1024)));
  }
}


int main(int argc, char *argv[]){
  method();
  return 0;
}


