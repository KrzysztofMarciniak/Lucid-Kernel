#ifndef LUCID_RECIPE_KMALLOC_TEST_H
#define LUCID_RECIPE_KMALLOC_TEST_H

/* REQUIRES: heap */

#include "status.h"

lucid_status kmalloc_test_pre(void);
lucid_status kmalloc_test_init(void);
lucid_status kmalloc_test_post(void);

#endif
