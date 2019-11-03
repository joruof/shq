#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>
#include <sys/mman.h>
#include <sys/stat.h>

#include <cstring>

namespace shq {

    /*
     * Helper method, locks mutex and fixes state, if previous owner died.
     */
    int robustLock (pthread_mutex_t* mutex, bool blocking = true) {

        int lockResult;

        if (blocking) { 
            lockResult = pthread_mutex_lock(mutex);
        } else { 
            lockResult = pthread_mutex_trylock(mutex);
        }

        if (EOWNERDEAD == lockResult) {
            pthread_mutex_consistent(mutex);
        }

        return lockResult;
    }

    /*
     * Header contains necessary synchronization data.
     * Written to the beginning of the shared memory segment.
     */
    struct header {

        // used to init robust and shared mutexes
        pthread_mutexattr_t mutexAttr;

        // locked on allocation/deallocation
        pthread_mutex_t mutex;

        // locked if reader present
        pthread_mutex_t readerMutex;
        
        // locked if writer present
        pthread_mutex_t writerMutex;

        // used to (re-)init shared condition variables
        pthread_condattr_t condAttr;

        // condition the reader can wait on
        pthread_cond_t readerCond;

        // condition the writer can wait on
        pthread_cond_t writerCond;

        // being of the allocation in the ring buffer
        size_t begin = 0;
        
        // end of the allocation in the ring buffer
        size_t end = 0;

        // true if the allocation is wrapped (end < begin)
        bool wrapped = false;
    };

    struct segment {

        // file descriptor of shared memory segment
        int descriptor;

        // pointer into the shared memory
        uint8_t* memory;

        // actual size of the shared memory segment
        size_t size;

        // name of the shared memory segment
        std::string name;

        // header used for synchronization
        header* hdr;

        // whether to wait for locks
        bool blocking;

        segment (const char* name, const size_t size, bool blocking = true) 
            : descriptor{shm_open(name, O_RDWR, 0666)},
              memory{nullptr},
              size{sizeof(header) + size + sizeof(uint32_t)},
              name{name},
              blocking{blocking} {

            if (-1 != descriptor) {
                // opening succeeded, reusing already created segment
                openMemory();
                return; 
            }

            // we need a new segment
            
            descriptor = shm_open(name, O_CREAT | O_RDWR, 0666);

            if (0 > descriptor) {
                throw std::runtime_error(
                        std::string("Creating shared memory segment \"")
                        + std::string(name)
                        + std::string("\" failed: ")
                        + std::strerror(errno));
            }

            // creation done, resizing to proper size
            if (0 > ftruncate(descriptor, size)) { 
                throw std::runtime_error(
                        std::string("Resizing shared memory segment \"")
                        + std::string(name)
                        + std::string("\" to ")
                        + std::to_string(size)
                        + std::string(" bytes failed: ")
                        + std::strerror(errno));
            }

            openMemory();
            std::memset(memory, 0, size);

            // initialize header
            
            *hdr = {};

            pthread_mutexattr_init(&hdr->mutexAttr);
            pthread_mutexattr_setpshared(
                    &hdr->mutexAttr, PTHREAD_PROCESS_SHARED);
            pthread_mutexattr_setrobust(
                    &hdr->mutexAttr, PTHREAD_MUTEX_ROBUST);

            pthread_mutex_init(&hdr->mutex, &hdr->mutexAttr);
            pthread_mutex_init(&hdr->readerMutex, &hdr->mutexAttr);
            pthread_mutex_init(&hdr->writerMutex, &hdr->mutexAttr);

            pthread_condattr_init(&hdr->condAttr);
            pthread_condattr_setpshared(
                    &hdr->condAttr, PTHREAD_PROCESS_SHARED);

            pthread_cond_init(&hdr->readerCond, &hdr->condAttr);
            pthread_cond_init(&hdr->writerCond, &hdr->condAttr);
        }

        ~segment () {

            munmap(hdr, size);
            close(descriptor);
        }

        void openMemory () {

            void* ptr = mmap(
                    0, 
                    size,
                    PROT_READ | PROT_WRITE, 
                    MAP_SHARED, 
                    descriptor,
                    0);

            if (MAP_FAILED == ptr) {
                throw std::runtime_error(
                        std::string("Opening shared memory failed: ")
                        + std::strerror(errno));
            }

            hdr = (header*)ptr;
            memory = ((uint8_t*)ptr) + sizeof(header);
        }

        bool empty () {

            return hdr->wrapped || hdr->begin != hdr->end;
        }
    };

    struct reader : segment {

        reader (const char* name, const size_t size, bool blocking = true)
            : segment(name, size, blocking) {

            // (Try to) register as a reader
            int lockResult = robustLock(&hdr->readerMutex, blocking);

            if (EBUSY == lockResult) { 
                throw std::runtime_error(
                        "Another reader already connected to segment!");
            }

            if (EOWNERDEAD == lockResult) { 
                // Is ignoring "blocking", but no way not to block here
                // without risking huge inconsistencies.
                robustLock(&hdr->mutex);
                // This fixes broken condition variables, which sometimes
                // occur, if the process died while waiting on a condition.
                pthread_cond_init(&stb->readerCond, &stb->condAttr);
                pthread_mutex_unlock(&hdr->readerMutex);
            }
        }

        ~reader () { 

            // unregister as reader
            pthread_mutex_unlock(&hdr->readerMutex);
        }
    };

    /*
    int initSegment () {

        return 0;
    }

    int destroySegment () {

        return 0;
    }
    */

}
