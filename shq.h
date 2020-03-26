#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <linux/futex.h>
#include <sys/syscall.h>

#include <tuple>
#include <vector>
#include <atomic>
#include <cstring>
#include <iostream>
#include <unordered_map>

#include <chrono>

long int getTime() {

    auto t = std::chrono::high_resolution_clock::now();
    return t.time_since_epoch().count();
}

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
     * Sadly, pthread condition variables do not behave like needed.
     *
     * See: https://bugs.debian.org/cgi-bin/bugreport.cgi?bug=884776
     * And: https://sourceware.org/bugzilla/show_bug.cgi?id=21422
     *
     * This is a lightweight condition variable replacement
     * using Linux futexes. Not very polished though and probably
     * prone to the "thundering herd" effect.
     */
    struct futex_cond_var {

        std::atomic_int value{0};
        std::atomic_uint previous{0};

        int wait (pthread_mutex_t* mtx) {

            int val = value;
            previous = val;

            pthread_mutex_unlock(mtx);
            int res = syscall(SYS_futex,
                    &value, FUTEX_WAIT, val, NULL, NULL, 0);
            robustLock(mtx);

            return res;
        }

        int signal () {
            
            unsigned int val = 1 + previous;
            value = val;

            return syscall(SYS_futex,
                    &value, FUTEX_WAKE, INT32_MAX, NULL, NULL, 0);
        }
    };

    /*
     * This contains the state of the message ring buffer
     * in 64 bit, which allows atomic access. This means
     * that even if the process is interrupted by e.g. SIGINT
     * the buffer state is still guaranteed to be consistent.
     *
     * However, this also means that the largest usable size
     * of a single shared memory segment is limited to ~2.1 GB.
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

    struct chunk {

        // payload size of the chunk in bytes
        uint32_t size;

        // sequence number of the chunk
        uint64_t seq;

        // pointer to the payload data of the chunk
        uint8_t* data;
    };

    /*
     * Header contains necessary synchronization data.
     * Written to the beginning of the shared memory segment.
     */
    struct header {

        // the total size of the shared memory segment
        size_t size;

        // the next sequence number to be used
        uint64_t nextSeq;

        // used to init robust and shared mutexes
        pthread_mutexattr_t mutexAttr;

        // locked on allocation/deallocation
        pthread_mutex_t mutex;

        // contains the state of the ring buffer
        std::atomic<ring_buffer> rb;

        // to signal readers that something new can be read
        futex_cond_var readerCondVar;
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

        // temporary ring buffer
        ring_buffer rb;

    public:

        // header used for synchronization
        header* hdr;

        // whether to wait for locks or return with error instead
        const bool blocking;

        // true if this is a reader segment, false if writer
        const bool reader;

        /*
         * name:       The name of the shared memory segment.
         * usableSize: The size of the shared memory that can be
         *             used to store user data. If there already
         *             is a segment with the same name but with smaller
         *             usableSize the existing segment will be expanded.
         *             If the usableSize is bigger, the existing segment
         *             will NOT be truncated.
         * reader:     Whether this segment should be used in read mode.
         * blocking:   Whether operations may block or rather return
         *             immediately with error.
         */
        segment (const char* name, 
                 const size_t usableSize, 
                 bool reader = true, 
                 bool blocking = true) 
            : descriptor{shm_open(name, O_RDWR, 0666)},
              size{sizeof(header) + usableSize + sizeof(uint32_t)},
              usableSize{usableSize},
              name{name},
              memory{nullptr},
              hdr{nullptr},
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
                hdr->nextSeq = 1;
                
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

        int lockHeader () {

            return robustLock(&hdr->mutex, blocking);
        }

        void unlockHeader () { 

            pthread_mutex_unlock(&hdr->mutex);
        }

        void signalPush () {

            hdr->readerCondVar.signal();
        }

        void waitPush () {

            hdr->readerCondVar.wait(&hdr->mutex);
        }

        void commitTemporaryRingBuffer () {

            hdr->rb = rb;
        }

        int setWriteLock (uint8_t* addr, size_t len) {

            flock lock;

            lock.l_type = F_WRLCK;
            lock.l_whence = SEEK_SET;
            lock.l_start = addr - memory;
            lock.l_len = len;

            if (-1 == fcntl(descriptor, F_SETLKW, &lock)) {
                std::cout << "Lock failed: " << std::strerror(errno) << std::endl;
                return errno;
            }

            return 0;
        }

        int checkWriteLock (uint8_t* addr, size_t len) {

            flock lock;

            lock.l_type = F_WRLCK;
            lock.l_whence = SEEK_SET;
            lock.l_start = addr - memory;
            lock.l_len = len;

            if (-1 == fcntl(descriptor, F_GETLK, &lock)) {
                std::cout << "Lock failed: " << std::strerror(errno) << std::endl;
                return errno;
            }

            return lock.l_type == F_UNLCK ? 1 : 0;
        }

        int checkReadLock (uint8_t* addr, size_t len) {

            flock lock;

            lock.l_type = F_RDLCK;
            lock.l_whence = SEEK_SET;
            lock.l_start = addr - memory;
            lock.l_len = len;

            if (-1 == fcntl(descriptor, F_GETLK, &lock)) {
                std::cout << "Lock failed: " << std::strerror(errno) << std::endl;
                return errno;
            }

            return lock.l_type == F_UNLCK ? 1 : 0;
        }

        int setReadLock (chunk c) {

            flock lock;

            lock.l_type = F_RDLCK;
            lock.l_whence = SEEK_SET;
            lock.l_start = c.data - memory;
            lock.l_len = c.size;

            if (-1 == fcntl(descriptor, F_SETLKW, &lock)) {
                std::cout << "Lock failed: " << std::strerror(errno) << std::endl;
                return errno;
            }

            return 0;
        }

        int clearLock (uint8_t* addr, size_t len) {

            flock lock;

            lock.l_type = F_UNLCK;
            lock.l_whence = SEEK_SET;
            lock.l_start = addr - memory;
            lock.l_len = len;

            if (-1 == fcntl(descriptor, F_SETLK, &lock)) {
                std::cout << "Unlock failed: " << std::strerror(errno) << std::endl;
                return errno;
            }

            return 0;
        }

        uint8_t* push (size_t chunkSize) { 

            lockHeader();

            openMemory();

            // sizeof(uint32_t) byte for chunk size prefix
            // sizeof(uint64_t) byte for the sequence number prefix
            size_t actualChunkSize =  sizeof(uint32_t) + sizeof(uint64_t) + chunkSize;

            // abort if trying to allocate 0 bytes
            // or if the chunkSize is bigger than the segment 
            if (actualChunkSize > usableSize || chunkSize == 0) {
                return nullptr;
            }

            // the bad and the ugly bit (we're missing the good one)
            
            ring_buffer rb_start = hdr->rb;
            rb = rb_start;

            bool foundChunk = false;

            uint8_t* ptr;

            while (!foundChunk) {

                if (1 == rb.wrapped) {
                    if (rb.begin - rb.end >= (int)actualChunkSize) {
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

                if (foundChunk) {
                    // ok it seems like we found a viable chunk
                
                    ptr = memory + rb.end;

                    // try to acquire write lock, may block
                    
                    setWriteLock(ptr + sizeof(uint32_t) + sizeof(uint64_t), chunkSize);

                    ring_buffer rb_now = hdr->rb;

                    if (rb_start.end != rb_now.end) {

                        // The ring buffer state has changed.
                        // This means another writer has already written this
                        // chunk, while we were waiting in setWriteLock above.
                        // Therefore, we release the lock and try again.
                        foundChunk = false;
                        rb_start = rb_now;
                        rb = rb_start;
                        clearLock(ptr + sizeof(uint32_t) + sizeof(uint64_t), chunkSize);
                    } 
                } else {
                    // no free chunk found, pop the oldest chunk
                    
                    uint32_t popChunkSize = *(uint32_t*)(memory + rb.begin);

                    if (1 == rb.wrapped && 0 == popChunkSize) {
                        // chunkSize == 0 means we are at the wrapping marker
                        rb.begin = 0;
                        popChunkSize = *(uint32_t*)(memory + rb.begin);
                        rb.wrapped = 0;
                    } 

                    rb.begin += sizeof(uint32_t) + sizeof(uint64_t) + popChunkSize;

                    // to remove now to be written segment from the ring buffer

                    ring_buffer writeBack = rb;
                    writeBack.end = rb_start.end;
                    hdr->rb = writeBack;
                } 
            }

            // write chunk size
            *(uint32_t*)ptr = (uint32_t)chunkSize;
            ptr += sizeof(uint32_t);

            // write sequence number
            *(uint64_t*)ptr = hdr->nextSeq++;
            ptr += sizeof(uint64_t);

            // advance end in temporary ring buffer
            rb.end += (int)actualChunkSize;

            unlockHeader();

            return ptr;
        }

        std::vector<chunk> chunks () {

            std::vector<chunk> cs;

            ring_buffer rb = hdr->rb;

            bool wrapped = rb.wrapped;
            uint32_t position = rb.begin;

            while (position != rb.end || wrapped != false) {

                uint8_t* ptr = memory + position;
                uint32_t chunkSize = *(uint32_t*)ptr;

                if (true == wrapped && 0 == chunkSize) {
                    // chunkSize == 0 means we are at the wrapping marker
                    position = 0;
                    chunkSize = *(uint32_t*)memory;
                    wrapped = false;
                    ptr = memory;
                } 

                // fill chunk struct with data

                cs.emplace_back();
                chunk& c = cs.back();

                c.size = chunkSize;
                ptr += sizeof(uint32_t);

                c.seq = *(uint64_t*)ptr;
                ptr += sizeof(uint64_t);

                c.data = ptr;
                ptr += chunkSize;

                position = ptr - memory;
            }

            return cs;
        }

        void printChunks () {

            lockHeader();

            std::vector<chunk> cs = chunks();
            const ring_buffer rb = hdr->rb;

            std::cout << "Segment size: " << usableSize << std::endl;
            std::cout << "Begin: " << rb.begin
                      << ", end: " << rb.end
                      << ", wrapped:" << rb.wrapped << std::endl;
            std::cout << "Found " << cs.size() << " chunks:" << std::endl;

            for (chunk& c : cs) {
                std::cout << "    seq: " << c.seq
                          << ", size: " << c.size + sizeof(uint32_t) + sizeof(uint64_t)
                         << ", offset: " << c.data - memory - sizeof(uint32_t) - sizeof(uint64_t)
                          << std::endl;
            }

            unlockHeader();
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
        ~writer () {
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

        // the last read sequence number 
        static uint64_t readerPrevSeq;

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

        // the chunk that was referred to;
        chunk chuk;

        message (reader& rdr) : seg{rdr} { 

            // try to acquire segment-wide lock
            
            seg.lockHeader();

            // try to find unread chunk
            // may block until chunk available
        
            bool foundChunk = false;
            
            while(!foundChunk) {
                for (chunk& c : seg.chunks()) {
                    if (c.seq > readerPrevSeq) {
                        chuk = c;
                        foundChunk = true;
                        break;
                    }
                }
                if (!foundChunk) {
                    if (!seg.blocking) {
                        isOk = false;
                        return;
                    }
                    seg.waitPush();
                }
            }

            // TODO: this can actually be a major performance
            // issue because everyone (writers AND readers)
            // has to wait for a blocking writer to finish
            // before the read lock can be set.
            // Maybe the issue can be mitigated a bit, if not
            // a mutex but fcntl is use to guard the header.
            // That way at least all readers can still operate.
            
            std::cout << "trying to read " << chuk.seq << std::endl;
            
            std::cout << "before read lock" << std::endl;
            
            if (-1 == seg.setReadLock(chuk)) {
                isOk = false;
                return;
            }

            std::cout << "after read lock" << std::endl;

            seg.unlockHeader();

            // at this point the chunk is protected from overwrite
            // but writers can still write to other chunks

            size = chuk.size;
            readerPrevSeq = chuk.seq;
            basePtr = chuk.data;

            uint8_t* ptr = basePtr;

            while (ptr - basePtr < (long int)size) { 

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

            // try to allocate chunk, may block until memory available
    
            basePtr = seg.push(size);

            if (nullptr == basePtr) {
                // in non-blocking mode or message too large
                isOk = false;
                return;
            }

            chuk.seq = seg.hdr->nextSeq - 1;

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

            if (!seg.reader) {
                seg.commitTemporaryRingBuffer();
                seg.signalPush();
                seg.clearLock(basePtr, size);
            }

            seg.clearLock(basePtr, size);
        }

        bool ok () {

            return isOk;
        }

        std::vector<std::string> keys () {

            std::vector<std::string> ks;
            for (auto d : dataOffsets) {
                ks.push_back(d.first);
            }
            return ks;
        }

        template<typename T>
        T& at (std::string entryName) {

            return (T&) *(basePtr + dataOffsets.at(entryName));
        }

        bool has (std::string entryName) { 

            return 1 == dataOffsets.count(entryName);
        }
    };

    uint64_t message::readerPrevSeq = 0;
}
