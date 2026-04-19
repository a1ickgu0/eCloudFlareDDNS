/**
 * @file logger_console.c
 * @brief Console logger implementation
 */

#include "hal/logger.h"
#include "hal/platform.h"
#include "common/types.h"
#include "common/macros.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <time.h>
#include <unistd.h>

/* ========== Console Logger Handle ========== */

typedef struct {
    log_config_t config;
    FILE *output;
    bool use_colors;
} console_logger_t;

/* ========== ANSI Color Codes ========== */

static const char *level_colors[] = {
    "\033[90m",    /* TRACE - gray */
    "\033[36m",    /* DEBUG - cyan */
    "\033[32m",    /* INFO  - green */
    "\033[33m",    /* WARN  - yellow */
    "\033[31m",    /* ERROR - red */
    "\033[35m",    /* FATAL - magenta */
};

static const char *color_reset = "\033[0m";

/* ========== Implementation Functions ========== */

static void *console_create(const log_config_t *config) {
    console_logger_t *handle = malloc(sizeof(console_logger_t));
    if (handle == NULL) return NULL;

    memset(handle, 0, sizeof(console_logger_t));

    if (config != NULL) {
        handle->config = *config;
    } else {
        handle->config = log_config_default();
    }

    /* Choose output stream based on level */
    handle->output = (handle->config.level >= LOG_LEVEL_WARN) ? stderr : stdout;

    /* Check if colors are supported */
    handle->use_colors = (config != NULL && (config->format & LOG_FORMAT_COLOR)) &&
                         isatty(fileno(handle->output));

    return handle;
}

static void console_destroy(void *handle) {
    if (handle != NULL) {
        free(handle);
    }
}

static void console_log(void *handle, log_level_t level,
                        const char *file, int line, const char *func,
                        const char *fmt, va_list args) {
    console_logger_t *cl = (console_logger_t *)handle;
    if (cl == NULL || cl->output == NULL) return;

    /* Get current time */
    uint64_t now_ms = timer_get_time_ms();
    time_t now_s = (time_t)(now_ms / 1000);
    struct tm tm;
    localtime_r(&now_s, &tm);

    char timestamp[32];
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", &tm);

    /* Build log line */
    if (cl->use_colors && level < LOG_LEVEL_OFF) {
        fprintf(cl->output, "%s[%s] %s%s ",
                level_colors[level], log_level_name(level), timestamp, color_reset);

        if (cl->config.format & LOG_FORMAT_FILE) {
            fprintf(cl->output, "%s:%d ", file, line);
        }
        if (cl->config.format & LOG_FORMAT_FUNC) {
            fprintf(cl->output, "[%s] ", func);
        }
    } else {
        fprintf(cl->output, "[%s] %s ", log_level_name(level), timestamp);

        if (cl->config.format & LOG_FORMAT_FILE) {
            fprintf(cl->output, "%s:%d ", file, line);
        }
        if (cl->config.format & LOG_FORMAT_FUNC) {
            fprintf(cl->output, "[%s] ", func);
        }
    }

    vfprintf(cl->output, fmt, args);
    fprintf(cl->output, "\n");

    if (cl->config.flush_on_write) {
        fflush(cl->output);
    }
}

static void console_flush(void *handle) {
    console_logger_t *cl = (console_logger_t *)handle;
    if (cl != NULL && cl->output != NULL) {
        fflush(cl->output);
    }
}

static void console_set_level(void *handle, log_level_t level) {
    console_logger_t *cl = (console_logger_t *)handle;
    if (cl != NULL) {
        cl->config.level = level;
        cl->output = (level >= LOG_LEVEL_WARN) ? stderr : stdout;
    }
}

/* ========== Implementation Registration ========== */

static logger_impl_t console_impl = {
    .name        = "console",
    .output_type = LOG_OUTPUT_CONSOLE,
    .create      = console_create,
    .destroy     = console_destroy,
    .log         = console_log,
    .flush       = console_flush,
    .set_level   = console_set_level,
    .reopen      = NULL,
};

int logger_register_console(void) {
    return logger_register_impl(&console_impl);
}