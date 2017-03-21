#include <iostream>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <time.h>
#include "framework.hpp"

using namespace std;

extern "C" {
	#include "WK.h"
	#include "lzoconf.h"
}


/*
 * Compile instructions:
 * g++ -c Framework.cpp -o Framework.o
 * gcc -c -std=c99 WK.c -o WK.o
 * gcc -c -I. -I./lzo -s -Wall -O2 -fomit-frame-pointer minilzo.c -o minilzo.o
 * gcc -c lzo1.c -o lzo1.o
 * gcc -c lzo_init.c -o lzo_init.o
 * g++ -o Framework Framework.o WK.o minilzo.o
 * 					OR
 * g++ -o Framework Framework.o WK.o lzo1.o lzo_init.o
 */


int PAGE_SIZE = 4096;
//int WORDS_PER_PAGE = PAGE_SIZE/sizeof(WK_word);

//================================= Extras for minilzo =====================================

typedef struct {
    lzo_bytep   ptr;
    lzo_uint    len;
    lzo_uint32_t adler;
    lzo_uint32_t crc;
    lzo_bytep   alloc_ptr;
    lzo_uint    alloc_len;
    lzo_uint    saved_len;
} mblock_t;

static mblock_t block_w;        /* wrkmem */

typedef struct
{
    const char *            name;
    int                     id;
    lzo_uint32_t            mem_compress;
    lzo_uint32_t            mem_decompress;
    lzo_compress_t          compress;
    lzo_optimize_t          optimize;
    lzo_decompress_t        decompress;
    lzo_decompress_t        decompress_safe;
    lzo_decompress_t        decompress_asm;
    lzo_decompress_t        decompress_asm_safe;
    lzo_decompress_t        decompress_asm_fast;
    lzo_decompress_t        decompress_asm_fast_safe;
    lzo_compress_dict_t     compress_dict;
    lzo_decompress_dict_t   decompress_dict_safe;
} compress_t;

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


#ifdef MINI
// Requires much more work to convert to something that can play nice with miniLZO
// r = lzo1x_1_compress(in,in_len,out,&out_len,wrkmem): 
// lzo_bytep, lzo_uint, lzo_bytep, lzo_uintp, lzo_voidp
// unsigned char *, unsigned int64, unsigned char *, &(unsigned int64), void * 

WK_word * minilzoAlgo::compress(WK_word *src, WK_word *dst, unsigned int numWords){

//
// Step 1: initialize the LZO library
//
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
        // this should NEVER happen
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
        // this should NEVER happen
        printf("internal error - decompression failed: %d\n", return_val);
        return NULL;
    }

    src = (WK_word *)input_buf;
    dst = (WK_word *)output_buf;
    WK_word *dst_len = dst + (output_length/sizeof(lzo_bytep));
    return dst_len;
}

#else
WK_word * lzo1Algo::compress(WK_word *src, WK_word *dst, unsigned int numWords){

	if (lzo_init() != LZO_E_OK){
        printf("internal error - lzo_init() failed !!!\n");
        printf("(this usually indicates a compiler bug - try recompiling\nwithout optimizations, and enable `-DLZO_DEBUG' for diagnostics)\n");
        exit(1);
    }

    const compress_t c = { "LZO1-1", 21, LZO1_MEM_COMPRESS, LZO1_MEM_DECOMPRESS, lzo1_compress, 0, lzo1_decompress, 0, 0, 0, 0, 0, 0, 0};

    HEAP_ALLOC(wrkmem, c.mem_compress);


	lzo_bytep input_buf = (lzo_bytep)src;
	lzo_bytep output_buf = (lzo_bytep)dst;
	lzo_uint input_length = numWords*sizeof(WK_word);
	lzo_uint output_length;

	int return_val = lzo1_compress(input_buf, input_length, output_buf, &output_length, wrkmem);
	if (return_val == LZO_E_OK);
      //printf("Algo compressed %lu bytes into %lu bytes\n", (unsigned long) input_length, (unsigned long) output_length);
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


WK_word * lzo1Algo::decompress(WK_word *src, WK_word *dst, unsigned int size){
	lzo_bytep input_buf = (lzo_bytep)src;
	lzo_bytep output_buf = (lzo_bytep)dst;
	lzo_uint input_length = size;
	lzo_uint output_length;

	int return_val = lzo1_decompress(input_buf,input_length,output_buf,&output_length,NULL);

	if (return_val == LZO_E_OK);
	  //printf("Algo decompressed %lu bytes back into %lu bytes\n", (unsigned long) output_length, (unsigned long) input_length);
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
#endif

struct timespec diff(struct timespec start, struct timespec end)
{
  struct timespec temp;
  if ((end.tv_nsec-start.tv_nsec)<0) {
    temp.tv_sec = end.tv_sec-start.tv_sec-1;
    temp.tv_nsec = 1000000000+end.tv_nsec-start.tv_nsec;
  } else {
    temp.tv_sec = end.tv_sec-start.tv_sec;
    temp.tv_nsec = end.tv_nsec-start.tv_nsec;
  }
  return temp;
}


int main(int argc, char *argv[]){

	if (argc != 3){
		printf("Invalid use of command. Include one input file and one output file. Include final param for lzo1.\n");
		return -1;
	}

	struct timespec start_time, end_time, total_time;


	WKAlgo test;


	WK_word* addr;
	WK_word* src_buf;
	WK_word* dest_buf;
	WK_word* udest_buf;
	WK_word* dest_end;
	WK_word* udest_end;
	unsigned int size;
	int i;
	addr = (WK_word *)malloc(sizeof(WK_word));
	src_buf = (WK_word*)malloc(PAGE_SIZE);
	dest_buf = (WK_word*)malloc(PAGE_SIZE*2);
	udest_buf = (WK_word*)malloc(PAGE_SIZE);


	FILE *infile = fopen(argv[1], "r");
	if(infile == NULL){
		printf("Invalid file name.\n");
		return -2;
	}

	FILE *outfile = fopen(argv[2], "w+");
	

	int holder;
	long long time_elapsed = 0;
	int count = 0;
	int inwards = 0;
	long long total_pre_compress = 0;
	long long total_post_compress = 0;

	page_info current_page;
	long long current_time = 0;
	int numLarge = 0;

	fread(addr, sizeof(WK_word), 1, infile);
	current_page.address = *addr;
	while ((holder = fread(src_buf, sizeof(WK_word), WORDS_PER_PAGE, infile)) == WORDS_PER_PAGE){


        clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &start_time);
        dest_end = test.compress(src_buf, dest_buf, WORDS_PER_PAGE);
        clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &end_time);
        total_time = diff(start_time, end_time);
        current_time = total_time.tv_sec*1000000000 + total_time.tv_nsec;
        size = ((char *)dest_end - (char *)dest_buf);

		current_page.comp_size = size;
		current_page.comp_time = current_time;

		clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &start_time);
		udest_end = test.decompress(dest_buf, udest_buf, size);
		clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &end_time);
		total_time = diff(start_time, end_time);
		current_time = total_time.tv_sec*1000000000 + total_time.tv_nsec;

		current_page.decomp_time = current_time;

		fwrite(&current_page, sizeof(page_info), 1, outfile);

		count++;
		fread(addr, sizeof(WK_word), 1, infile);
		current_page.address = *addr;
		if (*addr > 0xffffffff){
		  // printf("*****Large addr: %lu******\n", *addr);
		  numLarge++;
		}
		//printf("%p   %p\n", (void *)*addr, (void *)current_page.address);
		if ((*addr | 0x8000000000000000) == *addr) inwards++;
	}

	
	fclose(infile);
	printf("****************Leftover bytes: %d  Number of pages: %d  Number inwards: %d   Number large: %d****************\n", holder, count, inwards, numLarge);
	printf("WK Compression and Decompression took: %lld seconds and %lld nanoseconds\n", (long long)time_elapsed/1000000000, (long long)time_elapsed%1000000000);
	printf("WK Compressed %lld bytes into %lld bytes for a percentage compressed of: %f\n", total_pre_compress, total_post_compress, 1-((double)total_post_compress/total_pre_compress));
	printf("Size of WK_word: %lu     Size of uintptr_t:   %lu     Size of void*: %lu\n", sizeof(WK_word), sizeof(uintptr_t), sizeof(void*));
}
