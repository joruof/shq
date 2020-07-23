#include "shq.h"

int main () {

    shq::reader reader("test_segment_name");

    shq::message msg(reader);

    if (msg.ok()) {

        double number = msg.at<double>("number");
        std::string text(msg.ptr<char>("text"), msg.entrySize("text"));

        std::cout << "Number is: " << number << std::endl;
        std::cout << "Text is: " << text << std::endl;
    }

    reader.destroy();
}
