#include <stdio.h>
#include <stdlib.h>
#include <string.h>


int* make_array (int n){
  int* a = (int*)malloc(sizeof(int)*n);
  int i;
  for (i=0; i<n; i+=1){
    a[i] = random();
  }
  return a;
}


//pointer to an array of pointers, length of the aray
int main (int argc, char **argv){
  int n = strtol(argv[1], NULL, 10);
  printf("%d\n", n);
  int* array = make_array(n);
  //add print_array(), verify()
  return 0;
}
