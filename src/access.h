#ifndef __ACCESS_H__
#define __ACCESS_H__

#ifdef __cplusplus
extern "C" {
#endif


typedef enum
{
    E_AUTH_NULL       = 0x0,
    E_AUTH_HTTP_READE = 0x01,
    E_AUTH_HTTP_WRITE = 0x02,
    E_AUTH_FTP_READE  = 0x04,
    E_AUTH_FTP_WRITE  = 0x08,
} e_auth_type;

int access_init();
void access_exit();

//用户不存在且添加成功时返回0.
//over_write非0时, 如果用户已存在则覆盖, 返回值为-2. 否则, 不覆盖且返回-1.
int access_add_user(const char *name, const char *pass, uint32_t mask, int over_write);
int access_del_user(const char *name);

//name存在且passwd正确时返回0, 并设置auth_mask. 否则返回-1
//ATTENTION 注意用户过多时的效率
int access_get_auth_state(const char *name, const char *pass, uint32_t *auth_mask/*out*/);
const char* access_get_pswd(const char *name);


#ifdef __cplusplus
}
#endif

#endif // __ACCESS_H__

