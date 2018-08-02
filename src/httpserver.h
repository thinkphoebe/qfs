#ifndef __HTTPSERVER_H__
#define __HTTPSERVER_H__

#ifdef __cplusplus
extern "C" {
#endif


#define TEMPLETE_FIELD_HEAD "TEMPLETE_FIELD1"
#define TEMPLETE_FIELD_END "TEMPLETE_FIELD2"

struct _filesys;

typedef struct _httpserver 
{
    char path[1024];
    uint16_t listen_port;

    int allow_anonymous;
    int anonymous_writable;

    int skt;
    struct _filesys *fs;
    struct _filesys *templete_fs;
    char templete_header[1024 * 8];
    char templete_footer[1024 * 8];
    char templete_dir[1024];
    char templete_file[1024];
    char templete_filepath[1024];
    char templete_upok[1024];
    char templete_upfailed[1024];

    int status; //0-->stoped, 1-->running, 2-->stoping

} httpserver_t;

httpserver_t* httpserver_create(const char *path);
void httpserver_destory(httpserver_t *h);

int httpserver_start(httpserver_t *h);

int httpserver_set_templete(httpserver_t *h, const char *header, const char *footer,
        const char *file, const char *dir, const char *upok, const char *upfailed,
        const char *filepath);
int httpserver_set_templete_file(httpserver_t *h, const char *path, const char *filename);


#ifdef __cplusplus
}
#endif

#endif // __HTTPSERVER_H__

