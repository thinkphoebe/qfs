
#ifndef __SWDEFINE_H__
#define __SWDEFINE_H__

/* 不使用的变量，为去除编译警告 */
#define SW_UNUSED(x) ((void)x)

#ifndef _MAX_PATH
#define _MAX_PATH 1024
#endif

#ifdef WORDS_BIGENDIAN
#define IS_MULTICAST_IP(ip) ( 0xE0000000<=(ip&0xFF000000) && (ip&0xFF000000)<0xF0000000 )
#else
#define IS_MULTICAST_IP(ip) ( 0xE0<=(ip&0xFF) && (ip&0xFF)<0xF0 )
#endif


#ifndef SW_FOURCC
#ifdef WORDS_BIGENDIAN
#define SW_FOURCC( a, b, c, d ) ( ((uint32_t)d) | ( ((uint32_t)c) << 8 )  | ( ((uint32_t)b) << 16 ) | ( ((uint32_t)a) << 24 ) )
#else
#define SW_FOURCC( a, b, c, d )   (((uint32_t)a) | ( ((uint32_t)b) << 8 ) | ( ((uint32_t)c) << 16 ) | ( ((uint32_t)d) << 24 ) )
#endif
#endif

#ifndef closesocket
#define closesocket close
#endif

#define FUNC_EXPORT __attribute__((visibility("default")))

#endif
