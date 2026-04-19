/**
 * @file logger_file.c
 * @brief File logger implementation
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
#include <sys/stat.h>
#include <errno.h>

/* ========== File Logger Handle ========== */

typedef struct {
    log_config_t config;
    FILE *fp;
    char file_path[256];
    size_t current_size;
    int rotation_count;
} file_logger_t;

/* ========== Implementation Functions ========== */

static void *file_create(const log_config_t *config) {
    file_logger_t *handle = malloc(sizeof(file_logger_t));
    if (handle == NULL) return NULL;

    memset(handle, 0, sizeof(file_logger_t));

    if (config != NULL) {
        handle->config = *config;
        if (strlen(config->file_path) > 0) {
            CFDDNS_STRNCPY(handle->file_path, config->file_path, sizeof(handle->file_path));
        }
    } else {
        handle->config = log_config_default();
    }

    /* Open log file */
    if (strlen(handle->file_path) > 0) {
        handle->fp = fopen(handle->file_path, "a");
        if (handle->fp == NULL) {
            free(handle);
            return NULL;
        }
    } else {
        /* Default log path */
        char default_path[256];
        if (fs_get_config_dir(default_path, sizeof(default_path)) == CFDDNS_OK) {
            snprintf(handle->file_path, sizeof(handle->file_path),
                     "%s/cfddns.log", default_path);
            handle->fp = fopen(handle->file_path, "a");
        }
        if (handle->fp == NULL) {
            handle->fp = fopen("/var/log/cfddns.log", "a");
            if (handle->fp != NULL) {
                CFDDNS_STRNCPY(handle->file_path, "/var/log/cfddns.log",
                               sizeof(handle->file_path));
            }
        }
        if (handle->fp == NULL) {
            /* Fallback to temp directory */
            handle->fp = fopen("/tmp/cfddns.log", "a");
            if (handle->fp != NULL) {
                CFDDNS_STRNCPY(handle->file_path, "/tmp/cfddns.log",
                               sizeof(handle->file_path));
            }
        }
    }

    if (handle->fp == NULL) {
        free(handle);
        return NULL;
    }

    /* Get current file size */
    struct stat st;
    if (stat(handle->file_path, &st) == 0) {
        handle->current_size = st.st_size;
    }

    return handle;
}

static void file_destroy(void *handle) {
    if (handle != NULL) {
        file_logger_t *fl = (file_logger_t *)handle;
        if (fl->fp != NULL) {
            fclose(fl->fp);
        }
        free(fl);
    }
}

static void file_log(void *handle, log_level_t level,
                     const char *file, int line, const char *func,
                     const char *fmt, va_list args) {
    file_logger_t *fl = (file_logger_t *)handle;
    if (fl == NULL || fl->fp == NULL) return;

    /* Get current time */
    uint64_t now_ms = timer_get_time_ms();
    time_t now_s = (time_t)(now_ms / 1000);
    struct tm tm;
    localtime_r(&now_s, &tm);

    char timestamp[32];
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", &tm);

    /* Build log line */
    fprintf(fl->fp, "[%s] [%s] ", log_level_name(level), timestamp);

    if (fl->config.format & LOG_FORMAT_FILE) {
        fprintf(fl->fp, "%s:%d ", file, line);
    }
    if (fl->config.format & LOG_FORMAT_FUNC) {
        fprintf(fl->fp, "[%s] ", func);
    }

    vfprintf(fl->fp, fmt, args);
    fprintf(fl->fp, "\n");

    /* Track file size for rotation */
    fflush(fl->fp);

    if (fl->config.flush_on_write) {
        fflush(fl->fp);
    }
}

static void file_flush(void *handle) {
    file_logger_t *fl = (file_logger_t *)handle;
    if (fl != NULL && fl->fp != NULL) {
        fflush(fl->fp);
    }
}

static void file_set_level(void *handle, log_level_t level) {
    file_logger_t *fl = (file_logger_t *)handle;
    if (fl != NULL) {
        fl->config.level = level;
    }
}

static int file_reopen(void *handle) {
    file_logger_t *fl = (file_logger_t *)handle;
    if (fl == NULL) return CFDDNS_ERR_NULL_POINTER;

    if (fl->fp != NULL) {
        fclose(fl->fp);
        fl->fp = NULL;
    }

    if (strlen(fl->file_path) > 0) {
        fl->fp = fopen(fl->file_path, "a");
        if (fl->fp == NULL) {
            return CFDDNS_ERR_FILE_OPEN_FAILED;
        }
    }

    return CFDDNS_OK;
}

/* ========== Implementation Registration ========== */

static logger_impl_t file_impl = {
    .name        = "file",
    .output_type = LOG_OUTPUT_FILE,
    .create      = file_create,
    .destroy     = file_destroy,
    .log         = file_log,
    .flush       = file_flush,
    .set_level   = file_set_level,
    .reopen      = file_reopen,
};

int logger_register_file(void) {
    return logger_register_impl(&file_impl);
}