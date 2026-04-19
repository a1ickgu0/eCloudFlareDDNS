/**
 * @file config.h
 * @brief Configuration management abstraction layer
 *
 * This module provides a unified interface for configuration file handling,
 * supporting multiple formats (JSON, INI, etc.)
 */

#ifndef CFDDNS_HAL_CONFIG_H
#define CFDDNS_HAL_CONFIG_H

#include "common/types.h"
#include "common/errors.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ========== Configuration Format ========== */

typedef enum {
    CONFIG_FORMAT_AUTO = 0,    /**< Auto-detect from file extension */
    CONFIG_FORMAT_JSON,        /**< JSON format */
    CONFIG_FORMAT_INI,         /**< INI format */
    CONFIG_FORMAT_TOML,        /**< TOML format (future) */
} config_format_t;

/* ========== Configuration Options ========== */

typedef struct {
    bool allow_include;        /**< Allow including other config files */
    bool allow_env_vars;       /**< Allow environment variable expansion */
    bool allow_comments;       /**< Allow comments in config */
    bool strict_mode;          /**< Strict parsing (fail on unknown keys) */
    bool auto_save;            /**< Auto-save on modification */
    char env_prefix[32];       /**< Environment variable prefix (e.g., "CFDDNS_") */
} config_options_t;

/* ========== Configuration Value Types ========== */

typedef enum {
    CONFIG_TYPE_NULL = 0,
    CONFIG_TYPE_BOOL,
    CONFIG_TYPE_INT,
    CONFIG_TYPE_FLOAT,
    CONFIG_TYPE_STRING,
    CONFIG_TYPE_ARRAY,
    CONFIG_TYPE_OBJECT,
} config_type_t;

/* ========== Configuration Item ========== */

typedef struct config_item {
    char *key;                 /**< Item key */
    config_type_t type;        /**< Item type */
    union {
        bool bool_val;
        int64_t int_val;
        double float_val;
        char *string_val;
        struct config_item *array_val;
        struct config_item *object_val;
    } value;
    struct config_item *next;  /**< Next item in list/object */
} config_item_t;

/* ========== Configuration Implementation ========== */

typedef struct config_impl {
    const char *name;          /**< Implementation name */
    config_format_t format;     /**< Supported format */

    /**
     * @brief Create config instance
     * @return Config handle on success, NULL on failure
     */
    void* (*create)(void);

    /**
     * @brief Destroy config instance
     * @param handle Config handle
     */
    void (*destroy)(void *handle);

    /**
     * @brief Load config from string
     * @param handle Config handle
     * @param str Config string
     * @param len String length (0 for null-terminated)
     * @return 0 on success, error code on failure
     */
    int (*load_string)(void *handle, const char *str, size_t len);

    /**
     * @brief Load config from file
     * @param handle Config handle
     * @param path File path
     * @return 0 on success, error code on failure
     */
    int (*load_file)(void *handle, const char *path);

    /**
     * @brief Save config to string
     * @param handle Config handle
     * @param str String buffer output (allocated)
     * @param len String length output
     * @return 0 on success, error code on failure
     */
    int (*save_string)(void *handle, char **str, size_t *len);

    /**
     * @brief Save config to file
     * @param handle Config handle
     * @param path File path
     * @return 0 on success, error code on failure
     */
    int (*save_file)(void *handle, const char *path);

    /**
     * @brief Check if key exists
     * @param handle Config handle
     * @param key Key path (e.g., "section.subsection.key")
     * @return 1 if exists, 0 if not exists, negative on error
     */
    int (*has)(void *handle, const char *key);

    /**
     * @brief Get value type
     * @param handle Config handle
     * @param key Key path
     * @return Value type, or CONFIG_TYPE_NULL if not found
     */
    config_type_t (*get_type)(void *handle, const char *key);

    /**
     * @brief Get boolean value
     * @param handle Config handle
     * @param key Key path
     * @param default_val Default value if key not found
     * @return Boolean value
     */
    bool (*get_bool)(void *handle, const char *key, bool default_val);

    /**
     * @brief Get integer value
     * @param handle Config handle
     * @param key Key path
     * @param default_val Default value if key not found
     * @return Integer value
     */
    int64_t (*get_int)(void *handle, const char *key, int64_t default_val);

    /**
     * @brief Get float value
     * @param handle Config handle
     * @param key Key path
     * @param default_val Default value if key not found
     * @return Float value
     */
    double (*get_float)(void *handle, const char *key, double default_val);

    /**
     * @brief Get string value
     * @param handle Config handle
     * @param key Key path
     * @param default_val Default value if key not found
     * @return String value (owned by config, do not free)
     */
    const char* (*get_string)(void *handle, const char *key, const char *default_val);

    /**
     * @brief Get string value into buffer
     * @param handle Config handle
     * @param key Key path
     * @param buf Output buffer
     * @param len Buffer length
     * @param default_val Default value if key not found
     * @return 0 on success, error code on failure
     */
    int (*get_string_buf)(void *handle, const char *key, char *buf, size_t len, const char *default_val);

    /**
     * @brief Set boolean value
     * @param handle Config handle
     * @param key Key path
     * @param val Boolean value
     * @return 0 on success, error code on failure
     */
    int (*set_bool)(void *handle, const char *key, bool val);

    /**
     * @brief Set integer value
     * @param handle Config handle
     * @param key Key path
     * @param val Integer value
     * @return 0 on success, error code on failure
     */
    int (*set_int)(void *handle, const char *key, int64_t val);

    /**
     * @brief Set float value
     * @param handle Config handle
     * @param key Key path
     * @param val Float value
     * @return 0 on success, error code on failure
     */
    int (*set_float)(void *handle, const char *key, double val);

    /**
     * @brief Set string value
     * @param handle Config handle
     * @param key Key path
     * @param val String value
     * @return 0 on success, error code on failure
     */
    int (*set_string)(void *handle, const char *key, const char *val);

    /**
     * @brief Delete key
     * @param handle Config handle
     * @param key Key path
     * @return 0 on success, error code on failure
     */
    int (*delete_key)(void *handle, const char *key);

    /**
     * @brief Get array size
     * @param handle Config handle
     * @param key Key path
     * @return Array size, or 0 if not array
     */
    int (*get_array_size)(void *handle, const char *key);

    /**
     * @brief Get array element
     * @param handle Config handle
     * @param key Key path
     * @param index Array index
     * @return Config item, or NULL if not found
     */
    const config_item_t* (*get_array_item)(void *handle, const char *key, int index);

} config_impl_t;

/* ========== Configuration Structure ========== */

typedef struct config {
    void *handle;              /**< Implementation handle */
    const config_impl_t *impl; /**< Implementation vtable */
    char *path;                /**< Config file path */
    config_options_t options;  /**< Config options */
    bool modified;             /**< Whether config has been modified */
} config_t;

/* ========== Config Creation/Destruction ========== */

/**
 * @brief Create config with default implementation (JSON)
 * @return Config on success, NULL on failure
 */
CFDDNS_API config_t *config_create(void);

/**
 * @brief Create config with specific format
 * @param format Config format
 * @return Config on success, NULL on failure
 */
CFDDNS_API config_t *config_create_with_format(config_format_t format);

/**
 * @brief Create config with specific implementation
 * @param impl_name Implementation name ("json", "ini", etc.)
 * @return Config on success, NULL on failure
 */
CFDDNS_API config_t *config_create_with_impl(const char *impl_name);

/**
 * @brief Destroy config
 * @param cfg Config
 */
CFDDNS_API void config_destroy(config_t *cfg);

/* ========== Load/Save ========== */

/**
 * @brief Load config from string
 * @param cfg Config
 * @param str Config string
 * @return 0 on success, error code on failure
 */
CFDDNS_API int config_load_string(config_t *cfg, const char *str);

/**
 * @brief Load config from string with length
 * @param cfg Config
 * @param str Config string
 * @param len String length
 * @return 0 on success, error code on failure
 */
CFDDNS_API int config_load_string_len(config_t *cfg, const char *str, size_t len);

/**
 * @brief Load config from file
 * @param cfg Config
 * @param path File path
 * @return 0 on success, error code on failure
 */
CFDDNS_API int config_load_file(config_t *cfg, const char *path);

/**
 * @brief Save config to string
 * @param cfg Config
 * @param str String buffer output (allocated, caller must free)
 * @return 0 on success, error code on failure
 */
CFDDNS_API int config_save_string(config_t *cfg, char **str);

/**
 * @brief Save config to file
 * @param cfg Config
 * @param path File path (NULL to use original path)
 * @return 0 on success, error code on failure
 */
CFDDNS_API int config_save_file(config_t *cfg, const char *path);

/* ========== Query Operations ========== */

/**
 * @brief Check if key exists
 * @param cfg Config
 * @param key Key path (e.g., "cloudflare.api_token")
 * @return 1 if exists, 0 if not exists, negative on error
 */
CFDDNS_API int config_has(config_t *cfg, const char *key);

/**
 * @brief Get value type
 * @param cfg Config
 * @param key Key path
 * @return Value type
 */
CFDDNS_API config_type_t config_get_type(config_t *cfg, const char *key);

/* ========== Get Operations ========== */

CFDDNS_API bool config_get_bool(config_t *cfg, const char *key, bool default_val);
CFDDNS_API int64_t config_get_int(config_t *cfg, const char *key, int64_t default_val);
CFDDNS_API double config_get_float(config_t *cfg, const char *key, double default_val);
CFDDNS_API const char *config_get_string(config_t *cfg, const char *key, const char *default_val);
CFDDNS_API int config_get_string_buf(config_t *cfg, const char *key, char *buf, size_t len, const char *default_val);

/* ========== Set Operations ========== */

CFDDNS_API int config_set_bool(config_t *cfg, const char *key, bool val);
CFDDNS_API int config_set_int(config_t *cfg, const char *key, int64_t val);
CFDDNS_API int config_set_float(config_t *cfg, const char *key, double val);
CFDDNS_API int config_set_string(config_t *cfg, const char *key, const char *val);

/* ========== Delete Operations ========== */

/**
 * @brief Delete key from config
 * @param cfg Config
 * @param key Key path
 * @return 0 on success, error code on failure
 */
CFDDNS_API int config_delete(config_t *cfg, const char *key);

/* ========== Array Operations ========== */

CFDDNS_API int config_get_array_size(config_t *cfg, const char *key);
CFDDNS_API const config_item_t *config_get_array_item(config_t *cfg, const char *key, int index);

/* ========== Utility Functions ========== */

/**
 * @brief Get default config file path
 * @param buf Buffer to store path
 * @param len Buffer length
 * @return 0 on success, error code on failure
 */
CFDDNS_API int config_get_default_path(char *buf, size_t len);

/**
 * @brief Get default config options
 * @return Default config options
 */
CFDDNS_API config_options_t config_get_default_options(void);

/**
 * @brief Set config options
 * @param cfg Config
 * @param options Options
 * @return 0 on success, error code on failure
 */
CFDDNS_API int config_set_options(config_t *cfg, const config_options_t *options);

/* ========== Implementation Registration ========== */

/**
 * @brief Register config implementation
 * @param impl Implementation interface
 * @return 0 on success, error code on failure
 */
CFDDNS_API int config_register_impl(const config_impl_t *impl);

#ifdef __cplusplus
}
#endif

#endif /* CFDDNS_HAL_CONFIG_H */