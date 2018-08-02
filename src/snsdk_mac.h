#ifndef __SWAPI_LINUX_H__
#define __SWAPI_LINUX_H__


#include <pthread.h>
#include <semaphore.h>
#include <errno.h>
#include <inttypes.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>
#include <getopt.h>
#include <termios.h>
#include <errno.h>
#include <netdb.h>
#include <arpa/inet.h>

#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/udp.h>

#include <net/if.h> 
#include <net/if_arp.h> 

#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/param.h> 
#include <sys/types.h>
#include <sys/timeb.h>
#include <sys/time.h>
#include <sys/reboot.h>
#include <dlfcn.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h> //for offsetof
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>


#ifdef SUPPORT_LIBC

#ifdef __cplusplus
extern "C"
{
#endif

size_t strlcpy(char *dst, const char *src, size_t siz);
size_t strlcat(char *dst, const char *src, size_t siz);

#ifdef __cplusplus
}
#endif

#endif


#endif
