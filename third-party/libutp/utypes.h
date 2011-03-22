#ifndef __UTYPES_H__
#define __UTYPES_H__

// standard types
typedef unsigned char byte;
typedef unsigned char uint8;
typedef signed char int8;
typedef unsigned short uint16;
typedef signed short int16;
typedef unsigned int uint;
typedef unsigned int uint32;
typedef signed int int32;

#ifdef _MSC_VER
typedef unsigned __int64 uint64;
typedef signed __int64 int64;
#else
typedef unsigned long long uint64;
typedef long long int64;
#endif

/* compile-time assert */
#ifndef CASSERT
#define CASSERT( exp, name ) typedef int is_not_##name [ (exp ) ? 1 : -1 ];
#endif

CASSERT(8 == sizeof(uint64), sizeof_uint64_is_8)
CASSERT(8 == sizeof(int64), sizeof_int64_is_8)

#ifndef INT64_MAX
#define INT64_MAX 0x7fffffffffffffffLL
#endif

// always ANSI
typedef const char * cstr;
typedef char * str;

#ifndef __cplusplus
#include <stdbool.h>
#endif

#endif //__UTYPES_H__
