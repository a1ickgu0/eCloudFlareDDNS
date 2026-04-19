/**
 * @file json_parser.h
 * @brief JSON parser abstraction layer
 *
 * This module provides a unified interface for JSON parsing and generation,
 * supporting multiple backends (cJSON, jansson, etc.)
 */

#ifndef CFDDNS_HAL_JSON_PARSER_H
#define CFDDNS_HAL_JSON_PARSER_H

#include "common/types.h"
#include "common/errors.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ========== JSON Value Types ========== */

typedef enum {
    JSON_TYPE_INVALID = 0,
    JSON_TYPE_NULL,
    JSON_TYPE_BOOL,
    JSON_TYPE_NUMBER,
    JSON_TYPE_STRING,
    JSON_TYPE_ARRAY,
    JSON_TYPE_OBJECT
} json_type_t;

/* ========== JSON Value Structure ========== */

/**
 * @brief JSON value handle
 *
 * This is an opaque handle to the underlying JSON value.
 * The actual structure is implementation-dependent.
 */
typedef void *json_value_t;

/* ========== JSON Parser Implementation ========== */

/**
 * @brief JSON parser implementation interface
 */
typedef struct json_parser_impl {
    const char *name;          /**< Implementation name */

    /* ========== Parsing ========== */

    /**
     * @brief Parse JSON string
     * @param str JSON string
     * @param len String length (0 for null-terminated)
     * @return JSON value handle, or NULL on failure
     */
    json_value_t (*parse)(const char *str, size_t len);

    /**
     * @brief Parse JSON string with error info
     * @param str JSON string
     * @param len String length (0 for null-terminated)
     * @param error_pos Error position output (can be NULL)
     * @param error_msg Error message buffer (can be NULL)
     * @param error_msg_len Error message buffer length
     * @return JSON value handle, or NULL on failure
     */
    json_value_t (*parse_with_error)(const char *str, size_t len,
                                      int *error_pos, char *error_msg, size_t error_msg_len);

    /* ========== Stringify ========== */

    /**
     * @brief Convert JSON value to string
     * @param value JSON value
     * @return Allocated string, or NULL on failure (caller must free)
     */
    char* (*stringify)(json_value_t value);

    /**
     * @brief Convert JSON value to formatted string
     * @param value JSON value
     * @param indent Indentation string (e.g., "  ")
     * @return Allocated string, or NULL on failure (caller must free)
     */
    char* (*stringify_pretty)(json_value_t value, const char *indent);

    /* ========== Type Query ========== */

    /**
     * @brief Get JSON value type
     * @param value JSON value
     * @return JSON type
     */
    json_type_t (*get_type)(json_value_t value);

    /* ========== Value Getters ========== */

    /**
     * @brief Get boolean value
     * @param value JSON value
     * @return Boolean value (false for non-bool types)
     */
    bool (*get_bool)(json_value_t value);

    /**
     * @brief Get number value
     * @param value JSON value
     * @return Number value (0 for non-number types)
     */
    double (*get_number)(json_value_t value);

    /**
     * @brief Get integer value
     * @param value JSON value
     * @return Integer value (0 for non-number types)
     */
    int64_t (*get_int)(json_value_t value);

    /**
     * @brief Get string value
     * @param value JSON value
     * @return String value (NULL for non-string types)
     *
     * Note: Returned string is owned by the JSON value, do not free.
     */
    const char* (*get_string)(json_value_t value);

    /**
     * @brief Get string value with length
     * @param value JSON value
     * @param len String length output
     * @return String value (NULL for non-string types)
     */
    const char* (*get_string_len)(json_value_t value, size_t *len);

    /* ========== Object Operations ========== */

    /**
     * @brief Get object item by key
     * @param obj JSON object
     * @param key Key string
     * @return JSON value, or NULL if not found
     */
    json_value_t (*get_object_item)(json_value_t obj, const char *key);

    /**
     * @brief Get object item by key (case-insensitive)
     * @param obj JSON object
     * @param key Key string
     * @return JSON value, or NULL if not found
     */
    json_value_t (*get_object_item_case)(json_value_t obj, const char *key);

    /**
     * @brief Check if object has key
     * @param obj JSON object
     * @param key Key string
     * @return true if key exists
     */
    bool (*has_object_item)(json_value_t obj, const char *key);

    /**
     * @brief Get object size (number of items)
     * @param obj JSON object
     * @return Object size, or 0 for non-object types
     */
    int (*get_object_size)(json_value_t obj);

    /**
     * @brief Iterate object items
     * @param obj JSON object
     * @param index Current index (start from 0)
     * @param key Key output
     * @return JSON value, or NULL if index out of range
     */
    json_value_t (*get_object_item_at)(json_value_t obj, int index, const char **key);

    /* ========== Array Operations ========== */

    /**
     * @brief Get array size
     * @param arr JSON array
     * @return Array size, or 0 for non-array types
     */
    int (*get_array_size)(json_value_t arr);

    /**
     * @brief Get array item by index
     * @param arr JSON array
     * @param index Item index
     * @return JSON value, or NULL if index out of range
     */
    json_value_t (*get_array_item)(json_value_t arr, int index);

    /* ========== Value Creation ========== */

    /**
     * @brief Create null value
     * @return JSON value
     */
    json_value_t (*create_null)(void);

    /**
     * @brief Create boolean value
     * @param val Boolean value
     * @return JSON value
     */
    json_value_t (*create_bool)(bool val);

    /**
     * @brief Create number value
     * @param val Number value
     * @return JSON value
     */
    json_value_t (*create_number)(double val);

    /**
     * @brief Create integer value
     * @param val Integer value
     * @return JSON value
     */
    json_value_t (*create_int)(int64_t val);

    /**
     * @brief Create string value
     * @param val String value (copied)
     * @return JSON value
     */
    json_value_t (*create_string)(const char *val);

    /**
     * @brief Create string value with length
     * @param val String value
     * @param len String length
     * @return JSON value
     */
    json_value_t (*create_string_len)(const char *val, size_t len);

    /**
     * @brief Create empty array
     * @return JSON value
     */
    json_value_t (*create_array)(void);

    /**
     * @brief Create empty object
     * @return JSON value
     */
    json_value_t (*create_object)(void);

    /* ========== Value Modification ========== */

    /**
     * @brief Add item to object
     * @param obj JSON object
     * @param key Key string
     * @param value Value to add (ownership transferred)
     * @return true on success
     */
    bool (*add_item_to_object)(json_value_t obj, const char *key, json_value_t value);

    /**
     * @brief Add item to array
     * @param arr JSON array
     * @param value Value to add (ownership transferred)
     * @return true on success
     */
    bool (*add_item_to_array)(json_value_t arr, json_value_t value);

    /**
     * @brief Replace item in object
     * @param obj JSON object
     * @param key Key string
     * @param value New value (ownership transferred)
     * @return true on success
     */
    bool (*replace_item_in_object)(json_value_t obj, const char *key, json_value_t value);

    /**
     * @brief Delete item from object
     * @param obj JSON object
     * @param key Key string
     */
    void (*delete_item_from_object)(json_value_t obj, const char *key);

    /**
     * @brief Delete item from array
     * @param arr JSON array
     * @param index Item index
     */
    void (*delete_item_from_array)(json_value_t arr, int index);

    /* ========== Reference Management ========== */

    /**
     * @brief Increase reference count
     * @param value JSON value
     * @return Same value
     */
    json_value_t (*incref)(json_value_t value);

    /**
     * @brief Decrease reference count and free if zero
     * @param value JSON value
     */
    void (*decref)(json_value_t value);

    /**
     * @brief Free JSON value (regardless of reference count)
     * @param value JSON value
     */
    void (*free)(json_value_t value);

} json_parser_impl_t;

/* ========== JSON Parser Structure ========== */

/**
 * @brief JSON parser structure (public interface)
 */
typedef struct json_parser {
    const json_parser_impl_t *impl;  /**< Implementation vtable */
    void *reserved[4];               /**< Reserved for future use */
} json_parser_t;

/* ========== Parser Creation/Destruction ========== */

/**
 * @brief Create JSON parser with default implementation
 * @return JSON parser on success, NULL on failure
 */
CFDDNS_API json_parser_t *json_parser_create(void);

/**
 * @brief Create JSON parser with specific implementation
 * @param impl_name Implementation name ("cjson", "jansson", etc.)
 * @return JSON parser on success, NULL on failure
 */
CFDDNS_API json_parser_t *json_parser_create_with_impl(const char *impl_name);

/**
 * @brief Destroy JSON parser
 * @param parser JSON parser
 */
CFDDNS_API void json_parser_destroy(json_parser_t *parser);

/* ========== Parsing Functions ========== */

/**
 * @brief Parse JSON string
 * @param parser JSON parser (NULL for default)
 * @param str JSON string
 * @return JSON value, or NULL on failure
 */
CFDDNS_API json_value_t json_parse(json_parser_t *parser, const char *str);

/**
 * @brief Parse JSON string with length
 * @param parser JSON parser (NULL for default)
 * @param str JSON string
 * @param len String length
 * @return JSON value, or NULL on failure
 */
CFDDNS_API json_value_t json_parse_len(json_parser_t *parser, const char *str, size_t len);

/**
 * @brief Parse JSON string with error info
 * @param parser JSON parser (NULL for default)
 * @param str JSON string
 * @param len String length (0 for null-terminated)
 * @param error_pos Error position output (can be NULL)
 * @param error_msg Error message buffer (can be NULL)
 * @param error_msg_len Error message buffer length
 * @return JSON value, or NULL on failure
 */
CFDDNS_API json_value_t json_parse_with_error(json_parser_t *parser, const char *str, size_t len,
                                               int *error_pos, char *error_msg, size_t error_msg_len);

/* ========== Stringify Functions ========== */

/**
 * @brief Convert JSON value to string
 * @param parser JSON parser (NULL for default)
 * @param value JSON value
 * @return Allocated string (caller must free), or NULL on failure
 */
CFDDNS_API char *json_stringify(json_parser_t *parser, json_value_t value);

/**
 * @brief Convert JSON value to formatted string
 * @param parser JSON parser (NULL for default)
 * @param value JSON value
 * @param indent Indentation string (NULL for default "  ")
 * @return Allocated string (caller must free), or NULL on failure
 */
CFDDNS_API char *json_stringify_pretty(json_parser_t *parser, json_value_t value, const char *indent);

/* ========== Type Query ========== */

/**
 * @brief Get JSON value type
 * @param parser JSON parser (NULL for default)
 * @param value JSON value
 * @return JSON type
 */
CFDDNS_API json_type_t json_get_type(json_parser_t *parser, json_value_t value);

/**
 * @brief Check if value is null
 */
CFDDNS_API bool json_is_null(json_parser_t *parser, json_value_t value);

/**
 * @brief Check if value is boolean
 */
CFDDNS_API bool json_is_bool(json_parser_t *parser, json_value_t value);

/**
 * @brief Check if value is number
 */
CFDDNS_API bool json_is_number(json_parser_t *parser, json_value_t value);

/**
 * @brief Check if value is string
 */
CFDDNS_API bool json_is_string(json_parser_t *parser, json_value_t value);

/**
 * @brief Check if value is array
 */
CFDDNS_API bool json_is_array(json_parser_t *parser, json_value_t value);

/**
 * @brief Check if value is object
 */
CFDDNS_API bool json_is_object(json_parser_t *parser, json_value_t value);

/* ========== Value Getters ========== */

CFDDNS_API bool json_get_bool(json_parser_t *parser, json_value_t value);
CFDDNS_API double json_get_number(json_parser_t *parser, json_value_t value);
CFDDNS_API int64_t json_get_int(json_parser_t *parser, json_value_t value);
CFDDNS_API const char *json_get_string(json_parser_t *parser, json_value_t value);
CFDDNS_API const char *json_get_string_len(json_parser_t *parser, json_value_t value, size_t *len);

/* ========== Object Operations ========== */

CFDDNS_API json_value_t json_get_object_item(json_parser_t *parser, json_value_t obj, const char *key);
CFDDNS_API json_value_t json_get_object_item_case(json_parser_t *parser, json_value_t obj, const char *key);
CFDDNS_API bool json_has_object_item(json_parser_t *parser, json_value_t obj, const char *key);
CFDDNS_API int json_get_object_size(json_parser_t *parser, json_value_t obj);
CFDDNS_API json_value_t json_get_object_item_at(json_parser_t *parser, json_value_t obj, int index, const char **key);

/* ========== Array Operations ========== */

CFDDNS_API int json_get_array_size(json_parser_t *parser, json_value_t arr);
CFDDNS_API json_value_t json_get_array_item(json_parser_t *parser, json_value_t arr, int index);

/* ========== Value Creation ========== */

CFDDNS_API json_value_t json_create_null(json_parser_t *parser);
CFDDNS_API json_value_t json_create_bool(json_parser_t *parser, bool val);
CFDDNS_API json_value_t json_create_number(json_parser_t *parser, double val);
CFDDNS_API json_value_t json_create_int(json_parser_t *parser, int64_t val);
CFDDNS_API json_value_t json_create_string(json_parser_t *parser, const char *val);
CFDDNS_API json_value_t json_create_string_len(json_parser_t *parser, const char *val, size_t len);
CFDDNS_API json_value_t json_create_array(json_parser_t *parser);
CFDDNS_API json_value_t json_create_object(json_parser_t *parser);

/* ========== Value Modification ========== */

CFDDNS_API bool json_add_item_to_object(json_parser_t *parser, json_value_t obj, const char *key, json_value_t value);
CFDDNS_API bool json_add_item_to_array(json_parser_t *parser, json_value_t arr, json_value_t value);
CFDDNS_API bool json_replace_item_in_object(json_parser_t *parser, json_value_t obj, const char *key, json_value_t value);
CFDDNS_API void json_delete_item_from_object(json_parser_t *parser, json_value_t obj, const char *key);
CFDDNS_API void json_delete_item_from_array(json_parser_t *parser, json_value_t arr, int index);

/* ========== Reference Management ========== */

CFDDNS_API json_value_t json_incref(json_parser_t *parser, json_value_t value);
CFDDNS_API void json_decref(json_parser_t *parser, json_value_t value);
CFDDNS_API void json_free(json_parser_t *parser, json_value_t value);

/* ========== Convenience Macros for Default Parser ========== */

#define JSON_PARSE(str)                    json_parse(NULL, str)
#define JSON_STRINGIFY(val)                json_stringify(NULL, val)
#define JSON_STRINGIFY_PRETTY(val, indent) json_stringify_pretty(NULL, val, indent)
#define JSON_GET_TYPE(val)                 json_get_type(NULL, val)
#define JSON_FREE(val)                     json_free(NULL, val)

/* ========== Implementation Registration ========== */

/**
 * @brief Register JSON parser implementation
 * @param impl Implementation interface
 * @return 0 on success, error code on failure
 */
CFDDNS_API int json_parser_register_impl(const json_parser_impl_t *impl);

#ifdef __cplusplus
}
#endif

#endif /* CFDDNS_HAL_JSON_PARSER_H */