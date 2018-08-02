#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h> //for close
#include <ctype.h> //for tolower

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
#include "ftpserver.h"
#include "ftpsession.h"


#define MSG_MODULE "FTPSESSION"
#define SESSION_TIMEOUT 120 //in seconds
#define MSG_BUF_SIZE 512
#define SEND_BUF_SIZE 8196 //注意, 栈内存分配, 不要太大
#define MAX_SESSIONS 128
#define SERVER_NAME "quickshare ftpd"

typedef enum
{
    SESSION_STATUS_NULL,
    SESSION_STATUS_INPROCESS,
    SESSION_STATUS_COMPLETE,
    SESSION_STATUS_ERROR,

} e_status;

typedef struct _session 
{
    const ftpserver_t *p_server;
    int cmd_skt;
    int data_skt;
    char msg_buf[MSG_BUF_SIZE];
    char msg_buf_recv[MSG_BUF_SIZE];
    filesys_t *fs;

    int exit_flag; //标记为1时退出, 2时中断当前传输
    e_status status;

    e_auth_state auth_state;
    int auth_attempt;

    time_t start_time;
    time_t complete_time; //only valid on SESSION_STATUS_COMPLETE
    time_t update_time;

    uint32_t remote_ip;
    uint16_t remote_port;

    int64_t restart_marker;
    char rnfr[SN_MAX_PATH];

    int readable;
    int writable;

    ftp_session_info_t info;

} session_t;

static session_t m_session_pool[MAX_SESSIONS];
static uint32_t m_session_count = 0;

static const char month_table[12][4] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
    "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"}; 


//int ftpsession_init()
//{
//}


//void ftpsession_exit()
//{
//}


static void print_session(session_t *session)
{
}


static void clean_path(char *path)
{
    int len;
    int i;

    len = strlen(path);
    for (i = 0; i < len; i++)
    {
        if (path[i] == 0x0A/*LF*/ || path[i] == 0x0D/*CR*/)
        {
            path[i] = '\0';
            break;
        }
    }
}


static int print_fileinfo(fileinfo_t *info, char *buf/*out*/)
{
    time_t curr;
    struct tm st;
    int cyear;
    int len;
    char *p;
    int t;
    int i;

    p = buf;
    len = 0;

    strcat(buf, (info->type == FT_DIR || info->type == FT_VDIR)
            ? "d" : (info->type == FT_LINK) ? "l" : "-");
    for (i = 0; i < 9; i++)
    {
        if (info->protection & (1 << (8 - i)))
        {
            switch (i % 3)
            {
                case 0: strcat(buf, "r"); break;
                case 1: strcat(buf, "w"); break;
                case 2: strcat(buf, "x"); break;
            }
        }
        else
            strcat(buf, "-");
    }

    len += 10;
    p += 10;

    sprintf(p, " %4d %8s %8s %11"LLD" ", 1, "ftp", "ftp", info->size);
    t = strlen(p);
    len += t;
    p += t;


    time(&curr);
    //gmtime_r(&curr, &st);
    localtime_r(&curr, &st);
    cyear = st.tm_year;

    //gmtime_r(&info->timestamp, &st);
    localtime_r(&info->timestamp, &st);
    
    if (st.tm_year == cyear)
        sprintf(p, " %s %02i %02i:%02i ", month_table[st.tm_mon], st.tm_mday, st.tm_hour, st.tm_min);
    else
        sprintf(p, " %s %02i %5i ", month_table[st.tm_mon], st.tm_mday, st.tm_year + 1900);

    t = strlen(p);
    len += t;
    p += t;

    snprintf(p, MAX_FILENAME + 2, "%s\r\n", info->name);
    t = strlen(p);
    len += t;
    p += t;

    return len;
}


static void send_reply(session_t *session, int code, const char* msg)
{
    snprintf(session->msg_buf, 8192, "%d %s\r\n", code, msg);
    sn_log_debug("send reply:%s\n", session->msg_buf);
	send(session->cmd_skt, session->msg_buf, strlen(session->msg_buf), 0);
}


static void do_quit(session_t *session)
{
    closesocket(session->cmd_skt);
    if (session->data_skt != -1)
        closesocket(session->data_skt);
    if (session->fs != NULL)
        filesys_destory(session->fs);
    time(&session->complete_time);
    session->status = SESSION_STATUS_NULL;
    exit_thread();
}


static void on_cmd_user(session_t *session, const char *user)
{
    if (session->p_server->allow_anonymous && (strncasecmp(user, "ftp", strlen("ftp")) == 0
                || strncasecmp(user, "anonymous", strlen("anonymous")) == 0) )
	{
		//anonymous login, user authenticated
        session->auth_state = AUTHSTATE_VALID;
        session->readable = 1;
        session->writable = session->p_server->anonymous_writable;
        send_event(SEVENT_FTP_LOGIN, &session->info, 0, (unsigned long)user);
        send_reply(session, 230, "Login successful.");
	}
	else
	{
        session->auth_state = AUTHSTATE_PASSWORD;
        strncpy(session->info.username, user, 127);
        session->info.username[127] = '\0';
        send_reply(session, 331, "User name okay, need password.");
	}
}


static void on_cmd_pass(session_t *session, const char *pass)
{
    if (session->auth_state == AUTHSTATE_PASSWORD)
    {
        uint32_t mask = 0;
        if (access_get_auth_state(session->info.username, pass, &mask) == 0)
        {
            session->readable = mask & E_AUTH_FTP_READE;
            session->writable = mask & E_AUTH_FTP_WRITE;
            send_event(SEVENT_FTP_LOGIN, &session->info, 0, (unsigned long)session->info.username);
            send_reply(session, 230, "Login successful.");
            session->auth_state = AUTHSTATE_VALID;
            return;
        }
    }

    //password didn't match, or we had no valid login
    if (session->auth_state != AUTHSTATE_INVALID)
    {
        send_event(SEVENT_FTP_LOGIN, &session->info, 1, (unsigned long)session->info.username);
        send_reply(session, 530, "Login incorrect.");
        //disconnect client if more than 3 attempts to login has been made
        session->auth_attempt++;
        if (session->auth_attempt > 3)
            do_quit(session);
        session->auth_state = AUTHSTATE_INVALID;
    }
    else
        send_reply(session, 503, "Login with USER first.");
}


static void on_cmd_quit(session_t *session)
{
    send_event(SEVENT_FTP_QUIT, &session->info, 0, 0);
    send_reply(session, 221, "Goodbye.");
    do_quit(session);
}


static void on_cmd_pasv(session_t *session)
{
	struct sockaddr_in sa;
    int len;
    int s = -1;;
    int opt;
	unsigned char *p, *a;
    char msg[128];

	if ((s = socket(PF_INET, SOCK_STREAM, 0)) < 0)
        goto ERR;

    opt = 1;	
    if (setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0)
        goto ERR;

	//opt = IP_PORTRANGE_HIGH;
	//if (setsockopt(s, IPPROTO_IP, IP_PORTRANGE, &opt, sizeof(opt)) < 0)
    //    goto ERR;

    len = sizeof(sa);
    if (getsockname(session->cmd_skt, (struct sockaddr *)&sa, (socklen_t *)&len) < 0)
        goto ERR;

    sa.sin_port = 0;
	if (bind(s, (struct sockaddr *)&sa, sizeof(sa)) < 0)
        goto ERR;

    len = sizeof(sa);
    if (getsockname(s, (struct sockaddr *)&sa, (socklen_t *)&len) < 0)
        goto ERR;

    if (listen(s, 1) < 0)
        goto ERR;

	a = (unsigned char *)&sa.sin_addr.s_addr;
	p = (unsigned char *)&sa.sin_port;

    session->remote_port = 0; //TODO !!!!!!
    session->data_skt = s; //TODO !!!!!!

    snprintf(msg, 128, "Entering Passive Mode (%u,%u,%u,%u,%u,%u)",
            a[0], a[1], a[2], a[3], p[0], p[1]);
    send_reply(session, 227, msg);

    return;

ERR:
    send_reply(session, 425, "Can't open passive connection.");
    if (s != -1)
        closesocket(s);
}


static void on_cmd_port(session_t *session, int *ip, int port)
{
    if (port >= 1024)
    {
		// TODO: validate IP?
        session->remote_ip = (ip[0] << 24) | (ip[1] << 16) | (ip[2] << 8) | (ip[3]);
        session->remote_port = port;
        send_reply(session, 200, "PORT command successful. Consider using PASV.");
    }
    else
        send_reply(session, 500, "Illegal command.");
}


static void on_cmd_syst(session_t *session)
{
    send_reply(session, 215, "UNIX Type: L8");
}


static int get_data_socket(session_t *session)
{
    //port
    if (session->remote_port > 0)
    {
        int s;
        struct sockaddr_in sa;
        int opt;

        //TODO 保留连接一定时间, 避免重复连接断开?
        if (session->data_skt != -1)
            closesocket(session->data_skt);

        if ((s = socket(PF_INET, SOCK_STREAM, 0)) < 0)
        {
            sn_log_error("create socket FAILED!\n");
            return -1;
        }

        //use same local port on send data to client
        opt = 1;	
        if (setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0)
        {
           closesocket(s);
           sn_log_error("could not change socket options\n");
           return -1;
        }
        memset(&sa, 0, sizeof(sa));
        sa.sin_family = AF_INET;
        sa.sin_port = htons(session->p_server->data_port);
        sa.sin_addr.s_addr = INADDR_ANY;
        if (bind(s, (struct sockaddr*)&sa, sizeof(sa)) < 0)
        {
           sn_log_error("could not bind to port\n");
           closesocket(s);
           return -1;
        }

        memset(&sa,0,sizeof(sa));
        sa.sin_family = AF_INET;
        sa.sin_port = htons(session->remote_port);
        sa.sin_addr.s_addr = htonl(session->remote_ip);
        if (connect(s, (struct sockaddr*)&sa, sizeof(sa)) < 0)
        {
            sn_log_error("connect FAILED!\n");
            return -1;
        }

        return s;
    }
    //pasv
    else
    {
        struct timeval timeout; 
        fd_set r;
        fd_set w;
        int max_fd;

        max_fd = session->data_skt;

        timeout.tv_sec = 10; //TODO
        timeout.tv_usec = 0; 
        FD_ZERO(&r);
        FD_ZERO(&w);
        FD_SET(session->data_skt, &r);

        if (select(max_fd + 1, &r, &w, NULL, &timeout) > 0)
        {
            if (FD_ISSET(session->data_skt, &r))
            {
                struct sockaddr_in sa;
                socklen_t sl;
                int dskt;

                sl = sizeof(sa);
                if ((dskt = accept(session->data_skt, (struct sockaddr*)&sa, &sl)) >= 0)
                {
                    uint32_t addr;
                    uint16_t port;
                    addr = htonl(sa.sin_addr.s_addr);
                    port = htons(sa.sin_port);
                    //sn_log_info("new client session, %d.%d.%d.%d:%d\n", (addr >> 24) & 0xff,
                    //        (addr >> 16) & 0xff, (addr >> 8) & 0xff, addr & 0xff, port);

                    return dskt;
                }
            }
        }
    }

    return -1;
}


static void on_cmd_list(session_t *session, const char *path, int names_only)
{
    fileinfo_t *finfo;
    fileinfo_t *info;
    char buf[SEND_BUF_SIZE];
    int len;
    int pos;
    int dskt = -1;

    if (filesys_get_absolute_path(session->fs, path, buf) != 0)
    {
        send_reply(session, 500, "Unable to open directory.");
        sn_log_error("filesys_get_absolute_path FAILED!\n");
        return;
    }
    sn_log_debug("file:%s\n", buf);
    if (fs_get_fileinfos(session->fs, buf, &finfo) != 0)
    {
        send_reply(session, 500, "Unable to open directory.");
		return;
    }
    fs_sort_fileinfos(&finfo, 0, 0);

    dskt = get_data_socket(session);
    if (dskt == -1)
    {
        send_reply(session, 500, "Unable to open data connection."); //mine
		return;
    }

    send_reply(session, 150, "Here comes the directory listing.");

    len = 0;
    pos = 0;
    memset(buf, 0, SEND_BUF_SIZE);
    info = finfo;
    while (info != NULL)
    {
        pos += print_fileinfo(info, buf + pos);
        info = info->p_next;

        if (SEND_BUF_SIZE - pos < MAX_FILENAME + 1024)
        {
            sn_log_debug("list:\n%s\n", buf);
            send(dskt, buf, pos, 0);
            pos = 0;
        }
    }
    if (pos > 0)
    {
        sn_log_debug("list:\n%s\n", buf);
        send(dskt, buf, pos, 0);
    }

    send_reply(session, 226, "Directory send OK.");

    //ATTENTION
    closesocket(dskt);

    fs_free_fileinfos(info);
}


static void on_cmd_type(session_t *session, const char *type)
{
	if (tolower(*type) == 'i')
        send_reply(session, 200, "Switching to Binary mode.");
    else if (tolower(*type) == 'a')
        send_reply(session, 200, "Switching to ASCII mode.");
    else
        send_reply(session, 500, "Command failed.");
}


static void on_cmd_retr(session_t *session, const char *file)
{
    char abspath[SN_MAX_PATH];
    int dskt = -1;
    file_t *fp = NULL;
    int64_t file_size = 0;
    int64_t total_send_size = 0;
    char sendbuf[SEND_BUF_SIZE];
    int read_size;
    int send_size;
    int ret;

    if (filesys_get_absolute_path(session->fs, file, abspath) != 0)
    {
        send_event(SEVENT_FTP_GET, &session->info, (unsigned long)file, 0);
        goto FAIL;
    }

    fileinfo_t info;
    if ((fs_get_fileinfo(NULL, abspath, &info) == 0) && (info.type == FT_DIR))
    {
        on_cmd_list(session, file, 0);
        return;
    }

    if (rule_match(session->info.ip, RULEOPT_FTP_DOWN, abspath) == 0)
        goto FAIL;

    send_event(SEVENT_FTP_GET, &session->info, (unsigned long)file, 0);

    if (session->readable == 0)
        goto FAIL;

    dskt = get_data_socket(session);
    if (dskt == -1)
    {
        send_event(SEVENT_FTP_GET, &session->info, (unsigned long)file, 2);
        send_reply(session, 500, "Unable to open data connection."); //mine
        return;
    }

    send_reply(session, 150, "Opening BINARY connection.");

    file_size = fs_get_filesize(abspath);
    sn_log_debug("file size:%d\n", file_size);

    session->info.total_size = file_size;
    session->info.curr_size = 0;

    fp = fs_open(abspath, FM_READ);
    if (fp == NULL)
    {
        sn_log_error("open file FAILED! %s\n", abspath);
        goto FAIL;
    }

    if (session->restart_marker > 0)
    {
        //TODO check whether seek succeed?
        fs_seek(fp, session->restart_marker, SEEK_SET);
        session->info.curr_size = session->restart_marker;
        session->restart_marker = 0;
    }

    for (; ;)
    {
        read_size = fs_read(fp, sendbuf, SEND_BUF_SIZE);
        if (read_size <= 0)
            break;
        send_size = 0;
        while (send_size < read_size)
        {
            ret = send(dskt, sendbuf + send_size, read_size - send_size, 0);
            if (ret <= 0)
                goto FAIL;
            send_size += ret;
            total_send_size += ret;
            session->info.curr_size += ret;

            if (session->exit_flag > 0)
            {
                sn_log_info("recv exit signal!\n");
                rule_add(session->info.ip, 0xFFFFFFFF, RULEOPT_FTP_DOWN, 120, abspath);
                goto FAIL;
            }
        }
    }
    send_reply(session, 226, "File send OK.");
    sn_log_debug("send size:%"LLD"\n", total_send_size);
    send_event(SEVENT_FTP_GET, &session->info, (unsigned long)file, 1);
    goto END;

FAIL:
    send_event(SEVENT_FTP_GET, &session->info, (unsigned long)file, 2);
    send_reply(session, 500, "Command failed."); //TODO 500?
END:
    if (fp != NULL)
        fs_close(fp);
    closesocket(dskt);
}


//TODO auto rename?
static void on_cmd_stor(session_t *session, const char *file, int append)
{
    char abspath[SN_MAX_PATH];
    int dskt = -1;
    file_t *fp = NULL;
    int64_t total_recv_size = 0;
    char recvbuf[SEND_BUF_SIZE];
    int recvsize;
    int writesize;

    send_event(SEVENT_FTP_PUT, &session->info, (unsigned long)file, 0);

    if (session->writable == 0)
        goto FAIL;

    dskt = get_data_socket(session);
    if (dskt == -1)
    {
        send_event(SEVENT_FTP_PUT, &session->info, (unsigned long)file, 2);
        send_reply(session, 500, "Unable to open data connection."); //mine
        return;
    }

    send_reply(session, 150, "Opening BINARY connection.");

    if (filesys_get_absolute_path(session->fs, file, abspath) != 0)
        goto FAIL;

    if (rule_match(session->info.ip, RULEOPT_FTP_UP, abspath) == 0)
        goto FAIL;

    fp = fs_open(abspath, append == 0 ? FM_WRITE : FM_WRITE_APPEND);
    if (fp == NULL)
    {
        sn_log_error("open file FAILED! %s\n", abspath);
        goto FAIL;
    }

    session->info.total_size = 0;
    session->info.curr_size = 0;
    if (session->restart_marker > 0)
    {
        //TODO check whether seek succeed?
        fs_seek(fp, session->restart_marker, SEEK_SET);
        session->info.curr_size = session->restart_marker;
        session->restart_marker = 0;
    }

    for (; ;)
    {
        recvsize = recv(dskt, recvbuf, SEND_BUF_SIZE, 0);
        if (recvsize <= 0)
            break;

        if ((writesize = fs_write(fp, recvbuf, recvsize)) != recvsize)
        {
            sn_log_error("write file FAILED! recvsize:%d, writesize:%d\n", recvsize, writesize);
            goto FAIL;
        }

        total_recv_size += recvsize;
        session->info.curr_size += recvsize;

        if (session->exit_flag > 0)
        {
            sn_log_info("recv exit signal!\n");
            fs_close(fp);
            fs_delete(abspath);
            rule_add(session->info.ip, 0xFFFFFFFF, RULEOPT_FTP_UP, 120, abspath);
            goto FAIL;
        }
    }
    send_reply(session, 226, "Command successful.");
    sn_log_info("recv size:%"LLD"\n", total_recv_size);
    send_event(SEVENT_FTP_PUT, &session->info, (unsigned long)file, 1);
    goto END;

FAIL:
    send_event(SEVENT_FTP_PUT, &session->info, (unsigned long)file, 2);
    send_reply(session, 500, "Command failed."); //TODO 500?
END:
    if (fp != NULL)
        fs_close(fp);
    closesocket(dskt);
}


static void on_cmd_cwd(session_t *session, const char *path)
{
    char newpath[SN_MAX_PATH];
    strcpy(newpath, path);
    if (filesys_change_working_dir(session->fs, newpath) == 0)
        send_reply(session, 250, "Command successful.");
    else
        send_reply(session, 550, "Command failed.");
}


static void on_cmd_rnfr(session_t *session, const char *file)
{
    if (session->writable == 0)
        goto FAIL;
    memset(session->rnfr, 0, SN_MAX_PATH);
    strncpy(session->rnfr, file, SN_MAX_PATH - 1);
    //TODO check whether file exist?
    send_reply(session, 350, "Got RNFR, waiting for RNTO.");
    return;
FAIL:
    send_reply(session, 550, "Command failed.");
}

static void on_cmd_rnto(session_t *session, const char *file)
{
    char from_full[SN_MAX_PATH];
    char to_full[SN_MAX_PATH];


    if (session->rnfr[0] == '\0')
        goto FAIL;

    if (filesys_get_absolute_path(session->fs, session->rnfr, from_full) != 0)
        goto FAIL;
    if (filesys_get_absolute_path(session->fs, file, to_full) != 0)
        goto FAIL;

    if (fs_move(from_full, to_full) != 0)
        goto FAIL;

    //snprintf(from_full, SN_MAX_PATH, "Renamed %s to %s.", session->rnfr, file);
    //send_reply(session, 250, from_full);
    send_reply(session, 250, "Rename succeed.");
    send_event(SEVENT_FTP_RENAME, &session->info, (unsigned long)file, 0);
    session->rnfr[0] = '\0';
    return;

FAIL:
    send_reply(session, 550, "Rename failed.");
    send_event(SEVENT_FTP_RENAME, &session->info, (unsigned long)file, 1);
    session->rnfr[0] = '\0';
}

static void on_cmd_dele(session_t *session, const char *file)
{
    char abspath[SN_MAX_PATH];
    if (session->writable == 0)
        goto FAIL;
    if (filesys_get_absolute_path(session->fs, file, abspath) != 0)
        goto FAIL;
    if (fs_delete(abspath) == 0)
    {
        //ATTENTION
        //snprintf(abspath, SN_MAX_PATH, "File \"%s\" deleted.", file);
        //send_reply(session, 250, abspath);
        send_reply(session, 250, "File deleted.");
        send_event(SEVENT_FTP_DEL, &session->info, (unsigned long)file, 0);
        return;
    }
FAIL:
    send_reply(session, 550, "File delete FAILED.");
    send_event(SEVENT_FTP_DEL, &session->info, (unsigned long)file, 1);
}

static void on_cmd_mkd(session_t *session, const char *dir)
{
    char abspath[SN_MAX_PATH];
    if (session->writable == 0)
        goto FAIL;
    if (filesys_get_absolute_path(session->fs, dir, abspath) != 0)
        goto FAIL;
    if (fs_create_dir(abspath, 0777) == 0)
    {
        //ATTENTION
        //snprintf(abspath, SN_MAX_PATH, "Directory \"%s\" created.", dir);
        //send_reply(session, 250, abspath);
        send_reply(session, 250, "Directory created.");
        send_event(SEVENT_FTP_MKD, &session->info, (unsigned long)dir, 0);
        return;
    }
FAIL:
    send_reply(session, 550, "Directory create FAILED.");
    send_event(SEVENT_FTP_MKD, &session->info, (unsigned long)dir, 1);
}


static void on_cmd_rmd(session_t *session, const char *dir)
{
    char abspath[SN_MAX_PATH];
    if (session->writable == 0)
        goto FAIL;
    if (filesys_get_absolute_path(session->fs, dir, abspath) != 0)
        goto FAIL;
    if (fs_delete(abspath) == 0)
    {
        //ATTENTION
        //snprintf(abspath, SN_MAX_PATH, "Directory \"%s\" deleted.", dir);
        //send_reply(session, 250, abspath);
        send_reply(session, 250, "Directory deleted.");
        send_event(SEVENT_FTP_RMD, &session->info, (unsigned long)dir, 0);
        return;
    }
FAIL:
    send_reply(session, 550, "Directory delete FAILED.");
    send_event(SEVENT_FTP_RMD, &session->info, (unsigned long)dir, 1);
}


static void on_cmd_site(session_t *session, const char *cmd)
{
    send_reply(session, 500, "Not understood.");
}


static void on_cmd_mode(session_t *session, const char *mode)
{
	// only stream-mode supported
	if (tolower(*mode) == 's')
        send_reply(session, 200, "Command successful.");
    else
        send_reply(session, 500, "Command failed.");
}


static void on_cmd_stru(session_t *session, const char *structure)
{
	// only file-structure supported
	if (tolower(*structure) == 'f')
        send_reply(session, 200, "Command successful.");
    else
        send_reply(session, 500, "Command failed.");
}


//TODO 绝对路径, 相对路径
static void on_cmd_size(session_t *session, const char *file)
{
    int64_t size = -1;
    char abspath[SN_MAX_PATH];
    char strsize[32];
    memset(strsize, 0, 32);
    if (filesys_get_absolute_path(session->fs, file, abspath) == 0)
    {
        size = fs_get_filesize(abspath);
        if (size >= 0)
        {
            snprintf(strsize, 32, "%"LLD, size);
            sn_log_debug("file:%s, size:%s\n", abspath, strsize);
        }
    }
    if (size < 0)
        send_reply(session, 550, "Could not get filesize.");
    else
        send_reply(session, 213, strsize);
}


static void on_cmd_mdtm(session_t *session, const char *file)
{
    char abspath[SN_MAX_PATH];
    fileinfo_t info;
    struct tm t;

    if (filesys_get_absolute_path(session->fs, file, abspath) == 0)
    {
        memset(&info, 0, sizeof(fileinfo_t));
        if (fs_get_fileinfo(NULL, abspath, &info) == 0)
        {
            localtime_r(&info.timestamp, &t);
            //ATTENTION use abspath
            snprintf(abspath, SN_MAX_PATH, "%04d%02d%02d%02d%02d%02d", t.tm_year + 1900,
                    t.tm_mon + 1, t.tm_mday, t.tm_hour, t.tm_min, t.tm_sec);
            send_reply(session, 215, abspath);
        }
        else
            send_reply(session, 550, "Could not get file info.");
    }
    else
        send_reply(session, 550, "File not found.");
}


static void on_cmd_pwd(session_t *session)
{
    char path[SN_MAX_PATH];
    memset(path, 0, SN_MAX_PATH);
    path[0] = '\"';
    if (filesys_get_working_dir(session->fs, path + 1) == 0)
    {
        strcat(path, "\"");
        send_reply(session, 257, path);
    }
    else
        //TODO
        send_reply(session, 500, "Command failed.");
}


static void on_cmd_rest(session_t *session, int64_t marker)
{
    char msg[128];
    msg[127] = '\0';
	if (marker < 0)
	{
        session->restart_marker = 0;
        snprintf(msg, 127, "Invalid restart position (%"LLD").", marker);
        send_reply(session, 501, msg);
    }
    else
    {
        session->restart_marker = marker;
        snprintf(msg, 127, "Restart position accepted (%"LLD").", marker);
        send_reply(session, 350, msg);
	}
}


static void process_cmd(session_t *session)
{
    char *c;
    c = strtok(session->msg_buf_recv, " ");
    if (c)
    {
        int i;
        unsigned int result = 0;

        for (i = 0; i < 4; i++, c++)
        {
            if (isalpha(*c))
                result = (result << 8) | tolower(*c);
        }

        //if user has not logged in, we only allow a small subset of commands
        if (session->auth_state != AUTHSTATE_VALID)
        {
            switch (result)
            {
            //USER <name>
            case FTPCMD_USER:
                {
                    char *user = strtok(NULL, "");
                    clean_path(user);
                    if(user)
                        on_cmd_user(session, user);
                    else
                        send_reply(session, 500, "Requires parameters.");
                }
                break;

            //PASS <password>
            case FTPCMD_PASS:
                {
                    char* pass = strtok(NULL,"");
                    clean_path(pass);
                    if(pass)
                        on_cmd_pass(session, pass);
                    else
                        send_reply(session, 530, "Login incorrect.");
                }
                break;

            case FTPCMD_QUIT:
                on_cmd_quit(session);
                break;

            case FTPCMD_NOOP:
                send_reply(session, 200, "Command successful.");
                break;

            default:
                //mine
                send_reply(session, 503, "Login with USER first.");
                break;
            }

            return;
        }

        switch (result)
        {
        case FTPCMD_USER:
        case FTPCMD_PASS:
            send_reply(session, 503, "Already authenticated.");
            break;

        case FTPCMD_PASV:
            on_cmd_pasv(session);
            break;

        //PORT a0,a1,a2,a3,p0,p1
        case FTPCMD_PORT:
            {
            int i;
            int ip[4];
            int port = 0;

            for (i = 0; i < 6; i++)
            {
                char *val = strtok(NULL, ",");
                if (!val)
                    break;
                if (i >= 0 && i < 4)
                    ip[i] = strtol(val, NULL, 10);
                else if (i == 4)
                    port = strtol(val, NULL, 10) * 256;
                else if (i == 5)
                    port += strtol(val, NULL, 10);
            }

            if (i == 6)
                on_cmd_port(session, ip, port);
            else
                send_reply(session, 501, "Illegal command.");
            }
            break;

        case FTPCMD_QUIT:
            on_cmd_quit(session);
            break;

        case FTPCMD_SYST:
            on_cmd_syst(session);
            break;

        case FTPCMD_LIST:
            {
            char* path = strtok(NULL, "");
            if (path != NULL && *path == '-')
                path = strtok(NULL, "");
            if(path)
                on_cmd_list(session, path, 0);
            else
                on_cmd_list(session, "", 0);
            }
            break;

        case FTPCMD_NLST:
            {
            char* path = strtok(NULL,"");
            if (*path == '-')
                path = strtok(NULL, "");
            if(path)
                on_cmd_list(session, path, 1);
            else
                on_cmd_list(session, "", 1);
            }
            break;

        //these all share the same setup
        case FTPCMD_TYPE:
        case FTPCMD_RETR:
        case FTPCMD_STOR:
        case FTPCMD_STOU:
        case FTPCMD_APPE:
        case FTPCMD_CWD:
        case FTPCMD_XCWD:
        case FTPCMD_DELE:
        case FTPCMD_RNFR:
        case FTPCMD_RNTO:
        case FTPCMD_MKD:
        case FTPCMD_XMKD:
        case FTPCMD_RMD:
        case FTPCMD_XRMD:
        case FTPCMD_SITE:
        case FTPCMD_MODE:
        case FTPCMD_STRU:
        case FTPCMD_SIZE:
        case FTPCMD_MDTM:
            {
            char* arg = strtok(NULL, "");
            if (arg)
            {
                switch (result)
                {
                case FTPCMD_TYPE:
                    on_cmd_type(session, arg);
                    break;
                case FTPCMD_RETR:
                    clean_path(arg);
                    session->info.file_flag = 2;
                    time(&session->info.file_start);
                    strncpy(session->info.fname, arg, SN_MAX_PATH - 1);
                    on_cmd_retr(session, arg);
                    session->info.file_flag = 0;
                    break;
                case FTPCMD_STOR:
                    clean_path(arg);
                    session->info.file_flag = 1;
                    time(&session->info.file_start);
                    strncpy(session->info.fname, arg, SN_MAX_PATH - 1);
                    on_cmd_stor(session, arg, 0);
                    session->info.file_flag = 0;
                    break;
                case FTPCMD_STOU:
                    send_reply(session, 501, "Command STOU not supported.");
                    break;
                case FTPCMD_APPE:
                    clean_path(arg);
                    session->info.file_flag = 1;
                    time(&session->info.file_start);
                    strncpy(session->info.fname, arg, SN_MAX_PATH - 1);
                    on_cmd_stor(session, arg, 1);
                    session->info.file_flag = 0;
                    break;
                case FTPCMD_CWD:
                case FTPCMD_XCWD:
                    clean_path(arg);
                    on_cmd_cwd(session, arg);
                    break;
                case FTPCMD_RNFR:
                    clean_path(arg);
                    on_cmd_rnfr(session, arg);
                    break;
                case FTPCMD_RNTO:
                    clean_path(arg);
                    on_cmd_rnto(session, arg);
                    break;
                case FTPCMD_DELE:
                    clean_path(arg);
                    on_cmd_dele(session, arg);
                    break;
                case FTPCMD_MKD:
                case FTPCMD_XMKD:
                    clean_path(arg);
                    on_cmd_mkd(session, arg);
                    break;
                case FTPCMD_RMD:
                case FTPCMD_XRMD:
                    clean_path(arg);
                    on_cmd_rmd(session, arg);
                    break;
                case FTPCMD_SITE:
                    on_cmd_site(session, arg);
                    break;
                case FTPCMD_MODE:
                    on_cmd_mode(session, arg);
                    break;
                case FTPCMD_STRU:
                    on_cmd_stru(session, arg);
                    break;
                case FTPCMD_SIZE:
                    clean_path(arg);
                    on_cmd_size(session, arg);
                    break;
                case FTPCMD_MDTM:
                    clean_path(arg);
                    on_cmd_mdtm(session, arg);
                    break;
                }
            }
            else
                send_reply(session, 500, "Requires parameters.");
            }
            break;

        case FTPCMD_PWD:
        case FTPCMD_XPWD:
            on_cmd_pwd(session);
            break;

        case FTPCMD_CDUP:
            on_cmd_cwd(session,"..");
            break;

        case FTPCMD_REST:
            {
            char *marker = strtok(NULL, "");
            //ATTENTION for atoi here
            if (marker)
                on_cmd_rest(session, atoll(marker));
            else
                send_reply(session, 500, "Requires parameters.");
            }
            break;

        case FTPCMD_ABOR:
            //TODO 中断当前传输
            send_reply(session, 200, "Command successful.");
            break;

        case FTPCMD_NOOP:
            send_reply(session, 200, "Command successful.");
            break;

        case FTPCMD_FEAT:
            //send_reply(session, 211, "Features:\r\nEPRT\r\nEPSV\r\nMDTM\r\nPASV\r\nREST STREAM\r\nSIZE\r\nTVFS\r\nUTF8\r\nEnd");
            snprintf(session->msg_buf, 8192, "211-Features:\r\n MDTM\r\n PASV\r\n REST STREAM\r\n SIZE\r\n TVFS\r\n UTF8\r\n211 End\r\n");
            sn_log_debug("send reply:%s\n", session->msg_buf);
            send(session->cmd_skt, session->msg_buf, strlen(session->msg_buf), 0);
            break;

        case FTPCMD_OPTS:
            {
            char* arg = strtok(NULL, "");
            if (arg)
            {
                if (strstr(arg, "UTF8 ON") != NULL || strstr(arg, "utf8 on") != NULL)
                //if (strcasestr(arg, "UTF8 ON") != NULL)
                {
#ifndef WIN32
                    send_reply(session, 200, "Always in UTF8 mode..");
                    break;
#endif
                }
            }
            }

        default:
            send_reply(session, 500, "Not understood.");
            break;
        }
    }
    else
        send_reply(session, 500, "Not understood.");
}


//thread proc
static void* ftpsession_process(void *param)
{
    session_t *session = (session_t *)param;
    struct timeval timeout; 
	fd_set r;
    int unblock = 1;
    int ret;

    time(&session->start_time);
    sn_log_info("session begin...\n");
    send_reply(session, 220, SERVER_NAME" ready.");

    //设置为非阻塞模式
#ifdef WIN32
    //TODO ??????
#else
    ioctl(session->cmd_skt, FIONBIO, (int)&unblock);
#endif

    for (; ;)
    {
        if (session->exit_flag == 1)
        {
            sn_log_info("recv exit signal!\n");
            goto EXIT;
        }
        //中断当前传输时exit_flag被置为2, 此处重置
        session->exit_flag = 0;

        timeout.tv_sec = 0; 
        timeout.tv_usec = 1000 * 200; 
        FD_ZERO(&r);
        FD_SET(session->cmd_skt, &r);

        if (select(session->cmd_skt + 1, &r, NULL, NULL, &timeout) <= 0)
            continue;

        if (FD_ISSET(session->cmd_skt, &r))
        {
            memset(session->msg_buf_recv, 0, MSG_BUF_SIZE);
            ret = recv(session->cmd_skt, session->msg_buf_recv, MSG_BUF_SIZE, 0);
            if (ret > 0)
            {
                sn_log_info("recv cmd:%s\n", session->msg_buf_recv);
                process_cmd(session);
            }
            else
            {
                sn_log_error("recv ERROR!\n");
                goto EXIT;
            }
        }
    }

EXIT:
    sn_log_info("session end!\n");
    do_quit(session);
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
        //    //TODO close socket
        //    p_session->status = SESSION_STATUS_NULL;
        //}
    }
}


int ftpsession_add(const struct _ftpserver *p_server, int cmd_skt, uint32_t ip)
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
    curr->cmd_skt = cmd_skt;
    curr->data_skt = -1;
    curr->info.ip = htonl(ip);
    curr->status = SESSION_STATUS_INPROCESS;
    curr->info.session_id = m_session_count;
    m_session_count++;
    curr->fs = filesys_copy(p_server->fs, 1);
    if (curr->fs == NULL)
        return -1;
    if (run_thread(ftpsession_process, curr) != 0)
        return -1;

    return 0;
}


void ftpsession_clear(const struct _ftpserver *p_server)
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


int ftpsession_kill(uint32_t id, int transonly)
{
    int i;
    for (i = 0; i < MAX_SESSIONS; i++)
    {
        if (m_session_pool[i].info.session_id == id)
        {
            if (transonly == 1)
                m_session_pool[i].exit_flag = 2;
            else
                m_session_pool[i].exit_flag = 1;
            return 0;
        }
    }
    return -1;
}


void ftp_get_session_info(void (*cb_session_info)(void *context, int type,
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
                cb_session_info(context, 1, s.info.ip, NULL, s.start_time, s.info.file_flag,
                        s.info.file_start, s.info.total_size, s.info.curr_size, s.info.fname,
                        s.info.session_id);
            }
        }
    }
}

