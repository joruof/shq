# SHQ (SHared Queue)

A robust, single-header, N-to-N, message queue for POSIX shared memory.

# Guarantees

SHQ guarantees the following: 

1. All messages are received in the same order they were published.

2. Any number of subscribers or publisher can access the shared memory concurrently without corruption.

3. Any subscriber or publisher can die at any moment without corrupting the shared memory or stalling other publishers or subscribers.

4. While a message is being read by at least on subscriber it will not be deallocated.

5. While a message is being read by at least on subscriber it will not be modified by a publisher.

(unless bugs, obviously)

SHQ does explicity NOT guarantee that:

1. A messages is read by any subscriber before being overwritten.
   If a subscriber is to slow the message will be overwritten, which is the desired behavior for many realtime applications.

# Interal structure

For allocation SHQ uses a ring buffer. Why a ring buffer and not a more advanced allocator?
This simplifies dealing with changing message size a lot, which means that also hereogeneous
message can be exchanged over the same message bus without any issues.

## Operation Modes

### Reliable

A message is only overwritten if every reader has read the message.


### Unreliable

A message may be overwritten before every reader has read the message.

Reader:

1.)

creates a new message object
in message object constructor do

    lock_ring_buffer_mutex()

    this.end = max(this.last_chunk.seq, writers.oldest_seq_number)

    while (this.end >= ring_buffer.end) {
        // this means there is no message available
        unlock_ring_buffer_mutex()
        wait on futex of ring_buffer.end
        lock_ring_buffer_mutex()
    }

    // this means there is an unread message at this.end

    set "reading" flag on message header
    ptr = pointer to message data segment 
    increment this.end by message size

    unlock_ring_buffer_mutex()

    return ptr

2.)

then do something with the message object

3.)

on deconstruction of message object

    clear "reading" flag of shared memory chunk


Writer:

creates a new message object
in message object constructor do

    lock_ring_buffer_mutex()

    if the next chunk is being read
        wait until all readers have unlocked




