
class Allocator{
        unsigned int total_frag, insertions;
	unsigned int histogram[10000000];
	static const int MINIMUM = 128; 	// Corresponds to a compressed page of all zeros
	static const int MAXIMUM = 4096;	// Corresponds to an uncompressed page

	static const int LOW = 1024;
	static const int HIGH = 2048;
public:
	Allocator();
	unsigned int getInsert();
	void add(unsigned int);
	unsigned int * getHistogram();
	unsigned int getFrag();
	double getAverage();
};
