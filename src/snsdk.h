#ifndef __SWAPI_H__
#define __SWAPI_H__


#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <stdarg.h>
#include <ctype.h>
#include <assert.h>
#include <math.h>

#include <sys/stat.h>

#ifdef	WIN32
#include "snsdk_win32.h"
#endif


#ifdef LINUX
#include "snsdk_linux.h"
#endif

#ifdef MAC
#include "snsdk_mac.h"
#endif

#include "sntype.h"
#include "sndefine.h"

#ifdef POINTER_64BIT
#define POINTERINT int64_t   
#define POINTERUINT uint64_t 
#define POINTERD "lld"       
#else
#define POINTERINT int32_t   
#define POINTERUINT uint32_t 
#define POINTERD "d"       
#endif


#endif
