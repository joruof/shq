#include <iostream> 
#include "shq.h"

int main (int, char**) {

    shq::writer writer("shq_counter_demo2", 100, 3);

    shq::definition def = {
            {"i", sizeof(int)},
        };

    shq::definition def_timed = {
            {"i", sizeof(int)},
            {"time", sizeof(int)}
        };

    for (int i = 1; i < 100000000; i++) { 

        {
            usleep(5000);
            shq::message msg(writer, def_timed);
            msg.at<int>("i") = i;

            //writer.printChunks();

            std::cout << "[WRITER] " << msg.chuk.seq << std::endl;
        }
    }
}
