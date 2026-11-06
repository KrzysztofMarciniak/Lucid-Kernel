#ifndef LUCID_RECIPE_ACPI_H
#define LUCID_RECIPE_ACPI_H

#include "status.h"

lucid_status acpi_pre(void);
lucid_status acpi_init(void);
lucid_status acpi_post(void);

void acpi_poweroff(void);

#endif
