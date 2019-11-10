#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>
#include <sys/mman.h>
#include <sys/stat.h>

#include <tuple>
#include <vector>
#include <atomic>
#include <cstring>
#include <unordered_map>

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
     * This contains the state of the ring buffer
     * in 64 bit, which allows atomic access.
     *
     * This means that the largest usable size of a
     * single shared memory segment is limited to ~2.1 GB.
     */ 
    struct ring_buffer { 

        uint64_t 
            // begin of the allocation in the ring buffer
            begin : 31,
            // end of the allocation in the ring buffer
            end : 31,
            // is 1 if the allocation is wrapped (end < begin)
            wrapped : 2;
    };

    /*
     * Header contains necessary synchronization data.
     * Written to the beginning of the shared memory segment.
     */
    struct header {

        // the total size of the shared memory segment
        size_t size;

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

        // contains the state of the ring buffer
        std::atomic<ring_buffer> rb;
    };

    class segment {

        // file descriptor of shared memory segment
        int descriptor;

        // total size of the shared memory segment
        size_t size;

        // actual usable size of the shared memory segment
        size_t usableSize;

        // name of the shared memory segment
        std::string name;

        // pointer into the shared memory
        uint8_t* memory;

        // header used for synchronization
        header* hdr;

        // points to the reader/writer mutex in shared memory
        pthread_mutex_t* roleMutexPtr;

        // points to the reader/writer condition in shared memory
        pthread_cond_t* roleCondPtr;

        // whether to wait for locks or return with error instead
        bool blocking;

        // true if this is a reader segment, false if writer
        bool reader;

    public:

        segment (const char* name, 
                 const size_t usableSize, 
                 bool reader = true, 
                 bool blocking = true) 
            : descriptor{shm_open(name, O_RDWR, 0666)},
              memory{nullptr},
              hdr{nullptr},
              size{sizeof(header) + usableSize + sizeof(uint32_t)},
              usableSize{usableSize},
              name{name},
              blocking{blocking},
              reader{reader} {

            if (-1 != descriptor) {
                // opening succeeded, reusing already created segment
                openMemory();
            } else {
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

                // initialize header
                
                hdr->size = size;
                
                ring_buffer rb = hdr->rb;
                rb.begin = 0;
                rb.end = 0;
                rb.wrapped = 0;
                hdr->rb = rb;
                
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

            // (try to) register as a reader/writer
            
            int lockResult = robustLock(roleMutexPtr, blocking);

            if (EBUSY == lockResult) { 
                throw std::runtime_error(std::string("Another ")
                        + (reader ? "reader" : "writer")
                        + " has already connected to the segment!");
            }

            if (EOWNERDEAD == lockResult) { 
                // Is ignoring "blocking", but no way not to block here
                // without risking huge inconsistencies.
                robustLock(&hdr->mutex);
                // This fixes broken condition variables, which sometimes
                // occur, if the process died while waiting on a condition.
                pthread_cond_init(roleCondPtr, &hdr->condAttr);
                pthread_mutex_unlock(&hdr->mutex);
            }

            // take care of adjusting size

            robustLock(&hdr->mutex);

            // only resize if bigger than current size
            if (size > hdr->size) {
                if (0 > ftruncate(descriptor, size)) { 
                    throw std::runtime_error(
                            std::string("Resizing shared memory segment \"")
                            + std::string(name)
                            + std::string("\" to ")
                            + std::to_string(size)
                            + std::string(" bytes failed: ")
                            + std::strerror(errno));
                }
                hdr->size = size;
            } else if (size < hdr->size) {
                openMemory();
            }

            pthread_mutex_unlock(&hdr->mutex);
        }

        ~segment () {

            // unregister as reader/writer
            pthread_mutex_unlock(roleMutexPtr);

            munmap(hdr, size);
            close(descriptor);
        }

        void openMemory () {

            if (nullptr != hdr) {
                if (hdr->size == size) { 
                    return;
                }

                size_t newSize = hdr->size;

                munmap(hdr, size);

                size = newSize;
                usableSize = size - sizeof(uint32_t) - sizeof(header);
            }

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

            if (reader) {
                roleMutexPtr = &hdr->readerMutex;
                roleCondPtr = &hdr->readerCond;
            } else {
                roleMutexPtr = &hdr->writerMutex;
                roleCondPtr = &hdr->writerCond;
            }
        }

        void destroy () {

            // Prevents the shared memory segment to be opened 
            // and marks the segment for deallocation once all
            // processes have unmapped its associated memory.
            // Already connected processes continue normally.
            shm_unlink(name.c_str());
        }

        size_t length () {

            return usableSize;
        }

        bool empty () {

            ring_buffer rb = hdr->rb;
            return 0 == rb.wrapped && rb.begin == rb.end;
        }

        int lock () {

            return robustLock(&hdr->mutex, blocking);
        }

        void unlock () { 

            if (roleCondPtr == &hdr->readerCond) { 
                pthread_cond_signal(&hdr->writerCond);
            } else { 
                pthread_cond_signal(&hdr->readerCond);
            }

            pthread_mutex_unlock(&hdr->mutex);
        }

        uint8_t* push (size_t chunkSize) { 

            openMemory();

            // sizeof(uint32_t) byte for chunk size prefix
            size_t actualChunkSize = sizeof(uint32_t) + chunkSize;

            // abort if trying to allocate 0 bytes
            // or if the chunkSize is bigger than the segment 
            if (actualChunkSize > usableSize || chunkSize == 0) {
                return nullptr;
            }

            // the bad and the ugly bit (we're missing the good one)
        
            ring_buffer rb = hdr->rb;
            
            bool foundChunk = false;

            while (!foundChunk) {

                if (1 == rb.wrapped) {
                    if (rb.begin - rb.end >= actualChunkSize) {
                        foundChunk = true;
                    }
                } else if (rb.end + actualChunkSize > usableSize) {
                    // wrapping needed, check if enough space at the beginning
                    if (rb.begin >= actualChunkSize) {
                        // ok enough space ...
                        foundChunk = true;
                        // ... write wrapping marker
                        *(uint32_t*)(memory + rb.end) = 0;
                        // .. and wrap
                        rb.end = 0;
                        rb.wrapped = 1;
                    }
                } else { 
                    foundChunk = true;
                }

                if (!foundChunk) { 
                    if (!blocking) {
                        return nullptr;
                    }
                    
                    // write back ring buffer state before waiting
                    hdr->rb = rb;

                    int resultWait = pthread_cond_wait(
                            &hdr->writerCond, &hdr->mutex); 
                    if (EOWNERDEAD == resultWait) {
                        pthread_mutex_consistent(&hdr->mutex);
                    }

                    openMemory();

                    // ring buffer state may have changed during wait 
                    rb = hdr->rb;
                }
            }

            uint8_t* ptr = memory + rb.end;

            // write chunk size
            *(uint32_t*)ptr = chunkSize;
            ptr += sizeof(uint32_t);

            // advance end 
            rb.end += actualChunkSize;

            // atomic write back of ring_buffer state
            hdr->rb = rb;

            return ptr;
        }

        uint8_t* pop (size_t& chunkSize) { 

            openMemory();

            while (empty()) {
                if (!blocking) {
                    return nullptr;
                }
                // wait until other thread/process adds a chunk
                int resultWait = pthread_cond_wait(
                        &hdr->readerCond, &hdr->mutex); 
                if (EOWNERDEAD == resultWait) {
                    pthread_mutex_consistent(&hdr->mutex);
                }

                openMemory();
            }

            ring_buffer rb = hdr->rb;

            chunkSize = *(uint32_t*)(memory + rb.begin);

            if (1 == rb.wrapped && 0 == chunkSize) {
                // chunkSize == 0 means we are at the wrapping marker
                rb.begin = 0;
                chunkSize = *(uint32_t*)(memory + rb.begin);
                rb.wrapped = 0;
            } 

            uint8_t* ptr = memory + rb.begin + sizeof(uint32_t);
            rb.begin += sizeof(uint32_t) + chunkSize;

            // atomic write back of ring buffer state
            hdr->rb = rb;

            return ptr;
        }
    };

    /*
     * Convenience types for read or write access to shared memory.
     */

    struct reader : segment {
        reader (const char* name, bool blocking = true)
            : segment(name, 0, true, blocking) { }
    };

    struct writer : segment {

        writer (const char* name,
                const size_t maxMsgSize,
                const size_t queueSize,
                bool blocking = true)
            : segment(name, 
                    (maxMsgSize + sizeof(uint32_t)) * queueSize,
                    false,
                    blocking) {
            }
        ~ writer () {
            destroy();
        }
    };

    /*
     * Message definition as a list of (name, size) pairs.
     */
    typedef std::vector<std::pair<std::string, uint32_t>> definition;

    /*
     * The message type is used to conveniently operate on segments.
     */
    class message {

        // stores the offset from the beginning of the message
        // memory chunk for the corresponding field name
        std::unordered_map<std::string, size_t> dataOffsets;

        // reference to the shared memory segment
        segment& seg;

        // used in non-blocking mode to check
        // if the message actually contains something
        bool isOk = true;

        // points to the (shared) memory chunk of the message
        uint8_t* basePtr = nullptr;

        // size used by message in shared memory
        size_t size = 0;

    public:

        message (reader& rdr) : seg{rdr} { 

            // try to acquire segment-wide lock

            if (EBUSY == seg.lock()) {
                // in non-blocking mode
                isOk = false;
                return;
            }

            // try to pop chunk, may block until chunk available

            size = 0;
            basePtr = seg.pop(size);
            if (nullptr == basePtr) {
                // in non-blocking mode
                isOk = false;
                return;
            }

            uint8_t* ptr = basePtr;

            while (ptr - basePtr < size + sizeof(uint32_t)) { 

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
                dataOffsets[entryName] = ptr - basePtr;
                ptr += entryDataLen;
            }
        }
        
        message (writer& wtr, definition def) : seg{wtr} {

            // first calculate message size from message definition

            for (std::pair<std::string, uint32_t>& entry : def) {
                // name size
                size += sizeof(uint8_t);
                // name 
                size += std::get<0>(entry).length();
                // data size
                size += sizeof(uint32_t);
                // data
                size += std::get<1>(entry);
            }

            // try to acquire segment-wide lock

            if (EBUSY == seg.lock()) {
                // in non-blocking mode
                isOk = false;
                return;
            }

            // try to allocate chunk, may block until memory available

            basePtr = seg.push(size);
            if (nullptr == basePtr) {
                // in non-blocking mode or message too large
                isOk = false;
                return;
            }

            uint8_t* ptr = basePtr;

            // initialize frame in chunk to hold data

            for (std::pair<std::string, uint32_t>& entry : def) {

                std::string& entryName = std::get<0>(entry);
                uint8_t entryNameLen = entryName.length();
                uint32_t entryDataLen = std::get<1>(entry);

                // write name size
                *(uint8_t*)ptr = entryNameLen;
                ptr += sizeof(uint8_t);

                // write entry name
                memcpy(ptr, entryName.c_str(), entryNameLen);
                ptr += entryNameLen;

                // write data size 
                *(uint32_t*)ptr = entryDataLen;
                ptr += sizeof(uint32_t);

                // store offset to buffer start
                dataOffsets[entryName] = ptr - basePtr;
                ptr += entryDataLen;
            }
        }

        ~message () {

            seg.unlock();
        }

        bool ok () {

            return isOk;
        }

        template<typename T>
        T& at (std::string entryName) {

            return (T&) *(basePtr + dataOffsets.at(entryName));
        }

        bool has (std::string entryName) { 

            return 1 == dataOffsets.count(entryName);
        }
    };
}
