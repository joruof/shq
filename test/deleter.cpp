#include <iostream> 
#include "shq.h"

int main (int, char**) {

    shq::writer writer("shq_test", 100, 3);

    writer.destroy();

    return 0;
}
