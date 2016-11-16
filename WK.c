/* =============================================================================================================================== */
/**
 * \file WK.c
 * \author Scott F. H. Kaplan <sfkaplan@cs.amherst.edu>
 * \date 2015-Jun-28
 * \brief A class of page-based, in-memory, fast yet effective compression algorithms.  Using a small, recency-based dictionary and
 *        partial matching on common upper-bits, it is tuned to compress pages of integer- and pointer-heavy data.
 *
 * Compressed output format, in memory order
 *
 *  1. a HEADER containing four word values that descrieb the compressed buffer:
 *
 *     i.   the number of words in the compressed representation (and thus the length of the tags area, described below).
 *     ii.  an offset into the buffer saying where the dictionary index area starts.
 *     iii. an offset into the buffer saying where the low-bits area starts.
 *     iv.  an offset into the buffer saying where the low-bits area ends.
 *
 *  2. a TAGS AREA holding one 2-bit tag for each word in the original page, packed into a space where no bits are wasted.  The bit
 *     values can be:
 *       00: The value is a zero.
 *       01: The value is a partial match to some dictionary entry.
 *       10: The value does not match (exactly or partially) any dictionary entry.
 *       11: The value is an exact match to some dictionary entry.
 *
 *  3. a variable-sized FULL WORDS AREA (always word aligned and an integral number of words) holding full-word patterns that were
 *     not in the dictionary when encoded (i.e., dictionary misses).
 *
 *  4. a variable-sized DICTIONARY INDEX AREA (always word aligned and an integral number of words) holding dictionary indices,
 *     packed into words.  (Ex: If the dictionary is 16 entries and the architecture uses 64-bit words, then indices are 4-bits
 *     each, and they are packed 16 to each word.)
 *
 *  5. a variable-sized LOW BITS AREA (always word aligned and an integral number of words) holding low-bit patterns (from partial
 *     matches), packed into words with possible waste.  (Ex: If the upper 52 bits of 64-bit words are used for partial matches,
 *     then each such match yields 12 lower bits that are explicitly encoded.  These would be packed 5 to a word, consuming 60
 *     bits and leaving 4 wasted and unused.)
 **/
/* gcc -std=c99 -ggdb -DWK_DICTIONARY_NUM_SETS=1 -DWK_DICTIONARY_SET_SIZE=16 -DWK_DEBUG -o WK WK.c */
/* =============================================================================================================================== */
/* =============================================================================================================================== */
/* INCLUDES */
#include <inttypes.h> // PRIx?? formatting
#include <stdio.h>    // printf
#include <stdlib.h>   // malloc
#include <string.h>   // memcmp
#include "WK.h"
#if DICTIONARY_ORG == FULLY_ASSOCIATIVE_CONST_LOOKUP_ORG
#include "WK_hashmap.h"
#endif
/* =============================================================================================================================== */
/* =============================================================================================================================== */
/* MACROS */
/**
 * The length of the compressed representation's header, in words.
 **/
#define HEADER_AREA_SIZE 4
/**
 * The number of words into the compressed region to find the start of the tags area.
 **/
#define TAGS_AREA_OFFSET HEADER_AREA_SIZE
/**
 * \brief Calculate how long the packed tags area is/will be in a compressed representation.
 * \param num_words The number of original, uncompressed words that the compressed representation will encode.
 * \return The size of the compressed representation's tags area, in words.
 **/                              
#define TAGS_AREA_SIZE(num_words) ((num_words) * NUM_TAG_BITS / BITS_PER_WORD)
/**
 * \brief Calculate the number of words into the compressed region to find the start of the full word patterns (for misses).
 * \param num_words The number of original, uncompressed words that the compressed representation will encode.
 * \return The number of words into the comrpessed region at which the full patterns region begins.
 **/
#define FULL_PATTERNS_AREA_OFFSET(num_words) (TAGS_AREA_OFFSET + TAGS_AREA_SIZE(num_words))
/**
 * \brief Assign the number of compressed words into the compressed region's header.
 * \param compr_dest_buf The buffer into the which compressed representation is being stored.  It must be a pointer to a word array.
 * \param num_words The number of words in the original, uncompressed representation.
 **/
#define SET_NUM_WORDS(compr_dest_buf,num_words) \
        (compr_dest_buf[0] = num_words)
/**
 * \brief Assign the dictionary-index area's starting offset in the compressed region's header.
 * \param compr_dest_buf The buffer into which the compressed representation is being stored.  It must be a pointer to a word array.
 * \param index_start_addr The starting address of the dictionary-index area.
 **/
#define SET_INDEX_AREA_START(compr_dest_buf,index_start_addr)  \
        (compr_dest_buf[1] = index_start_addr - compr_dest_buf)
/**
 * \brief Assign the low-bits area's starting offset in the compressed region's header.
 * \param compr_dest_buf The buffer into which the compressed representation is being stored.  It must be a pointer to a word array.
 * \param lb_start_addr The starting address of the low-bits area.
 **/
#define SET_LOW_BITS_AREA_START(compr_dest_buf,lb_start_addr) \
        (compr_dest_buf[2] = lb_start_addr - compr_dest_buf)
/**
 * \brief Assign the low-bits area's ending offset in the compressed region's header.
 * \param compr_dest_buf The buffer into which the compressed representation is being stored.  It must be a pointer to a word array.
 * \param lb_end_addr The ending address of the low-bits area.
 **/
#define SET_LOW_BITS_AREA_END(compr_dest_buf,lb_end_addr) \
        (compr_dest_buf[3] = lb_end_addr - compr_dest_buf)
/**
 * \brief Calculate where the tags area begins.
 * \param decomp_src_buf The address of a compressed region to be decompressed.
 * \returns The address of the tags area in the compressed region.
 **/
#define TAGS_AREA_START(decomp_src_buf) \
        (decomp_src_buf + TAGS_AREA_OFFSET)
/**
 * \brief Calculate where the tags area ends.  Uses the number of words given in the header.
 * \param decomp_src_buf The address of a compressed region to be decompressed.
 * \returns The address that immediately follows the last byte of the tags area in the compressed region.
 **/
#define TAGS_AREA_END(decomp_src_buf) \
        (TAGS_AREA_START(decomp_src_buf) + TAGS_AREA_SIZE(GET_NUM_WORDS(decomp_src_buf)))
/**
 * \brief Calculate where the tag area ends.
 * \param decomp_src_buf The address of a compressed region to be decompressed.
 * \returns The address that immediately follows the last byte of the tags area in the compressed region.
 **/
#define FULL_WORD_AREA_START(the_buf) \
        TAGS_AREA_END(the_buf)
/**
 * \brief Grab, from the compressed header, the number of decompressed words represented.
 * \param decomp_src_buf The buffer that stores the compressed representation to be decompressed.
 * \return The number of words represented.
 **/
#define GET_NUM_WORDS(decomp_src_buf) (decomp_src_buf[0])
/**
 * \brief Calculate where the dictionary-index area begins.
 * \param decomp_src_buf The address of a compressed region to be decompressed.
 * \returns The address of the dictionary-index area in the compressed region.
 **/
#define INDEX_AREA_START(decomp_src_buf) \
        (decomp_src_buf + (decomp_src_buf[1]))
/**
 * \brief Calculate where the low-bits area begins.
 * \param decomp_src_buf The address of a compressed region to be decompressed.
 * \returns The address of the low-bits area in the compressed region.
 **/
#define LOW_BITS_AREA_START(decomp_src_buf) \
        (decomp_src_buf + (decomp_src_buf[2]))
/**
 * \brief Calculate where the dictionary-index area ends.
 * \param decomp_src_buf The address of a compressed region to be decompressed.
 * \returns The address that immediately follows the last byte of the dictionary-index area in the compressed region.
 **/
#define INDEX_AREA_END(the_buf) \
        LOW_BITS_AREA_START(the_buf)
/**
 * \brief Calculate where the low-bits area ends.
 * \param decomp_src_buf The address of a compressed region to be decompressed.
 * \returns The address that immediately follows the last byte of the low-bits area in the compressed region.
 **/
#define LOW_BITS_AREA_END(decomp_src_buf)     \
        (decomp_src_buf + (decomp_src_buf[3]))
/* ============================================================================================================================== */
/* ============================================================================================================================== */
/* DERIVED and PERMANENT CONSTANTS */
#define LOW_BITS_MASK ((1 << NUM_LOW_BITS) - 1)
#define HIGH_BITS_MASK (~LOW_BITS_MASK)
#define NUM_TAG_BITS 2
#define TAG_PACKING_MASK_32_BIT 0x03030303
#define TAG_PACKING_MASK_64_BIT 0x0303030303030303
typedef uint8_t WK_unpacked_tags_32_t;
typedef uint8_t WK_unpacked_tags_64_t;
/**
 * Tag values (2 bits).
 **/
#define ZERO_TAG    0x0
#define PARTIAL_TAG 0x1
#define MISS_TAG    0x2
#define EXACT_TAG   0x3
/**
 * Generalized patterns for packing k-bit values into 32- and 64-bit words.  Here, 1 <= k <= 16.
 **/
/* k = 1, 32 bit: 1 x 8 = 8, 64 bit: 1 x 8 = 8 */
typedef uint8_t WK_unpacked_1_to_32_t;
#define PACKING_MASK_1_to_32 0x01010101
typedef uint8_t WK_unpacked_1_to_64_t;
#define PACKING_MASK_1_to_64 0x0101010101010101
/* k = 2, 32 bit: 2 x 4 = 8, 64 bit: 2 x 4 = 8 */
typedef uint8_t WK_unpacked_2_to_32_t;
#define PACKING_MASK_2_to_32 0x03030303
typedef uint8_t WK_unpacked_2_to_64_t;
#define PACKING_MASK_2_to_64 0x0303030303030303
/* k = 3, 32 bit: 3 x 5 = 15, 64 bit: 3 x 21 = 63 */
typedef uint16_t WK_unpacked_3_to_32_t;
#define PACKING_MASK_3_to_32 0x00070007
typedef uint64_t WK_unpacked_3_to_64_t;
#define PACKING_MASK_3_to_64 0x0000000000000007
/* k = 4, 32 bit: 4 x 2 = 8, 64 bit: 4 x 2 = 8 */
typedef uint8_t WK_unpacked_4_to_32_t;
#define PACKING_MASK_4_to_32 0x0F0F0F0F
typedef uint8_t WK_unpacked_4_to_64_t;
#define PACKING_MASK_4_to_64 0x0F0F0F0F0F0F0F0F
/* k = 5, 32 bit: 5 x 3 = 15, 64 bit: 5 x 3 = 15 */
typedef uint16_t WK_unpacked_5_to_32_t;
#define PACKING_MASK_5_to_32 0x001F001F
typedef uint16_t WK_unpacked_5_to_64_t;
#define PACKING_MASK_5_to_64 0x001F001F001F001F
/* k = 6, 32 bit: 6 x 5 = 30, 64 bit: 6 x 5 = 30 */
typedef uint32_t WK_unpacked_6_to_32_t;
#define PACKING_MASK_6_to_32 0x0000003F
typedef uint32_t WK_unpacked_6_to_64_t;
#define PACKING_MASK_6_to_64 0x0000003F0000003F
/* k = 7, 32 bit: 7 x 1 = 7, 7 x 9 = 63 */
typedef uint8_t WK_unpacked_7_to_32_t;
#define PACKING_MASK_7_to_32 0x7F7F7F7F
typedef uint64_t WK_unpacked_7_to_64_t;
#define PACKING_MASK_7_to_64 0x000000000000007F
/* k = 8, 32 bit: 8 x 1 = 8, 8 x 1 = 8 */
typedef uint8_t WK_unpacked_8_to_32_t;
#define PACKING_MASK_8_to_32 0xFFFFFFFF
typedef uint8_t WK_unpacked_8_to_64_t;
#define PACKING_MASK_8_to_64 0xFFFFFFFFFFFFFFFF
/* k = 9, 32 bit: 9 x 3 = 27, 64 bit: 9 x 7 = 63 */
typedef uint32_t WK_unpacked_9_to_32_t;
#define PACKING_MASK_9_to_32 0x000001FF
typedef uint64_t WK_unpacked_9_to_64_t;
#define PACKING_MASK_9_to_64 0x00000000000001FF
/* k = 10, 32 bit: 10 x 3 = 30, 64 bit: 10 x 3 = 30 */
typedef uint32_t WK_unpacked_10_to_32_t;
#define PACKING_MASK_10_to_32 0x000003FF
typedef uint32_t WK_unpacked_10_to_64_t;
#define PACKING_MASK_10_to_64 0x000003FF000003FF
/* k = 11, 32 bit: 11 x 2 = 22, 64 bit: 11 x 5 = 55 */
typedef uint32_t WK_unpacked_11_to_32_t;
#define PACKING_MASK_11_to_32 0x000007FF
typedef uint32_t WK_unpacked_11_to_64_t;
#define PACKING_MASK_11_to_64 0x00000000000007FF
/* k = 12, 32 bit: 12 x 2 = 24, 64 bit = 12 x 5 = 60 */
typedef uint32_t WK_unpacked_12_to_32_t;
#define PACKING_MASK_12_to_32 0x00000FFF
typedef uint64_t WK_unpacked_12_to_64_t;
#define PACKING_MASK_12_to_64 0x0000000000000FFF
/* k = 13, 32 bit: 13 x 2 = 26, 64 bit = 13 x 2 = 26 */
typedef uint32_t WK_unpacked_13_to_32_t;
#define PACKING_MASK_13_to_32 0x00001FFF
typedef uint32_t WK_unpacked_13_to_64_t;
#define PACKING_MASK_13_to_64 0x00001FFF00001FFF
/* k = 14, 32 bit: 14 x 2 = 28, 64 bit: 14 x 2 = 28 */
typedef uint32_t WK_unpacked_14_to_32_t;
#define PACKING_MASK_14_to_32 0x00003FFF
typedef uint32_t WK_unpacked_14_to_64_t;
#define PACKING_MASK_14_to_64 0x00003FFF00003FFF
/* k = 15, 32 bit: 15 x 2 = 30, 64 bit: 15 x 2 = 30 */
typedef uint32_t WK_unpacked_15_to_32_t;
#define PACKING_MASK_15_to_32 0x00007FFF
typedef uint32_t WK_unpacked_15_to_64_t;
#define PACKING_MASK_15_to_64 0x00007FFF00007FFF
/* k = 16, 32 bit: 16 x 1 = 16, 64 bit: 16 x 1 = 16 */
typedef uint16_t WK_unpacked_16_to_32_t;
#define PACKING_MASK_16_to_32 0xFFFFFFFF
typedef uint16_t WK_unpacked_16_to_64_t;
#define PACKING_MASK_16_to_64 0xFFFFFFFFFFFFFFFF
/**
 * Critical constants for packing the dictionary index bits.  These values and masks are dependent on the dictionary size and the
 * word size.  We use the constants defined above for packing k bits into w-bit words.
 **/
#if DICTIONARY_SIZE == 4
  #define NUM_DICT_INDEX_BITS 2
  #define DICT_INDEX_PACKING_MASK_32_BIT PACKING_MASK_2_to_32
  #define DICT_INDEX_PACKING_MASK_64_BIT PACKING_MASK_2_to_64
  typedef WK_unpacked_2_to_32_t WK_unpacked_dict_index_32_t;
  typedef WK_unpacked_2_to_64_t WK_unpacked_dict_index_64_t;
#elif DICTIONARY_SIZE == 8
  #define NUM_DICT_INDEX_BITS 3
  #define DICT_INDEX_PACKING_MASK_32_BIT PACKING_MASK_3_to_32
  #define DICT_INDEX_PACKING_MASK_64_BIT PACKING_MASK_3_to_64
  typedef WK_unpacked_3_to_32_t WK_unpacked_dict_index_32_t;
  typedef WK_unpacked_3_to_64_t WK_unpacked_dict_index_64_t;
#elif DICTIONARY_SIZE == 16
  #define NUM_DICT_INDEX_BITS 4
  #define DICT_INDEX_PACKING_MASK_32_BIT PACKING_MASK_4_to_32
  #define DICT_INDEX_PACKING_MASK_64_BIT PACKING_MASK_4_to_64
  typedef WK_unpacked_4_to_32_t WK_unpacked_dict_index_32_t;
  typedef WK_unpacked_4_to_64_t WK_unpacked_dict_index_64_t;
#elif DICTIONARY_SIZE == 32
  #define NUM_DICT_INDEX_BITS 5
  #define DICT_INDEX_PACKING_MASK_32_BIT PACKING_MASK_5_to_32
  #define DICT_INDEX_PACKING_MASK_64_BIT PACKING_MASK_5_to_64
  typedef WK_unpacked_5_to_32_t WK_unpacked_dict_index_32_t;
  typedef WK_unpacked_5_to_64_t WK_unpacked_dict_index_64_t;
#elif DICTIONARY_SIZE == 64
  #define NUM_DICT_INDEX_BITS 6
  #define DICT_INDEX_PACKING_MASK_32_BIT PACKING_MASK_6_to_32
  #define DICT_INDEX_PACKING_MASK_64_BIT PACKING_MASK_6_to_64
  typedef WK_unpacked_6_to_32_t WK_unpacked_dict_index_32_t;
  typedef WK_unpacked_6_to_64_t WK_unpacked_dict_index_64_t;
#elif DICTIONARY_SIZE == 128
  #define NUM_DICT_INDEX_BITS 7
  #define DICT_INDEX_PACKING_MASK_32_BIT PACKING_MASK_7_to_32
  #define DICT_INDEX_PACKING_MASK_64_BIT PACKING_MASK_7_to_64
  typedef WK_unpacked_7_to_32_t WK_unpacked_dict_index_32_t;
  typedef WK_unpacked_7_to_64_t WK_unpacked_dict_index_64_t;
#elif DICTIONARY_SIZE == 256
  #define NUM_DICT_INDEX_BITS 8
  #define DICT_INDEX_PACKING_MASK_32_BIT PACKING_MASK_8_to_32
  #define DICT_INDEX_PACKING_MASK_64_BIT PACKING_MASK_8_to_64
  typedef WK_unpacked_8_to_32_t WK_unpacked_dict_index_32_t;
  typedef WK_unpacked_8_to_64_t WK_unpacked_dict_index_64_t;
#elif DICTIONARY_SIZE == 512
  #define NUM_DICT_INDEX_BITS 9
  #define DICT_INDEX_PACKING_MASK_32_BIT PACKING_MASK_9_to_32
  #define DICT_INDEX_PACKING_MASK_64_BIT PACKING_MASK_9_to_64
  typedef WK_unpacked_9_to_32_t WK_unpacked_dict_index_32_t;
  typedef WK_unpacked_9_to_64_t WK_unpacked_dict_index_64_t;
#elif DICTIONARY_SIZE == 1024
  #define NUM_DICT_INDEX_BITS 10
  #define DICT_INDEX_PACKING_MASK_32_BIT PACKING_MASK_10_to_32
  #define DICT_INDEX_PACKING_MASK_64_BIT PACKING_MASK_10_to_64
  typedef WK_unpacked_10_to_32_t WK_unpacked_dict_index_32_t;
  typedef WK_unpacked_10_to_64_t WK_unpacked_dict_index_64_t;
#elif DICTIONARY_SIZE == 2048
  #define NUM_DICT_INDEX_BITS 11
  #define DICT_INDEX_PACKING_MASK_32_BIT PACKING_MASK_11_to_32
  #define DICT_INDEX_PACKING_MASK_64_BIT PACKING_MASK_11_to_64
  typedef WK_unpacked_11_to_32_t WK_unpacked_dict_index_32_t;
  typedef WK_unpacked_11_to_64_t WK_unpacked_dict_index_64_t;
#elif DICTIONARY_SIZE == 4096
  #define NUM_DICT_INDEX_BITS 12
  #define DICT_INDEX_PACKING_MASK_32_BIT PACKING_MASK_12_to_32
  #define DICT_INDEX_PACKING_MASK_64_BIT PACKING_MASK_12_to_64
  typedef WK_unpacked_12_to_32_t WK_unpacked_dict_index_32_t;
  typedef WK_unpacked_12_to_64_t WK_unpacked_dict_index_64_t;
#elif DICTIONARY_SIZE == 8192
  #define NUM_DICT_INDEX_BITS 13
  #define DICT_INDEX_PACKING_MASK_32_BIT PACKING_MASK_13_to_32
  #define DICT_INDEX_PACKING_MASK_64_BIT PACKING_MASK_13_to_64
  typedef WK_unpacked_13_to_32_t WK_unpacked_dict_index_32_t;
  typedef WK_unpacked_13_to_64_t WK_unpacked_dict_index_64_t;
#elif DICTIONARY_SIZE == 16384
  #define NUM_DICT_INDEX_BITS 14
  #define DICT_INDEX_PACKING_MASK_32_BIT PACKING_MASK_14_to_32
  #define DICT_INDEX_PACKING_MASK_64_BIT PACKING_MASK_14_to_64
  typedef WK_unpacked_14_to_32_t WK_unpacked_dict_index_32_t;
  typedef WK_unpacked_14_to_64_t WK_unpacked_dict_index_64_t;
#elif DICTIONARY_SIZE == 32768
  #define NUM_DICT_INDEX_BITS 15
  #define DICT_INDEX_PACKING_MASK_32_BIT PACKING_MASK_15_to_32
  #define DICT_INDEX_PACKING_MASK_64_BIT PACKING_MASK_15_to_64
  typedef WK_unpacked_15_to_32_t WK_unpacked_dict_index_32_t;
  typedef WK_unpacked_15_to_64_t WK_unpacked_dict_index_64_t;
#elif DICTIONARY_SIZE == 65536
  #define NUM_DICT_INDEX_BITS 16
  #define DICT_INDEX_PACKING_MASK_32_BIT PACKING_MASK_16_to_32
  #define DICT_INDEX_PACKING_MASK_64_BIT PACKING_MASK_16_to_64
  typedef WK_unpacked_16_to_32_t WK_unpacked_dict_index_32_t;
  typedef WK_unpacked_16_to_64_t WK_unpacked_dict_index_64_t;
#endif
/**
 * The number of low bits determines how those values are recorded in unpacked form (into what type of array).  This type may also
 * depend on the word size.  Again, these values and masks are taken from the above generic macros and types for packing k-bit
 * values into w-bit words.
 *
 * [SFHK: It would seem than any fewer than 4 lower bits for partial matches would be nuts.  More than 16 also seems unlikely,
 *        but may be worth exploring some day, especially for dividing words into multiple splits.]
 **/
#if NUM_LOW_BITS == 4
  #define LOW_BITS_PACKING_MASK_32_BIT PACKING_MASK_4_to_32
  #define LOW_BITS_PACKING_MASK_64_BIT PACKING_MASK_4_to_64
  typedef WK_unpacked_4_to_32_t WK_unpacked_low_bits_32_t;
  typedef WK_unpacked_4_to_64_t WK_unpacked_low_bits_64_t;
#elif NUM_LOW_BITS == 5
  #define LOW_BITS_PACKING_MASK_32_BIT PACKING_MASK_5_to_32
  #define LOW_BITS_PACKING_MASK_64_BIT PACKING_MASK_5_to_64
  typedef WK_unpacked_5_to_32_t WK_unpacked_low_bits_32_t;
  typedef WK_unpacked_5_to_64_t WK_unpacked_low_bits_64_t;
#elif NUM_LOW_BITS == 6
  #define LOW_BITS_PACKING_MASK_32_BIT PACKING_MASK_6_to_32
  #define LOW_BITS_PACKING_MASK_64_BIT PACKING_MASK_6_to_64
  typedef WK_unpacked_6_to_32_t WK_unpacked_low_bits_32_t;
  typedef WK_unpacked_6_to_64_t WK_unpacked_low_bits_64_t;
#elif NUM_LOW_BITS == 7
  #define LOW_BITS_PACKING_MASK_32_BIT PACKING_MASK_7_to_32
  #define LOW_BITS_PACKING_MASK_64_BIT PACKING_MASK_7_to_64
  typedef WK_unpacked_7_to_32_t WK_unpacked_low_bits_32_t;
  typedef WK_unpacked_7_to_64_t WK_unpacked_low_bits_64_t;
#elif NUM_LOW_BITS == 8
  #define LOW_BITS_PACKING_MASK_32_BIT PACKING_MASK_8_to_32
  #define LOW_BITS_PACKING_MASK_64_BIT PACKING_MASK_8_to_64
  typedef WK_unpacked_8_to_32_t WK_unpacked_low_bits_32_t;
  typedef WK_unpacked_8_to_64_t WK_unpacked_low_bits_64_t;
#elif NUM_LOW_BITS == 9
  #define LOW_BITS_PACKING_MASK_32_BIT PACKING_MASK_9_to_32
  #define LOW_BITS_PACKING_MASK_64_BIT PACKING_MASK_9_to_64
  typedef WK_unpacked_9_to_32_t WK_unpacked_low_bits_32_t;
  typedef WK_unpacked_9_to_64_t WK_unpacked_low_bits_64_t;
#elif NUM_LOW_BITS == 10
  #define LOW_BITS_PACKING_MASK_32_BIT PACKING_MASK_10_to_32
  #define LOW_BITS_PACKING_MASK_64_BIT PACKING_MASK_10_to_64
  typedef WK_unpacked_10_to_32_t WK_unpacked_low_bits_32_t;
  typedef WK_unpacked_10_to_64_t WK_unpacked_low_bits_64_t;
#elif NUM_LOW_BITS == 11
  #define LOW_BITS_PACKING_MASK_32_BIT PACKING_MASK_11_to_32
  #define LOW_BITS_PACKING_MASK_64_BIT PACKING_MASK_11_to_64
  typedef WK_unpacked_11_to_32_t WK_unpacked_low_bits_32_t;
  typedef WK_unpacked_11_to_64_t WK_unpacked_low_bits_64_t;
#elif NUM_LOW_BITS == 12
  #define LOW_BITS_PACKING_MASK_32_BIT PACKING_MASK_12_to_32
  #define LOW_BITS_PACKING_MASK_64_BIT PACKING_MASK_12_to_64
  typedef WK_unpacked_12_to_32_t WK_unpacked_low_bits_32_t;
  typedef WK_unpacked_12_to_64_t WK_unpacked_low_bits_64_t;
#elif NUM_LOW_BITS == 13
  #define LOW_BITS_PACKING_MASK_32_BIT PACKING_MASK_13_to_32
  #define LOW_BITS_PACKING_MASK_64_BIT PACKING_MASK_13_to_64
  typedef WK_unpacked_13_to_32_t WK_unpacked_low_bits_32_t;
  typedef WK_unpacked_13_to_64_t WK_unpacked_low_bits_64_t;
#elif NUM_LOW_BITS == 14
  #define LOW_BITS_PACKING_MASK_32_BIT PACKING_MASK_14_to_32
  #define LOW_BITS_PACKING_MASK_64_BIT PACKING_MASK_14_to_64
  typedef WK_unpacked_14_to_32_t WK_unpacked_low_bits_32_t;
  typedef WK_unpacked_14_to_64_t WK_unpacked_low_bits_64_t;
#elif NUM_LOW_BITS == 15
  #define LOW_BITS_PACKING_MASK_32_BIT PACKING_MASK_15_to_32
  #define LOW_BITS_PACKING_MASK_64_BIT PACKING_MASK_15_to_64
  typedef WK_unpacked_15_to_32_t WK_unpacked_low_bits_32_t;
  typedef WK_unpacked_15_to_64_t WK_unpacked_low_bits_64_t;
#elif NUM_LOW_BITS == 16
  #define LOW_BITS_PACKING_MASK_32_BIT PACKING_MASK_16_to_32
  #define LOW_BITS_PACKING_MASK_64_BIT PACKING_MASK_16_to_64
  typedef WK_unpacked_16_to_32_t WK_unpacked_low_bits_32_t;
  typedef WK_unpacked_16_to_64_t WK_unpacked_low_bits_64_t;
#endif
/**
 * With potential types and masks set, choose which to use based on the actual word size.
 **/
#if BITS_PER_WORD == 32
  #define TAG_PACKING_MASK            TAG_PACKING_MASK_32_BIT
  typedef WK_unpacked_tags_32_t       WK_unpacked_tags_t;
  #define DICT_INDEX_PACKING_MASK     DICT_INDEX_PACKING_MASK_32_BIT
  typedef WK_unpacked_dict_index_32_t WK_unpacked_dict_index_t;
  #define LOW_BITS_PACKING_MASK       LOW_BITS_PACKING_MASK_32_BIT
  typedef WK_unpacked_low_bits_32_t   WK_unpacked_low_bits_t;
#elif BITS_PER_WORD == 64
  #define TAG_PACKING_MASK            TAG_PACKING_MASK_64_BIT
  typedef WK_unpacked_tags_64_t       WK_unpacked_tags_t;
  #define DICT_INDEX_PACKING_MASK     DICT_INDEX_PACKING_MASK_64_BIT
  typedef WK_unpacked_dict_index_64_t WK_unpacked_dict_index_t;
  #define LOW_BITS_PACKING_MASK       LOW_BITS_PACKING_MASK_64_BIT
  typedef WK_unpacked_low_bits_64_t   WK_unpacked_low_bits_t;
#endif
/* ============================================================================================================================== */
/* ============================================================================================================================== */
/* Global macros */
/**
 * Shift out the low bits of a pattern to give the high bits pattern.  The stripped patterns are used for initial tests of partial
 * matches.
 **/
#define HIGH_BITS(word_pattern) (word_pattern >> NUM_LOW_BITS)
/**
 * Eliminate the high bits of a pattern so the low order bits can be included in an encoding of a partial match.
 **/
#define LOW_BITS(word_pattern) (word_pattern & LOW_BITS_MASK)
#if defined WK_DEBUG
  #define DEBUG_PRINT_MSG(string) printf(string)
  #define DEBUG_PRINT_VAL(string,value) printf("%s0x%" WORD_FORMAT_WIDTH PRIxWORD "\n", string, value)
#else
  #define DEBUG_PRINT_MSG(string)
  #define DEBUG_PRINT_VAL(string,value)
#endif
#if defined TEMP_DEBUG
#define TEST_PRINT_MSG(string) {					\
    printf(string);							\
    fflush(stdout);							\
  }
#define TEST_PRINT_VAL(string,value) {					\
    printf("%s0x%" WORD_FORMAT_WIDTH PRIxWORD "\n", string, value);	\
    fflush(stdout);							\
  }
#else
  #define TEST_PRINT_MSG(string)
  #define TEST_PRINT_VAL(string,value)
#endif
#if (DICTIONARY_ORG == DIRECT_MAPPED_ORG) || (DICTIONARY_ORG == SET_ASSOCIATIVE_ORG)
/**
 * The table that hashes some byte from its given value (see HASH_TO_DICT_ENTRY, below, which uses the lower byte of the upper bits)
 * to a dictionary index.  The standard lookup table simply maps these byte values randomly across the dictionary indices; the zero
 * lookup table (which, oddly, is the default) maps only the byte value 0x00 to dictionary index 0, and all others to randomly
 * chosen, non-0 byte values to the non-0 dictionary indices.  Zero is a common and important enough value that this special lookup
 * table arrangement is likely worthwhile.
 *
 * This sequence of lookup tables, standard and zero, across the range of available dictionary sizes, was generated using the Python
 * script 'hash_lookup_gen.py'.
 **/
#include "hash-lookup-table.h"
/* Assume that the low byte of the high bits are used to hash into the lookup table. */
#define HASH_TO_SET(pattern)		\
  (hash_lookup_table[HIGH_BITS(pattern) & 0xFF])
#endif /* DIRECT_MAPPED_ORG || SET_ASSOCIATIVE_ORG */
/**
 * EMIT... emit bytes or words into the intermediate arrays
 **/
#define EMIT_BYTE(fill_ptr,byte_value) {*fill_ptr++ = byte_value;}
#define EMIT_WORD(fill_ptr,word_value) {*fill_ptr++ = word_value;}
/**
 * RECORD... record the results of modeling in the intermediate arrays
 */
#define RECORD_ZERO {             \
    EMIT_BYTE(next_tag,ZERO_TAG); \
  }
#define RECORD_EXACT(dict_index) {          \
    EMIT_BYTE(next_tag,EXACT_TAG);	    \
    EMIT_BYTE(next_dict_index,(dict_index));	\
  }
#define RECORD_PARTIAL(dict_index,low_bits_pattern) { \
    EMIT_BYTE(next_tag,PARTIAL_TAG);		      \
    EMIT_BYTE(next_dict_index,(dict_index));	      \
    EMIT_WORD(next_low_bits,(low_bits_pattern));      \
  }
#define RECORD_MISS(word_pattern) {           \
    EMIT_BYTE(next_tag,MISS_TAG);             \
    EMIT_WORD(next_full_patt,(word_pattern)); \
  }
/* ============================================================================================================================== */
/* ============================================================================================================================== */
/* DICTIONARY MACROS */
/* ===== DIRECT MAPPED DICTIONARY ===== */
#if DICTIONARY_ORG == DIRECT_MAPPED_ORG
/* For a direct-mapped dictionary, set a given entry to the given word. */
#define DICT_SET_VALUE(entry_ptr,new_word)	\
  *entry_ptr = new_word;
#define DICT_GET_VALUE(entry_ptr) (*entry_ptr)
#define DICT_MOVE_TO_FRONT(entry_ptr)
#define DICT_CREATE()     					    \
    dictionary_element_s dictionary[DICTIONARY_SIZE];		    \
    unsigned int hash_lookup_table [] = HASH_LOOKUP_TABLE_CONTENTS;
#define DICT_INITIALIZE() {	                \
    for (int i = 0; i < DICTIONARY_SIZE; ++i) { \
      dictionary[i] = 1;	                \
    }						\
  }
/* For a direct-mapped dictionary, the set *is* the entry.  Find that entry and grab its word. */
#define DICT_LOOKUP(original_word)					\
  WK_unpacked_dict_index_t dict_index = HASH_TO_SET(original_word);	\
  dictionary_element_s* dict_ptr      = dictionary + dict_index;	\
  WK_word dict_word                   = DICT_GET_VALUE(dict_ptr);    
/**
 * For a direct-mapped dictionary, find the entry (which is the set) and update it with the new word.  Nothing else needs to be done
 * to choose an entry to be replaced.
 **/
#define DICT_UPDATE(new_word) {						\
    WK_unpacked_dict_index_t dict_index = HASH_TO_SET(new_word);	\
    WK_word* dict_ptr                   = dictionary + dict_index;	\
    DICT_SET_VALUE(dict_ptr,new_word);					\
  }
/* ===== FULLY ASSOCIATIVE DICTIONARY WITH LINEAR-TIME LOOKUP ===== */
#elif DICTIONARY_ORG == FULLY_ASSOCIATIVE_LINEAR_LOOKUP_ORG
#define DICT_SET_VALUE(entry_ptr,new_word) \
  entry_ptr->value = new_word;
#define DICT_GET_VALUE(entry_ptr) (entry_ptr->value)
#define DICT_CREATE()                               \
  dictionary_element_s dictionary[DICTIONARY_SIZE]; \
  dictionary_element_s* dict_lru_head = NULL;	    \
  dictionary_element_s* dict_lru_tail = NULL;
#define DICT_INITIALIZE() {				     \
    for (int i = 0; i < DICTIONARY_SIZE; ++i) {		     \
      dictionary_element_s* init_element = dictionary + i;   \
      DICT_SET_VALUE(init_element, 1);			     \
      init_element->next  = init_element + 1;		     \
      init_element->prev  = init_element - 1;		     \
    }							     \
    dictionary[0].prev                    = NULL;	     \
    dictionary[DICTIONARY_SIZE - 1].next  = NULL;	     \
    dict_lru_head = dictionary;				     \
    dict_lru_tail = dictionary + (DICTIONARY_SIZE - 1);	     \
  }
#define DICT_MOVE_TO_FRONT(dict_ptr) {		\
    if (dict_ptr != dict_lru_head) {		\
      dict_ptr->prev->next = dict_ptr->next;	\
      if (dict_ptr != dict_lru_tail) {		\
	dict_ptr->next->prev = dict_ptr->prev;	\
      } else {					\
	dict_lru_tail = dict_ptr->prev;		\
      }						\
      dict_lru_head->prev = dict_ptr;		\
      dict_ptr->next = dict_lru_head;		\
      dict_ptr->prev = NULL;			\
      dict_lru_head = dict_ptr;			\
    }						\
  }
/* Try to find an entry based on the upper bits.  Move that entry to the front of the LRU queue. */
#define DICT_LOOKUP(original_word)					\
  WK_word original_upper = HIGH_BITS(original_word);			\
  dictionary_element_s* dict_ptr = NULL;						\
  WK_word dict_word;							\
  for (dictionary_element_s* entry_ptr = dict_lru_head; entry_ptr != NULL; entry_ptr = entry_ptr->next) { \
    dict_word = DICT_GET_VALUE(entry_ptr);				\
    WK_word dict_upper = HIGH_BITS(dict_word);				\
    if (original_upper == dict_upper) {					\
      dict_ptr = entry_ptr;						\
      break;								\
    }									\
  }									\
  if (dict_ptr == NULL) {						\
    dict_ptr = dict_lru_tail;						\
  }									\
  WK_unpacked_dict_index_t dict_index = dict_ptr - dictionary;		\
  DICT_MOVE_TO_FRONT(dict_ptr);
#define DICT_UPDATE(new_word) {						\
    WK_word new_upper = HIGH_BITS(new_word);				\
    dictionary_element_s* dict_ptr = NULL;				\
    WK_word dict_word;							\
    for (dictionary_element_s* entry_ptr = dict_lru_head; entry_ptr != NULL; entry_ptr = entry_ptr->next) { \
      dict_word = DICT_GET_VALUE(entry_ptr);				\
      WK_word dict_upper = HIGH_BITS(dict_word);			\
      if (new_upper == dict_upper) {					\
	dict_ptr = entry_ptr;						\
	break;								\
      }									\
    }									\
    if (dict_ptr == NULL) {						\
      dict_ptr = dict_lru_tail;						\
    }									\
    DICT_MOVE_TO_FRONT(dict_ptr);					\
    DICT_SET_VALUE(dict_ptr, new_word);					\
  }
/* ===== FULLY ASSOCIATIVE DICTIONARY WITH CONSTANT-TIME LOOKUP ===== */
#elif DICTIONARY_ORG == FULLY_ASSOCIATIVE_CONST_LOOKUP_ORG
#define DICT_SET_VALUE(entry_ptr,new_word) \
  entry_ptr->value = new_word;
#define DICT_GET_VALUE(entry_ptr) (entry_ptr->value)
#define DICT_CREATE()                               \
  dictionary_element_s dictionary[DICTIONARY_SIZE]; \
  dictionary_element_s* dict_lru_head = NULL;	    \
  dictionary_element_s* dict_lru_tail = NULL;	    \
  HASHMAP_CREATE(dict_map);
#define DICT_INITIALIZE() {				     \
    for (int i = 0; i < DICTIONARY_SIZE; ++i) {		     \
      dictionary_element_s* init_element = dictionary + i;   \
      DICT_SET_VALUE(init_element, 1);			     \
      init_element->next  = init_element + 1;		     \
      init_element->prev  = init_element - 1;		     \
    }							     \
    dictionary[0].prev                    = NULL;	     \
    dictionary[DICTIONARY_SIZE - 1].next  = NULL;	     \
    dict_lru_head = dictionary;				     \
    dict_lru_tail = dictionary + (DICTIONARY_SIZE - 1);	     \
    HASHMAP_INIT(dict_map);					     \
  }
#define DICT_MOVE_TO_FRONT(dict_ptr) {		\
    if (dict_ptr != dict_lru_head) {		\
      dict_ptr->prev->next = dict_ptr->next;	\
      if (dict_ptr != dict_lru_tail) {		\
	dict_ptr->next->prev = dict_ptr->prev;	\
      } else {					\
	dict_lru_tail = dict_ptr->prev;		\
      }						\
      dict_lru_head->prev = dict_ptr;		\
      dict_ptr->next = dict_lru_head;		\
      dict_ptr->prev = NULL;			\
      dict_lru_head = dict_ptr;			\
    }						\
  }
/* Try to find an entry based on the upper bits.  Move that entry to the front of the LRU queue.  Use the hashmap for constant time
   lookup; if the item isn't found, remove the hashmap entry to the LRU tail (being replaced) and add the entry to the new head. */
#define DICT_LOOKUP(original_word)					\
  WK_word original_upper = HIGH_BITS(original_word);			\
  dictionary_element_s* dict_ptr = NULL;				\
  WK_word dict_word;							\
  HASHMAP_LOOKUP(dict_ptr, dict_map, original_upper);			\
  if (dict_ptr == NULL) {						\
    dict_ptr = dict_lru_tail;						\
    HASHMAP_DELETE(dict_map, DICT_GET_VALUE(dict_ptr));			\
    HASHMAP_INSERT(dict_map, original_upper, dict_ptr);			\
  }									\
  WK_unpacked_dict_index_t dict_index = dict_ptr - dictionary;		\
  DICT_MOVE_TO_FRONT(dict_ptr);
/* SFHK: This macro is used only in decompression, where the hashmap has no role. */
#define DICT_UPDATE(new_word) {						\
    WK_word new_upper = HIGH_BITS(new_word);				\
    dictionary_element_s* dict_ptr = NULL;				\
    WK_word dict_word;							\
    for (dictionary_element_s* entry_ptr = dict_lru_head; entry_ptr != NULL; entry_ptr = entry_ptr->next) { \
      dict_word = DICT_GET_VALUE(entry_ptr);				\
      WK_word dict_upper = HIGH_BITS(dict_word);			\
      if (new_upper == dict_upper) {					\
	dict_ptr = entry_ptr;						\
	break;								\
      }									\
    }									\
    if (dict_ptr == NULL) {						\
      dict_ptr = dict_lru_tail;						\
    }									\
    DICT_MOVE_TO_FRONT(dict_ptr);					\
    DICT_SET_VALUE(dict_ptr, new_word);					\
  }
/* ===== SET ASSOCIATIVE DICTIONARY  ===== */
#elif DICTIONARY_ORG == SET_ASSOCIATIVE_ORG
#define DICT_SET_VALUE(entry_ptr,new_word) \
  entry_ptr->value = new_word;
#define DICT_GET_VALUE(entry_ptr) (entry_ptr->value)
#define DICT_CREATE()							\
  dictionary_element_s dictionary[DICTIONARY_SIZE];			\
  dictionary_element_s* set_lru_head[DICTIONARY_NUM_SETS];		\
  dictionary_element_s* set_lru_tail[DICTIONARY_NUM_SETS];		\
  unsigned int hash_lookup_table [] = HASH_LOOKUP_TABLE_CONTENTS;
#define DICT_INITIALIZE() {						\
    for (int i = 0; i < DICTIONARY_NUM_SETS; ++i) {			\
      dictionary_element_s* set_base  = dictionary + (i * DICTIONARY_SET_SIZE); \
      dictionary_element_s* set_limit = set_base + DICTIONARY_SET_SIZE; \
      dictionary_element_s* set_last = set_limit - 1;			\
      for (dictionary_element_s* init_element = set_base; init_element != set_limit; ++init_element) { \
	DICT_SET_VALUE(init_element, 1);				\
	init_element->next  = init_element + 1;				\
	init_element->prev  = init_element - 1;				\
      }									\
      set_base->prev = NULL;						\
      set_last->next = NULL;						\
      set_lru_head[i] = set_base;					\
      set_lru_tail[i] = set_last;					\
    }									\
  }
#define DICT_MOVE_TO_FRONT(dict_ptr) {					\
    int dict_index = dict_ptr - dictionary;				\
    int set_number = dict_index / DICTIONARY_SET_SIZE;			\
    if (dict_ptr != set_lru_head[set_number]) {				\
      dict_ptr->prev->next = dict_ptr->next;				\
      if (dict_ptr != set_lru_tail[set_number]) {			\
	dict_ptr->next->prev = dict_ptr->prev;				\
      } else {								\
	set_lru_tail[set_number] = dict_ptr->prev;			\
      }									\
      set_lru_head[set_number]->prev = dict_ptr;			\
      dict_ptr->next                 = set_lru_head[set_number];	\
      dict_ptr->prev                 = NULL;				\
      set_lru_head[set_number]       = dict_ptr;			\
    }									\
  }
/* Try to find an entry in this set based on the upper bits.  Move that entry to the front of the set's LRU queue. */
#define DICT_LOOKUP(original_word)					\
  WK_word original_upper         = HIGH_BITS(original_word);		\
  dictionary_element_s* dict_ptr = NULL;				\
  unsigned int set_number        = HASH_TO_SET(original_word);		\
  WK_word dict_word;							\
  for (dictionary_element_s* entry_ptr = set_lru_head[set_number]; entry_ptr != NULL; entry_ptr = entry_ptr->next) { \
    dict_word = DICT_GET_VALUE(entry_ptr);				\
    WK_word dict_upper = HIGH_BITS(dict_word);				\
    if (original_upper == dict_upper) {					\
      dict_ptr = entry_ptr;						\
      break;								\
    }									\
  }									\
  if (dict_ptr == NULL) {						\
    dict_ptr = set_lru_tail[set_number];				\
  }									\
  DICT_MOVE_TO_FRONT(dict_ptr);						\
  WK_unpacked_dict_index_t dict_index = dict_ptr - dictionary;
#define DICT_UPDATE(new_word) {						\
    WK_word new_upper = HIGH_BITS(new_word);				\
    int set_number = HASH_TO_SET(new_word);				\
    dictionary_element_s* dict_ptr = NULL;				\
    WK_word dict_word;							\
    for (dictionary_element_s* entry_ptr = set_lru_head[set_number]; entry_ptr != NULL; entry_ptr = entry_ptr->next) { \
      dict_word = DICT_GET_VALUE(entry_ptr);				\
      WK_word dict_upper = HIGH_BITS(dict_word);			\
      if (new_upper == dict_upper) {				\
	dict_ptr = entry_ptr;						\
	break;								\
      }									\
    }									\
    if (dict_ptr == NULL) {						\
      dict_ptr = set_lru_tail[set_number];						\
    }									\
    DICT_MOVE_TO_FRONT(dict_ptr);					\
    DICT_SET_VALUE(dict_ptr, new_word);					\
  }
#else
#error "Unknown dictionary organization."
#endif /* DICTIONARY_ORG */
/* ============================================================================================================================== */
/* ============================================================================================================================== */
/* PACKING and UNPACKING FUNCTIONS */
/**
 * Pack values in some loose array representation into a tight array of words, with multiple values per word.
 *
 * \param src_buf The original, unpacked sequence of values.  The original may be an array of another type, but here we move
 *                through the array as though it were words, thus potentially packing many words in parallel.
 * \param src_end A pointer to the end of the unpacked values.
 * \param dest_buf The array of words into which to write the packed values.
 * \param bits_per_value The number of meaningful bits to be maintained in each value from the original source array.
 * \param unpacked_entry_size The size of each entry in the original source array.
 * \return A pointer to the end of the packed portion of the destination array.
 **/
WK_word*
WK_pack_bits (WK_word* src_buf,
	      WK_word* src_end,
	      WK_word* dest_buf,
	      unsigned int bits_per_value,
	      unsigned int unpacked_entry_size) {
  WK_word* src_next = src_buf;
  WK_word* dest_next = dest_buf;
  int reps = unpacked_entry_size / bits_per_value;
  
  DEBUG_PRINT_MSG("WK_pack_bits(): About to pack...\n");
  DEBUG_PRINT_VAL("  bits_per_value      = ", (WK_word)bits_per_value);
  DEBUG_PRINT_VAL("  unpacked_entry_size = ", (WK_word)unpacked_entry_size);
  DEBUG_PRINT_VAL("  reps                = ", (WK_word)reps);
  while (src_next < src_end) {
    WK_word temp = *src_next;
    int i;
    for (i = 1; i < reps; ++i) {
      temp |= (src_next[i] << (i * bits_per_value));
    }
    *dest_next = temp;
    ++dest_next;
    src_next += reps;
  }
  return dest_next;
}
WK_word*
WK_unpack_bits (WK_word* src_buf,
		WK_word* src_end,
		WK_word* dest_buf,
		unsigned int bits_per_value,
		unsigned int unpacked_entry_size,
		WK_word packing_mask) {
  WK_word* src_next = src_buf;
  WK_word* dest_next = dest_buf;
  int reps = unpacked_entry_size / bits_per_value;
  DEBUG_PRINT_MSG("WK_unpack_bits(): About to unpack...\n");
  DEBUG_PRINT_VAL("  bits_per_value      = ", (WK_word)bits_per_value);
  DEBUG_PRINT_VAL("  unpacked_entry_size = ", (WK_word)unpacked_entry_size);
  DEBUG_PRINT_VAL("  reps                = ", (WK_word)reps);
  DEBUG_PRINT_VAL("  unpacking mask      = ", packing_mask);
  while (src_next < src_end) {
    WK_word temp = src_next[0];
    int i;
    for (i = 0; i < reps; ++i) {
      dest_next[i] = (temp >> (i * bits_per_value)) & packing_mask;
    }
    ++src_next;
    dest_next += reps;
  }
  return dest_next;
}
/* ============================================================================================================================== */
/* ============================================================================================================================== */
/* COMPRESSION AND DECOMPRESSION FUNCTIONS */
/**
 * \brief Compress a source buffer into a destination buffer.
 * \param src_buf The source buffer of uncompressed data.
 * \param dst_buf The destination buffer that will contain the compressed representation.
 * \param num_words The number of words in the source to compress.
 * \return A pointer to the end of the destination buffer, marking the end of the compressed reprensetation generated.
 **/
WK_word*
WK_compress (WK_word* src_buf,
	     WK_word* dest_buf,
	     unsigned int num_words) {
  /* ============================================================== */
  /* PHASE 1: Model by matching against a recency-based dictionary. */
  /* ============================================================== */
  /* Create the dictionary. */
  DICT_CREATE();
  /*
   * Arrays that hold output data in intermediate form during modeling and whose contents are packed into the actual output after
   * modeling.
   */
  WK_unpacked_tags_t       temp_tags        [num_words];
  WK_unpacked_dict_index_t temp_dict_indices[num_words];
  WK_unpacked_low_bits_t   temp_low_bits    [num_words];
  /*
   * Keep track of how far into the compressed buffer we've gone.
   */
  WK_word* boundary_tmp;
  /*
   * Fill pointers for filling temporary arrays (of queue positions and low bits, unpacked) during encoding.  Full words go straight
   * to the destination buffer area reserved for them.  (Right after where the tags go.)
   */
  WK_word*                  next_full_patt  = dest_buf + FULL_PATTERNS_AREA_OFFSET(num_words);
  WK_unpacked_tags_t*       next_tag        = temp_tags;
  WK_unpacked_dict_index_t* next_dict_index = temp_dict_indices;
  WK_unpacked_low_bits_t*   next_low_bits   = temp_low_bits;
  WK_word*                  end_of_input    = src_buf + num_words;
  int                       stride_offset   = 0;
  
  /* Preload the dictionary.  Candidate for loop unrolling.*/
  DICT_INITIALIZE();
  DEBUG_PRINT_MSG("\nIn WK_compress\n");
  DEBUG_PRINT_VAL("src_buf        = ", (WK_word)src_buf);
  DEBUG_PRINT_VAL("dictionary     = ", (WK_word)dictionary);
  DEBUG_PRINT_VAL("dest_buf       = ", (WK_word)dest_buf);
  DEBUG_PRINT_VAL("next_full_patt = ", (WK_word)next_full_patt);
  while (stride_offset < WK_STRIDE) {
    WK_word* next_input_word = src_buf + stride_offset;
    while (next_input_word < end_of_input) {
      /* Attempt to look up this word in the dictionary. */
      WK_word input_word = *next_input_word;
      DICT_LOOKUP(input_word);
      /**
       * Determine what kind of value we have, as well as what kind of dictionary hit (or not), in order to determine what modeling
       * to record.
       **/
      if (input_word == dict_word) {
	RECORD_EXACT(dict_index);
	DICT_MOVE_TO_FRONT(dict_ptr);
      } else if (input_word == 0) {
	RECORD_ZERO;
      } else {
	WK_word input_high_bits = HIGH_BITS(input_word);
	if (input_high_bits == HIGH_BITS(dict_word)) {
	  RECORD_PARTIAL(dict_index, LOW_BITS(input_word));
	} else {
	  RECORD_MISS(input_word);
	}
	DICT_SET_VALUE(dict_ptr, input_word);
	DICT_MOVE_TO_FRONT(dict_ptr);
      }
      next_input_word += WK_STRIDE;
    } /* while next_input_word */
    ++stride_offset;
  } /* while stride_offset */
  DEBUG_PRINT_MSG("AFTER MODELING in WK_compress()\n");
  DEBUG_PRINT_VAL("num tags         = ", (WK_word)(next_tag - temp_tags));
  DEBUG_PRINT_VAL("num dict indices = ", (WK_word)(next_dict_index - temp_dict_indices));
  DEBUG_PRINT_VAL("num low bits     = ", (WK_word)(next_low_bits - temp_low_bits));
  DEBUG_PRINT_VAL("num full patts   = ", (WK_word)(next_full_patt - (dest_buf + FULL_PATTERNS_AREA_OFFSET(num_words))));
  
#if WK_DEBUG
  {
    WK_word* whole_words  = dest_buf + FULL_PATTERNS_AREA_OFFSET(num_words);
    printf("First 10 tags:\n");
    for (int i = 0; i < 10; ++i)
      printf("  tag[%d]         = 0x%" WORD_FORMAT_WIDTH PRIxWORD "\n", i, (WK_word)temp_tags[i]);
    printf("First 10 dictionary indices:\n");
    for (int i = 0; i < 10; ++i)
      printf("  dict_index[%d]  = 0x%" WORD_FORMAT_WIDTH PRIxWORD "\n", i, (WK_word)temp_dict_indices[i]);  
    printf("First 10 low bits:\n");
    for (int i = 0; i < 10; ++i)
      printf("  low_bits[%d]    = 0x%" WORD_FORMAT_WIDTH PRIxWORD "\n", i, (WK_word)temp_low_bits[i]);  
    printf("First 10 whole words:\n");
    for (int i = 0; i < 10; ++i)
      printf("  whole_words[%d] = 0x%" WORD_FORMAT_WIDTH PRIxWORD "\n", i, whole_words[i]);  
  }
#endif // WK_DEBUG
  /* ====================================================================================== */
  /* PHASE 2: Encode the modeling, packing the compressed bits into a tight representation. */
  /* ====================================================================================== */
  /*
   * Record (into the header):
   *   (1) The number of original, uncompressed words being processed.
   *   (2) Where we stopped writing full words, which is where we will pack the queue positions.  (Recall that we wrote the full
   *       words directly into the dest buffer during modeling.)
   */
  SET_NUM_WORDS(dest_buf,num_words);
  SET_INDEX_AREA_START(dest_buf,next_full_patt);
  /*
   * Pack the tags into the tags area, between the page header and the full words area.  We don't pad for the packer because we
   * assume that the compressed page's size (in bytes) is a multiple of the word size.
   */     
  boundary_tmp = WK_pack_bits((WK_word*)temp_tags,
			      (WK_word*)next_tag,
			      dest_buf + HEADER_AREA_SIZE,
			      NUM_TAG_BITS,
			      sizeof(WK_unpacked_tags_t) * BITS_PER_BYTE);
  /*
   * Pack the dictionary indices into the area just after the full words.  We have to round up the source region to the number of
   * 'reps' (as calculated in the bit packing/unpacking functions), filling in zeroes in the trailing entries to avoid ill effects
   * during packing from extraneous non-zero values.
   */
  {
    unsigned int num_entries = next_dict_index - temp_dict_indices;
    unsigned int reps = (sizeof(WK_unpacked_dict_index_t) * BITS_PER_BYTE) / NUM_DICT_INDEX_BITS;
    while (num_entries % reps != 0) {
      *next_dict_index = 0;
      ++next_dict_index;
      ++num_entries;
    }
    boundary_tmp = WK_pack_bits((WK_word*)temp_dict_indices,
				(WK_word*)next_dict_index,
				next_full_patt,
				NUM_DICT_INDEX_BITS,
				sizeof(WK_unpacked_dict_index_t) * BITS_PER_BYTE);
    /* Record (into the header) where we stopped packing queue positions, which is where we will start packing low bits. */
    SET_LOW_BITS_AREA_START(dest_buf,boundary_tmp);
  }
  /*
   * Pack the low bit patterns into the area just after the queue positions.  We have to round up the source region to a multiple of
   * the number of words per packed together (the 'reps' in the packing/unpacking functions).  Zero-fill trailing entries to avoid
   * ill effects during the packing.
   */
  {
    unsigned int num_entries = next_low_bits - temp_low_bits;
    unsigned int reps = (sizeof(WK_unpacked_low_bits_t) * BITS_PER_BYTE) / NUM_LOW_BITS;
    while (num_entries % reps != 0) {
      *next_low_bits = 0;
      ++next_low_bits;
      ++num_entries;
    }
    boundary_tmp = WK_pack_bits((WK_word*)temp_low_bits,
				(WK_word*)next_low_bits,
				boundary_tmp,
				NUM_LOW_BITS,
				sizeof(WK_unpacked_low_bits_t) * BITS_PER_BYTE);
    SET_LOW_BITS_AREA_END(dest_buf,boundary_tmp);
  }
  return boundary_tmp;
}
/**
 * \brief Decompress a source buffer into a destination buffer.
 * \param src_buf The source buffer of compressed data.
 * \param dst_buf The destination buffer that will contain the uncompressed representation.
 * \return A pointer to the end of the destination buffer, marking the end of the uncompressed reprensetation generated.
 **/
WK_word*
WK_decompress (WK_word* src_buf,
	       WK_word* dest_buf) {
  /* PHASE 1: Decode the packed representation of the compressed buffer. */
  /* Make the dictionary (and any associated structured, e.g., hash lookup table). */
  DICT_CREATE();
  /* Extract the number of uncompressed words to be processed from the compressed header. */
  unsigned int num_words = GET_NUM_WORDS(src_buf);
  /*
   * Arrays that hold output data in intermediate form during modeling and whose contents are packed into the actual output after
   * modeling.
   */
  WK_unpacked_tags_t       temp_tags        [num_words];
  WK_unpacked_dict_index_t temp_dict_indices[num_words];
  WK_unpacked_low_bits_t   temp_low_bits    [num_words];
  /* Preload the dictionary. */
  DICT_INITIALIZE();
  DEBUG_PRINT_MSG("\nIn WK_decompress\n");
  DEBUG_PRINT_VAL("num_words  = ", (WK_word)num_words);
  DEBUG_PRINT_VAL("src_buf    = ", (WK_word)src_buf);
  DEBUG_PRINT_VAL("dictionary = ", (WK_word)dictionary);
  DEBUG_PRINT_VAL("dest_buf   = ", (WK_word)dest_buf);
#if WK_DEBUG
  {
    WK_word* whole_words  = src_buf + FULL_PATTERNS_AREA_OFFSET(num_words);
    printf("First 10 whole words pre-unpacking:\n");
    for (int i = 0; i < 10; ++i)
      printf("  whole_words[%d] = 0x%" WORD_FORMAT_WIDTH PRIxWORD "\n", i, whole_words[i]);  
  }
#endif // WK_DEBUG
  WK_unpack_bits(TAGS_AREA_START(src_buf),
                 TAGS_AREA_END(src_buf),
		 (WK_word*)temp_tags,
		 NUM_TAG_BITS,
		 sizeof(WK_unpacked_tags_t) * BITS_PER_BYTE,
		 TAG_PACKING_MASK);
  WK_unpack_bits(INDEX_AREA_START(src_buf),
		 INDEX_AREA_END(src_buf),
		 (WK_word*)temp_dict_indices,
		 NUM_DICT_INDEX_BITS,
		 sizeof(WK_unpacked_dict_index_t) * BITS_PER_BYTE,
		 DICT_INDEX_PACKING_MASK);
  WK_unpack_bits(LOW_BITS_AREA_START(src_buf),
		 LOW_BITS_AREA_END(src_buf),
		 (WK_word*)temp_low_bits,
		 NUM_LOW_BITS,
		 sizeof(WK_unpacked_low_bits_t) * BITS_PER_BYTE,
		 LOW_BITS_PACKING_MASK);
#if WK_DEBUG
  {
    WK_word* whole_words  = src_buf + FULL_PATTERNS_AREA_OFFSET(num_words);
    printf("First 10 tags:\n");
    for (int i = 0; i < 10; ++i)
      printf("  tag[%d]         = 0x%" WORD_FORMAT_WIDTH PRIxWORD "\n", i, (WK_word)temp_tags[i]);
    printf("First 10 dictionary indices:\n");
    for (int i = 0; i < 10; ++i)
      printf("  dict_index[%d]  = 0x%" WORD_FORMAT_WIDTH PRIxWORD "\n", i, (WK_word)temp_dict_indices[i]);  
    printf("First 10 low bits:\n");
    for (int i = 0; i < 10; ++i)
      printf("  low_bits[%d]    = 0x%" WORD_FORMAT_WIDTH PRIxWORD "\n", i, (WK_word)temp_low_bits[i]);  
    printf("First 10 whole words:\n");
    for (int i = 0; i < 10; ++i)
      printf("  whole_words[%d] = 0x%" WORD_FORMAT_WIDTH PRIxWORD "\n", i, whole_words[i]);  
  }
#endif // WK_DEBUG
  
  /* PHASE 2: Unmodel.  Given the unpacked representation generated by the modeling, restore the original data. */
  DEBUG_PRINT_MSG("AFTER UNPACKING, about to enter main block \n");
  {
    register WK_unpacked_tags_t* next_tag        = temp_tags;
    WK_unpacked_dict_index_t*    next_dict_index = temp_dict_indices;
    WK_unpacked_low_bits_t*      next_low_bits   = temp_low_bits;
    WK_word*                     next_full_word  = FULL_WORD_AREA_START(src_buf);
    WK_word*                     next_output;
    WK_word*                     end_of_output   = dest_buf + num_words;
    int                          stride_offset   = 0;
    DEBUG_PRINT_VAL("next_output      = ", (WK_word)next_output);
    DEBUG_PRINT_VAL("next_tag         = ", (WK_word)next_tag);
    DEBUG_PRINT_VAL("next_dict_index  = ", (WK_word)next_dict_index);
    DEBUG_PRINT_VAL("next_low_bits    = ", (WK_word)next_low_bits);
    DEBUG_PRINT_VAL("next_full_word   = ", (WK_word)next_full_word);
    while (stride_offset < WK_STRIDE) {
    
      next_output = dest_buf + stride_offset;
      while (next_output < end_of_output) {
	switch(*next_tag) {
	case ZERO_TAG: {
	  /*
	   * It was just a zero.  The dictionary is unused; just append the zero to the decompressed buffer.
	   */
	  *next_output = 0;
	  break;
	}
	case EXACT_TAG: {
	  /*
	   * For an exact match, just lookup the given dictionary entry and use its words.  Because the match was exact, no update to
	   * the dictionary is needed.
	   */
	  dictionary_element_s* dict_ptr = dictionary + *(next_dict_index++);
	  *next_output = DICT_GET_VALUE(dict_ptr);
	  DICT_MOVE_TO_FRONT(dict_ptr);
	  break;
	}
	case PARTIAL_TAG: {
	  /*
	   * The upper bits of the dictionary entry match, but the lower bits don't.  Reconstruct the correct word, but update the
	   * dictionary entry to use these new low bits.  Note that we don't perform a replacement on the dictionary entry -- we just
	   * update this entry in-place.  One could argue that something more complex should happen with this set of the dictionary,
	   * but for now we keep it simple.
	   */
	  dictionary_element_s* dict_ptr = dictionary + *(next_dict_index++);
	  WK_word dict_entry = DICT_GET_VALUE(dict_ptr);
	  /* Zero the low bits and then paste them into place from the compressed encoding's low-bits collection. */
	  dict_entry &= HIGH_BITS_MASK;
	  dict_entry |= *(next_low_bits++);
	  /* Given this new word value, update the dictionary entry and add the word to the decompressed buffer. */
	  *next_output = dict_entry;
	  DICT_SET_VALUE(dict_ptr, dict_entry);
	  DICT_MOVE_TO_FRONT(dict_ptr);
	  break;
	}
	case MISS_TAG: {
	  /*
	   * For an outright miss, the full value must be taken from the compressed pool of full word patterns.  Here, the dictionary
	   * must be updated by replacing some word in the set (if it is full) with this new, wholly unmatching word.
	   */
	  WK_word missed_word = *(next_full_word++);
	  DICT_UPDATE(missed_word);
	  *next_output = missed_word;    /* and echo it to output */
	  break;
	}
	
	} // switch (*next_tag)
      
	/* Advance to the next word in both the compressed and decompressed representations. */
	++next_tag;
	next_output += WK_STRIDE;
      } // while (next_output < end_of_output)
      ++stride_offset;
      
    }
      
    DEBUG_PRINT_MSG("AFTER DECOMPRESSING\n");
    DEBUG_PRINT_VAL("next_output     = ", (WK_word)next_output);
    DEBUG_PRINT_VAL("next_tag is     = ", (WK_word)next_tag);
    DEBUG_PRINT_VAL("next_full_word  = ", (WK_word)next_full_word);
    DEBUG_PRINT_VAL("next_dict_index = ", (WK_word)next_dict_index);
    return next_output;
  } // PHASE 2
}
#if defined WK_DEBUG_MAIN
/**
 * \brief Generate artificial pages of data.  Validate and measure the compression and decompression of each.
 * \return A status exit code.
 **/
int
main () {
   WK_word* src_buf;
   WK_word* dest_buf;
   WK_word* udest_buf;
   WK_word* dest_end;
   WK_word* udest_end;
   unsigned int size;
   int i;
   src_buf = (WK_word*)malloc(BYTES_PER_PAGE);
   dest_buf = (WK_word*)malloc(MAX_COMPRESSED_BYTES);
   udest_buf = (WK_word*)malloc(BYTES_PER_PAGE);
   
   for (i = 0; i < WORDS_PER_PAGE; ++i) {
      src_buf[i] = -1 - i;
   }
   for (i = 0; i < WORDS_PER_PAGE; ++i) {
      udest_buf[i] = -1;
   }
   printf("src_buf is 0x%" PRIxWORD ", dest_buf is 0x%" PRIxWORD ", udest_buf is 0x%" PRIxWORD "\n",
          (WK_word)src_buf,
	  (WK_word)dest_buf,
	  (WK_word)udest_buf);
   dest_end = WK_compress(src_buf, dest_buf, WORDS_PER_PAGE);
   size = (dest_end - dest_buf) * BYTES_PER_WORD;
   printf("Compressed %d bytes to %d bytes\n", BYTES_PER_PAGE, size);
   udest_end = WK_decompress(dest_buf, udest_buf);
   size = (udest_end - udest_buf) * BYTES_PER_WORD;
   printf("Decompressed back to %d bytes\n", size);
   i = memcmp((void*)src_buf, (void*)udest_buf, BYTES_PER_PAGE);
   printf("bcmp of orig. and compr'd/decompr'd copy (shd be 0) is %u\n", i);
   return 0;
}
#endif // WK_DEBUG_MAIN