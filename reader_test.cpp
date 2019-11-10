#include <iostream>
#include "shq.h"

int main(int argc, char** argv) {

    shq::reader reader("shq_counter_demo", 230);

    for (int i = 0; i < 100; i++) { 

        shq::message msg(reader);
        std::cout << msg.has("time") << std::endl;
    }
}
