#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <linux/futex.h>
#include <sys/syscall.h>

#include <vector>
#include <atomic>
#include <cassert>
#include <cstring>
#include <iostream>
#include <unordered_map>

namespace shq {

    /*
     * Simple object wrapper around the file lock concept.
     *
     * We use file locks excessively, as they allow read/write
     * locks and are cleaned up when the owning process dies. 
     */
    struct file_lock {
        
        const int descriptor;
        const int offset;
        const size_t len;
        const int type;

        file_lock (int descriptor, int offset, size_t len, int type) :
            descriptor{descriptor}, offset{offset}, len{len}, type{type} {
        }

        flock buildFlock() {

            flock lock;

            lock.l_whence = SEEK_SET;
            lock.l_start = offset;
            lock.l_len = len;

            return lock;
        }

        int lock (bool blocking = true) { 

            flock lock = buildFlock();
            lock.l_type = type;

            if (-1 == fcntl(descriptor, blocking ? F_SETLKW : F_SETLK, &lock)) {
                return errno;
            }

            return 0;
        }

        int unlock () {

            flock lock = buildFlock();
            lock.l_type = F_UNLCK;

            if (-1 == fcntl(descriptor, F_SETLK, &lock)) {
                return errno;
            }

            return 0;
        }

        bool check () {

            flock lock = buildFlock();
            lock.l_type = type;

            if (-1 == fcntl(descriptor, F_GETLK, &lock)) {
                return false;
            }

            return lock.l_type == F_UNLCK;
        }
    };

    /*
     * Sadly, pthread condition variables do not behave like needed.
     *
     * See: https://bugs.debian.org/cgi-bin/bugreport.cgi?bug=884776
     * And: https://sourceware.org/bugzilla/show_bug.cgi?id=21422
     *
     * This is a lightweight condition variable replacement
     * using Linux futexes and file locks. Not very polished
     * and probably prone to the "thundering herd" effect,
     * meaning that the cache will be competely trashed, when
     * a lot of processes wake up at once and all want to lock
     * the same file_lock.
     */
    struct futex_cond_var {

        std::atomic_int value{0};
        std::atomic_uint previous{0};

        int wait (file_lock& fl) {

            int val = value;
            previous = val;

            fl.unlock();
            int res = syscall(SYS_futex,
                    &value, FUTEX_WAIT, val, NULL, NULL, 0);
            fl.lock();

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
     * However, this also means that the largest addressable size
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

    /*
     * A chunk is one allocated unit (i.e. a message) in
     * the shared memory segment.
     */
    struct chunk {

        static size_t headerSize;

        // payload size of the chunk in bytes
        uint32_t* size = nullptr;

        // sequence number of the chunk
        uint64_t* seq = nullptr;

        // pointer to the payload data of the chunk
        uint8_t* data = nullptr;
    };

    // sizeof(uint32_t) for chunk size prefix
    // sizeof(uint64_t) for the sequence number prefix
    size_t chunk::headerSize = sizeof(uint32_t) + sizeof(uint64_t);

    /*
     * Header contains necessary synchronization data.
     * Written to the beginning of the shared memory segment.
     */
    struct header {

        // the total size of the shared memory segment
        size_t size;

        // the next sequence number to be used
        uint64_t nextSeq;

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
        // meaning: size - sizeof(header)
        size_t usableSize;

        // temporary ring buffer
        ring_buffer rb;

        // pointer into the shared memory after the header
        uint8_t* memory;

        // header used for synchronization, points to shared memory
        header* hdr;

        // wrap lock parameters 
        int wrapLockOffset;
        int wrapLockSize;

    public:

        // name of the shared memory segment
        const std::string name;

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
              memory{nullptr},
              hdr{nullptr},
              wrapLockOffset{-1},
              wrapLockSize{-1},
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

                lockHeader(F_WRLCK);

                openMemory();

                // initialize header
                
                hdr->size = size;
                hdr->nextSeq = 1;
                
                ring_buffer rb = hdr->rb;
                rb.begin = 0;
                rb.end = 0;
                rb.wrapped = 0;
                hdr->rb = rb;
            }

            // take care of adjusting size
            
            lockHeader(F_WRLCK);

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

            unlockHeader();
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

        int lockHeader (int type) {

            file_lock headerLock(descriptor, 0, 1, type);
            return headerLock.lock(blocking);
        }

        int unlockHeader () { 

            file_lock headerLock(descriptor, 0, 1, 0);
            return headerLock.unlock();
        }

        int clearLock (chunk c) {

            file_lock l(
                    descriptor, 
                    c.data - chunk::headerSize - (uint8_t*)hdr, 
                    chunk::headerSize + *c.size,
                    F_RDLCK);

            return l.unlock();
        }

        void commit () {

            hdr->rb = rb;
            hdr->readerCondVar.signal();

            if (wrapLockOffset != -1) {
                file_lock l(
                        descriptor,
                        wrapLockOffset,
                        wrapLockSize,
                        F_WRLCK);
            }
        }

        bool alloc (size_t chunkSize, chunk& result) { 

            if (-1 == lockHeader(F_WRLCK)) {
                return false;
            }

            openMemory();

            size_t actualChunkSize = chunk::headerSize + chunkSize;

            // abort if trying to allocate 0 bytes
            // or if the chunkSize is bigger than the segment 
            if (actualChunkSize > usableSize || chunkSize == 0) {
                return false;
            }

            // start of the bad and ugly bit (we're missing the good one)
            
            ring_buffer rb_start = hdr->rb;
            rb = rb_start;

            wrapLockOffset = -1;

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

                        wrapLockOffset = memory + rb.end - (uint8_t*)hdr;
                        wrapLockSize = usableSize - rb.end;

                        // try to acquire write lock from
                        // ring buffer end until segment end

                        file_lock wrapLock(
                                descriptor,
                                wrapLockOffset,
                                wrapLockSize,
                                F_WRLCK);

                        if (-1 == lockHeader(F_RDLCK)) {
                            return false;
                        }

                        if (-1 == wrapLock.lock(blocking)) {
                            return false;
                        }

                        if (-1 == lockHeader(F_WRLCK)) {
                            return false;
                        }

                        // Check if the ring buffer state has changed.
                        // If it has, then the chunk we tried to wrap
                        // on, may be now be used by a smaller message.
                        // Therefore, we abort mission and try again.

                        ring_buffer rb_now = hdr->rb;

                        if (rb_start.end != rb_now.end) {
                            rb_start = rb_now;
                            rb = rb_start;
                            wrapLock.unlock();

                            continue;
                        }

                        // ok, enough space ...
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
                    // ok, it seems like we found a viable chunk
                
                    ptr = memory + rb.end;

                    // convert header write lock to read lock
                    // so that readers may already proceed
                    if (-1 == lockHeader(F_RDLCK)) {
                        return false;
                    }

                    // try to acquire chunk write lock,
                    // may wait for unfinished readers
                    file_lock writeLock(
                            descriptor, 
                            ptr - (uint8_t*)hdr,
                            actualChunkSize, 
                            F_WRLCK);

                    if (-1 == writeLock.lock(blocking)) {
                        return false;
                    }

                    ring_buffer rb_now = hdr->rb;

                    if (rb_start.end != rb_now.end) {
                        // The ring buffer state has changed.
                        // This means another writer has already written this
                        // chunk, while we were waiting in writeLock.lock().
                        // Therefore, we release the lock and try again.
                        foundChunk = false;
                        rb_start = rb_now;
                        rb = rb_start;
                        writeLock.unlock();

                        // also convert read lock to write lock,
                        // as we may need to modify the buffer state again
                        if (-1 == lockHeader(F_WRLCK)) {
                            return false;
                        }
                    } 
                } else {
                    // no free chunk found, pop the oldest chunk
                    
                    uint32_t popChunkSize = *(uint32_t*)(memory + rb.begin);

                    if (1 == rb.wrapped && 0 == popChunkSize) {
                        // 0 == popChunkSize means we are at the wrapping marker
                        rb.begin = 0;
                        rb.wrapped = 0;
                        popChunkSize = *(uint32_t*)memory;
                    } 

                    rb.begin += chunk::headerSize + popChunkSize;

                    // removes now to be written chunk from the ring buffer

                    ring_buffer writeBack = rb;
                    writeBack.end = rb_start.end;
                    hdr->rb = writeBack;
                } 
            }

            // write chunk size
            result.size = (uint32_t*)ptr;
            *result.size = chunkSize;
            ptr += sizeof(uint32_t);

            // write sequence number
            result.seq = (uint64_t*)ptr;
            *result.seq = hdr->nextSeq++;
            ptr += sizeof(uint64_t);

            // write pointer to payload data
            result.data = ptr;

            // advance end in temporary ring buffer
            rb.end += (int)actualChunkSize;
            
            unlockHeader();

            // Until now the new message allocation only happend
            // in a temporary ring buffer. This ring buffer
            // still has to be commited, so the chunk becomes
            // visible to the readers. So if the process dies
            // before committing, the memory segment still
            // remains in a valid state, only the chunk is lost.
            // Committing happens in the destructor of "message".

            return true;
        }

        bool get (uint64_t prevSeq, chunk& result) {

            // try to acquire segment-wide lock
            
            if (-1 == lockHeader(F_RDLCK)) {
                return false;
            }

            // try to find unread chunk
            // may block until chunk available
        
            bool foundChunk = false;
            
            while(!foundChunk) {
                for (chunk& c : chunks()) {
                    if (*c.seq > prevSeq) {
                        result = c;
                        foundChunk = true;
                        break;
                    }
                }
                if (!foundChunk) {
                    if (!blocking) {
                        return false;
                    }
                    // did not find any new chunk -> sleep
                    file_lock headerLock(descriptor, 0, 1, F_RDLCK);
                    hdr->readerCondVar.wait(headerLock);
                }
            }

            file_lock readLock(
                    descriptor,
                    result.data - chunk::headerSize - (uint8_t*)hdr, 
                    *result.size + chunk::headerSize,
                    F_RDLCK);

            if (-1 == readLock.lock(blocking)) {
                return false;
            }

            // at this point the chunk is protected from overwrite

            unlockHeader();

            return true;
        }

        std::vector<chunk> chunks () const {

            std::vector<chunk> cs;

            ring_buffer rb = hdr->rb;

            bool wrapped = rb.wrapped;
            uint32_t position = rb.begin;

            while (position != rb.end || wrapped != false) {

                uint8_t* ptr = memory + position;
                uint32_t chunkSize = *(uint32_t*)ptr;

                if (1 == wrapped && 0 == chunkSize) {
                    // chunkSize == 0 means we are at the wrapping marker
                    position = 0;
                    chunkSize = *(uint32_t*)memory;
                    wrapped = false;
                    ptr = memory;
                } 

                // fill chunk struct with data

                cs.emplace_back();
                chunk& c = cs.back();

                c.size = (uint32_t*)ptr;
                ptr += sizeof(uint32_t);

                c.seq = (uint64_t*)ptr;
                ptr += sizeof(uint64_t);

                c.data = ptr;
                ptr += chunkSize;

                position = ptr - memory;
            }

            return cs;
        }

        void printChunks () {

            lockHeader(F_RDLCK);

            std::vector<chunk> cs = chunks();
            const ring_buffer rb = hdr->rb;

            std::cout << "Segment size: " << usableSize << std::endl;
            std::cout << "Begin: " << rb.begin
                      << ", end: " << rb.end
                      << ", wrapped:" << rb.wrapped << std::endl;
            std::cout << "Found " << cs.size() << " chunks:" << std::endl;

            for (chunk& c : cs) {
                std::cout << "    seq: " << *c.seq
                          << ", size: " << *c.size + chunk::headerSize
                          << ", offset: " << c.data - memory - chunk::headerSize
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
                    (maxMsgSize + chunk::headerSize) * queueSize,
                    false,
                    blocking) {
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
        // if the message was obtained sucessfully
        // is also set if an error occurs
        bool isOk = true;

    public:

        chunk chuk;

        message (reader& rdr) : seg{rdr} { 

            if (!seg.get(readerPrevSeq, chuk)) {
                isOk = false;
                return;
            }

            readerPrevSeq = *chuk.seq;

            uint8_t* ptr = chuk.data;

            while (ptr - chuk.data < (long int)*chuk.size) { 

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
                dataOffsets[entryName] = ptr - chuk.data;
                ptr += entryDataLen;
            }
        }
        
        message (writer& wtr, definition def) : seg{wtr} {

            // first calculate message size from message definition
            
            size_t size = 0;

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
    
            if (!seg.alloc(size, chuk)) {
                // in non-blocking mode or message too large
                isOk = false;
                return;
            }

            uint8_t* ptr = chuk.data;

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
                dataOffsets[entryName] = ptr - chuk.data;
                ptr += entryDataLen;
            }
        }

        ~message () {

            if (isOk) {
                if (!seg.reader) {
                    seg.commit();
                }

                seg.clearLock(chuk);
            }
        }

        bool ok () {

            return isOk;
        }

        template<typename T>
        T& at (std::string entryName) {

            return (T&) *(chuk.data + dataOffsets.at(entryName));
        }

        bool has (std::string entryName) { 

            return 1 == dataOffsets.count(entryName);
        }

        std::vector<std::string> keys () {

            std::vector<std::string> ks;
            for (auto d : dataOffsets) {
                ks.push_back(d.first);
            }
            return ks;
        }
    };

    uint64_t message::readerPrevSeq = 0;
} 
