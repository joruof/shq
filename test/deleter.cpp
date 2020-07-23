#include "shq.h"

/**
 * In case something goes wrong in test.cpp, this can be
 * used to delete the allocated shared memory segment.
 */
int main (int, char**) {

    shq::reader reader("test");
    reader.destroy();

    return 0;
}
