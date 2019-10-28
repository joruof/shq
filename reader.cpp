#include "shq.h"
#include <chrono>

int main(int argc, char** argv) {

    int i = 0;

    auto start = std::chrono::high_resolution_clock::now();

    shq::seg test("segment_test", 8192);

    while (true) {

        test.pop();
        
        i++;

        auto now = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double, std::milli> ms = now - start;
        if (ms.count() > 1000) {
            std::cout << i << std::endl;
            i = 0;
            start = now;
        }
    }
}
