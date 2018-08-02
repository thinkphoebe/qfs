#ifndef __EVENT_H__
#define __EVENT_H__

#ifdef __cplusplus
extern "C" {
#endif


typedef enum
{
    SEVENT_NULL,

    SEVENT_BIND_FAILED, //wparam: 0-->http, 1-->ftp; lparam: port

    SEVENT_FTP_CONNECT, //identifier-->server句柄, wparam-->ip, lparam-->port
    SEVENT_FTP_LOGIN,   //identifier-->session句柄, wparam: 0-->succeed, 1-->failed; lparam-->username
    SEVENT_FTP_QUIT,    //identifier-->session句柄
    SEVENT_FTP_KILL,    //TODO
    SEVENT_FTP_GET,     //identifier-->session句柄, wparam-->file, lparam: 0-->start, 1-->complete, 2-->failed
    SEVENT_FTP_PUT,     //同上
    SEVENT_FTP_MKD,     //identifier-->session句柄, wparam-->dir, lparam:0-->succeed, 1-->failed
    SEVENT_FTP_RMD,     //identifier-->session句柄, wparam-->dir, lparam:0-->succeed, 1-->failed 
    SEVENT_FTP_DEL,     //identifier-->session句柄, wparam-->file, lparam:0-->succeed, 1-->failed  
    SEVENT_FTP_RENAME,  ///dentifier-->session句柄, wparam-->to file, lparam:0-->succeed, 1-->failed  

    SEVENT_HTTP_GET,    //identifier-->session句柄, wparam-->path, lparam: 0-->start, 1-->complete, 2-->failed
    SEVENT_HTTP_POST,   //identifier-->session句柄, wparam-->path(lparam为1时有效), lparam: 0-->start, 1-->complete, 2-->failed

    SEVENT_MAX

} e_sevent;


typedef void (*cb_event_t)(void *context, e_sevent type, void *identifier, unsigned long wparam, unsigned long lparam);

//上层调用这个接口注册事件回调
void set_event_callback(cb_event_t cbfun, void *context);

//server内部调用这个接口抛事件, 事件回调中不应有耗时的操作
//TODO 将事件加入内部队列, 由单独线程向外抛
void send_event(e_sevent type, void *identifier, unsigned long wparam, unsigned long lparam);


#ifdef __cplusplus
}
#endif

#endif // __EVENT_H__

