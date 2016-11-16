#include <iostream>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include "framework.hpp"

using namespace std;

extern "C" {
	#include "WK.h"
}

/*
 * Compile instructions:
 * g++ -c Framework.cpp -o Framework.o
 * gcc -c -std=c99 WK.c -o WK.o
 * gcc -c -I. -I./lzo -s -Wall -O2 -fomit-frame-pointer minilzo.c -o minilzo.o
 * g++ -o Framework Framework.o WK.o minilzo.o
 */


int PAGE_SIZE = 4096;
//int WORDS_PER_PAGE = PAGE_SIZE/sizeof(WK_word);

//================================= Extras for minilzo =====================================

/* Work-memory needed for compression. Allocate memory in units
 * of 'lzo_align_t' (instead of 'char') to make sure it is properly aligned.
 */

#define HEAP_ALLOC(var,size) \
    lzo_align_t __LZO_MMODEL var [ ((size) + (sizeof(lzo_align_t) - 1)) / sizeof(lzo_align_t) ]

//==========================================================================================




WK_word * PassthroughAlgo::compress(WK_word *src, WK_word *dst, unsigned int numWords){
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

WK_word * PassthroughAlgo::decompress(WK_word *src, WK_word *dst, unsigned int size){
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


WK_word * WKAlgo::compress(WK_word *src, WK_word *dst, unsigned int numWords){
	return WK_compress(src, dst, numWords);
}

WK_word * WKAlgo::decompress(WK_word *src, WK_word *dst, unsigned int size){
	return WK_decompress(src, dst);
}


// Requires much more work to convert to something that can play nice with miniLZO
// r = lzo1x_1_compress(in,in_len,out,&out_len,wrkmem): 
// lzo_bytep, lzo_uint, lzo_bytep, lzo_uintp, lzo_voidp
// unsigned char *, unsigned int64, unsigned char *, &(unsigned int64), void * 
WK_word * minilzoAlgo::compress(WK_word *src, WK_word *dst, unsigned int numWords){

/*
 * Step 1: initialize the LZO library
 */
    if (lzo_init() != LZO_E_OK){
        printf("internal error - lzo_init() failed !!!\n");
        printf("(this usually indicates a compiler bug - try recompiling\nwithout optimizations, and enable '-DLZO_DEBUG' for diagnostics)\n");
        return NULL;
    }

	lzo_bytep input_buf = (lzo_bytep)src;
	lzo_bytep output_buf = (lzo_bytep)dst;
	lzo_uint input_length = numWords*sizeof(WK_word);
	lzo_uint output_length;
	HEAP_ALLOC(wrkmem, LZO1X_1_MEM_COMPRESS);

	int return_val = lzo1x_1_compress(input_buf, input_length, output_buf, &output_length, wrkmem);
    if (return_val == LZO_E_OK)
    	printf("Algo compressed %lu bytes into %lu bytes\n", (unsigned long) input_length, (unsigned long) output_length);
    else{
        /* this should NEVER happen */
        printf("internal error - compression failed: %d\n", return_val);
        return NULL;
    }


    src = (WK_word *)input_buf;
    dst = (WK_word *)output_buf;
    WK_word *dst_len =(WK_word *) ((char *)dst + output_length);
    return dst_len;
}

WK_word * minilzoAlgo::decompress(WK_word *src, WK_word *dst, unsigned int size){
	lzo_bytep input_buf = (lzo_bytep)src;
	lzo_bytep output_buf = (lzo_bytep)dst;
	lzo_uint input_length = size;
	lzo_uint output_length;

	int return_val = lzo1x_decompress(input_buf,input_length,output_buf,&output_length,NULL);

	if (return_val == LZO_E_OK)
        printf("Algo decompressed %lu bytes back into %lu bytes\n", (unsigned long) output_length, (unsigned long) input_length);
    else{
        /* this should NEVER happen */
        printf("internal error - decompression failed: %d\n", return_val);
        return NULL;
    }

    src = (WK_word *)input_buf;
    dst = (WK_word *)output_buf;
    WK_word *dst_len = dst + (output_length/sizeof(lzo_bytep));
    return dst_len;
}


int main(int argc, char *argv[]){

	if (argc != 2){
		printf("Invalid use of command. Include one file name.\n");
		return -1;
	}

	WKAlgo test;

	WK_word* src_buf;
	WK_word* dest_buf;
	WK_word* udest_buf;
	WK_word* dest_end;
	WK_word* udest_end;
	unsigned int size;
	int i;
	src_buf = (WK_word*)malloc(PAGE_SIZE);
	dest_buf = (WK_word*)malloc(PAGE_SIZE*2);
	udest_buf = (WK_word*)malloc(PAGE_SIZE);

	FILE *file = fopen(argv[1], "r");
	if(file == NULL){
		printf("Invalid file name.\n");
		return -2;
	}

	int holder;
	int count = 0;
	int total_pre_compress = 0;
	int total_post_compress = 0;
	WK_word *addr = (WK_word *)malloc(sizeof(WK_word));
	fread(addr, sizeof(WK_word), 1, file);
	while ((holder = fread(src_buf, sizeof(WK_word), WORDS_PER_PAGE, file)) == WORDS_PER_PAGE){
		dest_end = test.compress(src_buf, dest_buf, WORDS_PER_PAGE);
		size = ((char *)dest_end - (char *)dest_buf);
		printf("Page number and direction is: %llu\n", *addr);
		printf("Compressed %d bytes to %d bytes\n", 4096, size);

		total_pre_compress += 4096;
		total_post_compress += size;

		udest_end = test.decompress(dest_buf, udest_buf, size);
		size = (udest_end - udest_buf) * sizeof(WK_word);
		printf("Decompressed back to %d bytes\n", size);
		count++;
		fread(addr, sizeof(WK_word), 1, file);
	}
	printf("****************Leftover bytes: %d  Number of pages: %d****************\n", holder, count);
	printf("Compressed %d bytes into %d bytes for a percentage comressed of: %f\n", total_pre_compress, total_post_compress, 1-((double)total_post_compress/total_pre_compress));
}
