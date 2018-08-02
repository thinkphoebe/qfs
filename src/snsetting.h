#ifndef __SNSETTING_H__
#define __SNSETTING_H__

#ifdef __cplusplus
extern "C" {
#endif


typedef struct _sn_setting sn_setting_t;

typedef enum
{
    SDT_NULL = 0,
    SDT_BOOL,       //值为true或false
    SDT_INTEGER,    //值为数字
    SDT_DOUBLE,     //值为包含小数点的数字
    SDT_STRING,     //值用""包含, 字符"需加转义符'\'
    SDT_TIME,       //值格式为year-month-dayThour:min:sec
    SDT_BINARY,     //值用''包含, 字符' 需加转义符'\'
} setting_data_type_t;


sn_setting_t* sn_setting_create();
void sn_setting_destory(sn_setting_t *h);

int sn_setting_load(sn_setting_t *h, const char *filename);
int sn_setting_save(sn_setting_t *h, const char *filename);


int sn_setting_delete(sn_setting_t *h, const char *session, const char *name);
setting_data_type_t sn_setting_query(sn_setting_t *h, const char *session, const char *name);
void sn_setting_print(sn_setting_t *h);


//以下函数读取失败时不修改输出参数的内容, 因此可将默认值填入输出参数
int sn_setting_read_bool(sn_setting_t *h, const char *session, const char *name, int *value/*out*/);
int sn_setting_write_bool(sn_setting_t *h, const char *session, const char *name, int value);

int sn_setting_read_integer(sn_setting_t *h, const char *session, const char *name, int *value/*out*/);
int sn_setting_write_integer(sn_setting_t *h, const char *session, const char *name, int value);

int sn_setting_read_double(sn_setting_t *h, const char *session, const char *name, double *value/*out*/);
int sn_setting_write_double(sn_setting_t *h, const char *session, const char *name, double value);

//value的内存snsetting模块分配, 外部负责释放
const char* sn_setting_read_string(sn_setting_t *h, const char *session, const char *name);
int sn_setting_write_string(sn_setting_t *h, const char *session, const char *name, const char *value);

int sn_setting_read_time(sn_setting_t *h, const char *session, const char *name, time_t *value/*out*/);
int sn_setting_write_time(sn_setting_t *h, const char *session, const char *name, time_t value);

//binary保存时使用base64编码
//value的内存snsetting模块分配, 外部负责释放. 返回值为实际读取的字节数. 读取错误返回-1.
int sn_setting_read_binary(sn_setting_t *h, const char *session, const char *name, char **value/*out*/);
//size为要求写入的字节数, 返回值为实际写入的字节数
int sn_setting_write_binary(sn_setting_t *h, const char *session, const char *name, const char *value, int size);

//迭代, 初次调用时将index指向NULL, 调用后以下函数填入位置信息, 下次调用时将上次填入的index传回
const char* sn_setting_get_session_name(sn_setting_t *h, void **index);
const char* sn_setting_get_value_name(sn_setting_t *h, const char *session, void **index);


#ifdef __cplusplus
}
#endif

#endif // __SNSETTING_H__

