extern "C" {
	#include "WK.h"
#ifndef MINI
	#include "lzo_conf.h"
	#include "lzoconf.h"
	#include "lzo1.h"
#endif
#ifdef MINI
	#include "minilzo.h"
#endif
}

typedef struct{
  WK_word       address;
  unsigned int  comp_size;
  long long     comp_time;
  long long     decomp_time;
} page_info;

class CompressionAlgo{
	virtual WK_word * compress(WK_word *src, WK_word *dst, unsigned int numWords) = 0;
	virtual WK_word * decompress(WK_word *src, WK_word *dst, unsigned int size) = 0;
};

class PassthroughAlgo: public CompressionAlgo{
public: 
	WK_word * compress(WK_word *src, WK_word *dst, unsigned int numWords);
	WK_word * decompress(WK_word *src, WK_word *dst, unsigned int size);
};

class WKAlgo: public CompressionAlgo{
public:
	WK_word * compress(WK_word *src, WK_word *dst, unsigned int numWords);
	WK_word * decompress(WK_word *src, WK_word *dst, unsigned int size);
};

#ifdef MINI
class minilzoAlgo: public CompressionAlgo{
public:
	WK_word * compress(WK_word *src, WK_word *dst, unsigned int numWords);
	WK_word * decompress(WK_word *src, WK_word *dst, unsigned int size);
};
#endif

#ifndef MINI
class lzo1Algo: public CompressionAlgo{
public:
	WK_word * compress(WK_word *src, WK_word *dst, unsigned int numWords);
	WK_word * decompress(WK_word *src, WK_word *dst, unsigned int size);
};
#endif
