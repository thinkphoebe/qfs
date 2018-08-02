#ifndef __FTPSERVER_H__
#define __FTPSERVER_H__

#ifdef __cplusplus
extern "C" {
#endif


typedef struct _ftpserver 
{
    char path[1024];
    uint16_t cmd_port;
    uint16_t data_port;

    int allow_anonymous;
    int anonymous_writable;

    int skt;
    struct _filesys *fs;

    int status; //0-->stoped, 1-->running, 2-->stoping

} ftpserver_t;

ftpserver_t* ftpserver_create(const char *path);
void ftpserver_destory(ftpserver_t *h);

int ftpserver_start(ftpserver_t *h);


#ifdef __cplusplus
}
#endif

#endif // __FTPSERVER_H__

