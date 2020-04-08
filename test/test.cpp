#include "shq.h"

#include <random>
#include <sys/wait.h>

#define SHQ_SEGMENT_NAME "shq_test"

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

    std::cout << "[WRITER " << getpid() << "] " << "starting" << std::endl;

    shq::writer writer(SHQ_SEGMENT_NAME, 30, 5);

    std::cout << "[WRITER " << getpid() << "] " << "opened writer" << std::endl;

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

            std::cout << 
                "[WRITER " << getpid() << "] "
                << "wrote seq "
                << *msg.chuk.seq
                << std::endl;

            usleep(conf.sleepInsideMsg);
        } else {
            shq::message msg(writer, def_timed);

            if (!msg.ok()) {
                std::cout << "------------------------------- MSG NO OK" << std::endl;
                continue;
            }

            msg.at<int>("i") = i;
            msg.at<int>("time") = 424242;

            std::cout <<
                "[WRITER " << getpid() << "] "
                << "wrote seq "
                << *msg.chuk.seq
                << std::endl;

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

    std::cout << "[READER " << getpid() << "] " << "starting" << std::endl;
    
    shq::reader reader(SHQ_SEGMENT_NAME);

    std::cout << "[READER " << getpid() << "] " << "opened reader " << reader.length() << std::endl;

    uint64_t prev = 0;

    for (int i = 0; i < conf.messageCount; i++) { 

        usleep(conf.sleepOutsideMsg);

        shq::message msg(reader);

        if (msg.ok()) {
        }

        if (!msg.ok()) {
            std::cout << 
                "[READER " << getpid() <<  "] "
                << "msg not ok"
                << std::endl;
        }

        if (msg.has("time")) {
            std::cout << 
                "[READER " << getpid() <<  "] "
                << "has time: "
                << msg.at<int>("time") << std::endl;

            if (msg.at<int>("time") != 424242) {
                exit(-2);
            }
        }
        if (msg.has("i")) { 
            std::cout << 
                "[READER " << getpid() <<  "] "
                << "seq: "
                << *msg.chuk.seq 
                << std::endl;
        }
        
        int skip = *msg.chuk.seq - prev;
        if (skip > 1) {
            std::cout << 
                "[READER " << getpid() <<  "] "
                "skipped "
                << skip - 1
                << std::endl;
        }

        prev = *msg.chuk.seq;

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
            std::cout << "Process exited normally: " << status << std::endl;
        }
    }

    return 0;
}
