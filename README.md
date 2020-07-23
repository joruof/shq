# shq (shared queue)

A robust, single-header, dynamic, N-to-M message queue in C++11 via shared memory for GNU/Linux.

## Usage

Usage is best demonstrated by example. This example consists of a writer,
which writes a message to a shared queue, and a reader which reads and prints the message.

### Writer

```C++
#include "shq.h"

int main () {

    const size_t segmentSize = 512;

    shq::writer writer("test_segment_name", segmentSize);

    std::string text = "Hello, World!";
    double number = 3.141592;

    shq::definition def{
        {"text", text.size()},
        {"number", sizeof(number)},
    };

    // messages use RAII for synchronization
    // message is send in destructor
    {
        shq::message msg(writer, def);

        text.copy(msg.ptr<char>("text"), text.size());
        msg.at<double>("number") = number;
    }
}
```

### Reader

```C++
#include "shq.h"

int main () {

    shq::reader reader("test_segment_name");

    shq::message msg(reader);

    if (msg.ok()) {

        double number = msg.at<double>("number");
        std::string text(msg.ptr<char>("text"), msg.entrySize("text"));

        std::cout << "Number is: " << number << std::endl;
        std::cout << "Text is: " << text << std::endl;
    }

    reader.destroy();
}
```
To run the examples build via the cmake file in the project root directory:

```
> mkdir build
> cd build
> cmake ..
> make
```

Then execute the respective example binaries. First the writer

```
> ./build/example_writer
```

then the reader.

```
> ./build/example/reader
< Number is: 3.141592
< Text is: Hello, World!
```

## Guarantees

The way shq is implemented **should** (unless bugs) guarantee the following: 

1. All messages are received in the same order they were published.
2. Any number of reader or writer processes can access the shared memory concurrently without corruption.
3. While a message is being read/written by at least one reader/writer it will not be modified by any other writers.
4. Any reader/writer can die* at any moment without corrupting the shared memory or stalling other readers/writers. 

\* for basically any reason, e.g. SIGINT, SIGKILL, SIGSEGV, RAM hot unplugged, giraffe, ...

**It is explicity NOT guaranteed that a message is read by any reader 
before being overwritten, meaning that readers may miss messages.**
A reliable transport mechanism may be implemented in the future.

## Restrictions

*  Currently only one thread per process is allowed. Synchronization for multiple threads is up to the user.
*  The size of a single shared memory segment is limited to about 2.1 GB (see code)
*  Only one writer can write to a shared memory segment at any time.
   Still multiple writers may be opened and write sequentially to the same segment.
   Other writers will simply block until the current writer finishes. 
   This may limit throughput for applications with lots of concurrent, high-rate
   writers operating on the same segment. 

## Disclaimer

Consider this code as early **alpha** version. Expect that it changes.
Bugs may be present. Things may break. You have been warned.
