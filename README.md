# smartlock
A simple C library used to manage asynchronous accesses to shared data created for an operating systems course. Like mutexes, semaphores, and monitors, this `SmartLock` library ensures data coherency through multiple acceses by different processes or threads. However, `SmartLock` also ensures that deadlocks will not occur by preventing circular waiting.

## Technology
* Uses semaphores to maintain mutual exclusion for readers and writers
* Maintains a resource-allocation graph (RAG) to prevent circular waiting
* Nodes in RAG assigned to threads by thread ID

## How to Use
To launch the built-in test program, navigate to the Makefile directory and run the commands:

```
make
./locking
```
