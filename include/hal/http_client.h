/**
 * @file http_client.h
 * @brief HTTP client abstraction layer
 *
 * This module provides a unified interface for HTTP operations,
 * supporting multiple backends (libcurl, mbedtls, etc.)
 */

#ifndef CFDDNS_HAL_HTTP_CLIENT_H
#define CFDDNS_HAL_HTTP_CLIENT_H

#include "common/types.h"
#include "common/errors.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ========== HTTP Constants ========== */

#define HTTP_METHOD_GET     "GET"
#define HTTP_METHOD_POST    "POST"
#define HTTP_METHOD_PUT     "PUT"
#define HTTP_METHOD_DELETE  "DELETE"
#define HTTP_METHOD_PATCH   "PATCH"
#define HTTP_METHOD_HEAD    "HEAD"

#define HTTP_DEFAULT_TIMEOUT_MS   30000
#define HTTP_MAX_REDIRECTS        5
#define HTTP_MAX_RESPONSE_SIZE    (1024 * 1024)  /* 1MB */

/* ========== HTTP Header ========== */

/**
 * @brief HTTP header structure
 */
typedef struct http_header {
    char *key;
    char *value;
    struct http_header *next;
} http_header_t;

/* ========== HTTP Response ========== */

/**
 * @brief HTTP response structure
 */
typedef struct http_response {
    int status_code;            /**< HTTP status code (e.g., 200, 404) */
    char *body;                 /**< Response body (allocated, caller must free) */
    size_t body_len;            /**< Response body length */
    http_header_t *headers;     /**< Response headers */
    char *content_type;         /**< Content-Type header value */
    char *error_msg;            /**< Error message if request failed */
} http_response_t;

/* ========== HTTP Request Options ========== */

/**
 * @brief HTTP request options
 */
typedef struct http_request_options {
    int timeout_ms;            /**< Request timeout in milliseconds */
    int max_redirects;         /**< Maximum number of redirects */
    bool follow_redirects;     /**< Whether to follow redirects */
    bool verify_ssl;           /**< Whether to verify SSL certificates */
    const char *ca_cert_path;  /**< Path to CA certificate bundle */
    const char *proxy;         /**< Proxy URL (e.g., "http://proxy:8080") */
    const char *proxy_user;    /**< Proxy username */
    const char *proxy_pass;    /**< Proxy password */

    /* Interface binding for multi-WAN support */
    const char *bind_interface;  /**< Bind to specific network interface (e.g., "eth0", "wan1") */
    const char *bind_address;    /**< Bind to specific source IP address */
    bool bind_ipv6;              /**< Use IPv6 for binding (when bind_address is IPv6) */
} http_request_options_t;

/* ========== HTTP Client Interface ========== */

/**
 * @brief HTTP client implementation interface
 */
typedef struct http_client_impl {
    const char *name;          /**< Implementation name */

    /**
     * @brief Create HTTP client instance
     * @return Client handle on success, NULL on failure
     */
    void* (*create)(void);

    /**
     * @brief Destroy HTTP client instance
     * @param handle Client handle
     */
    void (*destroy)(void *handle);

    /**
     * @brief Perform HTTP request
     * @param handle Client handle
     * @param method HTTP method
     * @param url Request URL
     * @param body Request body (NULL for GET/HEAD)
     * @param body_len Request body length
     * @param headers Request headers (NULL-terminated list)
     * @param options Request options (NULL for defaults)
     * @param response Response structure to fill
     * @return 0 on success, error code on failure
     */
    int (*request)(void *handle,
                   const char *method,
                   const char *url,
                   const char *body,
                   size_t body_len,
                   const http_header_t *headers,
                   const http_request_options_t *options,
                   http_response_t *response);

    /**
     * @brief Set authentication token
     * @param handle Client handle
     * @param token Bearer token
     * @return 0 on success, error code on failure
     */
    int (*set_auth_token)(void *handle, const char *token);

    /**
     * @brief Set default headers
     * @param handle Client handle
     * @param headers Headers to set
     * @return 0 on success, error code on failure
     */
    int (*set_default_headers)(void *handle, const http_header_t *headers);

    /**
     * @brief Set request timeout
     * @param handle Client handle
     * @param timeout_ms Timeout in milliseconds
     * @return 0 on success, error code on failure
     */
    int (*set_timeout)(void *handle, int timeout_ms);

    /**
     * @brief Get implementation capabilities
     * @param handle Client handle
     * @return Capability flags
     */
    uint32_t (*get_capabilities)(void *handle);

} http_client_impl_t;

/* ========== HTTP Client Structure ========== */

/**
 * @brief HTTP client structure (public interface)
 */
typedef struct http_client {
    void *handle;              /**< Implementation handle */
    const http_client_impl_t *impl;  /**< Implementation vtable */
    char *auth_token;          /**< Bearer token for authentication */
    http_header_t *default_headers;  /**< Default headers */
    http_request_options_t options;  /**< Default options */
} http_client_t;

/* ========== Client Creation/Destruction ========== */

/**
 * @brief Create HTTP client with default implementation
 * @return HTTP client on success, NULL on failure
 */
CFDDNS_API http_client_t *http_client_create(void);

/**
 * @brief Create HTTP client with specific implementation
 * @param impl_name Implementation name ("libcurl", "mbedtls", etc.)
 * @return HTTP client on success, NULL on failure
 */
CFDDNS_API http_client_t *http_client_create_with_impl(const char *impl_name);

/**
 * @brief Destroy HTTP client
 * @param client HTTP client
 */
CFDDNS_API void http_client_destroy(http_client_t *client);

/* ========== Request Methods ========== */

/**
 * @brief Perform HTTP request
 * @param client HTTP client
 * @param method HTTP method
 * @param url Request URL
 * @param body Request body (NULL for GET/HEAD)
 * @param headers Request headers (NULL for default)
 * @param response Response structure to fill
 * @return 0 on success, error code on failure
 */
CFDDNS_API int http_client_request(http_client_t *client,
                                   const char *method,
                                   const char *url,
                                   const char *body,
                                   const http_header_t *headers,
                                   http_response_t *response);

/**
 * @brief Perform HTTP GET request
 * @param client HTTP client
 * @param url Request URL
 * @param headers Request headers (NULL for default)
 * @param response Response structure to fill
 * @return 0 on success, error code on failure
 */
CFDDNS_API int http_client_get(http_client_t *client,
                               const char *url,
                               const http_header_t *headers,
                               http_response_t *response);

/**
 * @brief Perform HTTP POST request
 * @param client HTTP client
 * @param url Request URL
 * @param body Request body
 * @param content_type Content-Type header value
 * @param headers Additional request headers (NULL for default)
 * @param response Response structure to fill
 * @return 0 on success, error code on failure
 */
CFDDNS_API int http_client_post(http_client_t *client,
                                const char *url,
                                const char *body,
                                const char *content_type,
                                const http_header_t *headers,
                                http_response_t *response);

/**
 * @brief Perform HTTP PUT request
 * @param client HTTP client
 * @param url Request URL
 * @param body Request body
 * @param content_type Content-Type header value
 * @param headers Additional request headers (NULL for default)
 * @param response Response structure to fill
 * @return 0 on success, error code on failure
 */
CFDDNS_API int http_client_put(http_client_t *client,
                               const char *url,
                               const char *body,
                               const char *content_type,
                               const http_header_t *headers,
                               http_response_t *response);

/**
 * @brief Perform HTTP DELETE request
 * @param client HTTP client
 * @param url Request URL
 * @param headers Request headers (NULL for default)
 * @param response Response structure to fill
 * @return 0 on success, error code on failure
 */
CFDDNS_API int http_client_delete(http_client_t *client,
                                  const char *url,
                                  const http_header_t *headers,
                                  http_response_t *response);

/* ========== Configuration Methods ========== */

/**
 * @brief Set authentication token
 * @param client HTTP client
 * @param token Bearer token
 * @return 0 on success, error code on failure
 */
CFDDNS_API int http_client_set_auth_token(http_client_t *client, const char *token);

/**
 * @brief Set default timeout
 * @param client HTTP client
 * @param timeout_ms Timeout in milliseconds
 * @return 0 on success, error code on failure
 */
CFDDNS_API int http_client_set_timeout(http_client_t *client, int timeout_ms);

/**
 * @brief Set SSL verification mode
 * @param client HTTP client
 * @param verify Whether to verify SSL certificates
 * @return 0 on success, error code on failure
 */
CFDDNS_API int http_client_set_ssl_verify(http_client_t *client, bool verify);

/**
 * @brief Set proxy
 * @param client HTTP client
 * @param proxy Proxy URL
 * @param username Proxy username (NULL for no auth)
 * @param password Proxy password (NULL for no auth)
 * @return 0 on success, error code on failure
 */
CFDDNS_API int http_client_set_proxy(http_client_t *client,
                                     const char *proxy,
                                     const char *username,
                                     const char *password);

/**
 * @brief Set network interface binding (for multi-WAN support)
 * @param client HTTP client
 * @param interface Network interface name (e.g., "eth0", "wan1") or NULL to unbind
 * @return 0 on success, error code on failure
 */
CFDDNS_API int http_client_set_bind_interface(http_client_t *client, const char *interface);

/**
 * @brief Set source IP address binding (for multi-WAN support)
 * @param client HTTP client
 * @param address Source IP address to bind (NULL to unbind)
 * @return 0 on success, error code on failure
 */
CFDDNS_API int http_client_set_bind_address(http_client_t *client, const char *address);

/* ========== Response Utilities ========== */

/**
 * @brief Initialize response structure
 * @param response Response structure
 */
CFDDNS_API void http_response_init(http_response_t *response);

/**
 * @brief Free response resources
 * @param response Response structure
 */
CFDDNS_API void http_response_free(http_response_t *response);

/**
 * @brief Check if response indicates success (2xx status)
 * @param response Response structure
 * @return true if successful, false otherwise
 */
CFDDNS_API bool http_response_is_success(const http_response_t *response);

/**
 * @brief Get header value from response
 * @param response Response structure
 * @param key Header key (case-insensitive)
 * @return Header value, or NULL if not found
 */
CFDDNS_API const char *http_response_get_header(const http_response_t *response,
                                                 const char *key);

/* ========== Header Utilities ========== */

/**
 * @brief Create header list
 * @return Empty header list
 */
CFDDNS_API http_header_t *http_header_list_create(void);

/**
 * @brief Add header to list
 * @param headers Header list
 * @param key Header key
 * @param value Header value
 * @return Updated header list, or NULL on failure
 */
CFDDNS_API http_header_t *http_header_list_add(http_header_t *headers,
                                                const char *key,
                                                const char *value);

/**
 * @brief Free header list
 * @param headers Header list
 */
CFDDNS_API void http_header_list_free(http_header_t *headers);

/* ========== Implementation Registration ========== */

/**
 * @brief Register HTTP client implementation
 * @param impl Implementation interface
 * @return 0 on success, error code on failure
 */
CFDDNS_API int http_client_register_impl(const http_client_impl_t *impl);

/**
 * @brief Get available implementation names
 * @param buf Buffer to store names
 * @param len Buffer length
 * @return Number of implementations written
 */
CFDDNS_API int http_client_get_impl_names(char buf[][32], int max_count);

#ifdef __cplusplus
}
#endif

#endif /* CFDDNS_HAL_HTTP_CLIENT_H */