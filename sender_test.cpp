#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>
#include <sys/mman.h>
#include <sys/stat.h>

#include <tuple>
#include <vector>
#include <cstring>
#include <iostream>
#include <unordered_map>

struct stub {

    int i = 0;
    pthread_mutexattr_t mutexAttr;
    pthread_mutex_t mutex;
    pthread_mutex_t readerMutex;
    pthread_mutex_t writerMutex;
    pthread_condattr_t condAttr;
    pthread_cond_t readerCond;
    pthread_cond_t writerCond;
};

int robustLock (pthread_mutex_t* mutex) {

    int lockResult = pthread_mutex_lock(mutex);

    if (EOWNERDEAD == lockResult) {
        return pthread_mutex_consistent(mutex);
    }

    return lockResult;
}

int main (int argc, char** argv) {

    const char* name = "cond_lock_test";
    size_t size = sizeof(stub);
    uint8_t* mem = nullptr;
    int fd = -1;

    fd = shm_open(name, O_CREAT | O_RDWR, 0777);
    ftruncate(fd, size);
    mem = (uint8_t*) mmap(0, size,
            PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);

    // get and initialize stub
    stub* stb = (stub*)mem;
    *stb = {};

    // intialize shared and recursive mutex in stub 
    pthread_mutexattr_init(&stb->mutexAttr);
    //pthread_mutexattr_settype(&stb->mutexAttr, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutexattr_setpshared(&stb->mutexAttr, PTHREAD_PROCESS_SHARED);
    pthread_mutexattr_setrobust(&stb->mutexAttr, PTHREAD_MUTEX_ROBUST);
    pthread_mutex_init(&stb->mutex, &stb->mutexAttr);
    pthread_mutex_init(&stb->readerMutex, &stb->mutexAttr);
    pthread_mutex_init(&stb->writerMutex, &stb->mutexAttr);
    // intialize shared condition in stub
    pthread_condattr_init(&stb->condAttr);
    pthread_condattr_setpshared(&stb->condAttr, PTHREAD_PROCESS_SHARED);
    pthread_cond_init(&stb->readerCond, &stb->condAttr);
    pthread_cond_init(&stb->writerCond, &stb->condAttr);

    pthread_mutex_lock(&stb->writerMutex);

    while (true) {

        std::cout << "locking ..." << std::endl;
        robustLock(&stb->mutex);
        std::cout << "locked" << std::endl;

        while (stb->i > 5) { 
            std::cout << "now waiting" << std::endl;
            int resultWait = pthread_cond_wait(&stb->writerCond, &stb->mutex); 
            if (EOWNERDEAD == resultWait) {
                pthread_mutex_consistent(&stb->mutex);
            }
            std::cout << "woken up:" << std::strerror(resultWait) << std::endl;
        }

        stb->i += 1;
        std::cout << "state: " << stb->i << std::endl;

        std::cout << "signaling ..." << std::endl;
        pthread_cond_signal(&stb->readerCond);
        std::cout << "signaled" << std::endl;

        std::cout << "unlocking ..." << std::endl;
        pthread_mutex_unlock(&stb->mutex);
        std::cout << "unlocked" << std::endl;
    }

    pthread_mutex_unlock(&stb->writerMutex);
}
