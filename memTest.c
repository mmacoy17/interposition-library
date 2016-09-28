#include <stdio.h>
#include <stdlib.h>

int main (int argc, char **argv){
	printf("Pre-malloc\n");
	char *a = (char*) malloc(4);
	printf("Post-malloc\n");
	free(a);
}