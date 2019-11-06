#include <iostream>

#include "shq_new.h"

int main(int argc, char** argv) {

    shq::reader reader("cond_lock_test", 10);
    shq::header* stb = reader.hdr;

    while (true) { 
        shq::message msg(reader);

        std::cout << "state: " << stb->begin << " -> ";
        stb->begin -= 1;
        std::cout << stb->begin << std::endl;
    }
}
