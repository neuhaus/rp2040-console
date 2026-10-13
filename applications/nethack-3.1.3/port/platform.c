#include <time.h>
#include <stdlib.h>
#include <unistd.h>
void nh_srandom(unsigned int seed) { unsigned int entropy = 0; if (getentropy(&entropy, sizeof entropy) != 0) entropy = (unsigned int)time(NULL); srandom(seed ^ entropy); }
