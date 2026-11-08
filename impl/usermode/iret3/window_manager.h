#ifndef LUCID_IMPL_USERMODE_IRET3_WINDOW_MANAGER_H
#define LUCID_IMPL_USERMODE_IRET3_WINDOW_MANAGER_H

#include "stdint.h"

void window_manager_manager_started(uint32_t slot);
void window_manager_window_started(uint32_t slot);
void window_manager_process_exited(uint32_t slot);
void window_manager_process_released(uint32_t slot);

#endif
