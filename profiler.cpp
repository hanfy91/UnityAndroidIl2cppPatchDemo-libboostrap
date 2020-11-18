//
// Created by lizhen on 2020/11/14.
//

#include <unistd.h>
#include "profiler.h"
#include "log.h"
#include "zip/shadow_zip.h"

PROFILER_MAP g_profiler_data;
PthreadRwMutex g_profiler_mutex;
int g_total_file_handlers;

void dump_profiler()
{
	PthreadReadGuard guard(g_profiler_mutex);
    MY_INFO("==================Profiler Dump================");
    for(PROFILER_MAP::iterator it=g_profiler_data.begin(); it != g_profiler_data.end(); ++it)
    {
        const char* name = it->first;
        ProfilerData* data = it->second;
        int totalMilliSec = (int)(data->totalNanoSec / 1000000);
        MY_INFO("Profiler [%s] count:[%d], total_time:[%d ms]", name, data->count, totalMilliSec);
    }
    MY_INFO("Profiler total files open:[%d], total ShadowZip cache count:[%d]", g_total_file_handlers, g_shadow_zip_cache.size());
 }

void *profiler_thread_proc(void *arg)
{
    while(true)
    {
        sleep(3);
        dump_profiler();
    }
}

void create_monitor_thread()
{
    pthread_t threadid;
    pthread_create(&threadid, NULL, &profiler_thread_proc, NULL);
    MY_INFO("create_monitor_thread");
}

void msleep(long msec)
{
    struct timespec ts;
    ts.tv_sec = msec / 1000;
    ts.tv_nsec = (msec % 1000) * 1000000;
    nanosleep(&ts, NULL);
}