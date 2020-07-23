#include "shq.h"

int main () {

    const size_t segmentSize = 512;

    shq::writer writer("test_segment_name", segmentSize);

    std::string text = "Hello, World!";
    double number = 3.141592;

    shq::definition def{
        {"text", text.size()},
        {"number", sizeof(number)},
    };

    // messages use RAII for synchronization
    // message is send in destructor
    {
        shq::message msg(writer, def);

        text.copy(msg.ptr<char>("text"), text.size());
        msg.at<double>("number") = number;
    }
}
