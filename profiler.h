//
// Created by lizhen on 2020/11/14.
//

#ifndef UNITYANDROIDIL2CPPPATCHDEMO_LIBBOOSTRAP_PROFILER_H
#define UNITYANDROIDIL2CPPPATCHDEMO_LIBBOOSTRAP_PROFILER_H

#include <unordered_map>
#include <sys/time.h>
#include "pthread_mutex.hpp"

#if 0
#define PROFILER_ENABLE
#define PROFILER_TIMER(name) ProfilerTimer timer(name);
#else
#define PROFILER_TIMER(name)
#endif

struct ProfilerData
{
    int count;
    long long totalNanoSec;
};
typedef std::unordered_map<const char*, ProfilerData*> PROFILER_MAP;
extern PROFILER_MAP g_profiler_data;
extern PthreadRwMutex g_profiler_mutex;
extern int g_total_file_handlers;

class ProfilerTimer
{
public:
	ProfilerTimer(const char* _name)
	{
	    name = _name;
	    clock_gettime(CLOCK_REALTIME, &time1);

	    PthreadWriteGuard guard(g_profiler_mutex);

        PROFILER_MAP::iterator it = g_profiler_data.find(name);
	    if (it == g_profiler_data.end())
	    {
	        data = new ProfilerData;
	        memset(data, 0, sizeof(ProfilerData));
	        g_profiler_data[name] = data;
	    }
	    else
	    {
	        data = it->second;
	    }
	}

    virtual ~ProfilerTimer()
    {
        timespec time2;
	    clock_gettime(CLOCK_REALTIME, &time2);

	    data->totalNanoSec += (time2.tv_sec - time1.tv_sec)*1000000000L + (time2.tv_nsec - time1.tv_nsec);
	    data->count++;
    }

private:
	const char* name;
	ProfilerData* data;
	timespec time1;
};

void create_monitor_thread();

#endif //UNITYANDROIDIL2CPPPATCHDEMO_LIBBOOSTRAP_PROFILER_H
