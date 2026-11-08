#ifndef LUCID_IMPL_USERMODE_IRET3_PROCESS_INTERNAL_H
#define LUCID_IMPL_USERMODE_IRET3_PROCESS_INTERNAL_H

#include "stdint.h"

int usermode_process_window_active(uint32_t slot);
int usermode_process_window_exists(uint32_t slot);
uint32_t usermode_process_window_desktop_raw(uint32_t slot);
void usermode_process_set_window_desktop(uint32_t slot, uint32_t desktop);
void usermode_process_close_window(uint32_t slot);
void usermode_process_enqueue_key(uint32_t slot, uint32_t key);

#endif
