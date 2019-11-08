#include <iostream>

#include "shq_new.h"

int main(int argc, char** argv) {

    shq::reader reader("cond_lock_test", 128);

    while (true) { 

        shq::message msg(reader);
        std::cout << msg.at<float>("a") << std::endl;
    }
}
