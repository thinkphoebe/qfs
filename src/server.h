#ifndef __SERVER_H__
#define __SERVER_H__

#ifdef __cplusplus
extern "C" {
#endif


#define MAX_USERS 32
#define MAX_VDIR 32

typedef struct _server_option 
{
    char root[1024];
    char vdir_path[MAX_VDIR][1024];
    char vdir_name[MAX_VDIR][256];

    int enable_ftp;
    int ftp_cmd_port;
    int ftp_data_port;

    int enable_http;
    int http_port;
    char http_templete_path[1024];

    int allow_anonymous;
    int anonymous_writable;

} server_option_t;

//注意: 本函数不改变已添加的用户账户信息
int server_update_option(const server_option_t *option);
//注意: 本函数会清空已添加的用户账户信息, 根据读入的信息重新添加
int server_load_option(const char *filename);
const server_option_t* server_get_option();

int server_start();
int server_stop();
int server_is_running();

struct _filesys;
struct _filesys* server_create_fs();


//type: 0-->http, 1-->ftp
void server_get_session_info(void (*cb_session_info)(void *context, int type,
    uint32_t ip, const char *user, time_t start, int fflag, time_t fstart,
    uint64_t totalsize, uint64_t currsize, const char *fname, uint32_t session_id),
    void *context);

//transonly为1时仅kill掉当前文件传输
int server_kill_session(int type, uint32_t session_id, int transonly);


#ifdef __cplusplus
}
#endif

#endif // __SERVER_H__

