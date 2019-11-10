#include <iostream>
#include "shq.h"

int main(int argc, char** argv) {

    shq::reader reader("shq_counter_demo");

    for (int i = 0; i < 100; i++) { 

        shq::message msg(reader);
        if (msg.has("i")) { 
            std::cout << msg.at<int>("i") << std::endl;
        }
    }
}
