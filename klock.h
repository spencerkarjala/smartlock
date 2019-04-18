#ifndef __KLOCK_H__
#define __KLOCK_H__

#include <pthread.h>

typedef struct {
	 pthread_mutex_t mutex;
} SmartLock;

void init_lock(SmartLock* lock);
int lock(SmartLock* lock);
void unlock(SmartLock* lock);
void cleanup();

#endif
