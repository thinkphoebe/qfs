#include <stdlib.h>
#ifdef WIN32
#include <winsock.h>
#include <winsock2.h>
#else
#include <sys/un.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#endif

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include <stdarg.h>  //va_start...
#include <unistd.h>  //usleep

#include "snlog.h"


typedef void (*log_output_t)(char *buf, int size);

#define TARGETS_NUM 3


static void log_output_console(char *buf, int size);
static void log_output_file   (char *buf, int size);
static void log_output_udp    (char *buf, int size);


static e_log_level m_level = SN_LOG_LEVEL_ALL;
static int m_target = SN_LOG_TARGET_NULL;
static const log_output_t m_output[] =
{
    log_output_console,
    log_output_file,
    log_output_udp
};

static char *m_logfile_path = NULL;
static FILE *m_logfile = NULL;

static int m_sockfd = -1;
static int m_islocal;
static struct sockaddr_in m_peeraddr;
#ifndef WIN32
static struct sockaddr_un m_localaddr;
#endif

static char m_logbuf[4][16 * 1024];
static int m_logbuf_index = 0;


int sn_log_init(e_log_level level)
{
    if (sn_log_set_level(level) != 0)
        return -1;
    return 0;
}


void sn_log_exit()
{
    sn_log_clear_target();
    //restore default value
    m_level = SN_LOG_LEVEL_ALL;
    m_target = SN_LOG_TARGET_NULL;
}


int sn_log_set_level(e_log_level level)
{
    if (level < SN_LOG_LEVEL_ALL || level > SN_LOG_LEVEL_FATAL)
        return -1;
    m_level = level;
    return 0;
}


int sn_log_get_level()
{
    return m_level;
}


int sn_log_add_target(e_log_target target, unsigned long wparam, unsigned long lparam)
{
    m_target |= target;

    if ((target & SN_LOG_TARGET_FILE) != 0)
    {
        sn_log_del_target(SN_LOG_TARGET_FILE);
        m_target |= target;

        if (lparam == 0)
            m_logfile = fopen((char *)wparam, "wb");
        else
            m_logfile = fopen((char *)wparam, "a");
        if (m_logfile == NULL)
            return -1;
        m_logfile_path = strdup((char *)wparam);
    }
    else if ((target & SN_LOG_TARGET_UDP) != 0)
    {
        if (wparam == 0)
        {
#ifndef WIN32
            m_sockfd = socket(AF_UNIX, SOCK_DGRAM,0);
            if (m_sockfd < 0)
                return -1;
            m_localaddr.sun_family = AF_LOCAL;
            strcpy(m_localaddr.sun_path, (char *)lparam);
            m_islocal = 1;
#else
            return -1;
#endif
        }
        else
        {
            m_sockfd = socket(AF_INET, SOCK_DGRAM, 0);
            if (m_sockfd < 0)
                return -1;
            memset(&m_peeraddr, 0, sizeof(struct sockaddr_in));
            m_peeraddr.sin_family = AF_INET;
            m_peeraddr.sin_addr.s_addr = inet_addr((char *)wparam);
            m_peeraddr.sin_port = htons((uint16_t)lparam);
            m_islocal = 0;
        }
    }

    return 0;
}


int sn_log_del_target(e_log_target target)
{
    if ((target & m_target) == 0)
        return -1;
	m_target &= ~target;
    if ((target & SN_LOG_TARGET_FILE) != 0)
    {
        usleep(1000 * 200);
        if (m_logfile != NULL)
            fclose(m_logfile);
        m_logfile = NULL;
        free(m_logfile_path);
        m_logfile_path = NULL;
    }
    else if ((target & SN_LOG_TARGET_UDP) != 0)
    {
        if (m_sockfd >= 0)
#ifdef WIN32
            closesocket(m_sockfd);
#else
            close(m_sockfd);
#endif
        m_sockfd = -1;
        memset(&m_peeraddr, 0, sizeof(struct sockaddr_in));
#ifndef WIN32
        memset(&m_localaddr, 0, sizeof(struct sockaddr_un));
#endif
    }
    return 0;
}


void sn_log_clear_target()
{
    sn_log_del_target(SN_LOG_TARGET_FILE);
    sn_log_del_target(SN_LOG_TARGET_UDP);
    m_target = SN_LOG_TARGET_NULL;
}


int sn_log(e_log_level level, sn_log_module_t *module, const char *function, int line, char *format, ...)
{
    va_list vl;
    time_t now;
    struct tm* tm;
    char *p_buf;
    int index;
    int size;
    int remain_size;
    int ret;
    int i;

    if (module != NULL && level < module->level)
        return 0;
    else if (level < m_level)
        return 0;

    index = m_logbuf_index + 1;
    if (index >= sizeof(m_logbuf) / sizeof(m_logbuf[0]))
        index = 0;
    m_logbuf_index = index;
    p_buf = m_logbuf[0];

    size = sizeof(m_logbuf[0]) - 1;
    p_buf[size] = '\0';
    remain_size = size;

    //timestamp
    va_start(vl, format);
    time(&now);
    tm = localtime/*gmtime*/(&now);
    ret = snprintf(p_buf, remain_size, "[%02d:%02d:%02d]", tm->tm_hour, tm->tm_min, tm->tm_sec);
    if (ret > 0)
    {
        remain_size -= ret;
        p_buf += ret;
    }

    //module name
    if (module != NULL)
    {
        ret = snprintf(p_buf, remain_size, "[%s]", module->name);
        if (ret > 0)
        {
            remain_size -= ret;
            p_buf += ret;
        }
    }

    //function
    ret = snprintf(p_buf, remain_size, "[%s]", function);
    if (ret > 0)
    {
        remain_size -= ret;
        p_buf += ret;
    }

    //line
    ret = snprintf(p_buf, remain_size, "(L%d) ", line);
    if (ret > 0)
    {
        remain_size -= ret;
        p_buf += ret;
    }

    //content
    ret = vsnprintf(p_buf, remain_size, format, vl);
    if (ret > 0)
    {
        remain_size -= ret;
        p_buf += ret;
    }

    va_end(vl);

    for (i = 0; i < TARGETS_NUM; i++)
        if (((1 << i) & m_target) != 0)
            m_output[i](p_buf + remain_size - size, size - remain_size);

    return 0;
}


static void log_output_console(char *buf, int size)
{
    fprintf(stderr, "%s", buf);
}


static void log_output_file(char *buf, int size)
{
    if (m_logfile != NULL)
        fwrite(buf, size, 1, m_logfile);
}


static void log_output_udp(char *buf, int size)
{
    if (m_sockfd < 0)
        return;
#ifndef WIN32
    if (m_islocal)
        sendto(m_sockfd, buf, size, 0, (struct sockaddr *)&m_localaddr, sizeof(struct sockaddr_in));
    else
#endif
        sendto(m_sockfd, buf, size, 0, (struct sockaddr *)&m_peeraddr, sizeof(struct sockaddr_in));
}

