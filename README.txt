# SHQ (SHared Queue)

A robust, single-header, N-to-N, message queue for POSIX shared memory.

## Operation Modes

### Reliable

A message is only overwritten if every reader has read the message.

### Unreliable

A message may be overwritten before every reader has read the message.
