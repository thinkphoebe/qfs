#ifndef __RULE_H__
#define __RULE_H__

#ifdef __cplusplus
extern "C" {
#endif


typedef enum
{
    RULEOPT_FTP_LOGIN = 0x01,
    RULEOPT_FTP_DOWN = 0x02,
    RULEOPT_FTP_UP = 0x04,
    RULEOPT_HTTP_LOGIN = 0x08,
    RULEOPT_HTTP_DOWN = 0x10,
    RULEOPT_HTTP_UP = 0x20,
} e_ruleopt;

int rule_init();
void rule_exit();

int rule_add(uint32_t ip, uint32_t mask, uint32_t opt, int duration, const char *filename);
int rule_match(uint32_t ip, uint32_t opt, const char *filename);



#ifdef __cplusplus
}
#endif

#endif // __RULE_H__

