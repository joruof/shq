#include <iostream> 
#include "shq_new.h"

int main (int argc, char** argv) {

    shm_unlink("cond_lock_test");

    shq::writer writer("cond_lock_test", 128);

    shq::definition def = {
        {"a", sizeof(float)}
    };

    int i = 0;

    while (true) { 

        //usleep(100000);

        shq::message msg(writer, def);
        msg.at<float>("a") = i++;
    }
}
