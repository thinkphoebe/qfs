#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h> //for close

#ifdef WIN32
#include <winsock.h>
#include <winsock2.h>
#else
#include <sys/socket.h>
#include <arpa/inet.h>
#include <sys/ioctl.h>
#endif

#include "utils.h"
#include "event.h"
#include "access.h"
#include "rule.h"
#include "filesystem.h"
#include "httpserver.h"
#include "httpauth.h"
#include "httpsession.h"


#define MSG_MODULE "HTTPSESSION"
#define SERVER_NAME "quickshare httpd"
#define SESSION_TIMEOUT 120 //in seconds
#define MSG_BUF_SIZE 1024
#define DATA_BUF_SIZE (1024 * 10) //注意, 栈内存分配, 不要太大
#define MAX_SESSIONS 128


typedef enum
{
    SESSION_STATUS_NULL,
    SESSION_STATUS_INPROCESS,
    SESSION_STATUS_COMPLETE,
    SESSION_STATUS_ERROR,

} e_status;

typedef struct _authinfo
{
	char alg[10];	    //算法名称: md5, md5-sess
	char user[64];		//用户名
	char pswd[64];		//密码
	char realm[128];	//realm name
	char nonce[64];		//服务器随机产生的nonce返回串
	char uri[256];		//请求URL
	char qop[10];		//qop-value: "", "auth", "auth-int"
	char opaque[64];	//opaque value

	char nc[9];
	char cnonce[64];
	hashhex_t response;

} authinfo_t;

typedef struct _header
{
    char method[8];
    char request_url[1024];
    char host[128];
    char accept_type[16];
    char accept_encoding[16];
    char connection[16];
    char content_type[32];
    char boundary[256];
    int64_t content_length;
    int header_length;

    int64_t range_start;
    int64_t range_end;

    authinfo_t auth;

} header_t;

typedef struct _session
{
    const httpserver_t *p_server;
    header_t header;
    char msg_buf[MSG_BUF_SIZE];
    int skt;

    int exit_flag;
    e_status status;

    time_t start_time;
    time_t complete_time; //only valid on SESSION_STATUS_COMPLETE
    time_t update_time;

    http_session_info_t info;

//#ifdef SUPPORT_HTTPS
//    void *ssl;
//#endif
} session_t;

static session_t m_session_pool[MAX_SESSIONS];
static uint32_t m_session_count = 0;

static const char RFC1123FMT[] = "%a, %d %b %Y %H:%M:%S GMT";
#ifdef WIN32
static const char *TEXT_ENCODING = "text/html";
#else
static const char *TEXT_ENCODING = "text/html;charset=utf-8";
#endif


static const char* response2msg(int code);
static const char* get_file_mime(const char *path);


static void print_session(session_t *session)
{
}


static int get_line(session_t *session)
{
	char *msg_buf = session->msg_buf;
	int  count = 0;
    memset(msg_buf, 0, MSG_BUF_SIZE);
	while (recv(session->skt, msg_buf + count, 1, 0) == 1)
	{
		session->header.header_length++;
		//if (msg_buf[count] == '\r')
        //    continue;
		if (msg_buf[count] == '\n')
			return count + 1;
		if (count < (MSG_BUF_SIZE - 2))
			count++;
	}
    return count;
}


static int parse_value(const char *buf, const char *name, char *value/*out*/, int size)
{
    char *p, *q;
    p = strstr(buf, name);
    if (p == NULL)
        return -1;
    p += strlen(name);
    if (*p == '\"')
        p++;
    q = strpbrk(p, ",\"");
    if (q == NULL)
        return -1;
    if (q - p >= size)
    {
        sn_log_error("value is too long, name:%s, bufsize:%d, valuesize:%d\n", name, size, q - p);
        return -1;
    }
    memcpy(value, p, q - p);
    value[q - p] = '\0';

    return 0;
}


static int recv_header(session_t *session)
{
	char *p = NULL;
    char *q = NULL;

	memset(&session->header, 0, sizeof(header_t));

	for (; ;)
	{
		if (get_line(session) <= 0)
			break;
        if (strncasecmp(session->msg_buf, "\r\n", strlen("\r\n")) == 0)
            break;

		if (strncasecmp(session->msg_buf, "GET", strlen("GET")) == 0
                || strncasecmp(session->msg_buf, "POST", strlen("POST")) == 0)
		{
            p = session->msg_buf;
			q = strpbrk(p, " \r\n");
			if (q == NULL)
				continue;
            if (q - p < sizeof(session->header.method) - 1)
                memcpy(session->header.method, p, q - p);
            else
                memcpy(session->header.method, p, sizeof(session->header.method) - 1);

            p = q + 1;
            while (*p == ' ')
                p++;
			q = strpbrk(p, " \r\n");
			if (q == NULL)
				continue;
            memset(session->header.request_url, 0, 1024);
            if (q - p < sizeof(session->header.request_url) - 1)
                memcpy(session->header.request_url, p, q - p);
            else
                memcpy(session->header.request_url, p, sizeof(session->header.request_url) - 1);
            url_decode(session->header.request_url);
		}
		else if (strncasecmp(session->msg_buf, "Host:", strlen("Host:")) == 0)
		{
            p = session->msg_buf + strlen("Host:");
            while (*p == ' ')
                p++;
			q = strpbrk(p, " \r\n");
			if (q == NULL)
				continue;
            if (q - p < sizeof(session->header.host) - 1)
                memcpy(session->header.host, p, q - p);
            else
                memcpy(session->header.host, p, sizeof(session->header.host) - 1);

            p = q + 1;
            while (*p == ' ')
                p++;
			q = strpbrk(p, " \r\n");
			if (q == NULL)
				continue;
            if (q - p < sizeof(session->header.request_url) - 1)
                memcpy(session->header.request_url, p, q - p);
            else
                memcpy(session->header.request_url, p, sizeof(session->header.request_url) - 1);
		}
		else if (strncasecmp(session->msg_buf, "Accept:", strlen("Accept:")) == 0)
		{
            p = session->msg_buf + strlen("Accept:");
            while (*p == ' ')
                p++;
			q = strpbrk(p, " \r\n");
			if (q == NULL)
				continue;
            if (q - p < sizeof(session->header.accept_type) - 1)
                memcpy(session->header.accept_type, p, q - p);
            else
                memcpy(session->header.accept_type, p, sizeof(session->header.accept_type) - 1);
		}
		else if (strncasecmp(session->msg_buf, "Accept-Encoding:", strlen("Accept-Encoding:")) == 0)
		{
            p = session->msg_buf + strlen("Accept-Encoding:");
            while (*p == ' ')
                p++;
			q = strpbrk(p, " \r\n");
			if (q == NULL)
				continue;
            if (q - p < sizeof(session->header.accept_encoding) - 1)
                memcpy(session->header.accept_encoding, p, q - p);
            else
                memcpy(session->header.accept_encoding, p, sizeof(session->header.accept_encoding) - 1);
		}
		else if (strncasecmp(session->msg_buf, "Content-Type:", strlen("Content-Type:")) == 0)
		{
            p = session->msg_buf + strlen("Content-Type:");
            while (*p == ' ')
                p++;
			q = strpbrk(p, " ;\r\n");
			if (q == NULL)
				continue;
            if (q - p < sizeof(session->header.content_type) - 1)
                memcpy(session->header.content_type, p, q - p);
            else
                memcpy(session->header.content_type, p, sizeof(session->header.content_type) - 1);

            p = strstr(session->msg_buf, "boundary=");
            if (p == NULL)
                continue;
            p += strlen("boundary=");
			q = strpbrk(p, " \r\n");
			if (q == NULL)
				continue;
            if (q - p < sizeof(session->header.boundary) - 1)
                memcpy(session->header.boundary, p, q - p);
            else
                memcpy(session->header.boundary, p, sizeof(session->header.boundary) - 1);
		}
		else if (strncasecmp(session->msg_buf, "Content-Length:", strlen("Content-Length:")) == 0)
		{
			p = strchr(session->msg_buf + strlen("Content-Length:"), ' ');
            if (p != NULL)
                session->header.content_length = atoll(p + 1);
		}
		else if (strncasecmp(session->msg_buf, "Connection:", strlen("Connection:")) == 0)
        {
            p = session->msg_buf + strlen("Connection:");
            while (*p == ' ')
                p++;
            q = strpbrk(p, " \r\n");
            if (q == NULL)
                continue;
            if (q - p < sizeof(session->header.connection) - 1)
                memcpy(session->header.connection, p, q - p);
            else
                memcpy(session->header.connection, p, sizeof(session->header.connection) - 1);
        }
		else if (strncasecmp(session->msg_buf, "Range:", strlen("Range:")) == 0)
        {
            p = session->msg_buf + strlen("Range:");
            while (*p == ' ')
                p++;
            sscanf(p, "bytes=%"LLD"-%"LLD, &session->header.range_start, &session->header.range_end);
        }
       else if (strncasecmp(session->msg_buf, "Authorization:", strlen("Authorization:")) == 0)
       {
           authinfo_t *p_auth = &session->header.auth;
           //TODO parse alg?
           parse_value(session->msg_buf, "username=", p_auth->user, 64);
           parse_value(session->msg_buf, "realm=", p_auth->realm, 128);
           parse_value(session->msg_buf, "qop=", p_auth->qop, 10);
           parse_value(session->msg_buf, "nonce=", p_auth->nonce, 64);
           parse_value(session->msg_buf, "uri=", p_auth->uri, 256);
           parse_value(session->msg_buf, "opaque=", p_auth->opaque, 64);
           parse_value(session->msg_buf, "qop=", p_auth->qop, 10);

           parse_value(session->msg_buf, "nc=", p_auth->nc, 9);
           parse_value(session->msg_buf, "cnonce=", p_auth->cnonce, 64);
           parse_value(session->msg_buf, "response=", p_auth->response, sizeof(p_auth->response));

           sn_log_debug("authinfo, user:%s,realm:%s,nonce:%s,uri:%s,qop:%s, opaque:%s,nc:%s,cnonce:%s, "
                   "response:%s\n", p_auth->user, p_auth->realm, p_auth->nonce, p_auth->uri, p_auth->qop,
                   p_auth->opaque, p_auth->nc, p_auth->cnonce, p_auth->response);
       }
	}

    sn_log_debug("method:%s\n", session->header.method);
    sn_log_debug("url:%s\n", session->header.request_url);
    sn_log_debug("host:%s\n", session->header.host);
    sn_log_debug("accept type:%s\n", session->header.accept_type);
    sn_log_debug("connection:%s\n", session->header.connection);
    sn_log_debug("content type:%s\n", session->header.content_type);
    sn_log_debug("boundary:%s\n", session->header.boundary);
    sn_log_debug("content length:%d\n", session->header.content_length);
    sn_log_debug("range start:%"LLD", range end:%"LLD"\n", session->header.range_start, session->header.range_end);

	return session->header.header_length;
}


//TODO use int64_t for content_length
static int send_header(session_t *session, int response_num, const char *content_type,
        const char *content_encoding, const char *connection, int64_t content_length, const char *addition_info)
{
	char *buf = session->msg_buf;
	const char *responseString;
	time_t timer = time(0);
	char timeStr[80];
	int i, len;

    responseString = response2msg(response_num);

	/* emit the current date */
	strftime(timeStr, sizeof(timeStr), RFC1123FMT, gmtime(&timer));

	len = sprintf(buf,
				  "HTTP/1.1 %d %s\r\n"
				  "Date: %s\r\n",
				  response_num, responseString, timeStr);
    if (response_num == 401)
    {
        srand((unsigned)time( NULL ) + rand()*2);
        len += sprintf(buf+len, "WWW-Authenticate: Digest realm=\"%s\", qop=\"auth\", nonce=\"%d\", "
                "opaque=\"abcd01082883008ab01082883008abcd\"\r\n", addition_info, rand());
        addition_info = NULL; //ATTENTION
    }
	if (content_type)
		len += sprintf(buf + len, "Content-type: %s\r\n", content_type);
	if (content_encoding)
		len += sprintf(buf + len, "Content-Encoding: %s\r\n", content_encoding);
	if (content_length)
		len += sprintf(buf + len, "Content-Length: %"LLD"\r\n", content_length);
	if (connection)
		len += sprintf(buf + len, "Connection: %s\r\n", connection);
    if (addition_info)
        len += sprintf(buf + len, "%s\r\n", addition_info);

	len += sprintf(buf + len, "\r\n");

    sn_log_debug("%s\n", buf);

	i = 0;
	while (i < len)
		i += send(session->skt, buf + i, len - i, 0);

	return len;
}


static int send_msg(session_t *session, int response_num, const char *msg)
{
    int size;
    size = strlen(msg);
    send_header(session, response_num, TEXT_ENCODING, NULL, NULL, size, NULL);
    sn_log_debug("%s\n", msg);
    send(session->skt, msg, size, 0);
    return 0;
}


static void send_file(session_t *session, const char *abspath)
{
    file_t *fp;
    int64_t fsize;
    int64_t total_send_size = 0;
    char sendbuf[DATA_BUF_SIZE];
    char tmp_buf[SN_MAX_PATH + 128];
    int read_size;
    int send_size;
    int ret;

    if (rule_match(session->info.ip, RULEOPT_HTTP_DOWN, abspath) == 0)
        goto FAIL;

    fsize = fs_get_filesize(abspath);
    if (fsize < 0)
    {
        sn_log_error("invalid file size:%"LLD"\n", fsize);
        return;
    }
    session->info.total_size = fsize;

    fp = fs_open(abspath, FM_READ);
    if (fp == NULL)
    {
        sn_log_error("open file FAILED! %s\n", abspath);
        send_header(session, 500, NULL, NULL, NULL, 0, NULL);
        goto FAIL;
    }

    {
    int i;
    int len;
    char *p;
    len = strlen(session->header.request_url);
    p = session->header.request_url + len;
    for (i = 0; i < len - 1; i++)
    {
        if (*(p - 1) == 0x2F /* "/" */)
            break;
        p--;
    }
    memset(tmp_buf, 0, sizeof(tmp_buf));
    //snprintf(tmp_buf, sizeof(tmp_buf) - 1, "Content-Disposition: attachment; filename=\"%s\"", p);
    snprintf(tmp_buf, sizeof(tmp_buf) - 1, "Content-Disposition: filename=\"%s\"", p);
    }

    if (session->header.range_start == 0)
        send_header(session, 200, get_file_mime(abspath), NULL, NULL, fsize, tmp_buf);
    else
    {
        if (session->header.range_start >= fsize)
            send_msg(session, 400, "Invalid Range!");
        else
        {
            if (fs_seek(fp, session->header.range_start, SEEK_SET) != 0)
                send_msg(session, 400, "Failed to seek file!");
            else
            {
                int len = strlen(tmp_buf);
                if (len > 0)
                {
                    strncpy(tmp_buf + len, "\r\n", sizeof(tmp_buf) - len - 1);
                    len += 2;
                }
                snprintf(tmp_buf + len, sizeof(tmp_buf) - len - 1, "Content-Range: bytes %"LLD"-%"LLD"/%"LLD,
                        session->header.range_start, fsize - 1, fsize);
                send_header(session, 206, get_file_mime(abspath), NULL, NULL,
                        fsize - session->header.range_start, tmp_buf);
            }
        }
    }

    for (; ;)
    {
        read_size = fs_read(fp, sendbuf, DATA_BUF_SIZE);
        if (read_size <= 0)
            break;
        send_size = 0;
        while (send_size < read_size)
        {
            ret = send(session->skt, sendbuf + send_size, read_size - send_size, 0);
            if (ret == -1)
                goto FAIL;
            send_size += ret;
            total_send_size += ret;
            session->info.curr_size += ret;

            if (session->exit_flag > 0)
            {
                sn_log_info("recv exit signal!\n");
                rule_add(session->info.ip, 0xFFFFFFFF, RULEOPT_HTTP_DOWN, 120, abspath);
                goto FAIL;
            }
        }
    }
    sn_log_info("send size:%"LLD"\n", total_send_size);

FAIL:
    if (fp != NULL)
        fs_close(fp);
    closesocket(session->skt);
    session->skt = -1;
}


static int fill_templete(const char *templete, const char *fields[],
        const int lens[], const char *values[], char buf[], int bufsize)
{
    const char *pt1, *pt2;
    char *pb;
    int use_size;
    int session_size;
    int flag;
    int i;

    pb = buf;
    use_size = 0;
    pt1 = templete;
    while ((pt2 = strstr(pt1, TEMPLETE_FIELD_HEAD)) != NULL)
    {
        session_size = pt2 - pt1;
        if (use_size + session_size > bufsize)
            return -1;
        memcpy(pb, pt1, session_size);
        use_size += session_size;
        pb += session_size;

        flag = 0;
        i = 0;
        while (fields[i] != NULL)
        {
            if (strncmp(fields[i], pt2, lens[i]) == 0)
            {
                flag = 1;
                break;
            }
            i++;
        }
        if (flag == 1)
        {
            session_size = strlen(values[i]);
            if (use_size + session_size > bufsize)
                return -1;
            memcpy(pb, values[i], session_size);
            use_size += session_size;
            pb += session_size;
            pt1 = pt2 + strlen(fields[i]);
        }
        else
        {
            char field_name[64];

            pt1 = strstr(pt2, TEMPLETE_FIELD_END);
            if (pt1 == NULL)
            {
                sn_log_error("incomplete templete field:%s\n", pt2);
                break;
            }
            pt1 += strlen(TEMPLETE_FIELD_END);

            //for log print only
            memset(field_name, 0, 64);
            session_size = pt1 - pt2;
            if (session_size > 64)
                session_size = 64;
            memcpy(field_name, pt2, session_size);
            sn_log_error("miss matched templete field:%s!\n", field_name);
        }
    }

    if (pt1 != NULL)
    {
        session_size = strlen(pt1);
        if (use_size + session_size > bufsize)
            return -1;
        memcpy(pb, pt1, session_size);
        use_size += session_size;
    }

    return use_size;
}


static void send_dir(session_t *session, const char *path, const char *fullpath,
        int sort_type, int sort_order)
{
    fileinfo_t *infos;
    fileinfo_t *info;
    char sendbuf[DATA_BUF_SIZE];
    char url_raw[1024];
    char url_encoded[1024];
    char last_path[1024];
    char timestr[32];
    char fsizestr[32];
    int fsize;
    const char *pt;
    int ret;
    int i;

    const char *header_footer_fields[4] = {
        TEMPLETE_FIELD_HEAD"_PATH_"TEMPLETE_FIELD_END,
        TEMPLETE_FIELD_HEAD"_LASTPATH_"TEMPLETE_FIELD_END,
        TEMPLETE_FIELD_HEAD"_SERVERNAME_"TEMPLETE_FIELD_END,
        NULL
    };
    const char *header_footer_values[4];
    int header_footer_lens[4];

    const char *file_dir_fields[5] = {
        TEMPLETE_FIELD_HEAD"_NAME_"TEMPLETE_FIELD_END,
        TEMPLETE_FIELD_HEAD"_URL_"TEMPLETE_FIELD_END,
        TEMPLETE_FIELD_HEAD"_TIME_"TEMPLETE_FIELD_END,
        TEMPLETE_FIELD_HEAD"_SIZE_"TEMPLETE_FIELD_END,
        NULL
    };
    const char *file_dir_values[5];
    int file_dir_lens[5];

    memset(last_path, 0, 1024);
    strncpy(last_path, path, 1023);
    for (i = strlen(last_path) - 2; i >= 0; i--)
    {
        if (last_path[i] == 0x2F /* "/" */)
        {
            last_path[i + 1] = '\0';
            break;
        }
    }
    header_footer_values[0] = path;
    header_footer_values[1] = last_path;
    header_footer_values[2] = SERVER_NAME;
    header_footer_values[3] = NULL;
    for (i = 0; i < 3; i++)
        header_footer_lens[i] = strlen(header_footer_fields[i]);

    if (fs_get_fileinfos(session->p_server->fs, fullpath, &infos) != 0)
    {
        send_header(session, 500, NULL, NULL, NULL, 0, NULL);
        goto FAIL;
    }
    fs_sort_fileinfos(&infos, sort_type, sort_order);

    //TODO real size?
    send_header(session, 200, TEXT_ENCODING, NULL, NULL, 0, NULL);

    fsize = 0;

    if ((ret = fill_templete(session->p_server->templete_header, header_footer_fields,
                header_footer_lens, header_footer_values, &sendbuf[fsize], DATA_BUF_SIZE - fsize)) < 0)
    {
        sn_log_error("templete header is too long!\n");
        goto FAIL;
    }
    fsize += ret;

    info = infos;
	while (info != NULL)
    {
        if (info->name == NULL || strcmp(info->name, ".") == 0 || strcmp(info->name, "..") == 0)
        {
            info = info->p_next;
            continue;
        }

        //ATTENTION
        if (info->type == FT_DIR || info->type == FT_VDIR)
            strcat(info->name, "/");

        snprintf(url_raw, 1024, "%s%s", path, info->name);
        url_encode(url_raw, url_encoded, 1024);

        file_dir_values[0] = info->name;
        file_dir_values[1] = url_encoded;

        time2str(&info->timestamp, timestr);
        file_dir_values[2] = timestr;

        size2str(info->size, fsizestr);
        file_dir_values[3] = fsizestr;

        file_dir_values[4] = NULL;

        for (i = 0; i < 3; i++)
            file_dir_lens[i] = strlen(file_dir_fields[i]);
        file_dir_lens[i] = 0;

        if (info->type == FT_DIR || info->type == FT_VDIR)
            pt = session->p_server->templete_dir;
        else
            pt = session->p_server->templete_file;

        if ((ret = fill_templete(pt, file_dir_fields, file_dir_lens, file_dir_values,
                        &sendbuf[fsize], DATA_BUF_SIZE - fsize)) < 0)
        {
            sendbuf[fsize] = '\0';
            fsize++;
            send(session->skt, sendbuf, fsize, 0);
            fsize = 0;

            if ((ret = fill_templete(pt, file_dir_fields, file_dir_lens, file_dir_values,
                            &sendbuf[fsize], DATA_BUF_SIZE - fsize)) < 0)
            {
                sn_log_error("templete dir is too long!\n");
                goto FAIL;
            }
        }
        fsize += ret;

        info = info->p_next;
	}

    if ((ret = fill_templete(session->p_server->templete_footer, header_footer_fields,
                header_footer_lens, header_footer_values, &sendbuf[fsize], DATA_BUF_SIZE - fsize)) < 0)
    {
        sendbuf[fsize] = '\0';
        fsize++;
        send(session->skt, sendbuf, fsize, 0);
        fsize = 0;

        if ((ret = fill_templete(session->p_server->templete_footer, header_footer_fields,
                        header_footer_lens, header_footer_values, &sendbuf[fsize],
                        DATA_BUF_SIZE - fsize)) < 0)
        {
            sn_log_error("templete footer is too long!\n");
            goto FAIL;
        }
    }
    fsize += ret;

    sendbuf[fsize] = '\0';
    fsize++;
    send(session->skt, sendbuf, fsize, 0);

FAIL:
    closesocket(session->skt);
    session->skt = -1;

    fs_free_fileinfos(infos);
}


static void send_post_result(session_t *session, const char *path, const char *file,
        int succeed, const char *msg)
{
    char buf[1024 * 2];
    int size;
    const char *pt;
    int i;

    const char *result_fields[4] = {
        TEMPLETE_FIELD_HEAD"_UPLOADPATH_"TEMPLETE_FIELD_END,
        TEMPLETE_FIELD_HEAD"_UPLOADFILE_"TEMPLETE_FIELD_END,
        TEMPLETE_FIELD_HEAD"_UPLOADRESULTMSG_"TEMPLETE_FIELD_END,
        NULL
    };
    const char *result_values[4];
    int result_lens[4];

    for (i = 0; i < 3; i++)
        result_lens[i] = strlen(result_fields[i]);
    result_values[0] = (path == NULL ? "" : path);
    result_values[1] = (file == NULL ? "" : file);
    result_values[2] = (msg == NULL ? "" : msg);

    if (succeed == 1)
        pt = session->p_server->templete_upok;
    else
        pt = session->p_server->templete_upfailed;

    if ((size = fill_templete(pt, result_fields, result_lens, result_values, buf, 1024 * 2)) < 0)
    {
        sn_log_error("templete upload result is too long!\n");
        return;
    }
    //buf[size] = '\0';
    //size++;

    send_header(session, 200, TEXT_ENCODING, NULL, NULL, size, NULL);
    send(session->skt, buf, size, 0);
}


static void process_post(session_t *session)
{
    char filename[1024];
    char fullpath[1024];
    char abspath[1024];
    file_t *fp;
    int blen;
    int i, j;

    char recv_buf[DATA_BUF_SIZE];
    int buf_pos;
    int64_t expect_size;
    int64_t total_recv_size;
    int recv_size;

    send_event(SEVENT_HTTP_POST, &session->info, 0, 0);

    blen = strlen(session->header.boundary);
    if (blen == 0)
    {
        send_msg(session, 400, "No boundary specified!");
        goto FAIL;
    }

    //boundary
    if (get_line(session) <= 0)
    {
        send_msg(session, 400, "Read boundary FAILED!");
        goto FAIL;
    }
    if (strstr(session->msg_buf, session->header.boundary) == 0)
    {
        sn_log_error("can not find boundary!\n");
        send_msg(session, 400, "Can not find boundary!");
        goto FAIL;
    }

    //Content-Disposition
    if (get_line(session) <= 0)
    {
        send_msg(session, 400, "Read Content-Disposition FAILED!");
        goto FAIL;
    }
    if (strncasecmp(session->msg_buf, "Content-Disposition:", strlen("Content-Disposition:")) != 0)
    {
        send_msg(session, 400, "Invalid Content-Disposition!");
        goto FAIL;
    }


    if (parse_value(session->msg_buf, "filename=", filename, 1024) != 0)
    {
        send_msg(session, 400, "Can not find filename!");
        goto FAIL;
    }
    if (strlen(filename) <= 0)
    {
        send_msg(session, 400, "Invalid filename!");
        goto FAIL;
    }

    i = 0, j = -1;
    while (filename[i] != 0)
    {
        if (filename[i] == 0x5C/* "\" */ || filename[i] == 0x2F/* "/" */)
            j = i;
        i++;
    }
    if (j >= 0)
        memmove(filename, filename + j + 1, 1024 - j - 1);
    
    sn_log_debug("filename:%s\n", filename);
    strncpy(session->info.fname, filename, SN_MAX_PATH - 1);

    //goto real file start
    i = 0;
    while (i < 10 && get_line(session) > 0)
    {
        if (strcmp(session->msg_buf, "\r\n") == 0)
            break;
        i++;
    }
    if (i >= 10)
    {
        send_msg(session, 400, "Can not find file start!");
        goto FAIL;
    }

    snprintf(fullpath, 1024, "%s%s", session->header.request_url, filename);
    if (filesys_get_absolute_path(session->p_server->fs, fullpath, abspath) != 0)
    {
        sn_log_error("filesys_get_absolute_path FAILED!\n");
        send_msg(session, 404, "Url not fond!");
        goto FAIL;
    }
    sn_log_debug("file:%s\n", abspath);

    if (rule_match(session->info.ip, RULEOPT_HTTP_UP, abspath) == 0)
        goto FAIL;

    fp = fs_open(abspath, FM_WRITE);
    if (fp == NULL)
    {
        sn_log_error("open file FAILED! %s\n", abspath);
        send_msg(session, 403, "FAILED to open write file!");
        goto FAIL;
    }

    //TODO quta check

    expect_size = session->header.content_length - session->header.header_length;
    total_recv_size = 0;
    recv_size = 0;
    buf_pos = 0;
    session->info.total_size = expect_size;
    for (; ;)
    {
        recv_size = recv(session->skt, recv_buf, DATA_BUF_SIZE - buf_pos, 0);
        if (recv_size <= 0)
            break;
        total_recv_size += recv_size;
        buf_pos += recv_size;
        session->info.curr_size = total_recv_size;

        if (total_recv_size >= expect_size)
            break;

        if (expect_size - total_recv_size <= blen + 2)
            continue;

        if (fs_write(fp, recv_buf, buf_pos) == 0)
        {
            sn_log_error("file write error!\n");
            send_msg(session, 500, "Write file error!");
            fs_close(fp);
            fs_delete(abspath);
            goto FAIL;
        }
        buf_pos = 0;

        if (session->exit_flag > 0)
        {
            sn_log_info("recv exit signal!\n");
            rule_add(session->info.ip, 0xFFFFFFFF, RULEOPT_HTTP_UP, 120, abspath);
            goto FAIL;
        }
    }

    *(recv_buf+buf_pos) = 0;
    if (strncmp(recv_buf + buf_pos - blen - 4, session->header.boundary, blen) != 0)
    {
        sn_log_error("unexpected file ends!!!\n");
        send_msg(session, 400, "Unexpected file ends!");
        goto FAIL;
    }
    fs_write(fp, recv_buf, buf_pos - blen - 4);

    fs_close(fp);

    send_post_result(session, session->header.request_url, filename, 1, "");
    send_event(SEVENT_HTTP_POST, &session->info, (unsigned long)filename, 1);
    return;

FAIL:
    send_event(SEVENT_HTTP_POST, &session->info, 0, 2);
}


//type: 0-->read, 1-->write
static int check_auth(session_t *session, int type)
{
    hashhex_t ha1;
    hashhex_t ha2 = "";
    hashhex_t right_response;
    authinfo_t *p_auth = &session->header.auth;
    const char *p_pass;
    uint32_t mask;

    p_pass = access_get_pswd(p_auth->user);
    if (p_pass == NULL)
        return -1;

    memset(&ha1, 0, sizeof(hashhex_t));
    memset(&right_response, 0, sizeof(hashhex_t));

    strcpy(p_auth->alg, "md5");
    digest_calc_ha1(p_auth->alg, p_auth->user, p_auth->realm, p_pass,
            p_auth->nonce, p_auth->cnonce, ha1);
	digest_calc_response(ha1, p_auth->nonce, p_auth->nc, p_auth->cnonce, p_auth->qop,
            session->header.method, p_auth->uri, ha2, right_response);

	if (strcmp(p_auth->response, right_response) != 0)
		return -1;

    mask = 0;
    if (access_get_auth_state(p_auth->user, p_pass, &mask) != 0)
        return -1;
    if (type == 0 && (mask & E_AUTH_HTTP_READE) == 0)
        return -1;
    if (type == 1 && (mask & E_AUTH_HTTP_WRITE) == 0)
        return -1;

    return 0;
}


//thread proc
static void* httpsession_process(void *param)
{
    session_t *session = (session_t *)param;
    struct timeval timeout;
	fd_set r;
    //int unblock = 1;

    time(&session->start_time);
    sn_log_info("session begin...\n");

    //其实这个没什么用
    if (session->exit_flag == 1)
    {
        sn_log_info("recv exit signal!\n");
        goto EXIT;
    }

    //ATTENTION 不能设置为非阻塞模式, 否则POST处理会有问题
    //设置为非阻塞模式 //ioctl(session->skt, FIONBIO, (int)&unblock);

    timeout.tv_sec = 0;
    timeout.tv_usec = 1000 * 200;
    FD_ZERO(&r);
    FD_SET(session->skt, &r);

    if (select(session->skt + 1, &r, NULL, NULL, &timeout) <= 0)
        goto EXIT;

    if (FD_ISSET(session->skt, &r))
    {
        if (recv_header(session) > 0)
        {
            int sort_type = 0;
            char *p;
            if ((p = strchr(session->header.request_url, '?')) != NULL)
            {
                //TODO
                if (strncmp(p + 1, "sort=name", strlen("sort=name")) == 0)
                    sort_type = 0;
                else if (strncmp(p + 1, "sort=size", strlen("sort=size")) == 0)
                    sort_type = 1;
                else if (strncmp(p + 1, "sort=date", strlen("sort=date")) == 0)
                    sort_type = 2;
                *p = '\0';
            }

            if (strncasecmp(session->header.method, "GET", strlen("GET")) == 0)
            {
                char abspath[1024];
                fileinfo_t finfo;
                int is_templete_file = 0;

                if (filesys_get_absolute_path(session->p_server->fs, session->header.request_url, abspath) != 0)
                {
                    sn_log_error("filesys_get_absolute_path FAILED!\n");
                    goto EXIT;
                }
                sn_log_debug("file:%s\n", abspath);

                if (fs_get_fileinfo(NULL, abspath, &finfo) != 0)
                {
                    if (session->p_server->templete_fs == NULL || filesys_get_absolute_path(session->p_server->templete_fs,
                                session->header.request_url, abspath) != 0)
                    {
                        sn_log_error("filesys_get_absolute_path FAILED!\n");
                        goto EXIT;
                    }
                    sn_log_debug("templete file:%s\n", abspath);

                    if (fs_get_fileinfo(NULL, abspath, &finfo) != 0)
                    {
                        send_header(session, 404, NULL, NULL, NULL, 0, NULL);
                        goto EXIT;
                    }

                    is_templete_file = 1;
                }

                //TODO
                //if (!access)
                //    send_header(session, 403, NULL, NULL, NULL, 0, NULL);

                if (finfo.type == FT_FILE)
                {
                    if (session->p_server->allow_anonymous == 0 && check_auth(session, 0) != 0)
                    {
                        send_header(session, 401, NULL, NULL, NULL, 0, "HttpDigestAuthentication");
                        goto EXIT;
                    }

                    if (is_templete_file == 0)
                        send_event(SEVENT_HTTP_GET, &session->info, (unsigned long)abspath, 0);
                    session->info.file_flag = 2;
                    if (session->header.request_url[0] == 0x2F)
                        strncpy(session->info.fname, session->header.request_url + 1 /* skip "/" */, SN_MAX_PATH - 1);
                    else
                        strncpy(session->info.fname, session->header.request_url, SN_MAX_PATH - 1);
                    time(&session->info.file_start);

                    send_file(session, abspath);

                    session->info.file_flag = 0;
                    if (is_templete_file == 0)
                        send_event(SEVENT_HTTP_GET, &session->info, (unsigned long)abspath, 1);
                }
                else if (finfo.type == FT_DIR || finfo.type == FT_VDIR)
                {
                    if (session->p_server->allow_anonymous == 0 && check_auth(session, 0) != 0)
                    {
                        send_header(session, 401, NULL, NULL, NULL, 0, "HttpDigestAuthentication");
                        goto EXIT;
                    }

                    int len = strlen(session->header.request_url);
                    if (len > 0 && session->header.request_url[len - 1] != '/')
                    {
                        session->header.request_url[len] = '/';
                        session->header.request_url[len + 1] = '\0';
                    }
                    send_dir(session, session->header.request_url, abspath, sort_type, 0);
                }
            }
            else if (strncasecmp(session->header.method, "POST", strlen("POST")) == 0)
            {
                if (session->p_server->anonymous_writable == 0 && check_auth(session, 1) != 0)
                {
                    send_header(session, 401, NULL, NULL, NULL, 0, "HttpDigestAuthentication");
                    goto EXIT;
                }

                session->info.file_flag = 1;
                memset(session->info.fname, 0, SN_MAX_PATH);
                time(&session->info.file_start);

                process_post(session);

                session->info.file_flag = 0;
            }
        }
    }

EXIT:
    sn_log_info("session end!\n");
    closesocket(session->skt);
    time(&session->complete_time);
    session->status = SESSION_STATUS_NULL;
    return NULL;
}


//check and remove completed in session pool
static void cleanup()
{
    session_t *p_session;
    time_t now;
    int i;

    time(&now);
    for (i = 0; i < MAX_SESSIONS; i++)
    {
        p_session = &m_session_pool[i];
        if (p_session->status == SESSION_STATUS_COMPLETE)
            p_session->status = SESSION_STATUS_NULL;
        if (p_session->status == SESSION_STATUS_ERROR)
        {
            sn_log_error("found error session!\n");
            print_session(p_session);
            p_session->status = SESSION_STATUS_NULL;
        }
        //if (p_session->status == SESSION_STATUS_INPROCESS && now - p_session->update_time > SESSION_TIMEOUT)
        //{
        //    sn_log_info("session timeout!\n");
        //    print_session(p_session);
        //    p_session->status = SESSION_STATUS_NULL;
        //}
    }
}


int httpsession_add(const struct _httpserver *p_server, int skt, uint32_t ip)
{
    session_t *curr = NULL;
    int i;

    cleanup();

    for (i = 0; i < MAX_SESSIONS; i++)
    {
        if (m_session_pool[i].status == SESSION_STATUS_NULL)
        {
            curr = &m_session_pool[i];
            break;
        }
    }
    if (curr == NULL)
    {
        sn_log_error("max client number (%d) reached!\n", MAX_SESSIONS);
        return -1;
    }

    memset(curr, 0, sizeof(session_t));
    curr->p_server = p_server;
    curr->skt = skt;
    curr->info.ip = htonl(ip);
    curr->status = SESSION_STATUS_INPROCESS;
    curr->info.session_id = m_session_count;
    m_session_count++;
    if (run_thread(httpsession_process, curr) != 0)
    {
        curr->status = SESSION_STATUS_NULL;
        return -1;
    }

    return 0;
}


void httpsession_clear(const struct _httpserver *p_server)
{
    int alive_count;
    int count;
    int i;
    count = 0;
    while (count < 500)
    {
        alive_count = 0;
        for (i = 0; i < MAX_SESSIONS; i++)
        {
            if (m_session_pool[i].p_server == p_server && m_session_pool[i].status != SESSION_STATUS_NULL)
            {
                m_session_pool[i].exit_flag = 1;
                alive_count++;
            }
        }
        if (alive_count == 0)
            break;
        count++;
        usleep(10 * 1000);
    }
    if (count == 500)
        sn_log_debug("clear sessions FAILED!\n");
}


int httpsession_kill(uint32_t id, int transonly)
{
    int i;
    for (i = 0; i < MAX_SESSIONS; i++)
    {
        if (m_session_pool[i].info.session_id == id)
        {
            m_session_pool[i].exit_flag = 1;
            return 0;
        }
    }
    return -1;
}


static const char* response2msg(int code)
{
	int i;

	static const struct
    {
		int code;
		const char *msg;
	}
    translation[] =
    {
		{100, "Continue"},
		{101, "Switching Protocols"},
		{102, "Processing"},
		{200, "OK"},
		{201, "Created"},
		{202, "Accepted"},
		{203, "Non-Authoritative Information"},
		{204, "No Content"},
		{205, "Reset Content"},
		{206, "Partial Content"},
		{207, "Multi-Status"},
		{300, "Multiple Choices"},
		{301, "Moved Permanently"},
		{302, "Found"},
		{303, "See Other"},
		{304, "Not Modified"},
		{305, "Use Proxy"},
		{307, "Temporary Redirect"},
		{400, "Bad Request"},
		{401, "Unauthorized"},
		{402, "Payment Required"},
		{403, "Forbidden"},
		{404, "Not Found"},
		{405, "Method Not Allowed"},
		{406, "Not Acceptable"},
		{407, "Proxy Authentication Required"},
		{408, "Request Timeout"},
		{409, "Conflict"},
		{410, "Gone"},
		{411, "Length Required"},
		{412, "Precondition Failed"},
		{413, "Request Entity Too Large"},
		{414, "URI too long"},
		{415, "Unsupported Media Type"},
		{416, "Requested Range Not Satisfiable"},
		{417, "Expectation Failed"},
		{422, "Unprocessable Entity"},
		{423, "Locked"},
		{424, "Failed Dependency"},
		{426, "Upgrade Required"},
		{500, "Internal Server Error"},
		{501, "Not Implemented"},
		{502, "Bad Gateway"},
		{503, "Service Unavailable"},
		{503, "Gateway Timeout"},
		{504, "HTTP Version Not Supported"},
		{507, "Insufficient Storage"},
		{0,""}
	};
	for (i = 0; translation[i].code != 0; i++)
		if (translation[i].code == code)
			return translation[i].msg;
	return "unknown error code";
}


typedef struct _mime
{
    char *ext;
    char *mime;
} mime_t;

mime_t mime_table[] =
{
    {"nml", "animation/narrative"},
    {"aiff", "audio/x-aiff"},
    {"au", "audio/basic"},
    {"mid", "midi/mid"},
    {"mp3", "audio/x-mpg"},
    {"m3u", "audio/x-mpegurl"},
    {"wav", "audio/x-wav"},
    {"wma", "audio/x-ms-wma"},
    {"ra", "audio/x-realaudio"},
    {"ram", "audio/x-pn-realaudio"},

    {"bmp", "image/bmp"},
    {"gif", "image/gif"},
    {"jpg", "image/pjpeg"},
    {"jpeg", "image/jpeg"},
    {"png", "image/x-png"},
    {"svg", "image/svg-xml"},
    {"tif", "image/tiff"},
    {"ico", "image/x-icon"},
    {"psd", "image/x-psd"},
    {"xpm", "image/x-xpixmap"},

    {"xml", "text/xml"},
    {"txt", "text/plain"},

    {"avi", "video/x-msvideo"},
    {"mp4", "video/mpeg"},
    {"mpeg", "video/x-mpeg2a"},
    {"mpa", "video/mpeg"},
    {"mpg", "video/mpeg"},
    {"mov", "video/quicktime"},
    {"rv", "video/vnd.rn-realvideo"},
    {"wmv", "video/x-ms-wmv"},
    {"asx", "video/x-ms-asf-plugin"},
    {"asf", "video/x-ms-asf"},

    {"bat", "application/x-msdos-program"},
    {"bz2", "application/x-bzip2"},
    {"cab", "application/vnd.ms-cab-compressed"},
    {"chm", "application/vnd.ms-htmlhelp"},
    {"class", "application/java-vm"},
    {"com", "application/x-msdos-program"},
    {"deb", "application/x-debian-package"},
    {"dll", "application/x-msdownload"},
    {"dmg", "application/x-apple-diskimage"},

    {"doc", "application/msword"},
    {"dot", "application/msword"},
    {"eps", "application/postscript"},
    {"exe", "application/x-msdownload"},
    {"gtar", "application/x-gtar"},
    {"gz", "application/x-gzip"},
    {"hlp", "application/winhlp"},
    {"iso", "application/x-iso9660-image"},
    {"jar", "application/java-archive"},
    {"latex", "application/x-latex"},

    {"o", "application/x-object"},
    {"ogg", "application/ogg"},
    {"package", "application/vnd.autopackage"},
    {"pkg", "vnd.apple.installer+xml"},
    {"pdf", "application/pdf"},
    {"pot", "application/mspowerpoint"},
    {"pps", "application/mspowerpoint"},
    {"ppt", "application/mspowerpoint"},
    {"ppz", "application/mspowerpoint"},
    {"ps", "application/postscript"},
    {"rar", "application/rar"},
    {"rtsp", "application/x-rtsp"},
    {"rss", "application/rss+xml"},
    {"sh", "application/x-sh"},
    {"sdp", "application/x-sdp"},
    {"smi", "application/smil"},
    {"smil", "application/smil"},
    {"swf", "application/x-shockwave-flash"},
    {"tar", "application/x-tar"},
    {"tex", "application/x-tex"},
    {"tgz", "application/x-compressed"},
    {"torrent", "application/x-bittorrent"},
    {"wms", "application/x-ms-wms"},
    {"z", "application/x-compress"},
    {"zip", "application/x-zip-compressed"},
    {"wml", "text/vnd.wap.wml"},
    {"java", "text/x-java"},
    {"css", "text/css"},
    {"js", "text/javascript"},
    {"htm", "text/html"},
    {"html", "text/html"},
    {"shtml", "server-parsed-html"},
    {"dib", "image/bmp"},
    {"eml", "message/rfc822"},
    {"m1v", "video/mpeg"},
    {"mht", "message/rfc822"},
    {"mhtml", "message/rfc822"},
    {"midi", "audio/mid"},
    {"mp2v", "video/mpeg"},
    {"mpv2", "video/mpeg"},
    {"tiff", "image/tiff"},
    {"xsl", "text/xml"},
};

static const char* get_file_mime(const char *path)
{
    const char *p_mime;
    char ext[16];
    int len;
    int i;

    len = strlen(path);
    memset(ext, 0, 16);
    for (i = 0; i < 15 && i < len; i++)
    {
        if (path[len - 1 - i] == '.')
        {
            strncpy(ext, path + len - i, 15);
            break;
        }
    }
    p_mime = NULL;
    if (strlen(ext) <= 0)
    {
        sn_log_debug("can not find file extension!\n", __FUNCTION__);
        goto END;
    }
    for (i = 0; i < sizeof(mime_table) / (sizeof(mime_table[0])); i++)
    {
        if (strcasecmp(ext, mime_table[i].ext) == 0)
        {
            p_mime = mime_table[i].mime;
            break;
        }
    }
END:
    if (p_mime == NULL)
        p_mime = "application/octet-stream";

    sn_log_debug("file ext:%s, mime:%s\n", ext, p_mime);

    return p_mime;
}


void http_get_session_info(void (*cb_session_info)(void *context, int type,
    uint32_t ip, const char *user, time_t start, int fflag, time_t fstart,
    uint64_t totalsize, uint64_t currsize, const char *fname, uint32_t session_id),
    void *context)
{
    session_t s;
    int i;
    for (i = 0; i < MAX_SESSIONS; i++)
    {
        if (m_session_pool[i].status != SESSION_STATUS_NULL)
        {
            s = m_session_pool[i];
            if (memcmp(&s, &m_session_pool[i], sizeof(session_t)) == 0)
            {
                cb_session_info(context, 0, s.info.ip, NULL, s.start_time, s.info.file_flag,
                        s.info.file_start, s.info.total_size, s.info.curr_size, s.info.fname,
                        s.info.session_id);
            }
        }
    }
}

