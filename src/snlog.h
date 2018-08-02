//TODO 按日期保存文件, 限定文件大小

#ifndef __SNLOG_H__
#define __SNLOG_H__

#ifdef __cplusplus
extern "C" {
#endif


typedef enum
{
    SN_LOG_LEVEL_ALL   = 0x0,	
    SN_LOG_LEVEL_DEBUG = 0x01,	
    SN_LOG_LEVEL_INFO  = 0x02,	
    SN_LOG_LEVEL_WARN  = 0x03,	
    SN_LOG_LEVEL_ERROR = 0x04,	
    SN_LOG_LEVEL_FATAL = 0x05,	
    SN_LOG_LEVEL_OFF   = 0xff

} e_log_level;

typedef enum
{
    SN_LOG_TARGET_NULL      = 0x0,
    SN_LOG_TARGET_CONSOLE   = 0x1,
    SN_LOG_TARGET_FILE      = 0x2,
    SN_LOG_TARGET_UDP       = 0x4

} e_log_target;

typedef struct _sn_log_module 
{
    const char *name;
    e_log_level level;

} sn_log_module_t;


int sn_log_init(e_log_level level);
void sn_log_exit();


int sn_log_set_level(e_log_level level);
int sn_log_get_level();


int sn_log_add_target(e_log_target target, unsigned long wparam, unsigned long lparam);
int sn_log_del_target(e_log_target target);
void sn_log_clear_target();

int sn_log(e_log_level level, sn_log_module_t *module, const char *function, int line, char *format, ...);


#if defined(DISABLE_LOG)

#define sn_log_debug(...)  ;
#define sn_log_info(...)   ;
#define sn_log_warn(...)   ;
#define sn_log_error(...)  ;
#define sn_log_fatal(...)  ;

#elif defined(SUPPORT_MSGMODULE)

#define sn_log_debug(...) sn_log(SN_LOG_LEVEL_DEBUG, module, __FUNCTION__, __LINE__, __VA_ARGS__);
#define sn_log_info(...)  sn_log(SN_LOG_LEVEL_INFO,  module, __FUNCTION__, __LINE__, __VA_ARGS__);
#define sn_log_warn(...)  sn_log(SN_LOG_LEVEL_WARN,  module, __FUNCTION__, __LINE__, __VA_ARGS__);
#define sn_log_error(...) sn_log(SN_LOG_LEVEL_ERROR, module, __FUNCTION__, __LINE__, __VA_ARGS__);
#define sn_log_fatal(...) sn_log(SN_LOG_LEVEL_FATAL, module, __FUNCTION__, __LINE__, __VA_ARGS__);

#else

#define sn_log_debug(...) sn_log(SN_LOG_LEVEL_DEBUG, NULL, __FUNCTION__, __LINE__, __VA_ARGS__);
#define sn_log_info(...)  sn_log(SN_LOG_LEVEL_INFO,  NULL, __FUNCTION__, __LINE__, __VA_ARGS__);
#define sn_log_warn(...)  sn_log(SN_LOG_LEVEL_WARN,  NULL, __FUNCTION__, __LINE__, __VA_ARGS__);
#define sn_log_error(...) sn_log(SN_LOG_LEVEL_ERROR, NULL, __FUNCTION__, __LINE__, __VA_ARGS__);
#define sn_log_fatal(...) sn_log(SN_LOG_LEVEL_FATAL, NULL, __FUNCTION__, __LINE__, __VA_ARGS__);

#endif


#ifdef __cplusplus
}
#endif

#endif // __SNLOG_H__

