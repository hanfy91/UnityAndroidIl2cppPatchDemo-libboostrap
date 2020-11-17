#ifndef FILE_MAPPLE_H
#define FILE_MAPPLE_H

#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <dirent.h>
#include <errno.h>
#include "log.h"

#include <vector>
#include <string>
#include <unordered_map>
#include "profiler.h"

extern char* g_use_data_path;

const uint64_t MY_MAGIC_NUM = 0x1983546019835460;
struct FileExtraData
{
	uint64_t magic_num;
	FileExtraData* self;
	ShadowZip* shadow_zip;
	FILE* file;
	int fd;
};

typedef std::unordered_map<int, FileExtraData*> FDMAP;
typedef std::unordered_map<FILE*, FileExtraData*> FSMAP;
extern FDMAP g_fd_to_file;
extern FSMAP g_fs_to_file;
extern PthreadRwMutex g_fd_to_file_mutex;
extern PthreadRwMutex g_fs_to_file_mutex;

// posix shm is not supported on Android.
// we should use memfd, but it is not supported in older android(platform api < 26) 
// https://developer.android.google.cn/ndk/reference/group/memory.html
// file fd is slow theoratically, but it is ok on a flash disk and I don't notice it.
// no other choice, as the mutex is buggy in fclose with timing irq.

static int delete_old_file_mapping_data()
{
	char fds_dir[512] = {0};
	snprintf(fds_dir, sizeof(fds_dir), "%s/rt_fd_mappings", g_use_data_path);
	MY_INFO("delete_old_file_mapping_data %s with apk_path:%s", TARGET_ARCH_ABI, fds_dir);
	
	//create dir if not exist
	DIR* dir = opendir(fds_dir);
    if (dir == NULL)
    {
		if (errno == ENOENT)
		{
			mkdir(fds_dir, 0700); 
			dir = opendir(fds_dir);
		}
		else
		{
			MY_ERROR("open %s failed! errno:%d", fds_dir, errno);
			return -1;
		}
    }
	
	//delete all files under the dir
    if (dir == NULL){ 
		MY_ERROR("open %s failed! errno:%d", fds_dir, errno);
		return -1; 
	}

    struct dirent *ent = NULL;
	std::vector<std::string> fd_files;
    while((ent = readdir(dir)) != NULL) {  
        if(ent->d_type & DT_REG) {  
			std::string fd_file = std::string(fds_dir) + "/" + ent->d_name;
            fd_files.push_back(fd_file);
        }  
    }  
    closedir(dir);
	
	for(int i = 0; i < fd_files.size(); i++)
	{
		std::string& fd_file = fd_files[i];
		if (0 != unlink(fd_file.c_str()))
		{
			MY_METHOD("delete fd file:[%s], ret:%d", fd_file.c_str(), errno);
			return -1;
		}
		MY_METHOD("delete fd file:[%s]", fd_file.c_str());
	}
	return 0;
}

static FileExtraData* save_file_mapping(ShadowZip* shadow_zip, bool is_fd)
{
	FileExtraData* file_extra_data = new FileExtraData();	
	file_extra_data->magic_num = MY_MAGIC_NUM;
	file_extra_data->self = file_extra_data;
	file_extra_data->shadow_zip = shadow_zip;

	shadow_zip->file_extra_data = file_extra_data;
	
	char save_path[512] = {0};
	snprintf(save_path, sizeof(save_path), "%s/rt_fd_mappings/%08llx", g_use_data_path, (unsigned long long)file_extra_data);
	file_extra_data->file = ::fopen(save_path, "wb+");
	if (file_extra_data->file == NULL)
	{		
		MY_ERROR("open save path:%s failed! errno:%d", save_path, errno);
		_exit(-1); 
	}
	file_extra_data->fd = fileno(file_extra_data->file);
	
	fwrite((void*)file_extra_data, 1, sizeof(FileExtraData), file_extra_data->file);
	fflush(file_extra_data->file);
	fseek(file_extra_data->file, 0, SEEK_SET);

    if (is_fd)
	{
	    PthreadWriteGuard guard(g_fd_to_file_mutex);
	    g_fd_to_file[file_extra_data->fd] = file_extra_data;
	}
	else
	{
	    PthreadWriteGuard guard(g_fs_to_file_mutex);
	    g_fs_to_file[file_extra_data->file] = file_extra_data;
	}

	MY_METHOD("FileExtraData saved to %s. fd:0x%08x, file*: 0x%08llx", save_path, file_extra_data->fd, (unsigned long long)file_extra_data->file);
	return file_extra_data;
}

static FileExtraData* get_file_mapping(int fd)
{
	PthreadReadGuard guard(g_fd_to_file_mutex);
    FDMAP::iterator it = g_fd_to_file.find(fd);
    if (it == g_fd_to_file.end())
    {
        return NULL;
    }

    return it->second != NULL ? it->second->self : NULL;

    /*
	off_t pos = lseek(fd, 0, SEEK_CUR);
	if (pos != 0){return NULL;}
	
	lseek(fd, 0, SEEK_SET);
	FileExtraData file_extra_data_copy;
	int read_cnt = read(fd, (void*)&file_extra_data_copy, sizeof(FileExtraData));
	lseek(fd, 0, SEEK_SET);	
	
	//validate
	if (read_cnt != sizeof(FileExtraData)){ return NULL; }	
	if (file_extra_data_copy.magic_num != MY_MAGIC_NUM) { return NULL; }
	if (file_extra_data_copy.fd != fd) { return NULL; }
	
	return file_extra_data_copy.self;	*/
}

static FileExtraData* get_file_mapping(FILE* file)
{
	PthreadReadGuard guard(g_fs_to_file_mutex);
    FSMAP::iterator it = g_fs_to_file.find(file);
    if (it == g_fs_to_file.end())
    {
        return NULL;
    }

    return it->second != NULL ? it->second->self : NULL;
    /*
	off_t pos = ftell(file);
	if (pos != 0){return NULL;}
	
	fseek(file, 0, SEEK_SET);
	FileExtraData file_extra_data_copy;
	int read_cnt = fread((void*)&file_extra_data_copy, 1, sizeof(FileExtraData), file);
	fseek(file, 0, SEEK_SET);	
	
	//validate
	if (read_cnt != sizeof(FileExtraData)){ return NULL; }	
	if (file_extra_data_copy.magic_num != MY_MAGIC_NUM) { return NULL; }
	if (file_extra_data_copy.file != file) { return NULL; }
	
	return file_extra_data_copy.self;	*/
}

static void clean_mapping_data(FileExtraData* file_extra_data, bool is_fd)
{
	fclose(file_extra_data->file);

	char save_path[512] = {0};
	snprintf(save_path, sizeof(save_path), "%s/rt_fd_mappings/%08llx", g_use_data_path, (unsigned long long)file_extra_data->self);
	unlink(save_path);

	delete file_extra_data->shadow_zip;

    if (is_fd)
	{
	    PthreadWriteGuard guard(g_fd_to_file_mutex);
	    g_fd_to_file[file_extra_data->fd] = NULL;
	}
	else
	{
	    PthreadWriteGuard guard(g_fs_to_file_mutex);
	    g_fs_to_file[file_extra_data->file] = NULL;
	}

	delete file_extra_data->self;
	MY_METHOD("FileExtraData deleted %s. fd:0x%08x, file*: 0x%08llx", save_path, file_extra_data->fd, (unsigned long long)file_extra_data->file);
}

static void release_to_cache(FileExtraData* file_extra_data, bool is_fd)
{
	 PthreadWriteGuard guard(g_shadow_zip_cache_mutex);

     // in case too many files are open, limit the cache num
	 if (g_shadow_zip_cache.size() > 10)
	 {
	    clean_mapping_data(file_extra_data, is_fd);
	 }
	 else
	 {
        g_shadow_zip_cache.push_back(file_extra_data->shadow_zip);
     }
}

#endif
