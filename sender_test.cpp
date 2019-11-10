#include <iostream> 
#include "shq.h"

int main (int argc, char** argv) {

    shq::writer writer("shq_counter_demo", 100, 10);

    shq::definition def = {
            {"i", sizeof(int)},
            {"time", sizeof(int)}
        };

    for (int i = 0; i < 1000; i++) { 
        shq::message msg(writer, def);
        msg.at<int>("i") = i;
    }
}
