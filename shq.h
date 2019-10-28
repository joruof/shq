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

namespace shq {

    typedef std::vector<std::pair<std::string, uint32_t>> def;

    uint8_t buf[256];

    struct stub {

        pthread_mutexattr_t mutexAttr;
        pthread_mutex_t mutex;
        pthread_condattr_t condAttr;
        pthread_cond_t cond;

        size_t begin = 0;
        size_t end = 0;
        bool wrapped = false;

        size_t chunkCounter = 0;
    };

    enum mode { 

        WAIT,
        NO_WAIT
    };

    struct seg {

        int fd;
        uint8_t* mem;
        size_t size;
        std::string name;
        stub* stb;
        
        seg (const char* name, const size_t size) 
            : fd{shm_open(name, O_RDWR, 0777)},
              mem{nullptr},
              size{size},
              name{name} {

            // memory management info + actual buffer size + end marker
            size_t actualSize = sizeof(stub) + size + sizeof(uint32_t);

            if (0 > fd) {
                // we need a new segment
                fd = shm_open(name, O_CREAT | O_RDWR, 0777);
                if (0 > fd) {
                    throw std::runtime_error(
                            std::string("Creating shared memory segment \"")
                            + std::string(name)
                            + std::string("\" failed: ")
                            + std::strerror(errno));
                }
                // creation done, resize shared memory segment
                if (0 > ftruncate(fd, actualSize)) { 
                    throw std::runtime_error(
                            std::string("Resizing shared memory segment \"")
                            + std::string(name)
                            + std::string("\" to ")
                            + std::to_string(size)
                            + std::string(" bytes failed: ")
                            + std::strerror(errno));
                }
                // map and initialize new memory management stub
                mem = (uint8_t*) mmap(0, actualSize,
                        PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
                if (MAP_FAILED == mem) {
                    throw std::runtime_error(
                            std::string("MMap failed: ")
                            + std::strerror(errno));
                }
                // get and initialize stub
                stb = (stub*)mem;
                *stb = {};

                // intialize shared and recursive mutex in stub 
                pthread_mutexattr_init(&stb->mutexAttr);
                pthread_mutexattr_settype(&stb->mutexAttr, PTHREAD_MUTEX_RECURSIVE);
                pthread_mutexattr_setpshared(&stb->mutexAttr, PTHREAD_PROCESS_SHARED);
                pthread_mutex_init(&stb->mutex, &stb->mutexAttr);
                // intialize shared condition in stub
                pthread_condattr_init(&stb->condAttr);
                pthread_condattr_setpshared(&stb->condAttr, PTHREAD_PROCESS_SHARED);
                pthread_cond_init(&stb->cond, &stb->condAttr);

                mem += sizeof(stub);
                std::memset(mem, 0, size);
            } else {
                // reuse old segment
                mem = (uint8_t*) mmap(0, actualSize,
                        PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
                if (MAP_FAILED == mem) {
                    throw std::runtime_error(
                            std::string("MMap failed: ")
                            + std::strerror(errno));
                }
                stb = (stub*)mem;
                mem += sizeof(stub);
            }

            pthread_mutex_lock(&stb->mutex);
        }

        ~seg () {

            pthread_mutex_unlock(&stb->mutex);
            munmap(mem - sizeof(stub), size);
            close(fd);
        }

        uint8_t* push (size_t chunkSize, mode m = WAIT) { 

            // sizeof(uint32_t) byte for chunk size prefix
            size_t actualChunkSize = sizeof(uint32_t) + chunkSize;

            // abort if trying to allocate 0 bytes
            // or if the chunkSize is bigger than the segment 
            if (actualChunkSize > size || chunkSize == 0) {
                return nullptr;
            }

            bool foundChunk = false;

            while (!foundChunk) {
                // optimistic approach to life
                foundChunk = true; 

                if (stb->wrapped) {
                    foundChunk = stb->begin - stb->end >= actualChunkSize;
                } else if (stb->end + actualChunkSize > size) {
                    // wrapping needed, check if enough space
                    foundChunk = stb->begin >= actualChunkSize;
                    // write wrapper marker
                    *(mem + stb->end) = 0;
                    stb->end = 0;
                    stb->wrapped = true;
                }

                if (!foundChunk) { 
                    if (WAIT == m) {
                        // wait until other thread/process removes chunk
                        pthread_cond_wait(&stb->cond, &stb->mutex);
                    } else {
                        return nullptr;
                    }
                }
            }

            uint8_t* ptr = (uint8_t*)(mem + stb->end);

            // write chunk size
            *(uint32_t*)ptr = chunkSize;
            ptr += sizeof(uint32_t);

            stb->end += actualChunkSize;
            stb->chunkCounter += 1;

            pthread_cond_signal(&stb->cond);

            return ptr;
        }

        void lock () {

            pthread_mutex_lock(&stb->mutex);
        }

        void unlock () {

            pthread_mutex_unlock(&stb->mutex);
            pthread_cond_signal(&stb->cond);
        }

        void pop (mode m = WAIT) { 

            while (WAIT == m && stb->chunkCounter < 1) {
                // wait until other thread/process adds a chunk
                pthread_cond_wait(&stb->cond, &stb->mutex); 
            }

            uint32_t chunkSize = *(uint32_t*)(mem + stb->begin);

            if (stb->wrapped && chunkSize == 0) {
                // if end has already wrapped, and we are
                // at the wrapping point (indicated by chunkSize of 0)
                stb->begin = 0;
                chunkSize = *(uint32_t*)(mem + stb->begin);
                stb->wrapped = false;
            } 

            // if this was a non empty chunk deallocate it
            if (chunkSize > 0) {
                *(uint32_t*)(mem + stb->begin) = 0;
                stb->begin += sizeof(uint32_t) + chunkSize;
                stb->chunkCounter = std::max(stb->chunkCounter - 1, (size_t)0);
            }
                     
            pthread_cond_signal(&stb->cond);
        }

        uint8_t* peek (mode m = WAIT) {

            while (stb->chunkCounter < 1) {
                if (NO_WAIT == m) {
                    return nullptr;
                }
                pthread_cond_wait(&stb->cond, &stb->mutex); 
            }

            return (uint8_t*)(stb->begin);
        }
    };

    struct send {

        std::unordered_map<std::string, size_t> dataOffsets;

        send (seg& segment, def d) { 

            uint32_t msgSize = 0;

            for (std::pair<std::string, uint32_t>& entry : d) {
                // name size
                msgSize += sizeof(uint8_t);
                // name 
                msgSize += std::get<0>(entry).length();
                // data size
                msgSize += sizeof(uint32_t);
                // data
                msgSize += std::get<1>(entry);
            }

            // find fitting chunk in shared memory 
            
            uint8_t* ptr = segment.push(msgSize);

            if (ptr == nullptr) {
                return;
            }

            // write size of following message segment

            *(uint32_t*)ptr = msgSize;
            ptr += sizeof(uint32_t);

            // initialize frame to hold data

            for (std::pair<std::string, uint32_t>& entry : d) {

                std::string& entryName = std::get<0>(entry);
                uint8_t entryNameLen = entryName.length();
                uint32_t entryDataLen = std::get<1>(entry);

                // write name size
                *(uint8_t*)ptr = entryNameLen;
                ptr += sizeof(uint8_t);

                // write name
                memcpy(ptr, entryName.c_str(), entryNameLen);
                ptr += entryNameLen;

                // write data size 
                *(uint32_t*)ptr = entryDataLen;
                ptr += sizeof(uint32_t);

                // store offset to buffer start
                dataOffsets[entryName] = ptr - buf;
                ptr += entryDataLen;
            }
        }

        template<typename T>
        T& at(std::string entryName) {

            return (T&) *(buf + dataOffsets.at(entryName));
        }
    };

    struct recv { 

        std::unordered_map<std::string, size_t> dataOffsets;

        recv (seg& segment) {

            // TODO: real implementation
            uint8_t* ptr = buf;

            uint32_t msgSize = *(uint32_t*)buf;
            ptr += sizeof(uint32_t);

            while (ptr - buf < msgSize + sizeof(uint32_t)) { 

                // read name size
                uint8_t entryNameLen = *ptr;
                ptr += sizeof(uint8_t);

                // read name
                std::string entryName((char*)ptr, entryNameLen);
                ptr += entryNameLen;

                // read data size
                uint32_t entryDataLen = *(uint32_t*)ptr;
                ptr += sizeof(uint32_t);

                // read data offset relative to buffer start
                dataOffsets[entryName] = ptr - buf;
                ptr += entryDataLen;
            }
        }

        template<typename T>
        T& at(std::string entryName) {

            return (T&) *(buf + dataOffsets.at(entryName));
        }
    };
}

/*
 * Defines the << operator for shq::seg.
 * For "better" printf debugging ...
 */
std::ostream& operator<<(std::ostream& os, shq::seg& s) {

    pthread_mutex_lock(&s.stb->mutex);

    os << "Segment state:" << std::endl;
    os << "    Begin: " << s.stb->begin << std::endl;
    os << "    End: " << s.stb->end << std::endl;
    os << "    Wrapped: " << s.stb->wrapped << std::endl;

    uint8_t* ptr = s.mem + s.stb->begin;
    bool wrapped = s.stb->wrapped;

    std::vector<size_t> chunkSizes;

    while (wrapped || ptr != (s.mem + s.stb->end)) {
        uint32_t chunkSize = *(uint32_t*)(ptr);
        if (s.stb->wrapped && 0 == chunkSize) {
            ptr = s.mem;
            wrapped = false;
        } else {
            chunkSizes.push_back(chunkSize);
            ptr += sizeof(uint32_t) + chunkSize;
        }
    }

    os << "Found "
       << chunkSizes.size()
       << (chunkSizes.size() == 1 ? " chunk:" : " chunks:")
       << std::endl;

    size_t memoryUsage = 0;

    for (int i = 0; i < chunkSizes.size(); ++i) { 
        os << "    Chunk "
           << i << ": " << chunkSizes[i]
           << " bytes" << std::endl;
        memoryUsage += sizeof(uint32_t) + chunkSizes[i];
    }

    os << "Used " 
       << memoryUsage << "/" << s.size
       << " bytes";

    pthread_mutex_unlock(&s.stb->mutex);

    return os;
}
