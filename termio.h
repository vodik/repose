#pragma once

#include <stdbool.h>
#include "util.h"

void enable_colors(void);
void colon_printf(const char *fmt, ...) _printf_(1, 2);
