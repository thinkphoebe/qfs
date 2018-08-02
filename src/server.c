#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h> //for strcpy
#include <time.h>

#include "snsetting.h"

#include "utils.h"
#include "filesystem.h"
#include "access.h"

#include "ftpserver.h"
#include "ftpsession.h"

#include "httpserver.h"
#include "httpsession.h"

#include "rule.h"
#include "server.h"


static server_option_t m_option;

//未调用server_update_option前为0
static int m_inited = 0;
static int m_status = 0; //0-->stoped, 1-->running

static ftpserver_t *m_ftpserver = NULL;
static httpserver_t *m_httpserver = NULL;


int server_update_option(const server_option_t *option)
{
    int running = m_status;
    if (running == 1)
    {
        if (server_stop() != 0)
            return -1;
    }
    m_option = *option;
    if (running == 1)
    {
        if (server_start() != 0)
            return -1;
    }
    m_inited = 1;
    return 0;
}


int server_load_option(const char *filename)
{
    int vint;
    const char *vp, *vp2;
    char name[128];
    fileinfo_t info;
    int i, j;

    sn_setting_t *s = sn_setting_create();
    if (s == NULL)
        return -1;
    if (sn_setting_load(s, filename) != 0)
    {
        sn_setting_destory(s);
        return -1;
    }

    memset(&m_option, 0, sizeof(m_option));

    if ((vp = sn_setting_read_string(s, NULL, "server_root")) != NULL)
        strncpy(m_option.root, vp, 1023);

    vint = 0;
    sn_setting_read_integer(s, NULL, "vdir_num", &vint);
    for (i = 0, j = 0; i < vint; i++)
    {
        memset(name, 0, 128);
        snprintf(name, 127, "vdir_%d_name", i);
        if ((vp = sn_setting_read_string(s, NULL, name)) == NULL)
            continue;

        memset(name, 0, 128);
        snprintf(name, 127, "vdir_%d_path", i);
        if ((vp2 = sn_setting_read_string(s, NULL, name)) == NULL)
            continue;

        if (fs_get_fileinfo(NULL, vp2, &info) != 0 || info.type != FT_DIR)
            continue;

        strncpy(m_option.vdir_name[j], vp, 255);
        strncpy(m_option.vdir_path[j], vp2, 1023);
        j++;
    }


    m_option.enable_http = 1;
    sn_setting_read_bool(s, NULL, "enable_http", &m_option.enable_http);
    if (m_option.enable_http == 1)
    {
        vint = 80;
        sn_setting_read_integer(s, NULL, "http_listen_port", &vint);
        m_option.http_port = vint;
    }

    if ((vp = sn_setting_read_string(s, NULL, "http_templete_path")) != NULL)
        strncpy(m_option.http_templete_path, vp, 1023);

    m_option.enable_ftp = 1;
    sn_setting_read_bool(s, NULL, "enable_ftp", &m_option.enable_ftp);
    if (m_option.enable_ftp == 1)
    {
        vint = 21;
        sn_setting_read_integer(s, NULL, "ftp_cmd_port", &vint);
        m_option.ftp_cmd_port = vint;

        vint = 20;
        sn_setting_read_integer(s, NULL, "ftp_data_port", &vint);
        m_option.ftp_data_port = vint;
    }


    vint = 1;
    sn_setting_read_bool(s, NULL, "allow_anonymous", &vint);
    m_option.allow_anonymous = vint;
    if (vint == 1)
    {
        vint = 0;
        sn_setting_read_bool(s, NULL, "anonymous_writable", &vint);
        m_option.anonymous_writable = vint;
    }

    access_exit();
    access_init();

    vint = 0;
    sn_setting_read_integer(s, NULL, "account_num", &vint);
    for (i = 0, j = 0; i < vint; i++)
    {
        const char *p_name;
        const char *p_pswd;
        uint32_t mask = 0;
        int value;

        memset(name, 0, 128);
        snprintf(name, 127, "account_%d_name", i);
        if ((p_name = sn_setting_read_string(s, NULL, name)) == NULL)
            continue;

        memset(name, 0, 128);
        snprintf(name, 127, "account_%d_pswd", i);
        p_pswd = sn_setting_read_string(s, NULL, name);

        value = 1;
        memset(name, 0, 128);
        snprintf(name, 127, "account_%d_http_readable", i);
        sn_setting_read_bool(s, NULL, name, &value);
        if (value > 0)
            mask |= E_AUTH_HTTP_READE;

        value = 1;
        memset(name, 0, 128);
        snprintf(name, 127, "account_%d_http_writable", i);
        sn_setting_read_bool(s, NULL, name, &value);
        if (value > 0)
            mask |= E_AUTH_HTTP_WRITE;

        value = 1;
        memset(name, 0, 128);
        snprintf(name, 127, "account_%d_ftp_readable", i);
        sn_setting_read_bool(s, NULL, name, &value);
        if (value > 0)
            mask |= E_AUTH_FTP_READE;

        value = 1;
        memset(name, 0, 128);
        snprintf(name, 127, "account_%d_ftp_writable", i);
        sn_setting_read_bool(s, NULL, name, &value);
        if (value > 0)
            mask |= E_AUTH_FTP_WRITE;

        access_add_user(p_name, p_pswd, mask, 1);

        j++;
    }

    sn_setting_destory(s);

    m_inited = 1;

    return 0;
}


const server_option_t* server_get_option()
{
    return &m_option;
}


static void* run_ftpserver()
{
    int i;

    m_ftpserver = ftpserver_create(m_option.root);
    m_ftpserver->cmd_port = m_option.ftp_cmd_port;
    m_ftpserver->data_port = m_option.ftp_data_port;
    m_ftpserver->allow_anonymous = m_option.allow_anonymous;
    m_ftpserver->anonymous_writable = m_option.anonymous_writable;

    for (i = 0; i < MAX_VDIR; i++)
    {
        if (strlen(m_option.vdir_name[i]) == 0)
            break;
        filesys_add_virtual_dir(m_ftpserver->fs, m_option.vdir_path[i], m_option.vdir_name[i]);
    }

    ftpserver_start(m_ftpserver);
    return NULL;
}


static void* run_httpserver()
{
    int i;
    m_httpserver = httpserver_create(m_option.root);
    m_httpserver->listen_port = m_option.http_port;
    m_httpserver->allow_anonymous = m_option.allow_anonymous;
    m_httpserver->anonymous_writable = m_option.anonymous_writable;

    httpserver_set_templete_file(m_httpserver, m_option.http_templete_path, "templete");

    for (i = 0; i < MAX_VDIR; i++)
    {
        if (strlen(m_option.vdir_name[i]) == 0)
            break;
        filesys_add_virtual_dir(m_httpserver->fs, m_option.vdir_path[i], m_option.vdir_name[i]);
    }

    httpserver_start(m_httpserver);
    return NULL;
}


int server_start()
{
    if (m_inited == 0)
        return -1;
    if (m_status == 1)
        return 0;

    rule_init();

    if (m_option.enable_ftp)
        run_thread(run_ftpserver, 0);
    if (m_option.enable_http)
        run_thread(run_httpserver, 0);

    m_status = 1;
    return 0;
}


int server_stop()
{
    if (m_status == 0)
        return 0;

    if (m_ftpserver != NULL)
        ftpserver_destory(m_ftpserver);
    m_ftpserver = NULL;

    if (m_httpserver != NULL)
        httpserver_destory(m_httpserver);
    m_httpserver = NULL;

    rule_exit();

    m_status = 0;
    return 0;
}


int server_is_running()
{
    return m_status;
}


struct _filesys* server_create_fs()
{
    filesys_t *fs;
    int i;
    if (m_inited == 0)
        return NULL;
    fs = filesys_create(m_option.root);
    if (fs == NULL)
        return NULL;
    for (i = 0; i < MAX_VDIR; i++)
    {
        if (strlen(m_option.vdir_name[i]) == 0)
            break;
        filesys_add_virtual_dir(fs, m_option.vdir_path[i], m_option.vdir_name[i]);
    }
    return fs;
}


void server_get_session_info(void (*cb_session_info)(void *context, int type,
    uint32_t ip, const char *user, time_t start, int fflag, time_t fstart,
    uint64_t totalsize, uint64_t currsize, const char *fname, uint32_t session_id),
    void *context)
{
    http_get_session_info(cb_session_info, context);
    ftp_get_session_info(cb_session_info, context);
}


int server_kill_session(int type, uint32_t session_id, int transonly)
{
    if (type == 0)
        return httpsession_kill(session_id, transonly);
    if (type == 1)
        return ftpsession_kill(session_id, transonly);
    return -1;
}
