extern "C" {
	#include "WK.h"
	#include "lzo_conf.h"
	#include "lzoconf.h"
	#include "lzo1.h"
	//#include "minilzo.h"
}

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

/*class minilzoAlgo: public CompressionAlgo{
public:
	WK_word * compress(WK_word *src, WK_word *dst, unsigned int numWords);
	WK_word * decompress(WK_word *src, WK_word *dst, unsigned int size);
};*/

class lzo1Algo: public CompressionAlgo{
public:
	WK_word * compress(WK_word *src, WK_word *dst, unsigned int numWords);
	WK_word * decompress(WK_word *src, WK_word *dst, unsigned int size);
};