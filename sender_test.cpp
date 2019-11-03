#include <iostream> 
#include "shq_new.h"

int main (int argc, char** argv) {

    shm_unlink("cond_lock_test");

    shq::segment seg("cond_lock_test", 10);
    shq::header* stb = seg.hdr;

    while (true) {

        shq::robustLock(&stb->mutex);

        while (stb->begin > 5) { 
            int resultWait = pthread_cond_wait(&stb->writerCond, &stb->mutex); 
            if (EOWNERDEAD == resultWait) {
                pthread_mutex_consistent(&stb->mutex);
            }
        }

        std::cout << "state: " << stb->begin << " -> ";
        stb->begin += 1;
        std::cout << stb->begin << std::endl;

        pthread_cond_signal(&stb->readerCond);
        pthread_mutex_unlock(&stb->mutex);

        usleep(1000000);
    }

    pthread_mutex_unlock(&stb->writerMutex);
}
