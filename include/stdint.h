/* stdint.h - minimal freestanding replacement
 * We don't use the system/libc header because a freestanding kernel
 * shouldn't depend on the host libc, and different toolchains/distros
 * put it in different places. This covers everything a hobby kernel
 * typically needs; add more as required.
 */
#ifndef _MYOS_STDINT_H
#define _MYOS_STDINT_H

typedef unsigned char      uint8_t;
typedef signed char        int8_t;

typedef unsigned short     uint16_t;
typedef signed short       int16_t;

typedef unsigned int       uint32_t;
typedef signed int         int32_t;

typedef unsigned long long uint64_t;
typedef signed long long   int64_t;

#if defined(__x86_64__)
typedef unsigned long      uintptr_t;
typedef signed long        intptr_t;
#else
typedef unsigned int       uintptr_t;
typedef signed int         intptr_t;
#endif

#endif /* _MYOS_STDINT_H */
