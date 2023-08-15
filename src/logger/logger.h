
#pragma once
#include <stdio.h>
#ifdef __cplusplus
extern "C"
{
#endif

void logger_init();

void logger_destroy();

void logger_print_error(const char *fmt, ...);

#ifdef __cplusplus
}
#endif