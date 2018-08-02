#define _LARGEFILE64_SOURCE 1
#define _FILE_OFFSET_BITS 64

#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <errno.h>

#include <sys/types.h>
#include <sys/stat.h>  
#include <dirent.h>
#include <unistd.h> //for rmdir

#include "snlog.h"

#include "utils.h"
#include "filesystem.h"

#ifdef __MINGW32__
#define fseeko(x,y,z)  fseeko64(x,y,z)
#define ftello(x)      ftello64(x)
#define stat _stati64
#endif

#define MSG_MODULE "FILESYS"
#define MAX_VIRTUAL_PATH 32

#define DS "/"


typedef struct _vdir 
{
    char base[SN_MAX_PATH];
    char name[MAX_FILENAME];
    struct _vdir *p_next;
} vdir_t;

struct _filesys
{
    char base_path[SN_MAX_PATH];
    char base_path_curr[SN_MAX_PATH];
    char relative_path[SN_MAX_PATH];

    vdir_t *curr_vdir;
    vdir_t *vdirs;
    int shared_vdirs;
};

struct _file
{
    FILE *fp;
};


//remove the ending "/" of path
static void trim_path_end(char *path)
{
    int len = strlen(path);
    for (; ;)
    {
#ifdef WIN32
        //"C:/"的情况
        if (len == 3 && path[1] == ':')
            break;
#endif
        if (len > 0 && path[len - 1] == 0x2F /* "/" */)
        {
            path[len - 1] = '\0';
            len--;
        }
        else
            break;
    }
}


static void get_first_session(const char *path, char name[MAX_FILENAME] /* out */)
{
    int i, j;
    i = 0;
    if (path[0] == 0x2F)
        i++;
    j = i;
    while ((i < strlen(path)) && (i - j < MAX_FILENAME - 1))
    {
        if (path[i] == 0x2F)
            break;
        i++;
    }
    memcpy(name, &path[j], i - j);
    name[i - j] = '\0';
    sn_log_debug("name:%s\n", name);
}


static void remove_first_session(char *path)
{
    char name[MAX_FILENAME];
    int lenp, lenn;
    get_first_session(path, name);
    lenp = strlen(path);
    lenn = strlen(name);
    if (lenp == lenn || lenp == 0)
        path[0] = '\0';
    else
        memmove(path, path + lenn + 1, lenp - lenn);
}


static void clean_path(char *path)
{
    int i = 0, j = 0;
    int len = strlen(path);
    while (i < len)
    {
        path[j] = path[i];
        i++;
        while (path[i] == 0x2F && path[j] == 0x2F)
            i++;
        j++;
    }
    if (j > 1 && path[j - 1] == 0x2F)
        j--;
    path[j] = '\0';
#ifdef WIN32
    //"C:/"的情况
    if (j > 0 && path[j - 1] == ':')
    {
       path[j] = 0x2F;
       path[j + 1] = 0;
    }
#endif
}


static vdir_t* find_vdir(filesys_t *fs, const char *name)
{
    vdir_t *p;
    p = fs->vdirs;
    while (p != NULL)
    {
        if (strncmp(p->name, name, SN_MAX_PATH) == 0)
            return p;
        p = p->p_next;
    }
    return NULL;
}


filesys_t* filesys_create(const char *base_path)
{
    filesys_t *fs;

    sn_log_info("base_path:%s\n", base_path);

    fs = (filesys_t *)malloc(sizeof(filesys_t));
    if (fs == NULL)
    {
        sn_log_error("malloc FAILED!\n");
        return NULL;
    }
    memset(fs, 0, sizeof(filesys_t));

    strncpy(fs->base_path, base_path, SN_MAX_PATH - 1);
    trim_path_end(fs->base_path);
    strcpy(fs->base_path_curr, fs->base_path);
    fs->relative_path[0] = '\0';

    return fs;
}


filesys_t* filesys_copy(const filesys_t *fs, int share_vdir)
{
    filesys_t *result;
    result = (filesys_t *)malloc(sizeof(filesys_t));
    if (result == NULL)
    {
        sn_log_error("malloc FAILED!\n");
        return NULL;
    }
    memset(result, 0, sizeof(filesys_t));
    memcpy(result, fs, sizeof(filesys_t));
    if (share_vdir > 0)
        result->shared_vdirs = 1;
    else
    {
        vdir_t *p, *q, *r = NULL;

        result->shared_vdirs = 0;
        result->vdirs = NULL;
        result->curr_vdir = NULL;

        p = fs->vdirs;
        while (p != NULL)
        {
            q = (vdir_t *)malloc(sizeof(vdir_t));
            if (q == NULL)
            {
                filesys_destory(result);
                return NULL;
            }
            memcpy(q, p, sizeof(vdir_t));
            q->p_next = NULL;

            if (result->vdirs == NULL)
                result->vdirs = q;
            else if (r == NULL)
            { sn_log_fatal("should not go here, code logic ERROR!\n"); }
            else
                r->p_next = q;

            if (fs->curr_vdir == p)
                result->curr_vdir = q;

            r = q;
            p = p->p_next;
        }
    }
    return result;
}


void filesys_destory(filesys_t *fs)
{
    vdir_t *p, *q;

    if (fs->shared_vdirs == 0)
    {
        p = fs->vdirs;
        while (p != NULL)
        {
            q = p->p_next;
            free(p);
            p = q;
        }
    }
    
    free(fs);
}


int filesys_add_virtual_dir(filesys_t *fs, const char *real_path, const char *name)
{
    struct stat st;
    vdir_t *p_new;

    if (fs->shared_vdirs > 0)
    {
        sn_log_error("shared virtual dirs, read only!\n");
        return -1;
    }

    if (find_vdir(fs, name) != NULL)
    {
        sn_log_error("virtual dir (%s) already exist!\n", name);
        return -1;
    }

    if (stat(real_path, &st) < 0)
    {
        sn_log_error("stat FAILED:%s, path:%s\n", strerror(errno), real_path);
        return -1;
    }

    p_new = (vdir_t *)malloc(sizeof(vdir_t));
    if (p_new == NULL)
    {
        sn_log_error("malloc FAILED!\n");
        return -1;
    }
    memset(p_new, 0, sizeof(vdir_t));
    strncpy(p_new->base, real_path, SN_MAX_PATH - 1);
    strncpy(p_new->name, name, MAX_FILENAME - 1);
    p_new->p_next = fs->vdirs;
    fs->vdirs = p_new;
    return 0;
}


int filesys_del_virtual_dir(filesys_t *fs, const char *name)
{
    vdir_t *p, *q;

    if (fs->shared_vdirs > 0)
    {
        sn_log_error("shared virtual dirs, read only!\n");
        return -1;
    }

    p = find_vdir(fs, name);
    if (p == NULL)
        return -1;

    //如果要删除的virtual在使用中, 将当前路径转到跟路径
    if (p == fs->curr_vdir)
    {
        fs->curr_vdir = NULL;
        strcpy(fs->base_path_curr, fs->base_path);
        strcpy(fs->relative_path, "");
    }

    if (p != fs->vdirs)
    {
        q = fs->vdirs;
        while (q->p_next != p)
        {
            q = q->p_next;
            if (q == NULL)
            {
                sn_log_error("find pre node error!\n");
                return -1;
            }
        }
        q->p_next = p->p_next;
    }
    else
        fs->vdirs = NULL;
    free(p);
    return 0;
}


int filesys_change_working_dir(filesys_t *fs, const char *new_path)
{
    sn_log_debug("base:%s\nrelative:%s\nto:%s\n",
            fs->base_path_curr, fs->relative_path, new_path);

    //ATTENTION
    if (new_path[0] == 0x2F /* "/" */)
        strcpy(fs->relative_path, "");

    if (new_path[0] == '.' && strlen(new_path) == 1)
    {
        return 0;
    }
    else if (strncmp(new_path, "..", 2) == 0 && strlen(new_path) == 2)
    {
        int len = strlen(fs->relative_path);
        int i;
        if (len <= 1)
        {
            if (fs->curr_vdir != NULL)
            {
                strcpy(fs->base_path_curr, fs->base_path);
                fs->curr_vdir = NULL;
                return 0;
            }
            else
                return -1;
        }
        else
        {
            for (i = len - 1; i >= 0; i--)
            {
                if (fs->relative_path[i] == 0x2F /* "/" */)
                {
                    fs->relative_path[i] = '\0';
                    trim_path_end(fs->relative_path);
                    sn_log_debug("new relative path1:%s\n", fs->relative_path);
                    return 0;
                }
            }
            fs->relative_path[0] = '\0';
            sn_log_debug("new relative path2:%s\n", fs->relative_path);
            return 0;
        }
    }
    else
    {
        struct stat st;
        char dir[1024];

        if (fs->curr_vdir == NULL && strlen(fs->relative_path) == 0 || new_path[0] == 0x2F)
        {
            char name[MAX_FILENAME];
            vdir_t *pv;

            get_first_session(new_path, name);

            pv = find_vdir(fs, name);
            if (pv != NULL)
            {
                fs->curr_vdir = pv;
                strcpy(fs->base_path_curr, pv->base);
                strcpy(fs->relative_path, new_path);
                remove_first_session(fs->relative_path);
                trim_path_end(fs->relative_path);
                sn_log_debug("new relative path3:%s\n", fs->relative_path);
                return 0;
            }
            fs->curr_vdir = NULL;
        }

        snprintf(dir, 1024, "%s/%s/%s", fs->base_path_curr, fs->relative_path, new_path);
        clean_path(dir);

        if (stat(dir, &st) < 0)
        {
            sn_log_error("stat FAILED:%s, dir:%s, path:%s\n", strerror(errno), dir, new_path);
            return -1;
        }
        if (!S_ISDIR(st.st_mode))
        {
            sn_log_error("input is not dir:%s\n", dir);
            return -1;
        }
        if (new_path[0] != 0x2F/* / */)
            strcat(fs->relative_path, "/");
        strcat(fs->relative_path, new_path);
        trim_path_end(fs->relative_path);
        sn_log_debug("new relative path4:%s\n", fs->relative_path);
        return 0;
    }
}


int filesys_get_working_dir(filesys_t *fs, char *relative_path/*out*/)
{
    if (fs->curr_vdir != NULL)
        snprintf(relative_path, SN_MAX_PATH, "/%s/%s", fs->curr_vdir->name, fs->relative_path);
    else
    {
        strcpy(relative_path, fs->relative_path);
        if (strlen(relative_path) == 0)
            strcpy(relative_path, "/");
    }
    clean_path(relative_path);
    return 0;
}


int filesys_get_absolute_path(filesys_t *fs, const char *path, char *absolute_path/*out*/)
{
    //check filename, 文件名不允许有目录跳转
    if (strstr(path, "../") != NULL || strstr(path, "..\\") != NULL)
    {
        sn_log_warn("a directory traversal vulnerability attack catched, path:%s\n", path);
        return -1;
    }

    if (path[0] == 0x2F /* "/" */ || strlen(fs->relative_path) == 0)
    {
        char name[SN_MAX_PATH];
        vdir_t *pv;

        get_first_session(path, name);

        pv = find_vdir(fs, name);
        if (pv != NULL)
        {
            int len = strlen(pv->base);
            absolute_path[SN_MAX_PATH - 1] = '\0';
            memcpy(absolute_path, pv->base, len);
            absolute_path[len] = 0x2F;
            len++;
            strncpy(absolute_path + len, path, SN_MAX_PATH - len - 1);
            remove_first_session(absolute_path + len);
        }
        else
            snprintf(absolute_path, SN_MAX_PATH, "%s/%s", fs->base_path_curr, path);
    }
    else
    {
        snprintf(absolute_path, SN_MAX_PATH, "%s/%s/%s", fs->base_path_curr, fs->relative_path, path);
    }

#ifdef WIN32
    {
    int len;
    int i;
    len = strlen(absolute_path);
    for (i = 0; i < len; i++)
    {
        if (absolute_path[i] == 0x5C /* "\" */)
            absolute_path[i] = 0x2F; /* "/" */
    }
    if (len > 1 && absolute_path[len - 1] == 0x2F && absolute_path[len - 2] == ':')
        absolute_path[len - 1] = 0;
    }
#endif

    clean_path(absolute_path);

    sn_log_debug("input:%s,base:%s,relative:%s,new:%s\n", path, fs->base_path_curr,
            fs->relative_path, absolute_path);
    return 0;
}


int fs_get_fileinfo(filesys_t *fs, const char *path, fileinfo_t *info_out/*out*/)
{
    struct stat st;

    if (stat(path, &st) < 0)
    {
        sn_log_error("stat FAILED:%s, path:%s\n", strerror(errno), path);
        return -1;
    }

    memset(info_out, 0, sizeof(fileinfo_t));
    info_out->timestamp = st.st_mtime; 
    info_out->size = st.st_size;
#ifdef WIN32
    info_out->protection = 0x1FF;
#else
    info_out->protection = st.st_mode & (S_IRWXU|S_IRWXG|S_IRWXO);
#endif

    if (S_ISDIR(st.st_mode))
    {
        info_out->type = FT_DIR;
        if (fs != NULL && strncmp(fs->base_path, path, strlen(fs->base_path)) != 0)
            info_out->type = FT_VDIR;
    }
#ifndef WIN32
    else if (S_ISLNK(st.st_mode))
        info_out->type = FT_LINK;
#endif
    else
        info_out->type = FT_FILE;

    return 0;
}


int fs_get_fileinfos(filesys_t *fs, const char *dir, fileinfo_t **info_out/*out*/)
{
    DIR *d;
    struct dirent *de;
    struct stat st;
    fileinfo_t *infos = NULL;
    fileinfo_t *p_info;
    char fullpath[SN_MAX_PATH];
    int under_vdir = 0;

    if (stat(dir, &st) < 0)
    {
        sn_log_error("stat FAILED:%s, dir:%s\n", strerror(errno), dir);
        return -1;
    }
    if (!S_ISDIR(st.st_mode))
    {
        sn_log_error("input is not dir:%d\n", dir);
        return -1;
    }
    if ((d = opendir(dir)) == NULL)
    {
        sn_log_error("opendir FAILED:%s\n", strerror(errno));
        return -1;
    }

    if (fs != NULL && strncmp(fs->base_path, dir, strlen(fs->base_path)) != 0)
        under_vdir = 1;

    p_info = NULL;
    while ((de = readdir(d)) != NULL)
    {
        if (p_info == NULL)
            p_info = (fileinfo_t *)malloc(sizeof(fileinfo_t));
        if (p_info == NULL)
        {
            sn_log_error("malloc FAILED!\n");
            *info_out = infos;
            return 0;
        }
        memset(p_info, 0, sizeof(fileinfo_t));

        memset(fullpath, 0, SN_MAX_PATH);
        snprintf(fullpath, SN_MAX_PATH, "%s/%s", dir, de->d_name);

        //ATTENTION
        if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0)
            continue;

        if (fs_get_fileinfo(NULL, fullpath, p_info) != 0)
            continue;
        strncpy(p_info->name, de->d_name, MAX_FILENAME - 1);
        if (p_info->type == FT_DIR && under_vdir == 1)
            p_info->type = FT_VDIR;

        p_info->p_next = infos;
        infos = p_info;
        p_info = NULL;
    }

    if (fs != NULL && strcmp(dir, fs->base_path) == 0)
    {
        struct stat st;
        vdir_t *pv = fs->vdirs;
        while (pv != NULL)
        {
            if (stat(pv->base, &st) < 0)
            {
                sn_log_error("stat FAILED:%s, path:%s\n", strerror(errno), pv->base);
                continue;
            }

            p_info = (fileinfo_t *)malloc(sizeof(fileinfo_t));
            if (p_info == NULL)
            {
                sn_log_error("malloc FAILED!\n");
                *info_out = infos;
                return 0;
            }
            memset(p_info, 0, sizeof(fileinfo_t));
            strncpy(p_info->name, pv->name, MAX_FILENAME - 1);
            p_info->type = FT_VDIR;
            p_info->timestamp = st.st_mtime;
#ifdef WIN32
            p_info->protection = 0x1FF;
#else
            p_info->protection = st.st_mode & (S_IRWXU|S_IRWXG|S_IRWXO);
#endif
            p_info->p_next = infos;
            infos = p_info;

            pv = pv->p_next;
        }
    }

    closedir(d);

    *info_out = infos;
    return 0;
}


void fs_free_fileinfos(fileinfo_t *infos)
{
    fileinfo_t *head = infos;
    fileinfo_t *p_info;
    while (head != NULL)
    {
        p_info = head->p_next;
        free(head);
        head = p_info;
    }
}


//type: 0-->sort by name, 1-->by size, 2-->by date
//order: 0-->ascend, 1->descend
void fs_sort_fileinfos(fileinfo_t **infos, int type, int order)
{
    fileinfo_t *p_ori = *infos;
    fileinfo_t *p_dir = NULL;
    fileinfo_t *p_file = NULL;
    fileinfo_t *curr;
    fileinfo_t *pre;
    fileinfo_t *node;
    fileinfo_t **head;
    int found;

    if (type < 0 || type > 2)
    {
        sn_log_error("invalid sort type:%d\n", type);
        type = 0;
    }
    if (order < 0 || order > 1)
    {
        sn_log_error("invalid sort order:%d\n", order);
        order = 0;
    }

    while (p_ori != NULL)
    {
        node = p_ori;
        p_ori = p_ori->p_next;

        head = ((node->type == FT_DIR || node->type == FT_VDIR) ? &p_dir : &p_file);
        curr = *head;
            
        found = 0;
        pre = NULL;
        while (curr != NULL)
        {
            if (order == 0)
            {
                if (type == 0 || ((node->type == FT_DIR || node->type == FT_VDIR) && type == 1))
                {
                    if (strcmp(node->name, curr->name) < 0)
                        found = 1;
                }
                else if (type == 1)
                {
                    if (node->size < curr->size)
                        found = 1;
                }
                else if (type == 2)
                {
                    if (node->timestamp < curr->timestamp)
                        found = 1;
                }
            }
            else
            {
                if (type == 0 || ((node->type == FT_DIR || node->type == FT_VDIR) && type == 1))
                {
                    if (strcmp(node->name, curr->name) >= 0)
                        found = 1;
                }
                else if (type == 1)
                {
                    if (node->size >= curr->size)
                        found = 1;
                }
                else if (type == 2)
                {
                    if (node->timestamp >= curr->timestamp)
                        found = 1;
                }
            }

            if (found == 1)
            {
                if (pre != NULL)
                    pre->p_next = node;
                else
                    *head = node;
                node->p_next = curr;
                break;
            }
            pre = curr;
            curr = curr->p_next;
        }

        if (found == 0)
        {
            if (pre != NULL)
                pre->p_next = node;
            else
                *head = node;
            node->p_next = NULL;
        }
    }

    p_ori = p_dir;
    if (p_ori == NULL)
        p_dir = p_file;
    else
    {
        while (p_ori->p_next != NULL)
            p_ori = p_ori->p_next;
        p_ori->p_next = p_file;
    }

    *infos = p_dir;
}


file_t* fs_open(const char *name, e_fopenmode mode)
{
    file_t *f = (file_t *)malloc(sizeof(file_t));
    if (f == NULL)
    {
        sn_log_error("malloc FAILED!\n");
        return NULL;
    }
    memset(f, 0, sizeof(file_t));

    if (mode == FM_READ)
        f->fp = fopen(name, "rb");
    else if (mode == FM_WRITE)
        f->fp = fopen(name, "w+b");
    else if (mode == FM_WRITE_APPEND)
        f->fp = fopen(name, "ab");

    if (f->fp == NULL)
    {
        sn_log_error("open file FAILED! mode:%d,name:%s\n", mode, name);
        free(f);
        return NULL;
    }
    return f;
}


int fs_close(file_t *file)
{
    if (file->fp != NULL)
        fclose(file->fp);
    free(file);
    return 0;
}


int fs_read(file_t *file, void *buf, int size)
{
    if (file->fp == NULL)
        return -1;
    //TODO 效率?
    return fread(buf, 1, size, file->fp);
}


int fs_write(file_t *file, void *buf, int size)
{
    if (file->fp == NULL)
        return -1;
    //TODO 效率?
    return fwrite(buf, 1, size, file->fp);
}


int fs_seek(file_t *file, int64_t offset, int origin)
{
    if (file->fp == NULL)
        return -1;
    return fseeko(file->fp, offset, origin);
}


int64_t fs_tell(file_t *file)
{
    if (file->fp == NULL)
        return -1;
    return ftello(file->fp);
}


int fs_create_dir(const char *dir, int mode)
{
    int ret;
#ifdef WIN32
    ret = mkdir(dir);
#else
    ret = mkdir(dir, mode);
#endif
    //sn_log_debug("fs_create_dir:%s\n", dir);
    if (ret != 0)
        sn_log_error("fs_create_dir FAILED,dir:%s,reasion:%s\n", dir, strerror(errno));
    return ret;
}


int fs_move(const char *oldpath, const char *newpath)
{
    int ret;
    //TODO check whether new path exist!
    //sn_log_debug("fs_move, old:%s, new:%s\n", oldpath, newpath);
    ret = rename(oldpath, newpath);
    if (ret != 0)
        sn_log_error("fs_move FAILED,old:%s,new:%s,reason:%s\n", oldpath, newpath, strerror(errno));
    return ret;
}


int fs_delete(const char *path)
{
    struct stat st;
    int ret;

    //sn_log_debug("fs_delete:%s\n", path);
    if (stat(path, &st) < 0)
    {
        sn_log_error("stat FAILED:%s, path:%s\n", strerror(errno), path);
        return -1;
    }

    if (S_ISDIR(st.st_mode))
        ret = rmdir(path);
    else
        ret = unlink(path);

    if (ret != 0)
        sn_log_error("fs_delete FAILED,path:%s,reasion:%s\n", path, strerror(errno));

    return ret;
}


int64_t fs_get_filesize(const char *name)
{
    FILE *fp;
    uint64_t size;
    fp = fopen(name, "rb");
    if (fp == NULL)
    {
        sn_log_error("open file FAILED! name:%s\n", name);
        return -1;
    }
    fseeko(fp, 0, SEEK_END);
    size = ftello(fp);
    fclose(fp);
    return size;
}

