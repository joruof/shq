#include <iostream> 
#include "shq.h"

int main (int, char**) {

    shq::writer writer("shq_counter_demo2", 100, 3);

    writer.destroy();

    return 0;
}
