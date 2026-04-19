/**
 * @file logger.c
 * @brief Logger abstraction layer implementation
 */

#include "hal/logger.h"
#include "hal/platform.h"
#include "common/macros.h"
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdio.h>

/* ========== Global Logger ========== */

static logger_t *g_logger = NULL;

/* ========== Implementation Registry ========== */

#define MAX_LOGGER_IMPLS 8

static const logger_impl_t *g_logger_impls[MAX_LOGGER_IMPLS] = {NULL};
static int g_logger_impl_count = 0;
static const logger_impl_t *g_default_impl = NULL;

/* ========== Level Names ========== */

static const char *level_names[] = {
    "TRACE", "DEBUG", "INFO", "WARN", "ERROR", "FATAL", "OFF"
};

const char *log_level_name(log_level_t level) {
    if (level >= LOG_LEVEL_OFF) return "OFF";
    return level_names[level];
}

log_level_t log_level_parse(const char *str) {
    if (str == NULL) return LOG_LEVEL_INFO;

    for (int i = 0; i < (int)LOG_LEVEL_OFF; i++) {
        if (strcasecmp(str, level_names[i]) == 0) {
            return (log_level_t)i;
        }
    }
    return LOG_LEVEL_INFO;
}

log_config_t log_config_default(void) {
    log_config_t config;
    memset(&config, 0, sizeof(config));
    config.level = LOG_LEVEL_INFO;
    config.output_type = LOG_OUTPUT_CONSOLE;
    config.format = LOG_FORMAT_DEFAULT;
    config.time_format = LOG_TIME_FULL;
    config.flush_on_write = false;
    config.quiet = false;
    return config;
}

/* ========== Implementation Registration ========== */

int logger_register_impl(const logger_impl_t *impl) {
    if (impl == NULL) return CFDDNS_ERR_NULL_POINTER;
    if (g_logger_impl_count >= MAX_LOGGER_IMPLS) return CFDDNS_ERR_OUT_OF_MEMORY;

    g_logger_impls[g_logger_impl_count++] = impl;

    if (g_default_impl == NULL) {
        g_default_impl = impl;
    }

    return CFDDNS_OK;
}

static const logger_impl_t *find_impl(const char *name) {
    if (name == NULL) return g_default_impl;

    for (int i = 0; i < g_logger_impl_count; i++) {
        if (strcasecmp(g_logger_impls[i]->name, name) == 0) {
            return g_logger_impls[i];
        }
    }
    return g_default_impl;
}

/* ========== Global Logger Functions ========== */

int logger_init(const log_config_t *config) {
    return logger_init_with_impl(NULL, config);
}

int logger_init_with_impl(const char *impl_name, const log_config_t *config) {
    /* Auto-register implementations */
    if (g_logger_impl_count == 0) {
        extern int logger_register_console(void);
        extern int logger_register_file(void);
        extern int logger_register_syslog(void);

        logger_register_console();
        logger_register_file();
        logger_register_syslog();
    }

    if (g_logger != NULL) {
        logger_cleanup();
    }

    log_config_t cfg = (config != NULL) ? *config : log_config_default();

    g_logger = logger_create(impl_name, &cfg);
    if (g_logger == NULL) return CFDDNS_ERR_OUT_OF_MEMORY;

    return CFDDNS_OK;
}

void logger_cleanup(void) {
    if (g_logger != NULL) {
        logger_destroy(g_logger);
        g_logger = NULL;
    }
}

logger_t *logger_get(void) {
    return g_logger;
}

void logger_set_level(log_level_t level) {
    if (g_logger != NULL) {
        logger_set_level_inst(g_logger, level);
    }
}

log_level_t logger_get_level(void) {
    if (g_logger != NULL) {
        return g_logger->level;
    }
    return LOG_LEVEL_INFO;
}

void logger_set_quiet(bool quiet) {
    if (g_logger != NULL) {
        g_logger->quiet = quiet;
    }
}

void logger_flush(void) {
    if (g_logger != NULL) {
        logger_flush_inst(g_logger);
    }
}

/* ========== Logger Instance Functions ========== */

logger_t *logger_create(const char *impl_name, const log_config_t *config) {
    const logger_impl_t *impl = find_impl(impl_name);
    if (impl == NULL) return NULL;

    logger_t *logger = malloc(sizeof(logger_t));
    if (logger == NULL) return NULL;

    memset(logger, 0, sizeof(logger_t));
    logger->impl = impl;
    logger->level = (config != NULL) ? config->level : LOG_LEVEL_INFO;
    logger->quiet = (config != NULL) ? config->quiet : false;

    /* Create implementation handle */
    logger->handle = impl->create(config);
    if (logger->handle == NULL) {
        free(logger);
        return NULL;
    }

    return logger;
}

void logger_destroy(logger_t *logger) {
    if (logger == NULL) return;

    if (logger->impl != NULL && logger->impl->destroy != NULL) {
        logger->impl->destroy(logger->handle);
    }

    CFDDNS_FREE(logger->name);
    free(logger);
}

void logger_log(logger_t *logger, log_level_t level,
                const char *file, int line, const char *func,
                const char *fmt, ...) {
    const logger_impl_t *impl = NULL;
    void *handle = NULL;

    if (logger == NULL) {
        logger = g_logger;
    }

    if (logger == NULL) {
        /* No logger configured, output to stderr */
        if (level >= LOG_LEVEL_WARN) {
            va_list args;
            va_start(args, fmt);
            fprintf(stderr, "[%s] %s:%d: ", log_level_name(level), file, line);
            vfprintf(stderr, fmt, args);
            fprintf(stderr, "\n");
            va_end(args);
        }
        return;
    }

    /* Check level */
    if (level < logger->level) return;
    if (logger->quiet) return;

    impl = logger->impl;
    handle = logger->handle;

    if (impl == NULL || impl->log == NULL) return;

    va_list args;
    va_start(args, fmt);
    impl->log(handle, level, file, line, func, fmt, args);
    va_end(args);
}

void logger_log_v(logger_t *logger, log_level_t level,
                   const char *file, int line, const char *func,
                   const char *fmt, va_list args) {
    const logger_impl_t *impl = NULL;
    void *handle = NULL;

    if (logger == NULL) {
        logger = g_logger;
    }

    if (logger == NULL) return;
    if (level < logger->level) return;
    if (logger->quiet) return;

    impl = logger->impl;
    handle = logger->handle;

    if (impl == NULL || impl->log == NULL) return;

    impl->log(handle, level, file, line, func, fmt, args);
}

void logger_set_level_inst(logger_t *logger, log_level_t level) {
    if (logger == NULL) return;

    logger->level = level;

    if (logger->impl != NULL && logger->impl->set_level != NULL) {
        logger->impl->set_level(logger->handle, level);
    }
}

void logger_flush_inst(logger_t *logger) {
    if (logger == NULL) return;

    if (logger->impl != NULL && logger->impl->flush != NULL) {
        logger->impl->flush(logger->handle);
    }
}

bool logger_is_level_enabled(logger_t *logger, log_level_t level) {
    if (logger == NULL) logger = g_logger;
    if (logger == NULL) return (level >= LOG_LEVEL_WARN);

    return (level >= logger->level);
}