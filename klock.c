#include "klock.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <assert.h>
#include <semaphore.h>

enum {
	false,
	true
};

/*
 *	defines a process node in the RAG; it has:
 * 		request:   current request edge
 *		next:      next thread in the thread list
 *		tid:			 associated thread ID
 *		travelled: 1 if seen during BFS; else 0
 */
typedef struct thread_t {
	struct resource_t* request;
	struct thread_t* next;
	int tid;
	_Bool travelled;
} thread_t;

/*
 *	defines a resource node in the RAG; it has:
 *		assignment:  current assignment edge
 *		next:				 next resource in the resource list
 *		lock:				 address of associated lock
 *		travelled:	 1 if seen during BFS; else 0
 */
typedef struct resource_t {
	struct thread_t* assignment;
	struct resource_t* next;
	SmartLock* lock;
	_Bool travelled;
} resource_t;

/*
 *	these components define a resource allocation graph
 *		threads: list of process nodes in the RAG
 *		resources: list of resouce nodes in the RAG
 */
thread_t* threads = NULL;
resource_t* resources = NULL;

_Bool firstRun = true;
sem_t assign_mutex;
sem_t assign_mutexRw;
int   assign_readers = 0;

struct resource_t* rag_createResource();
struct thread_t* rag_createThread();
void rag_addResource();
void rag_addThread();
resource_t* rag_getResource(SmartLock* lock);
thread_t* rag_getThread(int tid);
_Bool rag_isAssigned();
void rag_setRequest(int tid, SmartLock* lock);
void rag_setAssignment(int tid, SmartLock* lock);
void rag_removeRequest(int tid);
void rag_removeAssignment(SmartLock* lock);
void rag_readerWait();
void rag_readerSignal();
void rag_writerWait();
void rag_writerSignal();
_Bool rag_isNewThread();
_Bool rag_checkForCycles();
_Bool rag_depthFirstSearch(thread_t* currentNode);

//initializes a SmartLock object with default values
void init_lock(SmartLock* lock) {

	//if being run for first time, create needed semaphores
	if (firstRun) {
		firstRun = false;
		sem_init(&assign_mutex,   0, 1);
		sem_init(&assign_mutexRw, 0, 1);
	}

	//initialize lock and add it to RAG
	pthread_mutex_init(&(lock->mutex), NULL);
	rag_addResource(lock);
}

//performs a mutually exclusive lock on a SmartLock
int lock(SmartLock* lock) {

	//get the calling thread's id
	int tid = pthread_self();

	//if the thread is new, add it to the threads list
	if (rag_isNewThread(tid)) {
		rag_addThread(tid);
	}

	//since the lock isn't already given, set a request edge
	rag_setRequest(tid, lock);

	//if the lock creates no cycles, give it to the thread
	if (!rag_checkForCycles(tid)) {

		printf("%lu locking\n", pthread_self());
		rag_setAssignment(tid, lock);
		pthread_mutex_lock(&(lock->mutex));
	}
	//if a cycle is detected, reject the thread
	else {
		rag_removeRequest(tid);
		return 0;
	}
	//remove the request edge now that assignment is created
	rag_removeRequest(tid);
	return 1;
}

//unlocks a given SmartLock object
void unlock(SmartLock* lock) {

	//remove the assignment edge associating the lock with a thread
	rag_removeAssignment(lock);
	pthread_mutex_unlock(&(lock->mutex));
}

/*
 * Cleanup any dynamic allocated memory for SmartLock to avoid memory leak
 * You can assume that cleanup will always be the last function call
 * in main function of the test cases.
 */
void cleanup() {

	//iterate through and release each resource object
	struct resource_t* temp_res = resources;
	for (struct resource_t* curr = resources; temp_res != NULL; curr = temp_res) {
		temp_res = curr->next;
		free(curr);
	}

	//iterate through and release each thread object
	struct thread_t* temp_thr = threads;
	for (struct thread_t* curr = threads; temp_thr != NULL; curr = temp_thr) {
		temp_thr = curr->next;
		free(curr);
	}
}

//creates a new resource node in a RAG with default parameters
struct resource_t* rag_createResource() {
	struct resource_t* newResource = malloc(sizeof(resource_t));
	newResource->assignment = NULL;
	newResource->lock = NULL;
	newResource->next = NULL;
	newResource->travelled = false;
	return newResource;
}

//creates a new process node in a RAG with default parameters
struct thread_t* rag_createThread() {
	struct thread_t* newThread = malloc(sizeof(thread_t));
	newThread->request = NULL;
	newThread->tid = 0;
	newThread->next = NULL;
	newThread->travelled = false;
	return newThread;
}

//adds a new resource to the resource list in the RAG
void rag_addResource(SmartLock* lock) {
	struct resource_t* newResource = rag_createResource();

	rag_writerWait();

	if (resources != NULL) {
		struct resource_t* curr = resources;
		while (curr->next != NULL) {
			curr = curr->next;
		}
		curr->next = newResource;
	} else {
		resources = newResource;
	}

	newResource->lock = lock;

	rag_writerSignal();
	return;
}

//adds a new thread to the thread list in the RAG
void rag_addThread(int tid) {
	struct thread_t* newThread = rag_createThread();

	rag_writerWait();

	if (threads != NULL) {
		struct thread_t* curr = threads;
		while (curr->next != NULL) {
			curr = curr->next;
		}
		curr->next = newThread;
	} else {
		threads = newThread;
	}

	newThread->tid = tid;

	rag_writerSignal();
	return;
}

//retrieves a resource from the resource list in the RAG
resource_t* rag_getResource(SmartLock* lock) {
	struct resource_t* curr = resources;
	while (curr != NULL) {
		if (curr->lock == lock) {
			return curr;
		}
		curr = curr->next;
	}
	return curr;
}

//retrieves a thread from the thread list in the RAG
thread_t* rag_getThread(int tid) {
	struct thread_t* curr = threads;
	while (curr != NULL) {
		if (curr->tid == tid) {
			return curr;
		}
		curr = curr->next;
	}
	return curr;
}

//returns 1 if the lock has been assigned to a thread; else, 0
_Bool rag_isAssigned(SmartLock* lock) {

	_Bool assignmentExists = false;

	rag_readerWait();

	resource_t* resourceToCheck = rag_getResource(lock);
	assignmentExists = resourceToCheck->assignment != NULL;

	rag_readerSignal();

	return assignmentExists;
}

//sets a request edge from 'tid' to 'lock'
void rag_setRequest(int tid, SmartLock* lock) {

	rag_readerWait();

	thread_t* threadToSet = rag_getThread(tid);
	resource_t* resourceToSet = rag_getResource(lock);

	rag_readerSignal();
	rag_writerWait();

	threadToSet->request = resourceToSet;

	rag_writerSignal();
	return;
}

//sets an assignment edge from 'lock' to 'edge'
void rag_setAssignment(int tid, SmartLock* lock) {

	rag_readerWait();

	resource_t* resourceToSet = rag_getResource(lock);
	thread_t* threadToSet = rag_getThread(tid);

	rag_readerSignal();
	rag_writerWait();

	resourceToSet->assignment = threadToSet;

	rag_writerSignal();
	return;
}

//removes any request edge associated with 'tid'
void rag_removeRequest(int tid) {

	rag_readerWait();

	thread_t* threadToRemove = rag_getThread(tid);

	rag_readerSignal();
	rag_writerWait();

	threadToRemove->request = NULL;

	rag_writerSignal();
	return;
}

//removes any assignment edge associated with 'lock'
void rag_removeAssignment(SmartLock* lock) {

	rag_readerWait();

	resource_t* resourceToRemove = rag_getResource(lock);

	rag_readerSignal();
	rag_writerWait();

	resourceToRemove->assignment = NULL;

	rag_writerSignal();
	return;
}

//performs semaphore waiting for a read operation
void rag_readerWait() {

	sem_wait(&assign_mutex);
	assign_readers++;
	if (assign_readers == 1) {
		sem_wait(&assign_mutexRw);
	}
	sem_post(&assign_mutex);
	return;
}

//performs semaphore signaling for a read operation
void rag_readerSignal() {

	sem_wait(&assign_mutex);
	assign_readers--;
	if (assign_readers == 0) {
		sem_post(&assign_mutexRw);
	}
	sem_post(&assign_mutex);
	return;
}

//performs semaphore waiting for a write operation
void rag_writerWait() {
	sem_wait(&assign_mutex);
	return;
}

//performs semaphore signaling for a write operation
void rag_writerSignal() {
	sem_post(&assign_mutex);
	return;
}

//checks if the given thread 'tid' has an associated thread object
_Bool rag_isNewThread(int tid) {

	rag_readerWait();

	struct thread_t* curr = threads;
	while (curr != NULL) {
		if (curr->tid == tid) {
			rag_readerSignal();
			return false;
		}
		curr = curr->next;
	}

	rag_readerSignal();
	return true;
}

//checks the graph for any cycles to prevent deadlocks
_Bool rag_checkForCycles(int tid) {

	rag_readerWait();

	thread_t* threadToSearch = rag_getThread(tid);
	_Bool isCycle = rag_depthFirstSearch(threadToSearch);

	for (resource_t* curr = resources; curr != NULL; curr = curr->next) {
		curr->travelled = false;
	}
	for (thread_t* curr = threads; curr != NULL; curr = curr->next) {
		curr->travelled = false;
	}

	rag_readerSignal();
	return isCycle;
}

//performs DFS traversal recursively on the RAG, returning 1 if a cycle is found; else 0.
_Bool rag_depthFirstSearch(thread_t* currentNode) {

	if (currentNode == NULL) {
		return false;
	}
	if (currentNode->travelled == true) {
		return true;
	}
	currentNode->travelled = true;

	resource_t* tempResource = currentNode->request;
	if (tempResource == NULL) {
		return false;
	}
	if (tempResource->travelled == true) {
		return true;
	}
	tempResource->travelled = true;

	thread_t* nextThread = tempResource->assignment;
	return rag_depthFirstSearch(nextThread);
}