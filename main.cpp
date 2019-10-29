#include "shq.h"

int main (int argc, char** argv) {

    shq::seg testSegment("segment_test", 128);

    shq::def testDef = {
            {"aaaa", sizeof(float)},
            {"y", sizeof(float)},
            {"z", sizeof(float)}
            {"zz", sizeof(float)}
        };

    {
        shq::send msg(testSegment, testDef);

        msg.at<float>("aaaa") = 42.0;
        msg.at<float>("y") = 78.0;
        msg.at<float>("z") = 89.0;
        msg.at<float>("zz") = 102.0;
    }

    {
        shq::recv msg(testSegment);

        std::cout << msg.at<float>("aaaa") << std::endl;
        std::cout << msg.at<float>("y") << std::endl;
        std::cout << msg.at<float>("z") << std::endl;
        std::cout << msg.at<float>("zz") << std::endl;
    }

    return 0;
}
