#include "shq.h"
#include <chrono>

int main(int argc, char** argv) {

    int i = 0;

    auto start = std::chrono::high_resolution_clock::now();

    shq::seg test("segment_test", 8192);

    std::cout << test << std::endl;

    while (true) {

        shq::recv msg(test, shq::NO_WAIT);

        if (msg.ok()) {

            msg.at<float>("aaaa");
            msg.at<float>("y"); 
            msg.at<float>("z");
            i++;
        }

        auto now = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double, std::milli> ms = now - start;
        if (ms.count() > 1000) {
            std::cout << i << std::endl;
            i = 0;
            start = now;
        }
    }
}
