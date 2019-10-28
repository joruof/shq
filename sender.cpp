#include "shq.h"

int main(int argc, char** argv) {

    shm_unlink("segment_test");

    int i = 0;

    shq::seg test("segment_test", 8192);
    test.unlock();

    while (true) {

        test.lock();
        test.push(64, shq::NO_WAIT);
        test.unlock();
        //std::cout << "i:" << i++ << std::endl;
        //std::cout << test << std::endl;
    }
}
