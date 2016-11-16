/* =============================================================================================================================== */
/**
 * \file WK.h
 * \author Scott F. H. Kaplan <sfkaplan@cs.amherst.edu>
 * \date 2015-Jun-27
 * \brief A class of page-based, in-memory, fast yet effective compression algorithms.  Using a small, recency-based dictionary and
 *        partial matching on common upper-bits, it is tuned to compress pages of integer- and pointer-heavy data.
 **/
/* =============================================================================================================================== */
/* =============================================================================================================================== */
/* AVOID MULTIPLE INCLUSION PROLOGUE */
#if !defined (_WK_H)
#define _WK_H
/* =============================================================================================================================== */
/* =============================================================================================================================== */
/* INCLUDES */
#include <inttypes.h> // For PRIx?? formatting macros
#include <stdint.h>   // For uint32_t and uint64_t
/* =============================================================================================================================== */
/* =============================================================================================================================== */
/* TYPES AND CONSTANTS */
#define TRUE  1
#define FALSE 0
/**
 * The machine word size, and values that follow from it.  Assume 64-bit, but allow 32-bit override.
 **/
#if defined WK_32_BIT_WORD
  typedef uint32_t WK_word;
  #define BYTES_PER_WORD 4
  #define BITS_PER_WORD 32
  #define PRIxWORD PRIx32
  #define WORD_FORMAT_WIDTH "8"
#else
  typedef uint64_t WK_word;
  #define BYTES_PER_WORD 8
  #define BITS_PER_WORD 64
  #define PRIxWORD PRIx64
  #define WORD_FORMAT_WIDTH "16"
#endif
/**
 * The number of bytes per page.  Assume the Linux small-page standard of 4 KB unless another value is specified outside this module.
 **/
#if defined WK_PAGE_SIZE
#define BYTES_PER_PAGE WK_PAGE_SIZE
#else
#define BYTES_PER_PAGE 4096
#endif
/**
 * The number of bits in a byte.
 **/
#define BITS_PER_BYTE 8
/**
 * The number of words per page.
 **/
#define WORDS_PER_PAGE (BYTES_PER_PAGE / BYTES_PER_WORD)
/**
 * The size of each set in the set-associative dictionary.
 **/
#if defined WK_DICTIONARY_SET_SIZE
#define DICTIONARY_SET_SIZE WK_DICTIONARY_SET_SIZE
#else
#define DICTIONARY_SET_SIZE 4
#endif
/**
 * The number of sets in the set-associative dictionary.
 **/
#if defined WK_DICTIONARY_NUM_SETS
#define DICTIONARY_NUM_SETS WK_DICTIONARY_NUM_SETS
#else
#define DICTIONARY_NUM_SETS 4
#endif
/**
 * The dictionary's size as given by the set size and number of sets.
 **/
#define DICTIONARY_SIZE (DICTIONARY_SET_SIZE * DICTIONARY_NUM_SETS)
/**
 * The dictionary's structure based on associativity.  Direct mapped is a special case for the structure.
 **/
#define SET_ASSOCIATIVE_ORG                 1
#define DIRECT_MAPPED_ORG                   2
#define FULLY_ASSOCIATIVE_LINEAR_LOOKUP_ORG 3
#define FULLY_ASSOCIATIVE_CONST_LOOKUP_ORG  4
#if DICTIONARY_SET_SIZE == 1
  #define DICTIONARY_ORG DIRECT_MAPPED_ORG
#elif DICTIONARY_NUM_SETS == 1
  #if defined WK_FULLY_ASSOCIATIVE_USE_MAP
    #define DICTIONARY_ORG FULLY_ASSOCIATIVE_CONST_LOOKUP_ORG
  #else
    #define DICTIONARY_ORG FULLY_ASSOCIATIVE_LINEAR_LOOKUP_ORG
  #endif
#else
  #define DICTIONARY_ORG SET_ASSOCIATIVE_ORG
#endif
/**
 * A structure to store each element of the dictionary.  The structure of these element, and of the overall dictionary, depends
 * on a number of paramters.  The default, used for direct-mapped and FIFO-replaced sets, is simply an array of simple elements
 * that are solely the word-sized pattern.  The fully associative, LRU-replaced, constant-time-mapped structure uses a more
 * complex linked list node structures.
 **/
#if DICTIONARY_ORG == DIRECT_MAPPED_ORG
typedef WK_word dictionary_element_s;
#else /* All other organizations. */
typedef struct dictionary_element_struct {
  struct dictionary_element_struct* next;
  struct dictionary_element_struct* prev;
  WK_word value;
} dictionary_element_s;
#endif /* Other organizations */
/**
 * For partial matching, the number of high and low bits into which the word is divided.  A default of 10 based on old 32-bit
 * experiments.  New experiments should change that default.
 **/
#if defined WK_LOW_BITS
#define NUM_LOW_BITS WK_LOW_BITS
#else
#define NUM_LOW_BITS 10
#endif
#define NUM_HIGH_BITS (BITS_PER_WORD - NUM_LOW_BITS)
/** The stride at which words of the uncompressed data are traversed. */
#if !defined (WK_STRIDE)
#define WK_STRIDE 1
#endif
/**
 * The maximum size of a compressed page, given a failure to model any regularities and an expansion of the representation.
 * [SFHK: For now, a rather loose bound. Tighten it when the details of the algorithm are better established.  Be sure to check
 *        the length of compressed pages against this limit as an invariant.]
 **/
#define MAX_COMPRESSED_BYTES (BYTES_PER_PAGE * 2)
/* =============================================================================================================================== */
/* =============================================================================================================================== */
/* C++ MANAGEMENT PROLOGUE */
/*
 * If this header is being included into a C++ module, we need to be sure that the function names for this module don't suffer C++
 * name mangling.
 */
#if defined (__cplusplus)
extern "C" {
#endif /* __cplusplus */
/* =============================================================================================================================== */
/* =============================================================================================================================== */
/* FUNCTION PROTOTYPES */
WK_word*
WK_compress (WK_word* source_page,
	     WK_word* destination_buffer,
	     unsigned int number_decompressed_words);
  
WK_word*
WK_decompress (WK_word* source_buffer,
	       WK_word* destination_page);
  /* =============================================================================================================================== */
/* =============================================================================================================================== */
/* C++ MANAGEMENT EPILOGUE */
#if defined (__cplusplus)
}
#endif /* __cplusplus */
/* =============================================================================================================================== */
/* =============================================================================================================================== */
/* AVOID MULTIPLE INCLUSION PROLOGUE */
#endif /* _WK_H */
/* =============================================================================================================================== */