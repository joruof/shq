#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/file.h>

#include <tuple>
#include <vector>
#include <atomic>
#include <cstring>
#include <unordered_map>

#include <chrono>
#include <iostream>

int main (int argc, char**) {

    int fd = shm_open("test_file", O_RDWR | O_CREAT, 0666);

    std::cout << fd << std::endl;
    std::cout << "before lock" << std::endl;
    std::cout << flock(fd, LOCK_EX) << std::endl;
    std::cout << "after lock" << std::endl;

    sleep(20);

    return 0;

    auto start = std::chrono::high_resolution_clock::now();

    int N = 1000000;

    for (int i = 0; i < N; i++) {
        flock(fd, LOCK_EX);
        flock(fd, LOCK_UN);
    }

    auto end = std::chrono::high_resolution_clock::now();

    double diff = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();

    std::cout << "Virtual file locks: " << diff / N << " microseconds" << std::endl;

    pthread_mutex_t mutex;
    pthread_mutexattr_t mutexAttr;

    pthread_mutexattr_init(&mutexAttr);
    pthread_mutexattr_setpshared(
            &mutexAttr, PTHREAD_PROCESS_SHARED);
    pthread_mutexattr_setrobust(
            &mutexAttr, PTHREAD_MUTEX_ROBUST);

    start = std::chrono::high_resolution_clock::now();

    pthread_mutex_init(&mutex, &mutexAttr);

    for (int i = 0; i < N; i++) {
        pthread_mutex_lock(&mutex);
        pthread_mutex_unlock(&mutex);
    }

    end = std::chrono::high_resolution_clock::now();

    diff = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();

    std::cout << "Mutexes: " << diff / N << " microseconds" << std::endl;
}
