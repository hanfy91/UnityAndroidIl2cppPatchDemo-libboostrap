#ifndef PTHREADMUTEX_H
#define PTHREADMUTEX_H

//#include <mutex>
////#include <shared_mutex>
//
//typedef std::mutex PthreadMutex;
//typedef std::mutex PthreadRwMutex;
////typedef std::shared_mutex PthreadRwMutex;
//typedef std::lock_guard<std::mutex> PthreadGuard;
//typedef std::lock_guard<std::mutex> PthreadReadGuard;
//typedef std::lock_guard<std::mutex> PthreadWriteGuard;
////typedef std::shared_lock<std::shared_mutex> PthreadReadGuard;
////typedef std::unique_lock<std::shared_mutex> PthreadWriteGuard;


#include <pthread.h>
//#include "log.h"

class PthreadRwMutex
{
public:
    PthreadRwMutex(){pthread_rwlock_init(&mutexlock, 0);}
    virtual ~PthreadRwMutex(){pthread_rwlock_destroy(&mutexlock);}

    void ReadLock(){
		pthread_rwlock_rdlock(&mutexlock);
		//MY_METHOD("lock at mutex: 0x%08llx", (unsigned long long)&mutexlock);
	}
    void WriteLock(){
		pthread_rwlock_wrlock(&mutexlock);
		//MY_METHOD("lock at mutex: 0x%08llx", (unsigned long long)&mutexlock);
	}

    void Unlock(){	
		//MY_METHOD("unlock at mutex: 0x%08llx", (unsigned long long)&mutexlock);
		pthread_rwlock_unlock(&mutexlock);
	}
private:
    pthread_rwlock_t mutexlock;
};

class PthreadReadGuard
{
public:
	PthreadReadGuard(PthreadRwMutex& m){mutex = &m; mutex->ReadLock();}
    virtual ~PthreadReadGuard(){mutex->Unlock();}
	
private:
	PthreadRwMutex* mutex;
};

class PthreadWriteGuard
{
public:
	PthreadWriteGuard(PthreadRwMutex& m){mutex = &m; mutex->WriteLock();}
    virtual ~PthreadWriteGuard(){mutex->Unlock();}

private:
	PthreadRwMutex* mutex;
};


#endif /* PTHREADMUTEX_H */

