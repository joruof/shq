#include "shq.h"

int main(int argc, char** argv) {

    shm_unlink("segment_test");

    int i = 0;

    shq::seg test("segment_test", 8192);

    while (true) {

        test.push(64);
        //std::cout << "i:" << i++ << std::endl;
        //std::cout << test << std::endl;
    }
}
