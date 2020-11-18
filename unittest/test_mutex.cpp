#include "pthread_mutex.hpp"
#include "profiler.h"
#include "log.h"

PthreadRwMutex g_test_mutex;
#define g_global_data (LeakSingleton<GlobalData, 0>::instance())

int count;
const size_t THREAD_NUM = 6; 

//-----------------------------------------------------------------------
void *thread_proc(void *arg)
{
    for(int i = 0; i < 500; i++)
    {
		PthreadWriteGuard guard(g_test_mutex);
		int old_count = count;
	    count = count + 1;
		msleep(5);
		if (old_count != count -1)
		{
		    MY_ERROR("pthread unit testing failed!]");
		}
	}

    pthread_exit(0);
    return 0;
}

int main()
{
	MY_INFO("pthread unit testing start...");
    pthread_t threadids[THREAD_NUM];
    for(size_t i = 0; i < THREAD_NUM; i++)
    {
        int err = pthread_create(&(threadids[i]), NULL, &thread_proc, (void *)(intptr_t)i);
    }

    for(size_t i = 0; i < THREAD_NUM; i++)
    {
        void *status = NULL;
        int rc = pthread_join(threadids[i], &status);
    }

	MY_INFO("pthread unit testing end...");
    return 0;
}