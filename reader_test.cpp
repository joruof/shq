#include <iostream>

#include "shq_new.h"

int main(int argc, char** argv) {

    shq::segment seg("cond_lock_test", 10);
    shq::header* stb = seg.hdr;

    if (EOWNERDEAD == shq::robustLock(&stb->readerMutex)) {
        shq::robustLock(&stb->mutex);
        // this seems to fix (possibly) broken condition variables
        pthread_cond_init(&stb->readerCond, &stb->condAttr);
        pthread_mutex_unlock(&stb->mutex);
    }

    while (true) {

        shq::robustLock(&stb->mutex);

        while (stb->begin < 1) { 
            int resultWait = pthread_cond_wait(&stb->readerCond, &stb->mutex); 
            if (EOWNERDEAD == resultWait) {
                pthread_mutex_consistent(&stb->mutex);
            }
        }

        std::cout << "state: " << stb->begin << " -> ";
        stb->begin -= 1;
        std::cout << stb->begin << std::endl;

        pthread_cond_signal(&stb->writerCond);
        pthread_mutex_unlock(&stb->mutex);
    }

    pthread_mutex_unlock(&stb->readerMutex);
}
