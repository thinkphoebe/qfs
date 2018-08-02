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
#include "ftpsession.h"
#include "ftpserver.h"

#define MSG_MODULE "FTPSERVER"


ftpserver_t* ftpserver_create(const char *path)
{
    ftpserver_t *h;
    h = (ftpserver_t *)malloc(sizeof(ftpserver_t));
    if (h == NULL)
    {
        sn_log_error("malloc FAILED!\n");
        return NULL;
    }
    memset(h, 0, sizeof(ftpserver_t));
    h->fs = filesys_create(path);

    if (h->fs == NULL)
    {
        free(h);
        return NULL;
    }
    snprintf(h->path, 1024, path);
    return h;
}


void ftpserver_destory(ftpserver_t *h)
{
    if (h->status == 1)
    {
        h->status = 2;
        while (h->status != 0)
            usleep(10000);
    }
    ftpsession_clear(h);
    filesys_destory(h->fs);
    free(h);
}


static void process_request(ftpserver_t *h)
{
    struct timeval timeout; 
    fd_set r;
    fd_set w;
    int max_fd;

    max_fd = h->skt;
    //TODO add PASV skt compare

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
                    addr = htonl(sa.sin_addr.s_addr);
                    //port = htons(sa.sin_port);
                    port = sa.sin_port;
                    sn_log_info("new client session, %d.%d.%d.%d:%d\n", (addr >> 24) & 0xff,
                            (addr >> 16) & 0xff, (addr >> 8) & 0xff, addr & 0xff, port);
                    send_event(SEVENT_FTP_CONNECT, h, addr, port);
                    if (ftpsession_add(h, cskt, addr) != 0)
                    {
                        //TODO send FAILED msg
                    }
                }
            }

            //TODO if (FD_ISSET(PASV skt, &r))
        }
    }

    closesocket(h->skt);
    h->skt = 0;
    h->status = 0;
}


int ftpserver_start(ftpserver_t *h)
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
    sa.sin_port = htons(h->cmd_port);
    sa.sin_addr.s_addr = INADDR_ANY;

    if (bind(s, (struct sockaddr*)&sa, sizeof(sa)) < 0)
    {
        send_event(SEVENT_BIND_FAILED, h, 1, h->cmd_port);
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
    sn_log_info("listening to port:%d\n", h->cmd_port);

    h->skt = s;
    h->status = 1;
    process_request(h);

    return 0;
}

