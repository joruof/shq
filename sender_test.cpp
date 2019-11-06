#include <iostream> 
#include "shq_new.h"

int main (int argc, char** argv) {

    shm_unlink("cond_lock_test");

    shq::writer writer("cond_lock_test", 10);
    shq::header* stb = writer.hdr;

    while (true) { 

        usleep(1000000);

        shq::message msg(writer);

        std::cout << "state: " << stb->begin << " -> ";
        stb->begin += 1;
        std::cout << stb->begin << std::endl;
    }
}
