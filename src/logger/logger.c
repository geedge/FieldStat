#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <uuid/uuid.h>
#include "logger/log_handle.h"
#include "logger/logger.h"

static struct log_handle *g_log_handle = NULL;
#define MAX_FILE_NAME_LEN 1024  // 1024: refer to log_handle_create
static char g_log_file_path[MAX_FILE_NAME_LEN] = {0};
#define LOG_FILE_HEAD "fieldstat4-log"
#define LOG_FILE_TAIL ".log"
#define MODULE_NAME "fieldstat4"


void init_log_file_path()
{
    memset(g_log_file_path, 0, MAX_FILE_NAME_LEN);
    uuid_t uuid;
    uuid_generate(uuid);
    char uuid_str[37];
    uuid_unparse(uuid, uuid_str);
    snprintf(g_log_file_path, 1024, "%s-%s%s", LOG_FILE_HEAD, uuid_str, LOG_FILE_TAIL);
}

void logger_init()
{
    init_log_file_path();
    g_log_handle = log_handle_create(g_log_file_path, LOG_DEBUG);
}

void logger_destroy()
{
    log_handle_destroy(g_log_handle);
}

void logger_print_error(const char *fmt, ...)
{
    char contents[1024] = {0};
    va_list args;
    va_start(args, fmt);
    vsnprintf(contents, 1024, fmt, args);
    va_end(args);

    log_print(g_log_handle, LOG_ERROR, MODULE_NAME, contents);
}