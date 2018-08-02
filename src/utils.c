#ifdef WIN32
#include <windows.h>
#include <winbase.h>
#else
#include <sys/times.h>
#include <unistd.h> //for usleep
#endif

#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h> //for memset
#include <stdarg.h> //for va_list, va_start, va_end
#include <pthread.h> //-lpthread

#include "utils.h"


int run_thread(void* (*start_routine)(void *), void* arg)
{
    pthread_t id;
    pthread_attr_t attr;
    size_t stack_size;
    int ret;

    ret = pthread_attr_init(&attr);
    if (ret != 0)
    {
        sn_log_error("pthread_attr_init FAILED:%s\n", strerror(ret));
        return -1;
    }

    ret = pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    if (ret != 0)
    {
        sn_log_error("pthread_attr_setdetachstate FAILED:%s\n", strerror(ret));
        return -1;
    }

    //得到当前的线程栈大小
    //ret = pthread_attr_getstacksize(&attr, &stack_size);
    //if (ret != 0)
    //{
    //    sn_log_error("pthread_attr_getstacksize FAILED:%s\n", strerror(ret));
    //    return -1;
    //}
    //sn_log_debug("default stack size:%u\n", stack_size);

    //设置当前的线程的大小
    ret = pthread_attr_setstacksize(&attr, THRD_STACK_SIZE);
    if (ret != 0)
    {
        sn_log_error("pthread_attr_setstacksize FAILED:%s\n", strerror(ret));
        return -1;
    }

    // 得到当前的线程栈的大小
    ret = pthread_attr_getstacksize(&attr, &stack_size);
    if (ret != 0)
    {
        sn_log_error("pthread_attr_getstacksize FAILED:%s\n", strerror(ret));
        return -1;
    }
    //sn_log_debug("current stack size:%u\n", stack_size);

    ret = pthread_create(&id, &attr, start_routine, arg);
    if (ret != 0)
    {
        sn_log_error("pthread_create FAILED:%s\n", strerror(ret));
        return -1;
    }

    return 0;
}


void exit_thread()
{
    pthread_exit(NULL);
}


#ifdef WIN32
uint32_t get_tick()
{
    //return GetTickCount();
    return timeGetTime();
}
#else
uint32_t get_tick()
{
   struct tms tm;
   static uint32_t timeorigin;
   static int firsttimehere = 0;
   uint32_t now = times(&tm);
   if(firsttimehere == 0)
   {
       timeorigin = now;
       firsttimehere = 1;
   }

   //unsigned long HZ = sysconf(_SC_CLK_TCK); 
   //now的单位是tick,每秒的tick数, 上面代码可获得每秒的tick数, 应该是100
   //因此下面乘10转换为ms
   return (now - timeorigin)*10;  
}
#endif


//int g_log_level = 1;
//FILE *g_fp_log;

//void print_log(const char *name, int control, const char *format, ...)
//{
//    va_list args;
    
//    if (g_fp_log == NULL)
//        g_fp_log = stdout;

//    if (control < g_log_level)
//        return;
//    if (name == NULL)
//        name = "NONAME";
//    //sn_log_debug("[%d](%s)", get_tick(), name);
//    fprintf(g_fp_log, "[%d](%s)", get_tick(), name);

//    va_start(args, format);
//    //vprintf(format, args);
//    vfprintf(g_fp_log, format, args);
//    va_end(args);
//}


void url_decode(char *url)
{
    char buf[3];
    int len = strlen(url);
    int i = 0, j = 0;
    buf[2] = 0;
    while (i < len)
    {
        if (url[i] == '%')
        {
            buf[0] = url[i + 1];
            buf[1] = url[i + 2];
            url[j] = strtol(buf, NULL, 16);
            i += 3;
        }
        else
        {
            if (i > j)
                url[j] = url[i];
            i++;
        }
        j++;
    }
    url[j] = '\0';
}


int url_encode(char *in, char *out, int size)
{
	int i = 0;
	char table[] = {'0','1','2','3','4','5','6','7','8','9','A','B','C','D','E','F'};
	int count = 0;
	int len = strlen(in);
	for (i = 0; i < len && count < size - 4; i++)
	{
	 	if (in[i] >= 0x21 && in[i] <= 0x7E && in[i] != '<' && in[i] != '>' && in[i] != '^'
                && in[i] != '[' && in[i] != ']' && in[i] != '{' && in[i] != '}' && in[i] != '%')
		{
			out[count++] = in[i];
		}
		else
		{
	    	out[count++] = '%';
	    	out[count++] = table[((unsigned char)in[i]) / 16];
	    	out[count++] = table[((unsigned char)in[i]) % 16];
		}
	}
	out[count++] = '\0';
	return strlen(out);
}


void time2str(const time_t *time, char result[32])
{
    struct tm st;
    //gmtime_r(&info->timestamp, &st);
    localtime_r(time, &st);
    memset(result, 0, 32);
    snprintf(result, 31, "%04i-%02i-%02i %02i:%02i", st.tm_year + 1900, st.tm_mon + 1, st.tm_mday, st.tm_hour, st.tm_min);
}


void size2str(uint64_t size, char result[32])
{
    memset(result, 0, 32);
    if (size < 1024)
        snprintf(result, 31, "%8i B", (uint32_t)size);
    else if (size < 1024 * 1024)
        snprintf(result, 31, "%8i KB", (uint32_t)(size / 1024));
    else if (size < 1024 * 1024 * 1024)
        snprintf(result, 31, "%4i.%3i MB", (uint32_t)(size / 1024 / 1024),
                       (uint32_t)((size % (1024 * 1024)) / 1024 * 1000 / 1024));
    else if (size < 1024LL * 1024 * 1024 * 1024)
        snprintf(result, 31, "%4i.%3i GB", (uint32_t)(size / 1024 / 1024 / 1024),
                       (uint32_t)((size % (1024 * 1024 * 1024)) / (1024 * 1024) * 1000 / 1024));
}

