#include "il2cpp.h"
#include <fstream>
#include <sstream>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <fcntl.h>
#include "log.h"
#include "zip/shadow_zip.h"
#include "xhook/xhook.h"
#include "io_github_noodle1983_Boostrap.h"
#include "mymap32.h"
#include "file_mapping.h"
#include "serial_utils.h"
#include "profiler.h"

static inline char * dupstr(const char* const str)
{
	if (str == NULL){ return NULL; }
	
	int len = strlen(str) + 1;
	char* ret = new char[len];
	memset(ret, 0, len);
	strncpy(ret, str, len);
	return ret;
}

const char* SPLITER = ";";
const char* g_data_file_path = NULL;
const char* g_apk_file_path = NULL;
const char* g_bundle_id = NULL;

static bool g_has_il2cpp_patch = false;
static bool g_has_assets_patch = false;

static bool bootstrap();
std::string get_apk_path(const std::string& bundle_id);

FDMAP g_fd_to_file;
FSMAP g_fs_to_file;
PthreadRwMutex g_fd_to_file_mutex;
PthreadRwMutex g_fs_to_file_mutex;

__attribute__ ((visibility ("default")))
JNIEXPORT void JNICALL Java_io_github_noodle1983_Boostrap_init
  (JNIEnv * jenv, jclass cls, jstring path)
{
	const char* data_file_path = jenv->GetStringUTFChars(path, NULL); 
	g_data_file_path = dupstr(data_file_path); // never delete, ok with only one
	jenv->ReleaseStringUTFChars(path, data_file_path);
	MY_INFO("data file path:%s", g_data_file_path);
}

__attribute__ ((visibility ("default")))
JNIEXPORT bool JNICALL Java_io_github_noodle1983_Boostrap_boostrap
  (JNIEnv * jenv, jclass cls)
{
	return bootstrap();
}

__attribute__ ((visibility ("default")))
JNIEXPORT void JNICALL Java_io_github_noodle1983_Boostrap_setpath
  (JNIEnv * jenv, jclass cls, jstring dataPath, jstring path, jstring apkPath)
{
	const char* target_file_path = jenv->GetStringUTFChars(path, NULL); 
	const char* apk_path = jenv->GetStringUTFChars(apkPath, NULL);
	const char* data_file_path = jenv->GetStringUTFChars(dataPath, NULL); 
	g_data_file_path = dupstr(data_file_path); // never delete, ok with only one

	use_data_dir(target_file_path, apk_path);

	MY_INFO("set_path jieluo:%s", target_file_path);

	jenv->ReleaseStringUTFChars(dataPath, data_file_path);
	jenv->ReleaseStringUTFChars(path, target_file_path);
	jenv->ReleaseStringUTFChars(apkPath, apk_path);
}

static std::string get_bundle_id()
{
	pid_t pid = getpid();
	char bundle_id_filename[256] = {0};
	snprintf(bundle_id_filename, sizeof(bundle_id_filename), "/proc/%d/cmdline", pid);
	
	std::fstream bundle_id_file(bundle_id_filename, std::fstream::in);
	if (!bundle_id_file.is_open())
	{
		MY_ERROR("failed to open %s ", bundle_id_filename);
		exit(-1);
		return "";
	}
	
	char bundle_id[256] = {0};
	bundle_id_file.getline(bundle_id, sizeof(bundle_id));
	bundle_id_file.close();
	return std::string(bundle_id);
}

static bool copy_file(const char* const from_path, const char* const to_path)
{
	MY_LOG("copy %s to %s", from_path, to_path);
	
	int fd_to, fd_from;
    char buf[4096];
    ssize_t nread;
    int saved_errno;

    fd_from = open(from_path, O_RDONLY);
    if (fd_from < 0){
		MY_ERROR("can't access to %s.", from_path);
        return false;
	}

    fd_to = open(to_path, O_WRONLY | O_CREAT | O_EXCL, 0755);
    if (fd_to < 0){		
		MY_ERROR("can't access to %s.", to_path);
		close( fd_from );
        return false;
	}

    while ((nread = read(fd_from, buf, sizeof buf))> 0) {
        char *out_ptr = buf;
        ssize_t nwritten;
        do {
            nwritten = write(fd_to, out_ptr, nread);
            if (nwritten >= 0) {
                nread -= nwritten;
                out_ptr += nwritten;
            }
            else if (errno != EINTR) {	
				MY_ERROR("can't write to %s. errno:%d", to_path, errno);
				close( fd_from );
				close( fd_to );
                return false;
            }
        } while (nread > 0);
    }
	close( fd_from );
	close( fd_to );

	return true;
}

static void clear_file(const char* const file_path)
{
    int fd = ::open(file_path, O_RDWR);
    if(fd < 0)
    {
        return;
    }
    else
    {
        int size = (int)lseek(fd, 0, SEEK_END);
		MY_INFO("clearing file:[%s], size:[%d]", file_path, size);

        ::ftruncate(fd, 0);
        ::close(fd);
    }
}
static bool prepare_so_lib(const char* const data_path, const std::string& bundle_id)
{
	DIR* dir = opendir(data_path);
	if (dir == NULL)
	{
		MY_ERROR("can't access data path:%s", data_path);
		return false;		
	}

    const char* NEW_SO_POSTFIX = ".new";
    int new_file_postfix_len = strlen(NEW_SO_POSTFIX);
	
	struct dirent *ent = NULL;
	while((ent = readdir(dir)) != NULL)
    {
        if(ent->d_type & DT_DIR) { continue; }
		
        const char* filename = ent->d_name;
		int len = strlen(filename);
		
        if (len > new_file_postfix_len && memcmp(filename + len - new_file_postfix_len, NEW_SO_POSTFIX, new_file_postfix_len) == 0)
		{
            // copy patch_dir/xxx.new -> data/files/xxx.new
            char from_filepath[256] = {0};
            snprintf(from_filepath, sizeof(from_filepath), "%s/%s", data_path, filename);
            char to_filepath[256] = {0};
            snprintf(to_filepath, sizeof(to_filepath), "%s/%s", g_data_file_path, filename);
            copy_file(from_filepath, to_filepath);

            // rename patch_dir/xxx.new -> patch_dir/xxx
            char new_filepath[256] = {0};
            strncpy(new_filepath, from_filepath, sizeof(new_filepath));
            new_filepath[strlen(from_filepath) - new_file_postfix_len] = '\0';
			unlink(new_filepath);
			rename(from_filepath, new_filepath);

			// clear the file content to save disk space. The real .so is already copied to data/files/, this one here is just for keeping a record,
			clear_file(new_filepath);
			continue;
		}
    }
	closedir(dir);
	return true;
	
}

__attribute__ ((visibility ("default")))
char* get_arch_abi()
{
	return dupstr(TARGET_ARCH_ABI);
}

char* g_use_data_path = NULL;
__attribute__ ((visibility ("default")))
char* use_data_dir(const char* data_path, const char* useless)
{	
	char formal_patch_info_path[256] = {0};
	snprintf(formal_patch_info_path, sizeof(formal_patch_info_path), "%s/user.db",  g_data_file_path);
	if (strlen(data_path) == 0){
		unlink(formal_patch_info_path);
		return NULL;
	}
	
	if (strlen(data_path) > 0){
		DIR* dir = opendir(data_path);
		if (dir == NULL)
		{
			MY_ERROR("can't access data path:%s", data_path);
			return strdup("can't access data dir!");		
		}
		closedir(dir);
	}
	
	std::string bundle_id = get_bundle_id();

    //1. prepare so lib
    prepare_so_lib(data_path, bundle_id);

    //2. pre-find apk path
	std::string apk_path_string = get_apk_path(bundle_id);

    //3. pre-calc partition
    ShadowZipGlobalData global_data;
    global_data.end_of_file_ = 0;
    if(0 != ShadowZip::init(data_path, apk_path_string.c_str(), &global_data)){
		char error_str[256] = {0};
		snprintf(error_str, sizeof(error_str), "failed to do ShadowZip::init(%s, %s)", data_path, apk_path_string.c_str());
		MY_INFO("%s", error_str);
		// Can't find any patch files in assets/bin/Data, and will try to load il2cpp only, do not return.
		//return dupstr(error_str);
    }

	char patch_info_path[256] = {0};
	snprintf(patch_info_path, sizeof(patch_info_path), "%s/user.db.tmp",  g_data_file_path);
	
    FILE* fw = ::fopen(patch_info_path, "wb");
	if (fw == NULL) 
	{
		char error_str[256] = {0};
		snprintf(error_str, sizeof(error_str), "can't open:%s, errno:%d", patch_info_path, errno);
		MY_ERROR("can't open:%s, errno:%d", patch_info_path, errno);
		return dupstr(error_str);
	}
	serial_uint32(fw, 5460);
    serial_string(fw, data_path);
    serial_string(fw, apk_path_string);
    serial_string(fw, bundle_id);
    serial_string_vector(fw, global_data.all_files_);
    serial_partition_vector(fw, global_data.patch_partitions_);
    serial_uint64(fw, global_data.end_of_file_);
    ::fclose(fw);
	unlink(formal_patch_info_path);
	rename(patch_info_path, formal_patch_info_path);

	MY_INFO("use_data_dir finished! apk_path_string=%s", apk_path_string.c_str());
	return NULL;
}

static bool pre_process_all_so_lib(const std::string& bundle_id)
{
	DIR* dir = opendir(g_data_file_path);
	if (dir == NULL)
	{
		MY_ERROR("can't access data path:%s", g_data_file_path);
		return false;		
	}

    const char* NEW_SO_POSTFIX = ".new";
    int new_file_postfix_len = strlen(NEW_SO_POSTFIX);
	
	char filepath_buffer[256] = {0};
	struct dirent *ent = NULL;
	while((ent = readdir(dir)) != NULL)
    {
        if(ent->d_type & DT_DIR) { continue; }
		
        const char* filename = ent->d_name;
		int len = strlen(filename);
		snprintf(filepath_buffer, sizeof(filepath_buffer), "%s/%s", g_data_file_path, filename);
		
        if (len > new_file_postfix_len && memcmp(filename + len - new_file_postfix_len, NEW_SO_POSTFIX, new_file_postfix_len) == 0)
		{
			std::string so_name = std::string(filename).substr(0, len - new_file_postfix_len);
			std::string new_filepath = filepath_buffer;
			std::string old_filepath = new_filepath.substr( 0, new_filepath.length() - new_file_postfix_len );
			unlink(old_filepath.c_str());
			rename(new_filepath.c_str(), old_filepath.c_str());
			chmod(old_filepath.c_str(), 0755);
			MY_LOG("rename %s to %s", new_filepath.c_str(), old_filepath.c_str());
			continue;
		}
    }
	closedir(dir);
	return true;
}

bool has_any_assets_patch(const char* _patch_dir)
{
    char apk_patch_path[512] = {0};
    snprintf( apk_patch_path, sizeof(apk_patch_path), "%s/assets_bin_Data", _patch_dir );

    DIR* dir = opendir(apk_patch_path);
    if (dir == NULL){
		MY_INFO("opendir failed:%d[%s]", errno, apk_patch_path);
		return false;
	}

	bool has_any = false;
    struct dirent *ent = NULL;
    while((ent = readdir(dir)) != NULL) {
        if(ent->d_type & DT_REG) {
			has_any = true;
			break;
        }
    }
    closedir(dir);

    return has_any;
}

bool has_any_il2cpp_patch(const char* _patch_dir)
{
	char il2cpp_path[256] = {0};
	snprintf(il2cpp_path, sizeof(il2cpp_path), "%s/libil2cpp.so", _patch_dir);
	FILE *fi = fopen (il2cpp_path, "r");
	if (fi == NULL)
	{
		MY_INFO("libil2cpp.so can't find:%d[%s]", errno, il2cpp_path);
	    return false;
	}
	else
	{
	    fclose (fi);
	    return true;
	}
}

static dev_t g_apk_device_id = -1;
static ino_t g_apk_ino = -1;
static bool extract_patch_info(std::string& default_path, std::string& patch_path)
{
	char default_il2cpp_path[256] = {0};
	snprintf(default_il2cpp_path, sizeof(default_il2cpp_path), "%s/../lib/libil2cpp.so",  g_data_file_path);
	default_path = std::string(default_il2cpp_path);
	patch_path.clear();

	char patch_info_path[256] = {0};
	snprintf(patch_info_path, sizeof(patch_info_path), "%s/user.db",  g_data_file_path);
	
	//get patch date
	struct stat user_stat;
	memset(&user_stat, 0, sizeof(struct stat));
	if (stat(patch_info_path, &user_stat) != 0) {
		MY_LOG("can't read file:%d[%s]", errno, patch_info_path);
		return false;
	}
	
    FILE* fp = ::fopen(patch_info_path, "rb");
	if (fp == NULL) 
	{
		MY_ERROR("can't open:%s, errno:%d", patch_info_path, errno);
		return false;
	}
	
    uint32_t magic = unserial_uint32(fp);
	if (magic != 5460){	
		MY_ERROR("wrong user.db format");
		return false;
	}
    std::string data_path_string = unserial_string(fp);
	MY_INFO("cached data path:%s", data_path_string.c_str());
	
    std::string apk_path_string = unserial_string(fp);
	MY_INFO("cached apk_path_string:%s", apk_path_string.c_str());
	
    std::string bundle_id = unserial_string(fp);
	MY_INFO("cached bundle_id:%s", bundle_id.c_str());
	
    LeakSingleton<ShadowZipGlobalData, 0>::init();
    unserial_string_vector(fp, g_shadowzip_global_data->all_files_);
    unserial_partition_vector(fp, g_shadowzip_global_data->patch_partitions_);
    g_shadowzip_global_data->end_of_file_ = unserial_uint64(fp);
    ::fclose(fp);
	ShadowZip::log(g_shadowzip_global_data);

	const char* data_path = data_path_string.c_str();
	DIR* dir = opendir(data_path);
	if (dir == NULL)
	{
		MY_ERROR("can't access data path:%s", data_path);
		return false;		
	}
	closedir(dir);
	
	//get and save apk file id
	const char* apk_path = apk_path_string.c_str();
	struct stat apk_stat;
	memset(&apk_stat, 0, sizeof(struct stat));
	if (stat(apk_path, &apk_stat) != 0) {
		MY_ERROR("can't read file:%d[%s]", errno, apk_path);
		return false;
	}
	g_apk_device_id = apk_stat.st_dev;
	g_apk_ino = apk_stat.st_ino;
		
	//if we have newer apk file, then no need to load patch
	if (apk_stat.st_mtime > user_stat.st_mtime){	
		MY_ERROR("newer apk file:%s, no need to patch", apk_path);
		unlink(patch_info_path);
		return false;
	}
	
	if (!pre_process_all_so_lib(bundle_id))
	{
		MY_ERROR("can't pre_process_all_so_lib");
		return false;
	}

    // There're two .so files, one at internal storage, the other at patch directory.
    // .so must be at internal storage path to be loaded.
    // We check both of them!
	char link_file[256] = {0};
	snprintf(link_file, sizeof(link_file), "%s/libil2cpp.so", g_data_file_path);
	std::string il2cpp_path(link_file);	
	FILE *fi = fopen (il2cpp_path.c_str(), "r");
	if (fi == NULL) 
	{
		MY_ERROR("can't libil2cpp:%s", il2cpp_path.c_str());
		g_has_il2cpp_patch = false;
	}
	else
	{
	    fseek(fi, 0, SEEK_END);
	    int so_size = ftell(fi);
		MY_INFO("find libil2cpp.so:[%s], size:[%d]", il2cpp_path.c_str(), so_size);
	    fclose (fi);

	    // also check if there's is a libil2cpp.so file at patch dir
		g_has_il2cpp_patch = has_any_il2cpp_patch(data_path);
	}

	g_has_assets_patch = has_any_assets_patch(data_path);

	if (g_use_data_path != NULL){delete[] g_use_data_path;}
	g_use_data_path = dupstr(data_path);
	if (g_apk_file_path != NULL){delete[] g_apk_file_path;}
	g_apk_file_path = dupstr(apk_path);
    if (g_bundle_id != NULL){delete[] g_bundle_id;}
    g_bundle_id = dupstr(bundle_id.c_str());
	patch_path = il2cpp_path;
	return g_has_il2cpp_patch || g_has_assets_patch;
}

static void* get_module_base( const char* module_name )
{
	FILE *fp;
	size_t addr = 0;
	char *pch;
	char filename[32];
	char line[1024];
	char* store_ptr = NULL;
	
	fp = fopen( "/proc/self/maps", "r" );
	if ( fp != NULL ){
		while ( fgets( line, sizeof(line), fp ) ){
			if ( strstr( line, module_name ) ){
				pch = strtok_r( line, "-", &store_ptr);
				addr = strtoull( pch, NULL, 16 );
				break;
			}
		}
		fclose( fp ) ;
	}
	return (void *)addr;
}

static void* get_module_base( char* module_buffer, int len )
{
	FILE *fp;
	size_t addr = 0;
	char *pch;
	char line[1024];
	char* matched_pos = NULL;
	char* store_ptr = NULL;
	
	fp = fopen( "/proc/self/maps", "r" );
	if ( fp != NULL ){
		while ( fgets( line, sizeof(line), fp ) ){
			if ((matched_pos = strstr( line, module_buffer )) != NULL){
				pch = strtok_r( line, "-", &store_ptr );
				addr = strtoull( pch, NULL, 16 );
				
				char* fullpath = matched_pos;
				while(fullpath > line && (*fullpath) != ' ' && (*fullpath) != '\t'){ fullpath--; }
				strncpy(module_buffer, fullpath + 1, len);
				for(int i = 0; i < len; i++){
					if(module_buffer[i] == ' ' || module_buffer[i] == '\r'|| module_buffer[i] == '\n'|| module_buffer[i] == '\t' || module_buffer[i] == '\0') {
						module_buffer[i] = 0;
						break;
					}
				}				
				break;
			}
		}
		fclose( fp ) ;
	}
	return (void *)addr;
}

static void dump_maps( )
{
	FILE *fp;
	size_t addr = 0;
	char *pch;
	char filename[32];
	char line[1024];
	
	fp = fopen( "/proc/self/maps", "r" );
	if ( fp != NULL ){
		while ( fgets( line, sizeof(line), fp ) ){
			MY_ERROR("%s", line);
		}
		fclose( fp ) ;
	}
}

std::string get_apk_path(const std::string& bundle_id)
{
	FILE *fp;
	char *found_path = NULL;
	char filename[32];
	char line[1024];
	const char* TAIL = ".apk";
	int TAIL_LEN = strlen(TAIL);
	std::string ret;
	
	fp = fopen( "/proc/self/maps", "r" );
	if ( fp != NULL ){
		int min_len = INT_MAX;
		while ( fgets( line, sizeof(line), fp ) ){	
			int line_len = strlen(line);
			while(line[line_len - 1] == ' ' || line[line_len - 1] == '\r' || line[line_len - 1] == '\n' || line[line_len - 1] == '\t') {line[line_len - 1] = '\0'; line_len--;}
			if ( strstr( line, bundle_id.c_str()) && (memcmp(line + strlen(line) - TAIL_LEN, TAIL, TAIL_LEN) == 0) ){
				found_path = strchr( line, '/' );
				if (ShadowZip::contains_path(found_path, "assets/bin/Data/"))
				{
					if(min_len > line_len)
					{
						ret = std::string(found_path);
						min_len = line_len;
					}
				}
			}
		}
		fclose( fp ) ;
	}

	MY_INFO("get_apk_path:%s", ret.c_str());
	return ret;
}

typedef int (* StatType)(const char *path, struct stat *file_stat);
static int my_stat(const char *path, struct stat *file_stat)
{	
	memset(file_stat, 0, sizeof(struct stat));
	int ret = stat(path, file_stat);
	if (ret != 0) {
		return ret;
	}
	
	if (g_apk_device_id == file_stat->st_dev && g_apk_ino == file_stat->st_ino)
	{
		uint64_t size = ShadowZip::get_eof_pos();
		if (size > 0){
			file_stat->st_size = (off_t)size;
		}
		return ret;
	}
	
	return ret;
}

static ShadowZip* get_cached_shadowzip(FILE *stream)
{
	FileExtraData* file_extra_data = get_file_mapping(stream);
	return file_extra_data == NULL ? NULL : file_extra_data->shadow_zip;
}

static ShadowZip* get_cached_shadowzip(int fd)
{
	FileExtraData* file_extra_data = get_file_mapping(fd);
	return file_extra_data == NULL ? NULL : file_extra_data->shadow_zip;
}

typedef FILE *(* FOpenType)(const char *path, const char *mode);
static FILE *my_fopen(const char *path, const char *mode)
{
	MY_METHOD("fopen([%s],[%s])", path, mode);

	PROFILER_TIMER("my_fopen");

	struct stat file_stat;
	memset(&file_stat, 0, sizeof(struct stat));
	if (stat(path, &file_stat) != 0) {
		
		MY_METHOD("not exist([%s],[%s])", path, mode);
		return fopen(path, mode);
	}

	if (g_apk_device_id == file_stat.st_dev && g_apk_ino == file_stat.st_ino)
	{
	    //Try acquire from cache
	    ShadowZip* shadow_zip = NULL;
	    {
	        PthreadWriteGuard guard(g_shadow_zip_cache_mutex);
	        if (g_shadow_zip_cache.size() > 0)
	        {
		        shadow_zip = g_shadow_zip_cache.back();
		        shadow_zip->rewind(NULL);
		        g_shadow_zip_cache.pop_back();
		        return shadow_zip->file_extra_data->file;
		    }
	    }
	    // Fail to acquire from cache, new one
	    shadow_zip = new ShadowZip();
		FILE* fp = shadow_zip->fopen();
		if (fp == NULL){	
			MY_ERROR("something bad happens!");
			delete shadow_zip;
			return fopen(path, mode); 
		}	
		
		FileExtraData* file_extra_data = save_file_mapping(shadow_zip, false);
		MY_LOG("shadow apk in fopen: %s, fd:0x%08x, file*: 0x%08llx", path, file_extra_data->fd, (unsigned long long)file_extra_data->file);	
		return file_extra_data->file;
	}
	
	MY_METHOD("not apk([%s],[%s])", path, mode);
	return fopen(path, mode);
}


typedef int (*FseekType)(FILE *stream, long offset, int whence);
static int my_fseek(FILE *stream, long offset, int whence)
{
	PROFILER_TIMER("my_fseek");
	
	ShadowZip* shadow_zip = get_cached_shadowzip(stream);
	if (shadow_zip == NULL){
		return fseek(stream, offset, whence);
	}else{
		return shadow_zip->fseek(stream, offset, whence);
	}
}

typedef long (*FtellType)(FILE *stream);
static int my_ftell(FILE *stream)
{
	
	ShadowZip* shadow_zip = get_cached_shadowzip(stream);
	if (shadow_zip == NULL){
		return ftell(stream);
	}else{
		return shadow_zip->ftell(stream);
	}
}

typedef void (*RewindType)(FILE *stream);
static void myrewind(FILE *stream)
{
	
	ShadowZip* shadow_zip = get_cached_shadowzip(stream);
	if (shadow_zip == NULL){
		rewind(stream);
	}else{
		shadow_zip->rewind(stream);
	}
}

typedef size_t (*FreadType)(void *ptr, size_t size, size_t nmemb, FILE *stream);
static size_t my_fread(void *ptr, size_t size, size_t nmemb, FILE *stream)
{
	PROFILER_TIMER("my_fread");

	ShadowZip* shadow_zip = get_cached_shadowzip(stream);
	if (shadow_zip == NULL){
		return fread(ptr, size, nmemb, stream);
	}else{
		return shadow_zip->fread(ptr, size, nmemb, stream);
	}
}

typedef char * (*Fgets)(char *s, int size, FILE *stream);
static char * my_fgets(char *s, int size, FILE *stream)
{
	PROFILER_TIMER("my_fgets");
	
	ShadowZip* shadow_zip = get_cached_shadowzip(stream);
	if (shadow_zip == NULL){
		return fgets(s, size, stream);
	}else{
		return shadow_zip->fgets(s, size, stream);
	}
}

typedef int (*FcloseType)(FILE* _fp);
static int my_fclose(FILE* stream)
{
	MY_METHOD("my_fclose: file*: 0x%08llx", (unsigned long long)stream);
	
	FileExtraData* file_extra_data = get_file_mapping(stream);
	if (file_extra_data != NULL)
	{
		release_to_cache(file_extra_data, false);
		return 0;
	}
	else
	{
		return fclose(stream);		
	}
}

typedef void *(*DlopenType)(const char *filename, int flags);
static void *my_dlopen(const char *filename, int flags)
{
	int il2cpp_postfix_len = strlen("libil2cpp.so");
	int len = strlen(filename);
	if (len > il2cpp_postfix_len && memcmp(filename + len - il2cpp_postfix_len, "libil2cpp.so", il2cpp_postfix_len) == 0)
	{		
		char link_file[256] = {0};
		snprintf(link_file, sizeof(link_file), "%s/libil2cpp.so", g_data_file_path);
		MY_LOG("redirect to %s", link_file);
		return dlopen(link_file, flags);
	}
	return dlopen(filename, flags);
}

typedef int (*OpenType)(const char *path, int flags, ...);
static int my_open(const char *path, int flags, ...)
{
	PROFILER_TIMER("my_open");

	mode_t mode = -1;
	int has_mode = ((flags & O_CREAT) == O_CREAT) || ((flags & 020000000) == 020000000);
	if (has_mode)
	{
		va_list args;
		va_start(args, flags);
		mode = static_cast<mode_t>(va_arg(args, int));
		va_end(args);
	}
	
	struct stat file_stat;
	memset(&file_stat, 0, sizeof(struct stat));
	if (stat(path, &file_stat) != 0) {
		int ret = has_mode ? open(path, flags, mode) : open(path, flags);
		MY_METHOD("open(can't access): %s -> fd:0x%08x", path, ret);
		return ret;
	}

	if (g_apk_device_id == file_stat.st_dev && g_apk_ino == file_stat.st_ino)
	{
	    //Try acquire from cache
	    ShadowZip* shadow_zip = NULL;
	    {
	        PthreadWriteGuard guard(g_shadow_zip_cache_mutex);
	        if (g_shadow_zip_cache.size() > 0)
	        {
		        shadow_zip = g_shadow_zip_cache.back();
		        shadow_zip->rewind(NULL);
		        g_shadow_zip_cache.pop_back();
		        return shadow_zip->file_extra_data->fd;
		    }
	    }
	    // Fail to acquire from cache, new one
		shadow_zip = new ShadowZip();
		FILE* fp = shadow_zip->fopen();
		if (fp == NULL){	
			MY_ERROR("something bad happens!");
			delete shadow_zip;
			int ret = has_mode ? open(path, flags, mode) : open(path, flags);
			MY_METHOD("open(rollback): %s -> fd:0x%08x", path, ret);
			return ret;
		}	
		
		FileExtraData* file_extra_data = save_file_mapping(shadow_zip, true);
		MY_LOG("shadow apk in open: %s, fd:0x%08x, file*: 0x%08llx", path, file_extra_data->fd, (unsigned long long)file_extra_data->file);	
		return file_extra_data->fd;
	}
	
	int ret = has_mode ? open(path, flags, mode) : open(path, flags);
	MY_METHOD("not apk open: %s -> fd:0x%08x", path, ret);
	return ret;
}

typedef ssize_t(*ReadType)(int fd, void *buf, size_t nbyte);
static ssize_t my_read(int fd, void *buf, size_t nbyte)
{
	PROFILER_TIMER("my_read");
	MY_METHOD("read: 0x%08x, %zu", fd, nbyte);
	
	
	ssize_t ret = 0;
	ShadowZip* shadow_zip = get_cached_shadowzip(fd);
	if (shadow_zip == NULL){
		ret = read(fd, buf, nbyte);
	}else{
		ret = shadow_zip->fread(buf, 1, nbyte, (FILE*)(size_t)fd);
	}
	MY_METHOD("readed: 0x%08x, %zd", fd, ret);
	return ret;
}

typedef off_t (*LseekType)(int fd, off_t offset, int whence);
off_t my_lseek(int fd, off_t offset, int whence)
{
	MY_METHOD("lseek: 0x%08x, offset: 0x%08lx, whence: %d", fd, offset, whence);
		
	off_t ret = 0;
	ShadowZip* shadow_zip = get_cached_shadowzip(fd);
	if (shadow_zip == NULL){
		ret = lseek(fd, offset, whence);
	}else{
		ret = shadow_zip->fseek((FILE*)(size_t)fd, offset, whence);
		if (ret == 0){ret = shadow_zip->ftell((FILE*)(size_t)fd);}
	}
	MY_METHOD("lseek: 0x%08x, return: %ld", fd, ret);
	return ret;
}

typedef off64_t (*Lseek64Type)(int fd, off64_t offset, int whence);
off64_t my_lseek64(int fd, off64_t offset, int whence)
{
	MY_METHOD("lseek64: 0x%08x, offset: 0x%08llx, whence: %d", fd, (unsigned long long)offset, whence);
	
	off64_t ret = 0;
	ShadowZip* shadow_zip = get_cached_shadowzip(fd);
	if (shadow_zip == NULL){
		ret =  lseek64(fd, offset, whence);
	}else{
		ret =  shadow_zip->fseek((FILE*)(size_t)fd, offset, whence);
		if (ret == 0){ret = shadow_zip->ftell((FILE*)(size_t)fd);}
	}
	MY_METHOD("lseek64: 0x%08x, return: %lld", fd, (unsigned long long)ret);
	return ret;
}

typedef int (*CloseType)(int fd);
static int my_close(int fd)
{
	MY_METHOD("my_close: 0x%08x", fd);
	
	FileExtraData* file_extra_data = get_file_mapping(fd);
	if (file_extra_data != NULL)
	{
		release_to_cache(file_extra_data, true);
		return 0;
	}
	else
	{
		return close(fd);	
	}
}

static int init_hook()
{
	/*
	char path[512] = {0};
	snprintf((char*)path, sizeof(path), "libunity.so");
	void const* addr = get_module_base(path, sizeof(path)-1);
	MY_INFO("libunity addr:0x%zx, path:%s", (size_t)addr, path);
	if (addr == NULL){	
		MY_ERROR("failed to load libunity info");
		return -1;
	}
	
	void *handle = dlopen(path, RTLD_LAZY);
	if (!handle) {
		MY_ERROR("failed to load libunity:%s, error:%s", path, dlerror());
		return -1;
	}
	
	std::vector<const char*> symbol_names;
	symbol_names.push_back("fopen");
	symbol_names.push_back("fseek");
	symbol_names.push_back("ftell");
	symbol_names.push_back("fread");
	symbol_names.push_back("fgets");
	symbol_names.push_back("fclose");
	symbol_names.push_back("stat");
	symbol_names.push_back("dlopen");
	symbol_names.push_back("open");
	symbol_names.push_back("read");
	symbol_names.push_back("lseek");
	symbol_names.push_back("lseek64");
	symbol_names.push_back("close");
	
	std::vector<void const *> new_func;
	new_func.push_back((void const *)my_fopen);
	new_func.push_back((void const *)my_fseek);
	new_func.push_back((void const *)my_ftell);
	new_func.push_back((void const *)my_fread);
	new_func.push_back((void const *)my_fgets);
	new_func.push_back((void const *)my_fclose);
	new_func.push_back((void const *)my_stat);
	new_func.push_back((void const *)my_dlopen);
	new_func.push_back((void const *)my_open);
	new_func.push_back((void const *)my_read);
	new_func.push_back((void const *)my_lseek);
	new_func.push_back((void const *)my_lseek64);
	new_func.push_back((void const *)my_close);
	
	std::vector<void **> old_func;
	old_func.push_back((void**)&old_fopen);
	old_func.push_back((void**)&old_fseek);
	old_func.push_back((void**)&old_ftell);
	old_func.push_back((void**)&old_fread);
	old_func.push_back((void**)&old_fgets);
	old_func.push_back((void**)&old_fclose);
	old_func.push_back((void**)&old_stat);
	old_func.push_back((void**)&old_dlopen);
	old_func.push_back((void**)&old_open);
	old_func.push_back((void**)&old_read);
	old_func.push_back((void**)&old_lseek);
	old_func.push_back((void**)&old_lseek64);
	old_func.push_back((void**)&old_close);
	
	elf_hook(path, addr, symbol_names, new_func, old_func);
	
	//dlclose(handle);
	*/
#define HOOK(lib, symbol) \
	if(0 != xhook_register(lib, #symbol, (void*)my_##symbol, NULL)){ \
		MY_ERROR("failed to find function:%s in %s", #symbol, lib); \
		return -1; \
	}

	//xhook_enable_debug(1);

    if (g_has_assets_patch)
    {
        HOOK("libunity.so", fopen);
        HOOK("libunity.so", fseek);
        HOOK("libunity.so", ftell);
        HOOK("libunity.so", fread);
        HOOK("libunity.so", fgets);
        HOOK("libunity.so", fclose);
        HOOK("libunity.so", stat);
        HOOK("libunity.so", open);
        HOOK("libunity.so", read);
        HOOK("libunity.so", lseek);
        HOOK("libunity.so", lseek64);
        HOOK("libunity.so", close);
	}

	if (g_has_il2cpp_patch)
	{
	    HOOK("libunity.so", dlopen);
	}
	
	if(0 != xhook_refresh(1)){
		MY_ERROR("failed to find replace function"); 
		return -1; 
	}	

	return 0;
}

static int init_art_hook()
{
//	char path[512] = {0};
//	snprintf((char*)path, sizeof(path), "libart.so");
//	void const* addr = get_module_base(path, sizeof(path)-1);
//	MY_LOG("libart addr:0x%zx, path:%s", (size_t)addr, path);
//	if (addr == NULL){	
//		MY_ERROR("failed to load libart info");
//		return -1;
//	}
//	
//	void *handle = dlopen(path, RTLD_LAZY);
//	if (!handle) {
//		MY_ERROR("failed to load libart:%s", dlerror());
//		return -1;
//	}
//	
//	std::vector<const char*> symbol_names;
//	symbol_names.push_back("dlopen");
//	
//	std::vector<void const *> new_func;
//	new_func.push_back((void const *)my_art_dlopen);
//	
//	std::vector<void **> old_func;
//	old_func.push_back((void**)&art_old_dlopen);
//	
//	elf_hook(path, addr, symbol_names, new_func, old_func);
//	
//	//dlclose(handle);
	return 0;
}

static bool bootstrap()
{
	std::string default_il2cpp_path;
	std::string patch_il2cpp_path;
	bool use_patch = extract_patch_info(default_il2cpp_path, patch_il2cpp_path);
	
	if (use_patch){
		MY_INFO("bootstrap running %s with apk_path:[%s], patch_so:[%s], patch_dir:[%s]", TARGET_ARCH_ABI, 
			g_apk_file_path, patch_il2cpp_path.c_str(), g_use_data_path);

        delete_old_file_mapping_data();

		bool success = (0 == init_hook());
		if (success)
		{
	        MY_INFO("bootstrap running with patch: il2cpp_patch: [%s], assets_patch: [%s]", g_has_il2cpp_patch ? "true" : "false", g_has_assets_patch ? "true" : "false");

            // 在某些IO性能奇葩的机型上(Oppo A31)，需要等待一会，防止出现莫名其妙的IO问题
			msleep(100);

#ifdef PROFILER_ENABLE
			create_monitor_thread();
#endif
			//ShadowZip::output_apk(g_use_data_path);
			return true;
		}// else we still do so hook
		else
		{
			MY_INFO("bootstrap running failed with patch");
		}
	}
	else
	{
		MY_INFO("bootstrap running without patch");
	}

	return false;
}

//static void entrance() __attribute__((constructor));
//void entrance() {
//}



