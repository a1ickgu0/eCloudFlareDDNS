/**
 * @file errors.c
 * @brief Error code implementation
 */

#include "common/errors.h"
#include "common/macros.h"

static const struct {
    cfddns_error_t code;
    const char *name;
    const char *description;
} error_table[] = {
    /* Success */
    { CFDDNS_OK, "CFDDNS_OK", "Success" },

    /* General errors */
    { CFDDNS_ERR_UNKNOWN, "CFDDNS_ERR_UNKNOWN", "Unknown error" },
    { CFDDNS_ERR_INVALID_ARG, "CFDDNS_ERR_INVALID_ARG", "Invalid argument" },
    { CFDDNS_ERR_NULL_POINTER, "CFDDNS_ERR_NULL_POINTER", "Null pointer" },
    { CFDDNS_ERR_OUT_OF_MEMORY, "CFDDNS_ERR_OUT_OF_MEMORY", "Out of memory" },
    { CFDDNS_ERR_BUFFER_TOO_SMALL, "CFDDNS_ERR_BUFFER_TOO_SMALL", "Buffer too small" },
    { CFDDNS_ERR_NOT_IMPLEMENTED, "CFDDNS_ERR_NOT_IMPLEMENTED", "Not implemented" },
    { CFDDNS_ERR_OPERATION_FAILED, "CFDDNS_ERR_OPERATION_FAILED", "Operation failed" },
    { CFDDNS_ERR_NOT_INITIALIZED, "CFDDNS_ERR_NOT_INITIALIZED", "Not initialized" },
    { CFDDNS_ERR_ALREADY_INITIALIZED, "CFDDNS_ERR_ALREADY_INITIALIZED", "Already initialized" },
    { CFDDNS_ERR_NOT_FOUND, "CFDDNS_ERR_NOT_FOUND", "Not found" },
    { CFDDNS_ERR_ALREADY_EXISTS, "CFDDNS_ERR_ALREADY_EXISTS", "Already exists" },
    { CFDDNS_ERR_TIMEOUT, "CFDDNS_ERR_TIMEOUT", "Timeout" },

    /* File system errors */
    { CFDDNS_ERR_FILE_NOT_FOUND, "CFDDNS_ERR_FILE_NOT_FOUND", "File not found" },
    { CFDDNS_ERR_FILE_OPEN_FAILED, "CFDDNS_ERR_FILE_OPEN_FAILED", "Failed to open file" },
    { CFDDNS_ERR_FILE_READ_FAILED, "CFDDNS_ERR_FILE_READ_FAILED", "Failed to read file" },
    { CFDDNS_ERR_FILE_WRITE_FAILED, "CFDDNS_ERR_FILE_WRITE_FAILED", "Failed to write file" },
    { CFDDNS_ERR_FILE_PERMISSION, "CFDDNS_ERR_FILE_PERMISSION", "File permission denied" },
    { CFDDNS_ERR_DIR_CREATE_FAILED, "CFDDNS_ERR_DIR_CREATE_FAILED", "Failed to create directory" },

    /* Configuration errors */
    { CFDDNS_ERR_CONFIG_NOT_FOUND, "CFDDNS_ERR_CONFIG_NOT_FOUND", "Configuration file not found" },
    { CFDDNS_ERR_CONFIG_PARSE_FAILED, "CFDDNS_ERR_CONFIG_PARSE_FAILED", "Failed to parse configuration" },
    { CFDDNS_ERR_CONFIG_INVALID_KEY, "CFDDNS_ERR_CONFIG_INVALID_KEY", "Invalid configuration key" },
    { CFDDNS_ERR_CONFIG_MISSING_KEY, "CFDDNS_ERR_CONFIG_MISSING_KEY", "Missing required configuration key" },
    { CFDDNS_ERR_CONFIG_INVALID_VALUE, "CFDDNS_ERR_CONFIG_INVALID_VALUE", "Invalid configuration value" },
    { CFDDNS_ERR_CONFIG_TYPE_MISMATCH, "CFDDNS_ERR_CONFIG_TYPE_MISMATCH", "Configuration type mismatch" },

    /* Network errors */
    { CFDDNS_ERR_NETWORK_FAILED, "CFDDNS_ERR_NETWORK_FAILED", "Network operation failed" },
    { CFDDNS_ERR_NETWORK_INIT_FAILED, "CFDDNS_ERR_NETWORK_INIT_FAILED", "Network initialization failed" },
    { CFDDNS_ERR_NETWORK_DNS_FAILED, "CFDDNS_ERR_NETWORK_DNS_FAILED", "DNS resolution failed" },
    { CFDDNS_ERR_NETWORK_CONNECT_FAILED, "CFDDNS_ERR_NETWORK_CONNECT_FAILED", "Connection failed" },
    { CFDDNS_ERR_NETWORK_DISCONNECTED, "CFDDNS_ERR_NETWORK_DISCONNECTED", "Network disconnected" },
    { CFDDNS_ERR_NETWORK_SEND_FAILED, "CFDDNS_ERR_NETWORK_SEND_FAILED", "Failed to send data" },
    { CFDDNS_ERR_NETWORK_RECV_FAILED, "CFDDNS_ERR_NETWORK_RECV_FAILED", "Failed to receive data" },
    { CFDDNS_ERR_NETWORK_SSL_FAILED, "CFDDNS_ERR_NETWORK_SSL_FAILED", "SSL/TLS error" },
    { CFDDNS_ERR_NETWORK_SSL_CERT, "CFDDNS_ERR_NETWORK_SSL_CERT", "SSL certificate error" },

    /* HTTP errors */
    { CFDDNS_ERR_HTTP_INVALID_URL, "CFDDNS_ERR_HTTP_INVALID_URL", "Invalid URL" },
    { CFDDNS_ERR_HTTP_INVALID_METHOD, "CFDDNS_ERR_HTTP_INVALID_METHOD", "Invalid HTTP method" },
    { CFDDNS_ERR_HTTP_REQUEST_FAILED, "CFDDNS_ERR_HTTP_REQUEST_FAILED", "HTTP request failed" },
    { CFDDNS_ERR_HTTP_RESPONSE_INVALID, "CFDDNS_ERR_HTTP_RESPONSE_INVALID", "Invalid HTTP response" },
    { CFDDNS_ERR_HTTP_4XX, "CFDDNS_ERR_HTTP_4XX", "HTTP client error (4xx)" },
    { CFDDNS_ERR_HTTP_5XX, "CFDDNS_ERR_HTTP_5XX", "HTTP server error (5xx)" },
    { CFDDNS_ERR_HTTP_TOO_MANY_REDIRECTS, "CFDDNS_ERR_HTTP_TOO_MANY_REDIRECTS", "Too many redirects" },
    { CFDDNS_ERR_INVALID_RESPONSE, "CFDDNS_ERR_INVALID_RESPONSE", "Invalid response" },

    /* JSON errors */
    { CFDDNS_ERR_JSON_PARSE_FAILED, "CFDDNS_ERR_JSON_PARSE_FAILED", "Failed to parse JSON" },
    { CFDDNS_ERR_JSON_INVALID_TYPE, "CFDDNS_ERR_JSON_INVALID_TYPE", "Invalid JSON type" },
    { CFDDNS_ERR_JSON_NOT_FOUND, "CFDDNS_ERR_JSON_NOT_FOUND", "JSON item not found" },
    { CFDDNS_ERR_JSON_ALLOC_FAILED, "CFDDNS_ERR_JSON_ALLOC_FAILED", "JSON allocation failed" },

    /* CloudFlare API errors */
    { CFDDNS_ERR_CF_AUTH_FAILED, "CFDDNS_ERR_CF_AUTH_FAILED", "CloudFlare authentication failed" },
    { CFDDNS_ERR_CF_INVALID_TOKEN, "CFDDNS_ERR_CF_INVALID_TOKEN", "Invalid CloudFlare API token" },
    { CFDDNS_ERR_CF_ZONE_NOT_FOUND, "CFDDNS_ERR_CF_ZONE_NOT_FOUND", "CloudFlare zone not found" },
    { CFDDNS_ERR_CF_RECORD_NOT_FOUND, "CFDDNS_ERR_CF_RECORD_NOT_FOUND", "DNS record not found" },
    { CFDDNS_ERR_CF_RECORD_EXISTS, "CFDDNS_ERR_CF_RECORD_EXISTS", "DNS record already exists" },
    { CFDDNS_ERR_CF_RATE_LIMIT, "CFDDNS_ERR_CF_RATE_LIMIT", "CloudFlare rate limit exceeded" },
    { CFDDNS_ERR_CF_API_ERROR, "CFDDNS_ERR_CF_API_ERROR", "CloudFlare API error" },
    { CFDDNS_ERR_CF_INVALID_RESPONSE, "CFDDNS_ERR_CF_INVALID_RESPONSE", "Invalid CloudFlare API response" },

    /* IP provider errors */
    { CFDDNS_ERR_IP_PROVIDER_FAILED, "CFDDNS_ERR_IP_PROVIDER_FAILED", "IP detection failed" },
    { CFDDNS_ERR_IP_INVALID, "CFDDNS_ERR_IP_INVALID", "Invalid IP address" },
    { CFDDNS_ERR_IP_IPV4_NOT_FOUND, "CFDDNS_ERR_IP_IPV4_NOT_FOUND", "IPv4 address not found" },
    { CFDDNS_ERR_IP_IPV6_NOT_FOUND, "CFDDNS_ERR_IP_IPV6_NOT_FOUND", "IPv6 address not found" },

    /* DDNS service errors */
    { CFDDNS_ERR_DDNS_NOT_RUNNING, "CFDDNS_ERR_DDNS_NOT_RUNNING", "DDNS service not running" },
    { CFDDNS_ERR_DDNS_ALREADY_RUNNING, "CFDDNS_ERR_DDNS_ALREADY_RUNNING", "DDNS service already running" },
    { CFDDNS_ERR_DDNS_UPDATE_FAILED, "CFDDNS_ERR_DDNS_UPDATE_FAILED", "DNS update failed" },
    { CFDDNS_ERR_DDNS_NO_CHANGE, "CFDDNS_ERR_DDNS_NO_CHANGE", "No IP change detected" },

    /* Platform errors */
    { CFDDNS_ERR_PLATFORM_NOT_SUPPORTED, "CFDDNS_ERR_PLATFORM_NOT_SUPPORTED", "Platform not supported" },
    { CFDDNS_ERR_PLATFORM_INIT_FAILED, "CFDDNS_ERR_PLATFORM_INIT_FAILED", "Platform initialization failed" },
    { CFDDNS_ERR_THREAD_CREATE_FAILED, "CFDDNS_ERR_THREAD_CREATE_FAILED", "Failed to create thread" },
    { CFDDNS_ERR_MUTEX_CREATE_FAILED, "CFDDNS_ERR_MUTEX_CREATE_FAILED", "Failed to create mutex" },
};

const char *cfddns_strerror(cfddns_error_t err) {
    for (size_t i = 0; i < CFDDNS_ARRAY_SIZE(error_table); i++) {
        if (error_table[i].code == err) {
            return error_table[i].description;
        }
    }
    return "Unknown error code";
}

const char *cfddns_errname(cfddns_error_t err) {
    for (size_t i = 0; i < CFDDNS_ARRAY_SIZE(error_table); i++) {
        if (error_table[i].code == err) {
            return error_table[i].name;
        }
    }
    return "UNKNOWN";
}