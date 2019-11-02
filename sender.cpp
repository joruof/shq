#include "shq.h"

int main(int argc, char** argv) {

    shm_unlink("segment_test");

    shq::seg testSegment("segment_test", 128);
    shq::def testDef = {
            {"aaaa", sizeof(float)},
            {"y", sizeof(float)},
            {"z", sizeof(float)}
        };

    while (true) {

        shq::send msg(testSegment, testDef, shq::NO_WAIT);

        if (msg.ok()) {
            msg.at<float>("aaaa") = 42.0;
            msg.at<float>("y") = 78.0;
            msg.at<float>("z") = 89.0;
        }
    }
}
