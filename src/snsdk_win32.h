#ifndef __SWAPI_WIN32_H__
#define __SWAPI_WIN32_H__


#define NOTINITIALISED  WSANOTINITIALISED
#define ENETDOWN  WSAENETDOWN
#define EINPROGRESS WSAEINPROGRESS
#define ENOPROTOOPT WSAENOPROTOOPT
#define ENOTSOCK  WSAENOTSOCK
#define EWOULDBLOCK WSAEWOULDBLOCK
#define ECONNREFUSED WSAECONNREFUSED

#define in_addr_t unsigned long


#include <windows.h>
#include <winbase.h>
#include <winsock.h>
#include <winsock2.h>
#include <ws2tcpip.h>

//#include <sys/ioctl.h>

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdarg.h>
#include <stddef.h>

#include <pthread.h>
#include <semaphore.h>
#include <time.h>

//for gettimeofday
#include <sys/time.h>

#if 0
#ifndef strlcpy
#define strlcpy(dest,src,len) \
    do {\
        *((char *)dest + len - 1) = '0'; \
        strncpy(dest, src, len - 1); \
    } while (0)
#endif

#ifndef strlcat
#define strlcat(dest,src,len) \
    do {\
        *((char *)dest + len - 1) = '0'; \
        strncat(dest, src, len - 1); \
    } while (0)
#endif
#endif

#ifndef drand48
#define drand48() ((double)(rand() << 16 | rand()) / (double)0xFFFFFFFF)
#endif

#ifndef lrand48
#define lrand48() ((long)(rand() << 16 | rand()))
#endif

#define fseeko fseeko64
#define ftello ftello64
#define lstat stat

#define ioctl ioctlsocket

#ifndef localtime_r
#define localtime_r(_clock, _result) (*(_result) = *localtime((_clock)), (_result))
#endif


#endif
