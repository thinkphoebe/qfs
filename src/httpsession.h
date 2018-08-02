#ifndef __HTTPSESSION_H__
#define __HTTPSESSION_H__

#ifdef __cplusplus
extern "C" {
#endif


struct _httpserver;

typedef struct _http_session_info
{
    char username[128];
    uint32_t ip;
    int file_flag; //0-->idle, 1-->upload, 2-->download
    time_t file_start; //有文件下载上传时, 记录开始时间
    uint64_t total_size; //有文件下载上传时, 记录文件的大小
    uint64_t curr_size; //有文件下载上传时, 记录当前传输的大小
    char fname[SN_MAX_PATH];
    uint32_t session_id;

} http_session_info_t;

int httpsession_add(const struct _httpserver *p_server, int skt, uint32_t ip);
void httpsession_clear(const struct _httpserver *p_server);
int httpsession_kill(uint32_t id, int transonly);

void http_get_session_info(void (*cb_session_info)(void *context, int type,
    uint32_t ip, const char *user, time_t start, int fflag, time_t fstart,
    uint64_t totalsize, uint64_t currsize, const char *fname, uint32_t session_id),
    void *context);


#ifdef __cplusplus
}
#endif

#endif // __HTTPSESSION_H__

