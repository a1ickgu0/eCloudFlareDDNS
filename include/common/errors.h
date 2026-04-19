/**
 * @file errors.h
 * @brief Error code definitions for CloudFlare DDNS client
 */

#ifndef CFDDNS_COMMON_ERRORS_H
#define CFDDNS_COMMON_ERRORS_H

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Error codes
 */
typedef enum {
    /* Success */
    CFDDNS_OK = 0,

    /* General errors (1-99) */
    CFDDNS_ERR_UNKNOWN = -1,
    CFDDNS_ERR_INVALID_ARG = -2,
    CFDDNS_ERR_NULL_POINTER = -3,
    CFDDNS_ERR_OUT_OF_MEMORY = -4,
    CFDDNS_ERR_BUFFER_TOO_SMALL = -5,
    CFDDNS_ERR_NOT_IMPLEMENTED = -6,
    CFDDNS_ERR_OPERATION_FAILED = -7,
    CFDDNS_ERR_NOT_INITIALIZED = -8,
    CFDDNS_ERR_ALREADY_INITIALIZED = -9,
    CFDDNS_ERR_NOT_FOUND = -10,
    CFDDNS_ERR_ALREADY_EXISTS = -11,
    CFDDNS_ERR_TIMEOUT = -12,

    /* File system errors (100-199) */
    CFDDNS_ERR_FILE_NOT_FOUND = -100,
    CFDDNS_ERR_FILE_OPEN_FAILED = -101,
    CFDDNS_ERR_FILE_READ_FAILED = -102,
    CFDDNS_ERR_FILE_WRITE_FAILED = -103,
    CFDDNS_ERR_FILE_PERMISSION = -104,
    CFDDNS_ERR_DIR_CREATE_FAILED = -105,

    /* Configuration errors (200-299) */
    CFDDNS_ERR_CONFIG_NOT_FOUND = -200,
    CFDDNS_ERR_CONFIG_PARSE_FAILED = -201,
    CFDDNS_ERR_CONFIG_INVALID_KEY = -202,
    CFDDNS_ERR_CONFIG_MISSING_KEY = -203,
    CFDDNS_ERR_CONFIG_INVALID_VALUE = -204,
    CFDDNS_ERR_CONFIG_TYPE_MISMATCH = -205,

    /* Network errors (300-399) */
    CFDDNS_ERR_NETWORK_FAILED = -300,
    CFDDNS_ERR_NETWORK_INIT_FAILED = -301,
    CFDDNS_ERR_NETWORK_DNS_FAILED = -302,
    CFDDNS_ERR_NETWORK_CONNECT_FAILED = -303,
    CFDDNS_ERR_NETWORK_DISCONNECTED = -304,
    CFDDNS_ERR_NETWORK_SEND_FAILED = -305,
    CFDDNS_ERR_NETWORK_RECV_FAILED = -306,
    CFDDNS_ERR_NETWORK_SSL_FAILED = -307,
    CFDDNS_ERR_NETWORK_SSL_CERT = -308,

    /* HTTP errors (400-499) */
    CFDDNS_ERR_HTTP_INVALID_URL = -400,
    CFDDNS_ERR_HTTP_INVALID_METHOD = -401,
    CFDDNS_ERR_HTTP_REQUEST_FAILED = -402,
    CFDDNS_ERR_HTTP_RESPONSE_INVALID = -403,
    CFDDNS_ERR_HTTP_4XX = -404,
    CFDDNS_ERR_HTTP_5XX = -405,
    CFDDNS_ERR_HTTP_TOO_MANY_REDIRECTS = -406,
    CFDDNS_ERR_INVALID_RESPONSE = -407,

    /* JSON errors (500-599) */
    CFDDNS_ERR_JSON_PARSE_FAILED = -500,
    CFDDNS_ERR_JSON_INVALID_TYPE = -501,
    CFDDNS_ERR_JSON_NOT_FOUND = -502,
    CFDDNS_ERR_JSON_ALLOC_FAILED = -503,

    /* CloudFlare API errors (600-699) */
    CFDDNS_ERR_CF_AUTH_FAILED = -600,
    CFDDNS_ERR_CF_INVALID_TOKEN = -601,
    CFDDNS_ERR_CF_ZONE_NOT_FOUND = -602,
    CFDDNS_ERR_CF_RECORD_NOT_FOUND = -603,
    CFDDNS_ERR_CF_RECORD_EXISTS = -604,
    CFDDNS_ERR_CF_RATE_LIMIT = -605,
    CFDDNS_ERR_CF_API_ERROR = -606,
    CFDDNS_ERR_CF_INVALID_RESPONSE = -607,

    /* IP provider errors (700-799) */
    CFDDNS_ERR_IP_PROVIDER_FAILED = -700,
    CFDDNS_ERR_IP_INVALID = -701,
    CFDDNS_ERR_IP_IPV4_NOT_FOUND = -702,
    CFDDNS_ERR_IP_IPV6_NOT_FOUND = -703,

    /* DDNS service errors (800-899) */
    CFDDNS_ERR_DDNS_NOT_RUNNING = -800,
    CFDDNS_ERR_DDNS_ALREADY_RUNNING = -801,
    CFDDNS_ERR_DDNS_UPDATE_FAILED = -802,
    CFDDNS_ERR_DDNS_NO_CHANGE = -803,

    /* Platform errors (900-999) */
    CFDDNS_ERR_PLATFORM_NOT_SUPPORTED = -900,
    CFDDNS_ERR_PLATFORM_INIT_FAILED = -901,
    CFDDNS_ERR_THREAD_CREATE_FAILED = -902,
    CFDDNS_ERR_MUTEX_CREATE_FAILED = -903,

} cfddns_error_t;

/**
 * @brief Get human-readable error description
 * @param err Error code
 * @return Error description string (never NULL)
 */
CFDDNS_API const char *cfddns_strerror(cfddns_error_t err);

/**
 * @brief Get error name (enum name as string)
 * @param err Error code
 * @return Error name string (never NULL)
 */
CFDDNS_API const char *cfddns_errname(cfddns_error_t err);

/**
 * @brief Check if error code indicates success
 */
#define CFDDNS_SUCCEEDED(err) ((err) >= 0)

/**
 * @brief Check if error code indicates failure
 */
#define CFDDNS_FAILED(err) ((err) < 0)

/**
 * @brief Return on error
 */
#define CFDDNS_RETURN_ON_ERROR(expr) do { \
    cfddns_error_t __err = (expr); \
    if (CFDDNS_FAILED(__err)) return __err; \
} while(0)

/**
 * @brief Goto label on error
 */
#define CFDDNS_GOTO_ON_ERROR(expr, label) do { \
    cfddns_error_t __err = (expr); \
    if (CFDDNS_FAILED(__err)) { err = __err; goto label; } \
} while(0)

#ifdef __cplusplus
}
#endif

#endif /* CFDDNS_COMMON_ERRORS_H */