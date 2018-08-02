#ifndef __UTILS_H__
#define __UTILS_H__

#ifdef __cplusplus
extern "C" {
#endif


#include "snlog.h"


#define THRD_STACK_SIZE (1024 * 64) //64K
int run_thread(void* (*start_routine)(void *), void* arg);
void exit_thread();

#ifdef WIN32
#define LLD "I64d"
#else
#define LLD "lld"
#endif

#ifndef WIN32
#define closesocket close
#endif

#ifdef WIN32
#define socklen_t int
#ifndef localtime_r
#define localtime_r(_clock, _result) (*(_result) = *localtime((_clock)), (_result))
#endif
#endif

#define IS_MULTICAST_IP(ip) (0xE0000000 <= (ip & 0xFF000000) && (ip & 0xFF000000) < 0xF0000000)

typedef enum
{
    LOG_DEBUG,
    LOG_INFO,
    LOG_WARN,
    LOG_ERROR,
} log_level;

uint32_t get_tick();
/*void print_log(const char *name, int control, const char *format, ...);*/

void url_decode(char *url);
int url_encode(char *in, char *out, int size);

void time2str(const time_t *time, char result[32]);
void size2str(uint64_t size, char result[32]);


#ifdef __cplusplus
}
#endif

#endif // __UTILS_H__

