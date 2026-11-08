#ifndef LUCID_RECIPE_SYSCALL_POSIX_H
#define LUCID_RECIPE_SYSCALL_POSIX_H

#include "syscall_abi.h"

#include "syscall_posix_open.h"
#include "syscall_posix_read.h"
#include "syscall_posix_write.h"
#include "syscall_posix_close.h"
#include "syscall_posix_lseek.h"
#include "syscall_posix_unlink.h"
#include "syscall_posix_access.h"
#include "syscall_posix_getcwd.h"

#define LUCID_POSIX_O_RDONLY 0x0000u
#define LUCID_POSIX_O_WRONLY 0x0001u
#define LUCID_POSIX_O_RDWR 0x0002u
#define LUCID_POSIX_O_ACCMODE 0x0003u
#define LUCID_POSIX_O_CREAT 0x0040u
#define LUCID_POSIX_O_EXCL 0x0080u
#define LUCID_POSIX_O_TRUNC 0x0200u
#define LUCID_POSIX_O_APPEND 0x0400u

#endif
