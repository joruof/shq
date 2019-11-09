#include <iostream> 
#include "shq_new.h"

#include <atomic>

int main (int argc, char** argv) {

    shm_unlink("cond_lock_test");

    shq::definition def = {
            {"a", sizeof(float)},
            {"b", sizeof(uint8_t)}
        };

    shq::writer writer("cond_lock_test", 128);

    int i = 0;

    while (true) { 

        //usleep(100000);

        shq::message msg(writer, def);
        msg.at<float>("a") = i++;
        msg.at<uint8_t>("b") = 'a';
    }
}
