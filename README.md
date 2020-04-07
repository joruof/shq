# shq (shared queue)

A robust, single-header, dynamic, N-to-M message queue in C++ via shared memory for GNU/Linux.

# Guarantees

The way shq is implemented guarantees the following: 

1. All messages are received in the same order they were published.
2. Any number of subscribers or publisher processes can access the shared memory concurrently without corruption.
3. While a message is being read by at least one subscriber it will not be modified by any publisher.
4. Any publisher or subscriber can die at any moment without corrupting the shared memory or stalling other publishers or subscribers.

**It is explicity NOT guaranteed that a messages is read by any subscriber
before being overwritten, meaning that subscribers may miss messages.**

Though a reliable transport mechanism may be implemented in the future.

# Restrictions

*  The size of a single shared memory segment is limited to about 2.1 GB (see code)
*  Only one publisher can write to a shared memory segment at any time.
   This may limit throughput for applications with lots of concurrent, high-rate
   publishers operating on the same segment. 

# Interal structure

For allocation shq uses a ring buffer. Why a ring buffer and not a more advanced
allocator? This simplifies dealing with changing message size a lot, which means
that also hereogeneous message types can be exchanged over the same message bus
without any issues.