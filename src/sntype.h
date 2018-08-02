/** 
 * @file swtype.h
 * @brief 类型定义
 * @author  Dou Hongchen 
 * @date 2007-11-19
 *
 */

#ifndef __SWTYPE_H__
#define __SWTYPE_H__


#ifndef __cplusplus

#ifndef bool
#define bool uint8_t
#endif	

#ifndef true
#define true 1
#endif	

#ifndef false
#define false 0
#endif  

#endif


#ifndef HANDLE
#define HANDLE void*
#endif

#ifndef HANLDE
#define HANLDE HANDLE
#endif


#ifndef SYSHANDLE
#define SYSHANDLE HANDLE
#endif

#ifndef LPVOID
#define LPVOID void*
#endif

#ifndef BOOL
#define BOOL int
#endif

#ifndef FALSE
#define FALSE 0
#endif

#ifndef TRUE
#define TRUE 1
#endif


#ifdef SW_OK
#undef SW_OK
#endif

#ifdef SW_ERROR
#undef SW_ERROR
#endif

#ifdef SW_STATUS
#undef SW_STATUS
#endif 

typedef unsigned int					SW_STATUS;  	/**<Sunniwell函数返回类型*/
#define SW_OK							0x00000000		/**<返回成功*/
#define SW_ERROR 						0xFFFFFFFF 		/**<一般错误*/									
#define SW_ERROR_INVALID_PARAMETER 		0x80000000 		/**<无效的参数*/
#define SW_ERROR_NOMEMORY		 		0x80000001 		/**<内存不足*/
#define SW_ERROR_SYSERR			 		0x80000002 		/**<系统错误*/
#define SW_ERROR_NOTINIT				0x80000003 		/**<没有初始化*/
#define SW_ERROR_NOTSUPPORT				0x80000004 		/**<不支持*/
#define SW_ERROR_TIMEOUT				0x80000005 		/**<超时*/

#ifndef NO_WAIT
#define NO_WAIT       0
#endif

#ifndef INFINITE
#define INFINITE -1
#endif

#ifndef WAIT_FOREVER
#define WAIT_FOREVER INFINITE
#endif



#ifndef SOCKET
#define SOCKET int
#endif

#ifndef SOCKET_ERROR
#define SOCKET_ERROR -1
#endif


#ifndef INVALID_SOCKET
#define INVALID_SOCKET -1
#endif

/**
 * @点坐标定义
 */
typedef struct _point{
	
	int x;	/**<X坐标*/
	int y;	/**<Y坐标*/
}swpoint_t;

/**
 * @矩形框定义
 */
typedef struct _rect{
	
	int x;				/**<X坐标*/
	int y;				/**<Y坐标*/
	unsigned int width; /**<宽度*/
	unsigned int height;/**<高度*/ 
}swrect_t;

#endif
