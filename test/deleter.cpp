#include "shq.h"

int main (int, char**) {

    shq::reader reader("shq_test");
    reader.destroy();

    return 0;
}
