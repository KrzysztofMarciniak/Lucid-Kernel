#ifndef LUCID_LIBC_INTERNAL_H
#define LUCID_LIBC_INTERNAL_H

#include <stdint.h>

#ifdef LUCID_USER_SYSCALL_TEST
uint32_t lucid_libc_test_transport(uint32_t number,
                                   uint32_t arg1,
                                   uint32_t arg2,
                                   uint32_t arg3,
                                   uint32_t arg4);
#endif

#endif
