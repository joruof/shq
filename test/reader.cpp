#include <iostream>
#include "shq.h"

int main(int, char**) {

    shq::reader reader("shq_counter_demo2");

    uint64_t prev = 0;

    for (int i = 0; i < 100000000; i++) { 

        shq::message msg(reader);

        if (msg.has("time")) {
            std::cout << "has time" << std::endl;
        }
        if (msg.has("i")) { 
            std::cout << "[READER] " << *msg.chuk.seq << std::endl;
        }
        
        int skip = *msg.chuk.seq - prev;
        if (skip > 1) {
            std::cout << "skipped " << skip - 1 << std::endl;
        }

        prev = *msg.chuk.seq;
    }
}
