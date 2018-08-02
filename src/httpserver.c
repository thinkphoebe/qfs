#include <stdlib.h> //for malloc
#include <stdio.h> //for printf
#include <stdint.h> //for uint16_t
#include <string.h> //for memset
#include <unistd.h> //for close

#ifdef WIN32
#include <winsock.h>
#include <winsock2.h>
#else
#include <sys/socket.h>
#include <arpa/inet.h>
#endif

#include "utils.h"
#include "event.h"
#include "filesystem.h"
#include "httpsession.h"
#include "httpserver.h"

#define MSG_MODULE "HTTPSERVER"


static void set_default_templete(httpserver_t *h);


httpserver_t* httpserver_create(const char *path)
{
    httpserver_t *h;
    h = (httpserver_t *)malloc(sizeof(httpserver_t));
    if (h == NULL)
    {
        sn_log_error("malloc FAILED!\n");
        return NULL;
    }
    memset(h, 0, sizeof(httpserver_t));
    h->fs = filesys_create(path);
    if (h->fs == NULL)
    {
        free(h);
        return NULL;
    }
    snprintf(h->path, 1024, path);
    set_default_templete(h);
    return h;
}


void httpserver_destory(httpserver_t *h)
{
    if (h->status == 1)
    {
        h->status = 2;
        while (h->status != 0)
            usleep(10000);
    }
    httpsession_clear(h);
    filesys_destory(h->fs);
    if (h->templete_fs != NULL)
        filesys_destory(h->templete_fs);
    free(h);
}


static void process_request(httpserver_t *h)
{
    struct timeval timeout; 
    fd_set r;
    fd_set w;
    int max_fd;

    max_fd = h->skt;

    while (h->status == 1)
    {
        timeout.tv_sec = 0; 
        timeout.tv_usec = 1000 * 200; 
        FD_ZERO(&r);
        FD_ZERO(&w);
        FD_SET(h->skt, &r);

        if (select(max_fd + 1, &r, &w, NULL, &timeout) > 0)
        {
            if (FD_ISSET(h->skt, &r))
            {
                struct sockaddr_in sa;
                socklen_t sl;
                int cskt;

                sl = sizeof(sa);
                if ((cskt = accept(h->skt, (struct sockaddr*)&sa, &sl)) >= 0)
                {
                    uint32_t addr;
                    uint16_t port;
                    int on = 1;
                    addr = htonl(sa.sin_addr.s_addr);
                    port = htons(sa.sin_port);
                    sn_log_info("new client session, %d.%d.%d.%d:%d\n", (addr >> 24) & 0xff,
                            (addr >> 16) & 0xff, (addr >> 8) & 0xff, addr & 0xff, port);

                    //ATTENTION
                    setsockopt(cskt, SOL_SOCKET, SO_KEEPALIVE, (void *)&on, sizeof (on));
                    if (httpsession_add(h, cskt, addr) != 0)
                    {
                        //TODO send FAILED msg
                    }
                }
            }
        }
    }

    closesocket(h->skt);
    h->skt = 0;
    h->status = 0;
}


int httpserver_start(httpserver_t *h)
{
    struct sockaddr_in sa;
    int s;
    int opt;

    //create server socket
    if ((s = socket(PF_INET, SOCK_STREAM, 0)) < 0)
    {
        sn_log_fatal("failed to create socket\n");
        return -1;
    }

    opt = 1;	
    //设定此参数后运行多实例绑定同一端口都会成功, 无法报错
    //if (setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0)
    //{
    //    closesocket(s);
    //    sn_log_fatal("could not change socket options\n");
    //    return -1;
    //}

    //try to bind socket
    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_port = htons(h->listen_port);
    sa.sin_addr.s_addr = INADDR_ANY;

    if (bind(s, (struct sockaddr*)&sa, sizeof(sa)) < 0)
    {
        send_event(SEVENT_BIND_FAILED, h, 1, h->listen_port);
        sn_log_fatal("could not bind to port\n");
        closesocket(s);
        return -1;
    }

    //listen to socket
    if (listen(s, 1) < 0)
    {
        sn_log_fatal("could not listen to port\n");
        closesocket(s);
        return -1;
    }
    sn_log_info("listening to port:%d\n", h->listen_port);

    h->skt = s;
    h->status = 1;
    process_request(h);

    return 0;
}


static void set_default_templete(httpserver_t *h)
{
    snprintf(h->templete_header, sizeof(h->templete_header), "<html>\n<head>\n"
            //"<meta http-equiv=\"Content-Type\" content=\"text/html; charset=utf-8\" />"
            "<title>Browse directory "
            TEMPLETE_FIELD_HEAD"_PATH_"TEMPLETE_FIELD_END" - "
            TEMPLETE_FIELD_HEAD"_SERVERNAME_"TEMPLETE_FIELD_END
            "</title>\n</head>\n<body><h3>Directory "
            TEMPLETE_FIELD_HEAD"_PATH_"TEMPLETE_FIELD_END
            "</h3>\n<hr />\n<ul>\n"

            "<form ENCTYPE=\"multipart/form-data\" method=\"post\">"
            //"<input name=\"file\" type=\"file\"/>"
            "<input name=\"file\" type=\"file\"/>"
            "<input type=\"submit\" value=\"upload\"/></form>\n"
            "<hr>\n<ul>\n");
    snprintf(h->templete_footer, sizeof(h->templete_footer), "\n</ul>\n<br /><hr />\n<address>"
            TEMPLETE_FIELD_HEAD"_SERVERNAME_"TEMPLETE_FIELD_END
            "</address>\n</body>\n</html>\n");

    snprintf(h->templete_dir, sizeof(h->templete_dir), "\t<li><a href=\""
            TEMPLETE_FIELD_HEAD"_URL_"TEMPLETE_FIELD_END
            "\">"
            TEMPLETE_FIELD_HEAD"_NAME_"TEMPLETE_FIELD_END
            "</a></li>\n");

    snprintf(h->templete_file, sizeof(h->templete_dir), "\t<li><a href=\""
            TEMPLETE_FIELD_HEAD"_URL_"TEMPLETE_FIELD_END
            "\">"
            TEMPLETE_FIELD_HEAD"_NAME_"TEMPLETE_FIELD_END
            "</a></li>\n");

    snprintf(h->templete_upok, sizeof(h->templete_upok), "File \""
            TEMPLETE_FIELD_HEAD"_UPLOADFILE_"TEMPLETE_FIELD_END"\" has upload to \""
            TEMPLETE_FIELD_HEAD"_UPLOADPATH_"TEMPLETE_FIELD_END"\"!");
    snprintf(h->templete_upfailed, sizeof(h->templete_upfailed), "File \""
            TEMPLETE_FIELD_HEAD"_UPLOADFILE_"TEMPLETE_FIELD_END"\" upload to \""
            TEMPLETE_FIELD_HEAD"_UPLOADPATH_"TEMPLETE_FIELD_END
            "\" FAILED!\r\nReason:"TEMPLETE_FIELD_HEAD"_UPLOADRESULTMSG_"TEMPLETE_FIELD_END);


    if (h->templete_fs != NULL)
        filesys_destory(h->templete_fs);
    h->templete_fs = NULL;
    memset(h->templete_filepath, 0, 1024);
}


int httpserver_set_templete(httpserver_t *h, const char *header, const char *footer,
        const char *file, const char *dir, const char *upok, const char *upfailed,
        const char *filepath)
{
    int size;
    int buffer_size;

    size = strlen(header);
    buffer_size = sizeof(h->templete_header);
    if (size > buffer_size - 1)
        goto FAIL;
    strncpy(h->templete_header, header, buffer_size - 1);

    size = strlen(footer);
    buffer_size = sizeof(h->templete_footer);
    if (size > buffer_size - 1)
        goto FAIL;
    strncpy(h->templete_footer, footer, buffer_size - 1);

    size = strlen(file);
    buffer_size = sizeof(h->templete_file);
    if (size > buffer_size - 1)
        goto FAIL;
    strncpy(h->templete_file, file, buffer_size - 1);

    size = strlen(dir);
    buffer_size = sizeof(h->templete_dir);
    if (size > buffer_size - 1)
        goto FAIL;
    strncpy(h->templete_dir, dir, buffer_size - 1);

    size = strlen(upok);
    buffer_size = sizeof(h->templete_upok);
    if (size > buffer_size - 1)
        goto FAIL;
    strncpy(h->templete_upok, upok, buffer_size - 1);

    size = strlen(upfailed);
    buffer_size = sizeof(h->templete_upfailed);
    if (size > buffer_size - 1)
        goto FAIL;
    strncpy(h->templete_upfailed, upfailed, buffer_size - 1);

    strncpy(h->templete_filepath, filepath, SN_MAX_PATH - 1);
    if (h->templete_fs != NULL)
        filesys_destory(h->templete_fs);
    h->templete_fs = filesys_create(filepath);
    if (h->templete_fs == NULL)
        goto FAIL;

    return 0;

FAIL:
    set_default_templete(h);
    return -1;
}


int httpserver_set_templete_file(httpserver_t *h, const char *path, const char *filename)
{
    char fullpath[SN_MAX_PATH];
    char *sessions[6];
    char *values[6];
    int lens[6];
    FILE *fp;
    char *buf = NULL;
    int i;

    sn_log_debug("filename:%s\n", filename);

    memset(fullpath, 0, SN_MAX_PATH);
    snprintf(fullpath, SN_MAX_PATH - 1, "%s/%s", path, filename);

    fp = fopen(fullpath, "rb");
    if (fp == NULL)
    {
        sn_log_error("file open FAILED! %s, %s\n", path, filename);
        goto FAIL;
    }

    buf = (char *)malloc(1024 * 64);
    if (buf == NULL)
    {
        sn_log_error("malloc FAILED!\n");
        goto FAIL;
    }

    memset(buf, 0, 1024 * 64);
    fread(buf, 1, 1024 * 64 - 1, fp);

    sessions[0] = "SESSION_TEMPLETE_HEADER";
    values[0] = h->templete_header;
    lens[0] = 1024 * 8;

    sessions[1] = "SESSION_TEMPLETE_FOOTER";
    values[1] = h->templete_footer;
    lens[1] = 1024 * 8;

    sessions[2] = "SESSION_TEMPLETE_DIR";
    values[2] = h->templete_dir;
    lens[2] = 1024;

    sessions[3] = "SESSION_TEMPLETE_FILE";
    values[3] = h->templete_file;
    lens[3] = 1024;

    sessions[4] = "SESSION_TEMPLETE_UPLOADOK";
    values[4] = h->templete_upok;
    lens[4] = 1024;

    sessions[5] = "SESSION_TEMPLETE_UPLOADFAILED";
    values[5] = h->templete_upfailed;
    lens[5] = 1024;

    for (i = 0; i < 6; i++)
    {
        char tmp[64];
        char *p, *q;

        memset(tmp, 0, 64);
        snprintf(tmp, 63, "%s_%s", sessions[i], "START");
        if ((p = strstr(buf, tmp)) == NULL)
        {
            sn_log_error("can not find session start:%s!\n", tmp);
            goto FAIL;
        }
        p += strlen(tmp);
        while (*p == '\r' || *p == '\n' || *p == ' ')
            p++;

        memset(tmp, 0, 64);
        snprintf(tmp, 63, "%s_%s", sessions[i], "END");
        if ((q = strstr(buf, tmp)) == NULL)
        {
            sn_log_error("can not find session end:%s!\n", tmp);
            goto FAIL;
        }
        //while (*(q - 1) == '\r' || *(q - 1) == '\n' || *(q - 1) == ' ')
        //    q--;

        if (q <= p)
        {
            sn_log_error("parse session ERROR! %d, %d\n", q, p);
            goto FAIL;
        }

        if (q - p >= lens[i])
        {
            sn_log_error("session is too long:%d, buf:%d\n", q - p, lens[i]);
            goto FAIL;
        }
        memcpy(values[i], p, q - p);
        values[i][q - p] = '\0';

        sn_log_debug("parse session succeed:%s,%s\n", sessions[i], values[i]);
    }

    strncpy(h->templete_filepath, path, SN_MAX_PATH - 1);
    if (h->templete_fs != NULL)
        filesys_destory(h->templete_fs);
    h->templete_fs = filesys_create(path);
    if (h->templete_fs == NULL)
        goto FAIL;

    free(buf);
    fclose(fp);

    return 0;

FAIL:
    if (fp != NULL)
        fclose(fp);
    free(buf);
    set_default_templete(h);
    return -1;
}

