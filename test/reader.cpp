#include <iostream>
#include "shq.h"

int main(int, char**) {

    shq::reader reader("shq_counter_demo2");

    uint64_t prev = 0;
    int failCount = 0;

    for (int i = 0; i < 100000000; i++) { 
        shq::message msg(reader);
        usleep(2000);
        if (msg.has("time")) {
            std::cout << "has time" << std::endl;
        }
        if (msg.has("i")) { 
            std::cout << "[READER] " << msg.chuk.seq << std::endl;
        }

        if (msg.chuk.seq - prev > 1) {
            failCount++;
            if (failCount > 1) {
                break;
            }
        }

        prev = msg.chuk.seq;
    }
}
