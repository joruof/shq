#include "shq.h"

int main (int argc, char** argv) {

    shm_unlink("segment_test");

    std::cout << "[Test 0]" << std::endl;
    {
        shq::seg segTest("segment_test", 36);
        segTest.push(32, shq::NO_WAIT);
        segTest.push(32, shq::NO_WAIT);
        segTest.push(32, shq::NO_WAIT);

        std::cout << segTest << std::endl;
    }
    std::cout << "\n";

    std::cout << "[Test 1]" << std::endl;
    {
        shq::seg segTest("segment_test", 36);
        segTest.push(4, shq::NO_WAIT);
        segTest.push(5, shq::NO_WAIT);
        segTest.push(32, shq::NO_WAIT);

        std::cout << segTest << std::endl;
    }
    std::cout << "\n";

    std::cout << "[Test 2]" << std::endl;
    {
        shq::seg segTest("segment_test", 36);
        segTest.pop(shq::NO_WAIT);
        segTest.push(32, shq::NO_WAIT);
        segTest.pop(shq::NO_WAIT);
        segTest.pop(shq::NO_WAIT);
        segTest.push(32, shq::NO_WAIT);

        std::cout << segTest << std::endl;
    }
    std::cout << "\n";

    std::cout << "[Test 3]" << std::endl;
    {
        shq::seg segTest("segment_test", 36);
        segTest.push(1, shq::NO_WAIT);
        segTest.push(2, shq::NO_WAIT);
        segTest.push(4, shq::NO_WAIT);
        segTest.push(8, shq::NO_WAIT);
        segTest.push(16, shq::NO_WAIT);
        segTest.push(32, shq::NO_WAIT);

        std::cout << segTest << std::endl;
    }
    std::cout << "\n";

    shm_unlink("segment_test");
    std::cout << "[Test 4]" << std::endl;
    {
        shq::seg segTest("segment_test", 36);
        segTest.push(1, shq::NO_WAIT);
        segTest.push(1, shq::NO_WAIT);
        segTest.push(16, shq::NO_WAIT);
        segTest.push(1, shq::NO_WAIT);
        segTest.push(2, shq::NO_WAIT);

        std::cout << segTest << std::endl;
    }
    std::cout << "\n";

    {
        shq::send msg("segment_test", {
                {"aaaa", sizeof(float)},
                {"y", sizeof(float)},
                {"z", sizeof(float)}
            });

        msg.at<float>("aaaa") = 42.0;
        msg.at<float>("y") = 78.0;
        msg.at<float>("z") = 89.0;
    }

    {
        shq::recv msg("segment_test");

        std::cout << msg.at<float>("aaaa") << std::endl;
        std::cout << msg.at<float>("y") << std::endl;
        std::cout << msg.at<float>("z") << std::endl;
    }

    /*
     * Read Option 1: with waiting
     *
     * shq::msg msg = shq::get("segment_test");
     *
     * Read Option 2: without waiting
     *
     * shq::msg msg = shq::get("segment_test", shq::NO_WAIT);
     * if (msg.empty()) {
     *    continue;
     * }
     *
     * Write Option 1: with waiting
     *
     * shq::msg msg(def);
     * shq::put("segment_test", msg);
     */

    return 0;
}
