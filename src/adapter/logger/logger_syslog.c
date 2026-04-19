/**
 * @file logger_syslog.c
 * @brief Syslog logger implementation stub
 */

#include "hal/logger.h"
#include "common/types.h"
#include "common/macros.h"

#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

/* ========== Syslog Logger Stub ========== */

static void *syslog_create(const log_config_t *config) {
    (void)config;
    return malloc(1);  /* Stub implementation */
}

static void syslog_destroy(void *handle) {
    if (handle != NULL) {
        free(handle);
    }
}

static void syslog_log(void *handle, log_level_t level,
                       const char *file, int line, const char *func,
                       const char *fmt, va_list args) {
    (void)handle;
    (void)level;
    (void)file;
    (void)line;
    (void)func;
    (void)fmt;
    (void)args;
    /* Stub implementation */
}

static void syslog_flush(void *handle) {
    (void)handle;
    /* Stub implementation */
}

static void syslog_set_level(void *handle, log_level_t level) {
    (void)handle;
    (void)level;
    /* Stub implementation */
}

/* ========== Implementation Registration ========== */

static logger_impl_t syslog_impl = {
    .name        = "syslog",
    .output_type = LOG_OUTPUT_SYSLOG,
    .create      = syslog_create,
    .destroy     = syslog_destroy,
    .log         = syslog_log,
    .flush       = syslog_flush,
    .set_level   = syslog_set_level,
    .reopen      = NULL,
};

int logger_register_syslog(void) {
    return logger_register_impl(&syslog_impl);
}