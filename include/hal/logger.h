/**
 * @file logger.h
 * @brief Logging system abstraction layer
 *
 * This module provides a unified interface for logging,
 * supporting multiple outputs (console, file, syslog, etc.)
 */

#ifndef CFDDNS_HAL_LOGGER_H
#define CFDDNS_HAL_LOGGER_H

#include "common/types.h"
#include "common/errors.h"
#include <stdarg.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ========== Log Levels ========== */

typedef enum {
    LOG_LEVEL_TRACE = 0,    /**< Very detailed information */
    LOG_LEVEL_DEBUG = 1,    /**< Debug information */
    LOG_LEVEL_INFO  = 2,    /**< Informational messages */
    LOG_LEVEL_WARN  = 3,    /**< Warning messages */
    LOG_LEVEL_ERROR = 4,    /**< Error messages */
    LOG_LEVEL_FATAL = 5,    /**< Fatal errors (program will exit) */
    LOG_LEVEL_OFF   = 6,    /**< Logging disabled */
} log_level_t;

/* ========== Log Output Types ========== */

typedef enum {
    LOG_OUTPUT_CONSOLE = 0,  /**< Console output (stdout/stderr) */
    LOG_OUTPUT_FILE,         /**< File output */
    LOG_OUTPUT_SYSLOG,       /**< Syslog output (Unix) */
    LOG_OUTPUT_CALLBACK,     /**< Custom callback */
    LOG_OUTPUT_NULL,         /**< Null output (discard) */
} log_output_type_t;

/* ========== Log Formatting Flags ========== */

typedef enum {
    LOG_FORMAT_LEVEL     = 1 << 0,   /**< Include log level */
    LOG_FORMAT_TIMESTAMP = 1 << 1,   /**< Include timestamp */
    LOG_FORMAT_FILE      = 1 << 2,   /**< Include file name */
    LOG_FORMAT_LINE      = 1 << 3,   /**< Include line number */
    LOG_FORMAT_FUNC      = 1 << 4,   /**< Include function name */
    LOG_FORMAT_THREAD    = 1 << 5,   /**< Include thread ID */
    LOG_FORMAT_COLOR     = 1 << 6,   /**< Colorize output */
    LOG_FORMAT_DEFAULT = LOG_FORMAT_LEVEL | LOG_FORMAT_TIMESTAMP | LOG_FORMAT_FILE | LOG_FORMAT_LINE,
} log_format_flags_t;

/* ========== Log Timestamp Formats ========== */

typedef enum {
    LOG_TIME_NONE = 0,        /**< No timestamp */
    LOG_TIME_SIMPLE,          /**< HH:MM:SS */
    LOG_TIME_FULL,            /**< YYYY-MM-DD HH:MM:SS */
    LOG_TIME_ISO8601,         /**< YYYY-MM-DDTHH:MM:SS.sss */
    LOG_TIME_UNIX,            /**< Unix timestamp */
} log_time_format_t;

/* ========== Log Callback ========== */

/**
 * @brief Log callback function type
 * @param level Log level
 * @param file Source file name
 * @param line Source line number
 * @param func Function name
 * @param message Log message
 * @param user_data User data passed to callback
 */
typedef void (*log_callback_t)(log_level_t level,
                                const char *file,
                                int line,
                                const char *func,
                                const char *message,
                                void *user_data);

/* ========== Log Configuration ========== */

typedef struct {
    log_level_t level;              /**< Minimum log level */
    log_output_type_t output_type;  /**< Output type */
    log_format_flags_t format;      /**< Format flags */
    log_time_format_t time_format;  /**< Timestamp format */
    char *file_path;                /**< Log file path (for LOG_OUTPUT_FILE) */
    size_t max_file_size;          /**< Max log file size (0 for unlimited) */
    int max_file_count;            /**< Max log file count (for rotation) */
    log_callback_t callback;        /**< Callback function (for LOG_OUTPUT_CALLBACK) */
    void *callback_data;            /**< Callback user data */
    bool flush_on_write;            /**< Flush after each write */
    bool quiet;                     /**< Suppress all output */
} log_config_t;

/* ========== Logger Implementation ========== */

typedef struct logger_impl {
    const char *name;               /**< Implementation name */
    log_output_type_t output_type;   /**< Supported output type */

    /**
     * @brief Create logger instance
     * @param config Logger configuration
     * @return Logger handle on success, NULL on failure
     */
    void* (*create)(const log_config_t *config);

    /**
     * @brief Destroy logger instance
     * @param handle Logger handle
     */
    void (*destroy)(void *handle);

    /**
     * @brief Write log message
     * @param handle Logger handle
     * @param level Log level
     * @param file Source file name
     * @param line Source line number
     * @param func Function name
     * @param fmt Format string
     * @param args Format arguments
     */
    void (*log)(void *handle, log_level_t level,
                const char *file, int line, const char *func,
                const char *fmt, va_list args);

    /**
     * @brief Flush log buffer
     * @param handle Logger handle
     */
    void (*flush)(void *handle);

    /**
     * @brief Set log level
     * @param handle Logger handle
     * @param level Minimum log level
     */
    void (*set_level)(void *handle, log_level_t level);

    /**
     * @brief Reopen log file (for log rotation)
     * @param handle Logger handle
     * @return 0 on success, error code on failure
     */
    int (*reopen)(void *handle);

} logger_impl_t;

/* ========== Logger Structure ========== */

typedef struct logger {
    void *handle;                   /**< Implementation handle */
    const logger_impl_t *impl;      /**< Implementation vtable */
    log_level_t level;              /**< Current log level */
    char *name;                     /**< Logger name */
    bool quiet;                     /**< Suppress all output */
    void *reserved[4];              /**< Reserved for future use */
} logger_t;

/* ========== Global Logger Functions ========== */

/**
 * @brief Initialize global logger
 * @param config Logger configuration
 * @return 0 on success, error code on failure
 */
CFDDNS_API int logger_init(const log_config_t *config);

/**
 * @brief Initialize global logger with implementation
 * @param impl_name Implementation name ("console", "file", "syslog")
 * @param config Logger configuration
 * @return 0 on success, error code on failure
 */
CFDDNS_API int logger_init_with_impl(const char *impl_name, const log_config_t *config);

/**
 * @brief Cleanup global logger
 */
CFDDNS_API void logger_cleanup(void);

/**
 * @brief Get global logger
 * @return Global logger, or NULL if not initialized
 */
CFDDNS_API logger_t *logger_get(void);

/**
 * @brief Set global log level
 * @param level Minimum log level
 */
CFDDNS_API void logger_set_level(log_level_t level);

/**
 * @brief Get global log level
 * @return Current log level
 */
CFDDNS_API log_level_t logger_get_level(void);

/**
 * @brief Set quiet mode
 * @param quiet True to suppress all output
 */
CFDDNS_API void logger_set_quiet(bool quiet);

/**
 * @brief Flush log buffer
 */
CFDDNS_API void logger_flush(void);

/* ========== Logger Instance Functions ========== */

/**
 * @brief Create logger instance
 * @param impl_name Implementation name ("console", "file", "syslog")
 * @param config Logger configuration
 * @return Logger on success, NULL on failure
 */
CFDDNS_API logger_t *logger_create(const char *impl_name, const log_config_t *config);

/**
 * @brief Destroy logger instance
 * @param logger Logger
 */
CFDDNS_API void logger_destroy(logger_t *logger);

/**
 * @brief Write log message
 * @param logger Logger (NULL for global)
 * @param level Log level
 * @param file Source file name
 * @param line Source line number
 * @param func Function name
 * @param fmt Format string
 * @param ... Format arguments
 */
CFDDNS_API void logger_log(logger_t *logger, log_level_t level,
                           const char *file, int line, const char *func,
                           const char *fmt, ...) CFDDNS_PRINTF(6, 7);

/**
 * @brief Write log message (va_list version)
 * @param logger Logger (NULL for global)
 * @param level Log level
 * @param file Source file name
 * @param line Source line number
 * @param func Function name
 * @param fmt Format string
 * @param args Format arguments
 */
CFDDNS_API void logger_log_v(logger_t *logger, log_level_t level,
                              const char *file, int line, const char *func,
                              const char *fmt, va_list args);

/**
 * @brief Set logger level
 * @param logger Logger (NULL for global)
 * @param level Minimum log level
 */
CFDDNS_API void logger_set_level_inst(logger_t *logger, log_level_t level);

/**
 * @brief Flush logger buffer
 * @param logger Logger (NULL for global)
 */
CFDDNS_API void logger_flush_inst(logger_t *logger);

/* ========== Convenience Macros ========== */

#define LOG_TRACE(logger, ...) \
    logger_log(logger, LOG_LEVEL_TRACE, __FILE__, __LINE__, __func__, __VA_ARGS__)
#define LOG_DEBUG(logger, ...) \
    logger_log(logger, LOG_LEVEL_DEBUG, __FILE__, __LINE__, __func__, __VA_ARGS__)
#define LOG_INFO(logger, ...) \
    logger_log(logger, LOG_LEVEL_INFO, __FILE__, __LINE__, __func__, __VA_ARGS__)
#define LOG_WARN(logger, ...) \
    logger_log(logger, LOG_LEVEL_WARN, __FILE__, __LINE__, __func__, __VA_ARGS__)
#define LOG_ERROR(logger, ...) \
    logger_log(logger, LOG_LEVEL_ERROR, __FILE__, __LINE__, __func__, __VA_ARGS__)
#define LOG_FATAL(logger, ...) \
    logger_log(logger, LOG_LEVEL_FATAL, __FILE__, __LINE__, __func__, __VA_ARGS__)

/* Global logger macros */
#define LOG_G_TRACE(...) LOG_TRACE(NULL, __VA_ARGS__)
#define LOG_G_DEBUG(...) LOG_DEBUG(NULL, __VA_ARGS__)
#define LOG_G_INFO(...)  LOG_INFO(NULL, __VA_ARGS__)
#define LOG_G_WARN(...)  LOG_WARN(NULL, __VA_ARGS__)
#define LOG_G_ERROR(...) LOG_ERROR(NULL, __VA_ARGS__)
#define LOG_G_FATAL(...) LOG_FATAL(NULL, __VA_ARGS__)

/* ========== Utility Functions ========== */

/**
 * @brief Get log level name
 * @param level Log level
 * @return Level name string
 */
CFDDNS_API const char *log_level_name(log_level_t level);

/**
 * @brief Parse log level from string
 * @param str Level string
 * @return Log level, or LOG_LEVEL_INFO on invalid input
 */
CFDDNS_API log_level_t log_level_parse(const char *str);

/**
 * @brief Get default log configuration
 * @return Default configuration
 */
CFDDNS_API log_config_t log_config_default(void);

/**
 * @brief Check if log level is enabled
 * @param logger Logger (NULL for global)
 * @param level Log level to check
 * @return true if level is enabled
 */
CFDDNS_API bool logger_is_level_enabled(logger_t *logger, log_level_t level);

/* ========== Implementation Registration ========== */

/**
 * @brief Register logger implementation
 * @param impl Implementation interface
 * @return 0 on success, error code on failure
 */
CFDDNS_API int logger_register_impl(const logger_impl_t *impl);

#ifdef __cplusplus
}
#endif

#endif /* CFDDNS_HAL_LOGGER_H */