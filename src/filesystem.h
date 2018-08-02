#ifndef __FILESYSTEM_H__
#define __FILESYSTEM_H__

#ifdef __cplusplus
extern "C" {
#endif


#define MAX_FILENAME 256
#define SN_MAX_PATH 1024

typedef struct _filesys filesys_t;

filesys_t* filesys_create(const char *base_path);
filesys_t* filesys_copy(const filesys_t *fs, int share_vdir);
void filesys_destory(filesys_t *fs);

int filesys_add_virtual_dir(filesys_t *fs, const char *real_path, const char *name);
int filesys_del_virtual_dir(filesys_t *fs, const char *name);

int filesys_change_working_dir(filesys_t *fs, const char *relative_path);
int filesys_get_working_dir(filesys_t *fs, char *relative_path/*out*/);
int filesys_get_absolute_path(filesys_t *fs, const char *path, char *absolute_path/*out*/);

typedef enum
{
	FT_FILE,
	FT_DIR,
	FT_VDIR,
	FT_LINK,

} e_filetype;

typedef struct _fileinfo
{
    time_t timestamp;
    e_filetype type;
    int protection; //3 bits protection per section: UrwxGrwxOrwx
    uint64_t size;
    char name[MAX_FILENAME];

    struct _fileinfo *p_next;

} fileinfo_t;


//以下两函数fs参数用于读取vdir信息, 如不关心可传NULL 
int fs_get_fileinfo(filesys_t *fs, const char *path, fileinfo_t *info_out/*out*/);
int fs_get_fileinfos(filesys_t *fs, const char *dir, fileinfo_t **info_out/*out*/);
//int fs_get_fileinfos(const char *dir, fileinfo_t **info_out[>out<]);
void fs_free_fileinfos(fileinfo_t *infos);
//type: 0-->sort by name, 1-->by size, 2-->by data
//order: 0-->ascend, 1->descend
void fs_sort_fileinfos(fileinfo_t **infos, int type, int order);

typedef enum
{
   FM_READ,
   FM_WRITE,
   FM_WRITE_APPEND,

} e_fopenmode;

typedef struct _file file_t;

file_t* fs_open(const char *name, e_fopenmode mode);
int fs_close(file_t *file);
int fs_read(file_t *file, void *buf, int size);
int fs_write(file_t *file, void *buf, int size);
int fs_seek(file_t *file, int64_t offset, int origin);
int64_t fs_tell(file_t *file);

int fs_create_dir(const char *dir, int mode);
int fs_move(const char *oldpath, const char *newpath);
int fs_delete(const char *path);
int64_t fs_get_filesize(const char *name);


#ifdef __cplusplus
}
#endif

#endif // __FILESYSTEM_H__

