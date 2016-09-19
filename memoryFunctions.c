#define _GNU_SOURCE
#include <stdio.h>
#include <dlfcn.h>
#include <stddef.h>

typedef void *(*orig_malloc)(size_t size); //Not sure about the need for two pointers or not

void *malloc(size_t size){
  printf("Passthrough of malloc()");
  
  orig_malloc original_malloc;
  original_malloc = (orig_malloc)dlsym(RTLD_NEXT, "malloc");
  return original_malloc(size);
}

typedef void (*orig_free)(void *ptr);

void free(void *ptr){
  printf("Passthrough of free()");
  
  orig_free original_free;
  original_free = (orig_free)dlsym(RTLD_NEXT, "free");
  return original_free(ptr);
}

