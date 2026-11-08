#ifndef LUCID_SYS_TYPES_H
#define LUCID_SYS_TYPES_H

#include <stdint.h>

#ifndef __ssize_t_defined
typedef int32_t ssize_t;
#define __ssize_t_defined
#endif

#ifndef __off_t_defined
typedef int32_t off_t;
#define __off_t_defined
#endif

#endif
