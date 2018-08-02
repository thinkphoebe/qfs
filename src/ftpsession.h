#ifndef __FTPSESSION_H__
#define __FTPSESSION_H__

#ifdef __cplusplus
extern "C" {
#endif


typedef enum
{
	AUTHSTATE_INVALID = 0,	// not authenticated
	AUTHSTATE_PASSWORD,	// require password
	AUTHSTATE_FAKEPASS,	// require password, but fail
	AUTHSTATE_VALID,	// authenticated

} e_auth_state;

typedef enum
{
	DATAMODE_IDLE = 0,	    // no data flow
	DATAMODE_READ,	    // reading data from connection
	DATAMODE_WRITE,	    // writing data to connection

} e_data_mode;

typedef enum
{
	CONNSTATE_IDLE = 0,	    // connection-state is idle (no pending/active connection)
	CONNSTATE_LISTEN,   // listening for connection
	CONNSTATE_CONNECT,	// pending connection
	CONNSTATE_RUNNING,	// active connection

} e_conn_state;

typedef enum
{
	DATAACTION_NONE = 0,
	DATAACTION_LIST,
	DATAACTION_NLST,
	DATAACTION_RETR,
	DATAACTION_STOR,
	DATAACTION_APPE,

} e_data_action;

#define FCOMMAND(a,b,c,d) (((a) << 24) | ((b) << 16) | ((c) << 8) | (d))

enum
{
	//required commands for functional ftp
	FTPCMD_USER = FCOMMAND('u','s','e','r'),
	FTPCMD_QUIT = FCOMMAND('q','u','i','t'),
	FTPCMD_PORT = FCOMMAND('p','o','r','t'),
	FTPCMD_TYPE = FCOMMAND('t','y','p','e'),
	FTPCMD_MODE = FCOMMAND('m','o','d','e'),
	FTPCMD_STRU = FCOMMAND('s','t','r','u'),
	FTPCMD_RETR = FCOMMAND('r','e','t','r'),
	FTPCMD_STOR = FCOMMAND('s','t','o','r'),
	FTPCMD_STOU = FCOMMAND('s','t','o','u'),
	FTPCMD_NOOP = FCOMMAND('n','o','o','p'),

	//additional commands
	FTPCMD_PASS = FCOMMAND('p','a','s','s'),
	FTPCMD_CWD  = FCOMMAND(  0,'c','w','d'),
	FTPCMD_XCWD = FCOMMAND('x','c','w','d'),
	FTPCMD_CDUP = FCOMMAND('c','d','u','p'),
	FTPCMD_PASV = FCOMMAND('p','a','s','v'),
	FTPCMD_APPE = FCOMMAND('a','p','p','e'),
	FTPCMD_RNFR = FCOMMAND('r','n','f','r'),
	FTPCMD_RNTO = FCOMMAND('r','n','t','o'),
	FTPCMD_DELE = FCOMMAND('d','e','l','e'),
	FTPCMD_RMD  = FCOMMAND(  0,'r','m','d'),
	FTPCMD_XRMD = FCOMMAND('x','r','m','d'),
	FTPCMD_MKD  = FCOMMAND(  0,'m','k','d'),
	FTPCMD_XMKD = FCOMMAND('x','m','k','d'),
	FTPCMD_PWD  = FCOMMAND(  0,'p','w','d'),
	FTPCMD_XPWD = FCOMMAND('x','p','w','d'),
	FTPCMD_LIST = FCOMMAND('l','i','s','t'),
	FTPCMD_NLST = FCOMMAND('n','l','s','t'),
	FTPCMD_SITE = FCOMMAND('s','i','t','e'),
	FTPCMD_SYST = FCOMMAND('s','y','s','t'),
	FTPCMD_REST = FCOMMAND('r','e','s','t'),
	FTPCMD_SIZE = FCOMMAND('s','i','z','e'),

    FTPCMD_ABOR = FCOMMAND('a','b','o','r'),
	//FTPCMD_ACCT = FCOMMAND('a','c','c','t'),
	//FTPCMD_ALLO = FCOMMAND('a','l','l','o'),
	//FTPCMD_SMNT = FCOMMAND('s','m','n','t'),

    FTPCMD_FEAT = FCOMMAND('f','e','a','t'),
    FTPCMD_OPTS = FCOMMAND('o','p','t','s'),
    FTPCMD_MDTM = FCOMMAND('m','d','t','m'),
	//FTPCMD_STAT = FCOMMAND('s','t','a','t'),
	//FTPCMD_HELP = FCOMMAND('h','e','l','p'),

};

enum
{
	SITECMD_MNT  = FCOMMAND(  0,'m','n','t'),
	SITECMD_UMNT = FCOMMAND('u','m','n','t'),
	SITECMD_SYNC = FCOMMAND('s','y','n','c'),
	SITECMD_HELP = FCOMMAND('h','e','l','p'),
};


//int ftpsession_init();
//void ftpsession_exit();

struct _ftpserver;

typedef struct _ftp_session_info
{
    char username[128];
    uint32_t ip;
    int file_flag; //0-->idle, 1-->upload, 2-->download
    time_t file_start; //有文件下载上传时, 记录开始时间
    uint64_t total_size; //有文件下载上传时, 记录文件的大小
    uint64_t curr_size; //有文件下载上传时, 记录当前传输的大小
    char fname[SN_MAX_PATH];
    uint32_t session_id;

} ftp_session_info_t;

int ftpsession_add(const struct _ftpserver *p_server, int cmd_skt, uint32_t ip);
void ftpsession_clear(const struct _ftpserver *p_server);
int ftpsession_kill(uint32_t id, int transonly);

void ftp_get_session_info(void (*cb_session_info)(void *context, int type,
    uint32_t ip, const char *user, time_t start, int fflag, time_t fstart,
    uint64_t totalsize, uint64_t currsize, const char *fname, uint32_t session_id),
    void *context);


#ifdef __cplusplus
}
#endif

#endif // __FTPSESSION_H__

