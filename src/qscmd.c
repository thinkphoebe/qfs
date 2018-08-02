#ifdef WIN32
#include <winsock2.h>
#endif

#include <stdint.h>
#include <stdio.h>
#include <string.h> //for strcpy
#include <signal.h>
#include <time.h>

#include "snlog.h"

#include "event.h"
#include "server.h"


static void on_event(void *context, e_sevent type, void *identifier,
        unsigned long wparam, unsigned long lparam)
{
    printf("1111111111111111111 type:%d, id:%d, wparam:%d, lparam:%d\n",
            type, identifier, wparam, lparam);
}


int main(int argc, char **argv)
{
#ifdef WIN32
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 1), &wsaData) != 0)
        return -1;
#endif

#ifndef WIN32
    //linux下tcp send对端关闭连接后, 调用两次write *可能* 会产生SIGPIPE信号, 默认结束进程
    //设置到SIG_IGN的处理函数, 第二次write返回-1
    signal(SIGPIPE, SIG_IGN);
#endif


    sn_log_init(SN_LOG_LEVEL_ALL);
    sn_log_add_target(SN_LOG_TARGET_CONSOLE, 0, 0);
    //sn_log_add_target(SN_LOG_TARGET_FILE, (unsigned long)"qscmd.log", 0);
    //sn_log_add_target(SN_LOG_TARGET_UDP, (unsigned long)"192.168.130.99", 12001);
    //sn_log_add_target(SN_LOG_TARGET_UDP, 0, (unsigned long)"/tmp/.logsocket");


    set_event_callback(on_event, NULL);


//    server_option_t option;
//    memset(&option, 0, sizeof(server_option_t));
//#ifdef WIN32
//    strcpy(option.root, "D:\\");
//#else
//    strcpy(option.root, "/data/nfs/");
//#endif
//    option.ftp_cmd_port = 10021;
//    option.ftp_data_port = 10020;
//    option.http_port = 10022;
//    option.allow_anonymous = 1;
//    option.anonymous_writable = 1;

//    strcpy(option.vdir_name[0], "tmp");
//    strcpy(option.vdir_path[0], "/tmp/");

//    strcpy(option.vdir_name[1], "share");
//    strcpy(option.vdir_path[1], "/data/share/");

    //strcpy(option.vdir_name[1], "usr");
    //strcpy(option.vdir_path[1], "/usr/");

    //server_update_option(&option);

    server_load_option("qscmd.conf");
    server_start();

    getchar();


    sn_log_exit();

#ifdef WIN32
    WSACleanup();
#endif

    return 0;
}
