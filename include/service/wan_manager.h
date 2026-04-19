/**
 * @file wan_manager.h
 * @brief WAN interface manager for multi-WAN support
 *
 * This module manages multiple WAN interfaces for gateway devices,
 * supporting interface-based IP detection, alias mapping, and failover.
 */

#ifndef CFDDNS_SERVICE_WAN_MANAGER_H
#define CFDDNS_SERVICE_WAN_MANAGER_H

#include "service/ip_provider.h"
#include "hal/http_client.h"
#include "hal/config.h"
#include "hal/platform.h"
#include "common/types.h"
#include "common/errors.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ========== Constants ========== */

#define WAN_MAX_NAME_LEN     32
#define WAN_MAX_ALIAS_LEN    64
#define WAN_MAX_ENDPOINT_LEN 256
#define WAN_MAX_COUNT        8

/* ========== WAN Interface Type ========== */

typedef enum {
    WAN_TYPE_DHCP = 0,        /**< DHCP assigned IP */
    WAN_TYPE_STATIC = 1,      /**< Static IP configuration */
    WAN_TYPE_PPPOE = 2,       /**< PPPoE connection */
    WAN_TYPE_AUTO = 3,        /**< Auto-detect type */
} wan_type_t;

/* ========== WAN Interface Configuration ========== */

typedef struct {
    char name[WAN_MAX_NAME_LEN];         /**< System interface name (e.g., "eth0", "wan1") */
    char alias[WAN_MAX_ALIAS_LEN];       /**< User-friendly alias (e.g., "主 WAN", "电信") */
    wan_type_t type;                     /**< WAN type */
    bool enabled;                        /**< Whether this WAN is enabled */

    /* IP detection settings */
    bool detect_ipv4;                    /**< Auto-detect IPv4 from this interface */
    bool detect_ipv6;                    /**< Auto-detect IPv6 from this interface */

    /* Detection methods (both supported) */
    bool use_external_detect;            /**< Use external IP detection service */
    bool use_local_read;                 /**< Read IP from local interface config */

    /* Custom endpoints (optional) */
    char ipv4_endpoint[WAN_MAX_ENDPOINT_LEN];  /**< Custom IPv4 detection endpoint */
    char ipv6_endpoint[WAN_MAX_ENDPOINT_LEN];  /**< Custom IPv6 detection endpoint */

    /* Priority and failover */
    int priority;                        /**< Priority for failover (lower = higher priority) */
    bool failover_enabled;               /**< Enable failover to this WAN */
    int check_interval;                  /**< Health check interval in seconds */

    /* Status tracking */
    bool is_online;                      /**< Current online status */
    char current_ipv4[48];               /**< Current IPv4 address */
    char current_ipv6[128];              /**< Current IPv6 address */
    uint64_t last_check_time;            /**< Last health check timestamp */
    int failed_checks;                   /**< Consecutive failed health checks */
} wan_config_t;

/* ========== WAN Manager Configuration ========== */

typedef struct {
    wan_config_t wans[WAN_MAX_COUNT];    /**< WAN configurations */
    int wan_count;                       /**< Number of configured WANs */
    char default_wan[WAN_MAX_NAME_LEN];  /**< Default WAN interface name */
    bool failover_enabled;               /**< Global failover enabled */
    int failover_threshold;              /**< Failed checks before failover */
    int check_interval;                  /**< Default health check interval */
} wan_manager_config_t;

/* ========== WAN IP Result ========== */

typedef struct {
    char wan_name[WAN_MAX_NAME_LEN];     /**< WAN interface name */
    char ipv4[48];                       /**< IPv4 address */
    char ipv6[128];                      /**< IPv6 address */
    uint64_t timestamp;                  /**< Detection timestamp */
    bool success;                        /**< Whether detection succeeded */
} wan_ip_result_t;

/* ========== WAN Manager Interface ========== */

typedef struct wan_manager {
    void *handle;                        /**< Internal handle */
    ip_provider_t *ip_provider;          /**< IP provider dependency */
    http_client_t *http_client;          /**< HTTP client dependency */

    /* Lifecycle */
    int (*init)(struct wan_manager *self, const wan_manager_config_t *config);
    void (*destroy)(struct wan_manager *self);

    /* IP detection - both external and local methods */
    int (*detect_ip_external)(struct wan_manager *self,
                               const char *wan_name,
                               ip_type_t type,
                               char *buf, size_t len);
    int (*detect_ip_local)(struct wan_manager *self,
                           const char *wan_name,
                           ip_type_t type,
                           char *buf, size_t len);
    int (*detect_ip)(struct wan_manager *self,
                     const char *wan_name_or_alias,
                     ip_type_t type,
                     char *buf, size_t len);
    int (*detect_all_ips)(struct wan_manager *self,
                          const char *wan_name,
                          wan_ip_result_t *result);

    /* Alias mapping */
    int (*map_alias_to_interface)(struct wan_manager *self,
                                   const char *alias,
                                   char *buf, size_t len);

    /* Health check */
    int (*health_check)(struct wan_manager *self, const char *wan_name);
    int (*check_all_health)(struct wan_manager *self);

    /* WAN management */
    int (*add_wan)(struct wan_manager *self, const wan_config_t *wan);
    int (*remove_wan)(struct wan_manager *self, const char *wan_name);
    int (*enable_wan)(struct wan_manager *self, const char *wan_name);
    int (*disable_wan)(struct wan_manager *self, const char *wan_name);
    int (*update_wan)(struct wan_manager *self, const char *wan_name, const wan_config_t *wan);

    /* Query */
    wan_config_t* (*get_wan)(struct wan_manager *self, const char *wan_name_or_alias);
    int (*get_all_wans)(struct wan_manager *self, wan_config_t *wans, int max_count);
    int (*get_active_wan)(struct wan_manager *self, char *buf, size_t len);
    int (*get_online_wans)(struct wan_manager *self, wan_config_t *wans, int max_count);

    /* Failover */
    int (*check_failover)(struct wan_manager *self);
    int (*switch_wan)(struct wan_manager *self, const char *wan_name);

    /* Dependencies */
    void (*set_ip_provider)(struct wan_manager *self, ip_provider_t *provider);
    void (*set_http_client)(struct wan_manager *self, http_client_t *client);

} wan_manager_t;

/* ========== WAN Manager Creation ========== */

/**
 * @brief Create WAN manager
 * @return WAN manager on success, NULL on failure
 */
CFDDNS_API wan_manager_t *wan_manager_create(void);

/**
 * @brief Create WAN manager with dependencies
 * @param ip_provider IP provider
 * @param http_client HTTP client
 * @return WAN manager on success, NULL on failure
 */
CFDDNS_API wan_manager_t *wan_manager_create_with_deps(ip_provider_t *ip_provider,
                                                        http_client_t *http_client);

/**
 * @brief Destroy WAN manager
 * @param manager WAN manager
 */
CFDDNS_API void wan_manager_destroy(wan_manager_t *manager);

/* ========== Initialization ========== */

/**
 * @brief Initialize WAN manager
 * @param manager WAN manager
 * @param config Configuration
 * @return 0 on success, error code on failure
 */
CFDDNS_API int wan_manager_init(wan_manager_t *manager, const wan_manager_config_t *config);

/**
 * @brief Get default WAN manager configuration
 * @return Default configuration
 */
CFDDNS_API wan_manager_config_t wan_manager_config_default(void);

/* ========== Config Parsing ========== */

CFDDNS_API int wan_config_from_json(config_t *cfg, int index, wan_config_t *wan);
CFDDNS_API int wan_manager_config_from_json(config_t *cfg, wan_manager_config_t *config);

/* ========== IP Detection ========== */

/**
 * @brief Detect IP from specific WAN interface
 * @param manager WAN manager
 * @param wan_name_or_alias WAN name or alias (supports both)
 * @param type IP type (IPv4 or IPv6)
 * @param buf Buffer to store IP address
 * @param len Buffer length
 * @return 0 on success, error code on failure
 */
CFDDNS_API int wan_manager_detect_ip(wan_manager_t *manager,
                                      const char *wan_name_or_alias,
                                      ip_type_t type,
                                      char *buf, size_t len);

/**
 * @brief Detect IP using external service (binds to interface)
 * @param manager WAN manager
 * @param wan_name WAN interface name
 * @param type IP type
 * @param buf Buffer to store IP address
 * @param len Buffer length
 * @return 0 on success, error code on failure
 */
CFDDNS_API int wan_manager_detect_ip_external(wan_manager_t *manager,
                                               const char *wan_name,
                                               ip_type_t type,
                                               char *buf, size_t len);

/**
 * @brief Detect IP from local interface config
 * @param manager WAN manager
 * @param wan_name WAN interface name
 * @param type IP type
 * @param buf Buffer to store IP address
 * @param len Buffer length
 * @return 0 on success, error code on failure
 */
CFDDNS_API int wan_manager_detect_ip_local(wan_manager_t *manager,
                                            const char *wan_name,
                                            ip_type_t type,
                                            char *buf, size_t len);

/**
 * @brief Detect both IPv4 and IPv6 from WAN interface
 * @param manager WAN manager
 * @param wan_name WAN interface name
 * @param result Result structure
 * @return 0 on success, error code on failure
 */
CFDDNS_API int wan_manager_detect_all_ips(wan_manager_t *manager,
                                           const char *wan_name,
                                           wan_ip_result_t *result);

/* ========== Alias Mapping ========== */

/**
 * @brief Map user alias to system interface name
 * @param manager WAN manager
 * @param alias User alias (e.g., "主 WAN")
 * @param buf Buffer to store interface name
 * @param len Buffer length
 * @return 0 on success, error code on failure
 */
CFDDNS_API int wan_manager_map_alias(wan_manager_t *manager,
                                      const char *alias,
                                      char *buf, size_t len);

/* ========== Health Check ========== */

/**
 * @brief Perform health check on WAN interface
 * @param manager WAN manager
 * @param wan_name WAN interface name
 * @return 0 if online, error code if offline
 */
CFDDNS_API int wan_manager_health_check(wan_manager_t *manager, const char *wan_name);

/**
 * @brief Check health of all WANs
 * @param manager WAN manager
 * @return Number of online WANs
 */
CFDDNS_API int wan_manager_check_all_health(wan_manager_t *manager);

/* ========== WAN Management ========== */

/**
 * @brief Add WAN interface configuration
 * @param manager WAN manager
 * @param wan WAN configuration
 * @return 0 on success, error code on failure
 */
CFDDNS_API int wan_manager_add_wan(wan_manager_t *manager, const wan_config_t *wan);

/**
 * @brief Remove WAN interface configuration
 * @param manager WAN manager
 * @param wan_name WAN interface name
 * @return 0 on success, error code on failure
 */
CFDDNS_API int wan_manager_remove_wan(wan_manager_t *manager, const char *wan_name);

/**
 * @brief Enable WAN interface
 * @param manager WAN manager
 * @param wan_name WAN interface name
 * @return 0 on success, error code on failure
 */
CFDDNS_API int wan_manager_enable_wan(wan_manager_t *manager, const char *wan_name);

/**
 * @brief Disable WAN interface
 * @param manager WAN manager
 * @param wan_name WAN interface name
 * @return 0 on success, error code on failure
 */
CFDDNS_API int wan_manager_disable_wan(wan_manager_t *manager, const char *wan_name);

/* ========== Query ========== */

/**
 * @brief Get WAN configuration by name or alias
 * @param manager WAN manager
 * @param wan_name_or_alias WAN name or alias
 * @return WAN configuration, or NULL if not found
 */
CFDDNS_API wan_config_t *wan_manager_get_wan(wan_manager_t *manager,
                                              const char *wan_name_or_alias);

/**
 * @brief Get all WAN configurations
 * @param manager WAN manager
 * @param wans Buffer to store WAN configurations
 * @param max_count Maximum number of WANs to return
 * @return Number of WANs returned
 */
CFDDNS_API int wan_manager_get_all_wans(wan_manager_t *manager,
                                         wan_config_t *wans,
                                         int max_count);

/**
 * @brief Get name of active (highest priority online) WAN
 * @param manager WAN manager
 * @param buf Buffer to store WAN name
 * @param len Buffer length
 * @return 0 on success, error code on failure
 */
CFDDNS_API int wan_manager_get_active_wan(wan_manager_t *manager,
                                           char *buf, size_t len);

/**
 * @brief Get all online WAN configurations
 * @param manager WAN manager
 * @param wans Buffer to store WAN configurations
 * @param max_count Maximum number of WANs to return
 * @return Number of online WANs returned
 */
CFDDNS_API int wan_manager_get_online_wans(wan_manager_t *manager,
                                            wan_config_t *wans,
                                            int max_count);

/* ========== Failover ========== */

/**
 * @brief Check and perform failover if needed
 * @param manager WAN manager
 * @return 0 if no failover needed, 1 if failover occurred, error code on failure
 */
CFDDNS_API int wan_manager_check_failover(wan_manager_t *manager);

/**
 * @brief Manually switch to specified WAN
 * @param manager WAN manager
 * @param wan_name WAN to switch to
 * @return 0 on success, error code on failure
 */
CFDDNS_API int wan_manager_switch_wan(wan_manager_t *manager, const char *wan_name);

/* ========== Dependencies ========== */

/**
 * @brief Set IP provider
 * @param manager WAN manager
 * @param provider IP provider
 */
CFDDNS_API void wan_manager_set_ip_provider(wan_manager_t *manager, ip_provider_t *provider);

/**
 * @brief Set HTTP client
 * @param manager WAN manager
 * @param client HTTP client
 */
CFDDNS_API void wan_manager_set_http_client(wan_manager_t *manager, http_client_t *client);

/* ========== Status Display ========== */

/**
 * @brief Print WAN status summary to stdout (table format)
 * @param manager WAN manager
 */
CFDDNS_API void wan_manager_show_status(wan_manager_t *manager);

/**
 * @brief Print detailed WAN configuration to stdout
 * @param manager WAN manager
 * @param wan_name WAN name or alias (NULL for all WANs)
 */
CFDDNS_API void wan_manager_show_config(wan_manager_t *manager, const char *wan_name);

/**
 * @brief Format WAN status as string
 * @param manager WAN manager
 * @param buf Buffer to store formatted string
 * @param len Buffer length
 * @return Number of characters written
 */
CFDDNS_API int wan_manager_format_status(wan_manager_t *manager, char *buf, size_t len);

/**
 * @brief Get WAN type name
 * @param type WAN type
 * @return Type name string
 */
CFDDNS_API const char *wan_type_name(wan_type_t type);

/* ========== CLI Show Commands ========== */

/**
 * @brief Show command options
 */
typedef enum {
    WAN_SHOW_ALL = 0,          /**< Show all WAN interfaces */
    WAN_SHOW_STATUS = 1,       /**< Show status summary only */
    WAN_SHOW_CONFIG = 2,       /**< Show detailed configuration */
    WAN_SHOW_IP = 3,           /**< Show IP addresses only */
    WAN_SHOW_INTERFACE = 4,    /**< Show specific interface */
} wan_show_mode_t;

/**
 * @brief Execute show command for WAN manager
 * @param manager WAN manager
 * @param mode Show mode
 * @param arg Optional argument (interface name/alias for WAN_SHOW_INTERFACE)
 */
CFDDNS_API void wan_manager_show(wan_manager_t *manager, wan_show_mode_t mode, const char *arg);

#ifdef __cplusplus
}
#endif

#endif /* CFDDNS_SERVICE_WAN_MANAGER_H */