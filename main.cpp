#include <fcntl.h>
#include <unistd.h>
#include <iostream>
#include <string.h>
#include <pthread.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <errno.h>

#include <tuple>
#include <vector>
#include <cstring>
#include <unordered_map>

namespace shm {

    typedef std::vector<std::pair<std::string, uint32_t>> def;

    uint8_t buf[256];

    constexpr size_t maxSegmentSize = 8192;

    struct stub {

        pthread_mutexattr_t attr;
        pthread_mutex_t mutex;

        uint8_t* startOffset = nullptr;
        uint8_t* endOffset = nullptr;
        bool wrapped = false;
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

            // check if we need a new segment
            if (0 > fd) {
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
                // get and initialize stub
                stb = (stub*)mem;
                *stb = {};

                // make memory mutex recursive
                pthread_mutexattr_init(&stb->attr);
                pthread_mutexattr_settype(&stb->attr, PTHREAD_MUTEX_RECURSIVE);
                pthread_mutexattr_setpshared(&stb->attr, PTHREAD_PROCESS_SHARED);
                pthread_mutex_init(&stb->mutex, &stb->attr);

                mem += sizeof(stub);
            } else {
                // also map and get stub
                mem = (uint8_t*) mmap(0, actualSize,
                        PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
                stb = (stub*)mem;
                mem += sizeof(stub);
            }

            stb->startOffset = mem;
            stb->endOffset = mem;

            // write end marker
            *(uint32_t*)(mem + sizeof(stub) + size) = 0;
        }

        uint8_t* alloc (size_t msgSize) { 

            pthread_mutex_lock(&stb->mutex);

            size_t actualMsgSize = sizeof(uint32_t) + msgSize;

            if (actualMsgSize > size || msgSize == 0) {
                return nullptr;
            }

            bool done = false;

            while (!done) {

                if (stb->wrapped) {
                    done = stb->startOffset - stb->endOffset >= actualMsgSize;
                } else if (stb->endOffset + actualMsgSize > mem + size) {
                    // need to wrap
                
                    // write wrapper marker
                    *(stb->endOffset) = 0;

                    // check if not enough space, deallocate needed
                    done = stb->startOffset - mem >= actualMsgSize;

                    stb->endOffset = mem;
                    stb->wrapped = true;
                } else {
                    //normal allocation all good
                    done = true; 
                }

                if (!done) { 
                    free();
                }
            }

            uint8_t* ptr = (uint8_t*)(stb->endOffset);

            // write message size
            *(uint32_t*)ptr = msgSize;
            ptr += sizeof(uint32_t);

            stb->endOffset += actualMsgSize;

            pthread_mutex_unlock(&stb->mutex);

            std::cout << "Alloc:" << std::endl;
            std::cout << "Start offset:" << stb->startOffset - mem << std::endl;
            std::cout << "End offset:" << stb->endOffset - mem << std::endl;
            std::cout << "Wrapped:" << stb->wrapped << std::endl;

            return ptr;
        }

        void free () { 

            pthread_mutex_lock(&stb->mutex);

            if (stb->endOffset != stb->startOffset || stb->wrapped) { 

                uint32_t* originalMsgSize = (uint32_t*)(stb->startOffset);
                uint32_t msgSize = *originalMsgSize;

                if (stb->wrapped) {
                    // if end has already wrapped, check if we are
                    // at the wrapping point (indicated by msgSize of 0)
                    if (msgSize == 0) {
                        // yes we are, warp along and update msgSize
                        stb->startOffset = mem;
                        msgSize = *(uint32_t*)(stb->startOffset);
                        stb->wrapped = false;
                    }
                } 

                // if this was a non empty chunk deallocate it
                if (msgSize > 0) {
                    stb->startOffset += sizeof(uint32_t) + msgSize;
                }

                // clear chunk
                *originalMsgSize = 0;
            }

            pthread_mutex_unlock(&stb->mutex);

            std::cout << "Free:" << std::endl;
            std::cout << "Start offset:" << stb->startOffset - mem << std::endl;
            std::cout << "End offset:" << stb->endOffset - mem << std::endl;
            std::cout << "Wrapped:" << stb->wrapped << std::endl;
        }
    };

    struct send {

        std::unordered_map<std::string, size_t> dataOffsets;

        send (std::string topic, def d) { 

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
            
            std::cout << "Searching for shared chunk with " 
                << msgSize 
                << " bytes ..."
                << std::endl;

            // TODO: real implementation
            uint8_t* ptr = buf;

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

        recv (std::string segmentName) {

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

int main (int argc, char** argv) {

    shm_unlink("segment_test");

    std::cout << "Test1" << std::endl;
    {
        shm::seg segTest("segment_test", 36);
        segTest.alloc(0);
        segTest.free();
        segTest.alloc(4);
    }
    std::cout << "\n" << std::endl;

    /*
    std::cout << "Test2" << std::endl;
    {
        shm::seg segTest("segment_test", 36);
        segTest.alloc(4);
        segTest.alloc(4);
        segTest.alloc(32);
    }
    std::cout << "\n" << std::endl;
    */

    /*
    {
        shm::send msg("segment_test", {
                {"aaaa", sizeof(float)},
                {"y", sizeof(float)},
                {"z", sizeof(float)}
            });

        msg.at<float>("aaaa") = 42.0;
        msg.at<float>("y") = 78.0;
        msg.at<float>("z") = 89.0;
    }

    {
        shm::recv msg("segment_test");

        std::cout << msg.at<float>("aaaa") << std::endl;
        std::cout << msg.at<float>("y") << std::endl;
        std::cout << msg.at<float>("z") << std::endl;
    }
    */

    return 0;
}
