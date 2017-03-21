#include <stdio.h>
#include <stdlib.h>
#include <math.h>

void method(){
  //FILE *output = fopen("trash.txt","w+"); 
  int *array = (int *) malloc(sizeof(int)*40000);
  int i;
  for (i=0; i<40000; i++){
    array[i] = rand();
  }
  //for (i=0; i<40000; i++){
  //  fprintf(output, "%d\n", array[i]);
  //}
  //fclose(output);
  
}


int main(int argc, char *argv[]){
  method();
  return 0;
}


