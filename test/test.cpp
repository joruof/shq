#include "shq.h"

#include <random>
#include <sys/wait.h>

#define SEGMENT_NAME "test"

struct Config {

    int n = 0;
    int messageCount = 0;
    int sleepOutsideMsg = 0;
    int sleepInsideMsg = 0;
};

void startWriter (Config conf) {

    if (0 != fork()) {
        // meaning this is the parent
        return;
    }

    std::string log = "[WRITER " + std::to_string(getpid()) + "] ";

    shq::writer writer(SEGMENT_NAME, 30, 5);

    std::cout << log << "opened writer" << std::endl;

    shq::definition def = {
            {"i", sizeof(int)},
        };

    shq::definition def_timed = {
            {"i", sizeof(int)},
            {"time", sizeof(int)}
        };

    for (int i = 0; i < conf.messageCount; i++) { 

        usleep(conf.sleepOutsideMsg);

        if (i % 2 == 0) {
            shq::message msg(writer, def);

            msg.at<int>("i") = i;

            std::cout << log << "wrote seq " << msg.seq() << std::endl;

            usleep(conf.sleepInsideMsg);
        } else {
            shq::message msg(writer, def_timed);

            if (!msg.ok()) {
                continue;
            }

            msg.at<int>("i") = i;
            msg.at<int>("time") = 424242;

            std::cout << log << "wrote seq " << msg.seq() << std::endl;

            usleep(conf.sleepInsideMsg);
        }
    }

    exit(0);
}

void startReader (Config conf) {

    if (0 != fork()) {
        // meaning this is the parent
        return;
    }

    std::string log = "[READER " + std::to_string(getpid()) + "] ";

    shq::reader reader(SEGMENT_NAME);

    std::cout << log << "opened reader" << std::endl;

    uint64_t prev = 0;

    for (int i = 0; i < conf.messageCount; i++) { 

        usleep(conf.sleepOutsideMsg);

        shq::message msg(reader);

        if (!msg.ok()) {
            std::cout << log << "msg not ok" << std::endl;
        }

        if (msg.has("time")) {
            std::cout << log << "has time: " 
                << msg.at<int>("time") << std::endl;

            if (msg.at<int>("time") != 424242) {
                exit(-2);
            }
        }
        if (msg.has("i")) { 
            std::cout << log << "seq: " << msg.seq() << std::endl;
        }
        
        int skip = msg.seq() - prev;
        if (skip > 1) {
            std::cout << log << "skipped " << skip - 1 << std::endl;
        }

        prev = msg.seq();

        usleep(conf.sleepInsideMsg);
    }

    exit(0);
}

int main(int, char**) {

    std::mt19937 gen(897245370892);
    std::uniform_int_distribution<> dis(0, 100000);

    for (int i = 0; i < 128; i++) {
        Config readerConf;
        readerConf.n = i;
        readerConf.messageCount = 50;
        readerConf.sleepOutsideMsg = dis(gen);
        readerConf.sleepInsideMsg = dis(gen);

        startReader(readerConf);
    }

    for (int i = 0; i < 128; i++) {
        Config writerConf;
        writerConf.n = i;
        writerConf.messageCount = 10;
        writerConf.sleepOutsideMsg = dis(gen);
        writerConf.sleepInsideMsg = dis(gen);

        startWriter(writerConf);
    }

    pid_t wpid;
    int status = 0;

    while ((wpid = wait(&status)) > 0) {
        if (status != 0) {
            std::cout << "Process crashed: " << status << std::endl;
            return -1;
        } else {
            std::cout << "Process exited normally." << std::endl;
        }
    }

    return 0;
}
