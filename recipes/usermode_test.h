#ifndef LUCID_RECIPE_USERMODE_TEST_H
#define LUCID_RECIPE_USERMODE_TEST_H

/* REQUIRES: usermode, syscall */

#include "status.h"

lucid_status usermode_test_pre(void);
lucid_status usermode_test_init(void);
lucid_status usermode_test_post(void);

#endif
