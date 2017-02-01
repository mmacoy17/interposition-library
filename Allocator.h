
class Allocator{
	unsigned int total_frag;
	static const int MINIMUM = 128; 	// Corresponds to a compressed page of all zeros
	static const int MAXIMUM = 4096;	// Corresponds to an uncompressed page
public:
	void add(unsigned int);
	unsigned int getFrag();
};