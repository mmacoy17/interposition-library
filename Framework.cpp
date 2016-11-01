#include <iostream>
#include <stdio.h>

using namespace std;


typedef uint64_t MM_word;
int PAGE_SIZE = 4096;
int WORDS_PER_PAGE = PAGE_SIZE/sizeof(MM_word);


class CompressionAlgo{
	virtual MM_word * compress(MM_word *src, MM_word *dst, unsigned int numWords) = 0;
	virtual MM_word * decompress(MM_word *src, MM_word *dst) = 0;
};

class PassthroughAlgo{
public: 
	MM_word * compress(MM_word *src, MM_word *dst, unsigned int numWords){
		int i = 0;
		*dst = numWords; // Allow the decompressor to know how many words will be decompressed
		dst++;
		while (i < numWords){
			*dst = *src;
			dst++;
			src++;
			i++;
		}

		return dst;
	}

	MM_word * decompress(MM_word *src, MM_word *dst){
		unsigned int numWords = *src;
		src++;
		int i = 0;
		while (i < numWords){
			*dst = *src;
			dst++;
			src++;
			i++;
		}

		return dst;
	}
};

int main(int argc, char *argv[]){

	if (argc != 2){
		printf("Invalid use of command. Include one file name.\n");
		return -1;
	}

	PassthroughAlgo test;

	MM_word* src_buf;
	MM_word* dest_buf;
	MM_word* udest_buf;
	MM_word* dest_end;
	MM_word* udest_end;
	unsigned int size;
	int i;
	src_buf = (MM_word*)malloc(PAGE_SIZE);
	dest_buf = (MM_word*)malloc(PAGE_SIZE*2);
	udest_buf = (MM_word*)malloc(PAGE_SIZE);

	FILE *file = fopen(argv[1], "r");
	if(file == NULL){
		printf("Invalid file name.\n");
		return -2;
	}

	while (fread(src_buf, sizeof(MM_word), WORDS_PER_PAGE, file) == WORDS_PER_PAGE){
		dest_end = test.compress(src_buf, dest_buf, WORDS_PER_PAGE);
		size = (dest_end - dest_buf) * sizeof(MM_word);
		printf("Compressed %d bytes to %d bytes\n", 4096, size);
		udest_end = test.decompress(dest_buf, udest_buf);
		size = (udest_end - udest_buf) * sizeof(MM_word);
		printf("Decompressed back to %d bytes\n", size);
	}

/*
	for (i = 0; i < WORDS_PER_PAGE; ++i) {
	  src_buf[i] = WORDS_PER_PAGE;
	}
	for (i = 0; i < WORDS_PER_PAGE; ++i) {
	  udest_buf[i] = -1;
	}

	dest_end = test.compress(src_buf, dest_buf, 512);
	size = (dest_end - dest_buf) * sizeof(MM_word);
	printf("Compressed %d bytes to %d bytes\n", 4096, size);
	udest_end = test.decompress(dest_buf, udest_buf);
	size = (udest_end - udest_buf) * sizeof(MM_word);
	printf("Decompressed back to %d bytes\n", size);
	*/
}