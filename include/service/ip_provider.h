/**
 * @file ip_provider.h
 * @brief IP address provider service interface
 *
 * This module provides a unified interface for obtaining public IP addresses
 * from various IP detection services.
 */

#ifndef CFDDNS_SERVICE_IP_PROVIDER_H
#define CFDDNS_SERVICE_IP_PROVIDER_H

#include "hal/http_client.h"
#include "common/types.h"
#include "common/errors.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ========== IP Address Types ========== */

typedef enum {
    IP_TYPE_IPV4 = 0,       /**< IPv4 address */
    IP_TYPE_IPV6 = 1,       /**< IPv6 address */
    IP_TYPE_AUTO = 2,       /**< Auto detect (prefer IPv4) */
} ip_type_t;

/* ========== IP Provider Configuration ========== */

typedef struct {
    int timeout_ms;                     /**< Request timeout in milliseconds */
    int retry_count;                    /**< Number of retries on failure */
    int retry_delay_ms;                 /**< Delay between retries in milliseconds */
    bool verify_response;               /**< Verify response format */
    ip_type_t preferred_type;           /**< Preferred IP type */

    /* Interface binding for multi-WAN support */
    const char *bind_interface;         /**< Network interface to bind (e.g., "eth0") */
} ip_provider_config_t;

/* ========== IP Provider Endpoint ========== */

typedef struct {
    const char *name;                   /**< Provider name */
    const char *url_ipv4;               /**< IPv4 detection URL */
    const char *url_ipv6;               /**< IPv6 detection URL */
    int priority;                       /**< Priority (lower is higher priority) */
} ip_provider_endpoint_t;

/* ========== IP Provider Result ========== */

typedef struct {
    char ipv4[48];                      /**< IPv4 address */
    char ipv6[128];                     /**< IPv6 address */
    uint64_t timestamp;                 /**< Detection timestamp (ms) */
    ip_provider_endpoint_t *provider;   /**< Provider that returned the result */
} ip_result_t;

/* ========== IP Provider Interface ========== */

typedef struct ip_provider {
    void *handle;                       /**< Internal handle */
    http_client_t *http_client;         /**< HTTP client dependency */

    /**
     * @brief Initialize IP provider
     * @param self IP provider
     * @param config Configuration
     * @return 0 on success, error code on failure
     */
    int (*init)(struct ip_provider *self, const ip_provider_config_t *config);

    /**
     * @brief Get public IP address
     * @param self IP provider
     * @param type IP type to get
     * @param buf Buffer to store IP address
     * @param len Buffer length
     * @return 0 on success, error code on failure
     */
    int (*get_ip)(struct ip_provider *self, ip_type_t type, char *buf, size_t len);

    /**
     * @brief Get both IPv4 and IPv6 addresses
     * @param self IP provider
     * @param result Result structure
     * @return 0 on success, error code on failure
     */
    int (*get_all_ips)(struct ip_provider *self, ip_result_t *result);

    /**
     * @brief Destroy IP provider
     * @param self IP provider
     */
    void (*destroy)(struct ip_provider *self);

    /**
     * @brief Set HTTP client
     * @param self IP provider
     * @param client HTTP client
     */
    void (*set_http_client)(struct ip_provider *self, http_client_t *client);

    /**
     * @brief Get public IP from specific interface (multi-WAN support)
     * @param self IP provider
     * @param type IP type to get
     * @param interface_name Network interface name (e.g., "eth0", "wan1")
     * @param buf Buffer to store IP address
     * @param len Buffer length
     * @return 0 on success, error code on failure
     */
    int (*get_ip_from_interface)(struct ip_provider *self, ip_type_t type,
                                  const char *interface_name, char *buf, size_t len);

    /**
     * @brief Set bind interface for IP detection
     * @param self IP provider
     * @param interface_name Interface name (NULL to unbind)
     * @return 0 on success, error code on failure
     */
    int (*set_bind_interface)(struct ip_provider *self, const char *interface_name);

} ip_provider_t;

/* ========== IP Provider Creation ========== */

/**
 * @brief Create IP provider with default endpoints
 * @return IP provider on success, NULL on failure
 */
CFDDNS_API ip_provider_t *ip_provider_create(void);

/**
 * @brief Create IP provider with custom HTTP client
 * @param http_client HTTP client (must not be NULL)
 * @return IP provider on success, NULL on failure
 */
CFDDNS_API ip_provider_t *ip_provider_create_with_http(http_client_t *http_client);

/**
 * @brief Destroy IP provider
 * @param provider IP provider
 */
CFDDNS_API void ip_provider_destroy(ip_provider_t *provider);

/* ========== IP Detection ========== */

/**
 * @brief Get public IP address
 * @param provider IP provider (NULL for global instance)
 * @param type IP type to get
 * @param buf Buffer to store IP address
 * @param len Buffer length
 * @return 0 on success, error code on failure
 */
CFDDNS_API int ip_provider_get_ip(ip_provider_t *provider, ip_type_t type, char *buf, size_t len);

/**
 * @brief Get IPv4 address only
 * @param provider IP provider (NULL for global instance)
 * @param buf Buffer to store IP address
 * @param len Buffer length
 * @return 0 on success, error code on failure
 */
CFDDNS_API int ip_provider_get_ipv4(ip_provider_t *provider, char *buf, size_t len);

/**
 * @brief Get IPv6 address only
 * @param provider IP provider (NULL for global instance)
 * @param buf Buffer to store IP address
 * @param len Buffer length
 * @return 0 on success, error code on failure
 */
CFDDNS_API int ip_provider_get_ipv6(ip_provider_t *provider, char *buf, size_t len);

/**
 * @brief Get both IPv4 and IPv6 addresses
 * @param provider IP provider (NULL for global instance)
 * @param result Result structure
 * @return 0 on success, error code on failure
 */
CFDDNS_API int ip_provider_get_all(ip_provider_t *provider, ip_result_t *result);

/* ========== Configuration ========== */

/**
 * @brief Get default IP provider configuration
 * @return Default configuration
 */
CFDDNS_API ip_provider_config_t ip_provider_config_default(void);

/**
 * @brief Set IP provider configuration
 * @param provider IP provider
 * @param config Configuration
 * @return 0 on success, error code on failure
 */
CFDDNS_API int ip_provider_set_config(ip_provider_t *provider, const ip_provider_config_t *config);

/**
 * @brief Set HTTP client for IP provider
 * @param provider IP provider
 * @param client HTTP client
 */
CFDDNS_API void ip_provider_set_http_client(ip_provider_t *provider, http_client_t *client);

/* ========== Custom Endpoints ========== */

/**
 * @brief Add custom IP detection endpoint
 * @param provider IP provider
 * @param endpoint Endpoint information
 * @return 0 on success, error code on failure
 */
CFDDNS_API int ip_provider_add_endpoint(ip_provider_t *provider, const ip_provider_endpoint_t *endpoint);

/**
 * @brief Remove custom endpoint
 * @param provider IP provider
 * @param name Endpoint name
 * @return 0 on success, error code on failure
 */
CFDDNS_API int ip_provider_remove_endpoint(ip_provider_t *provider, const char *name);

/**
 * @brief Get available endpoints
 * @param provider IP provider
 * @param endpoints Endpoint array output
 * @param max_count Maximum endpoints to return
 * @return Number of endpoints returned
 */
CFDDNS_API int ip_provider_get_endpoints(ip_provider_t *provider,
                                          ip_provider_endpoint_t *endpoints,
                                          int max_count);

/* ========== Utility Functions ========== */

/**
 * @brief Validate IP address format
 * @param ip IP address string
 * @param type Expected IP type
 * @return 1 if valid, 0 if invalid
 */
CFDDNS_API int ip_validate(const char *ip, ip_type_t type);

/**
 * @brief Detect IP address type
 * @param ip IP address string
 * @return IP type, or IP_TYPE_IPV4 if cannot determine
 */
CFDDNS_API ip_type_t ip_detect_type(const char *ip);

/**
 * @brief Compare IP addresses
 * @param ip1 First IP address
 * @param ip2 Second IP address
 * @return 0 if equal, non-zero if different
 */
CFDDNS_API int ip_compare(const char *ip1, const char *ip2);

/* ========== Default Endpoints ========== */

/**
 * @brief Get default IP detection endpoints
 * @return Array of default endpoints (NULL-terminated)
 */
CFDDNS_API const ip_provider_endpoint_t *ip_provider_default_endpoints(void);

/* ========== Interface Binding (Multi-WAN) ========== */

/**
 * @brief Get public IP from specific network interface
 * @param provider IP provider (NULL for global instance)
 * @param type IP type to get
 * @param interface_name Network interface name (e.g., "eth0", "wan1")
 * @param buf Buffer to store IP address
 * @param len Buffer length
 * @return 0 on success, error code on failure
 */
CFDDNS_API int ip_provider_get_ip_from_interface(ip_provider_t *provider,
                                                  ip_type_t type,
                                                  const char *interface_name,
                                                  char *buf, size_t len);

/**
 * @brief Get public IP from an explicit endpoint URL (with optional interface binding)
 * @param provider IP provider
 * @param type IP type to get
 * @param endpoint_url Endpoint URL from configuration
 * @param interface_name Network interface name (NULL for no binding)
 * @param buf Buffer to store IP address
 * @param len Buffer length
 * @return 0 on success, error code on failure
 */
CFDDNS_API int ip_provider_get_ip_from_endpoint(ip_provider_t *provider,
                                                 ip_type_t type,
                                                 const char *endpoint_url,
                                                 const char *interface_name,
                                                 char *buf, size_t len);

/**
 * @brief Set bind interface for IP detection
 * @param provider IP provider
 * @param interface_name Interface name (NULL to unbind)
 * @return 0 on success, error code on failure
 */
CFDDNS_API int ip_provider_set_bind_interface(ip_provider_t *provider, const char *interface_name);

#ifdef __cplusplus
}
#endif

#endif /* CFDDNS_SERVICE_IP_PROVIDER_H */