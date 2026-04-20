/**
 * @file ddns_service.h
 * @brief DDNS service interface for CloudFlare
 *
 * This module provides the core DDNS functionality including:
 * - DNS record management (create, update, delete)
 * - IP change detection
 * - Automatic updates on schedule
 */

#ifndef CFDDNS_SERVICE_DDNS_SERVICE_H
#define CFDDNS_SERVICE_DDNS_SERVICE_H

#include "hal/http_client.h"
#include "hal/json_parser.h"
#include "hal/logger.h"
#include "hal/config.h"
#include "ip_provider.h"
#include "wan_manager.h"
#include "common/types.h"
#include "common/errors.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ========== CloudFlare API Constants ========== */

#define CF_API_BASE_URL    "https://api.cloudflare.com/client/v4"
#define CF_API_TIMEOUT_MS  30000

/* ========== DNS Record Types ========== */

typedef enum {
    DNS_RECORD_A = 0,        /**< IPv4 address record */
    DNS_RECORD_AAAA = 1,     /**< IPv6 address record */
    DNS_RECORD_CNAME = 2,    /**< Canonical name record */
    DNS_RECORD_TXT = 3,      /**< Text record */
    DNS_RECORD_MX = 4,       /**< Mail exchange record */
} dns_record_type_t;

/* ========== CloudFlare DNS Record ========== */

typedef struct {
    char id[64];             /**< Record ID (CloudFlare internal) */
    char name[256];          /**< Record name (e.g., "ddns.example.com") */
    dns_record_type_t type;  /**< Record type */
    char content[128];       /**< Record content (IP address) */
    int ttl;                 /**< TTL in seconds */
    bool proxied;            /**< Whether proxied through CloudFlare */
    bool locked;             /**< Whether record is locked */
    char zone_id[64];        /**< Zone ID */
    char zone_name[128];     /**< Zone name (e.g., "example.com") */
    uint64_t created_on;     /**< Creation timestamp */
    uint64_t modified_on;    /**< Last modification timestamp */
} dns_record_t;

/* ========== DDNS Service Status ========== */

typedef enum {
    DDNS_STATUS_STOPPED = 0,
    DDNS_STATUS_RUNNING = 1,
    DDNS_STATUS_UPDATING = 2,
    DDNS_STATUS_ERROR = 3,
    DDNS_STATUS_PAUSED = 4,
} ddns_status_t;

/* ========== DDNS Event Types ========== */

typedef enum {
    DDNS_EVENT_STARTED,          /**< Service started */
    DDNS_EVENT_STOPPED,          /**< Service stopped */
    DDNS_EVENT_IP_CHECK,         /**< IP check performed */
    DDNS_EVENT_IP_CHANGED,       /**< IP address changed */
    DDNS_EVENT_UPDATE_START,     /**< DNS update started */
    DDNS_EVENT_UPDATE_SUCCESS,   /**< DNS update successful */
    DDNS_EVENT_UPDATE_FAILED,    /**< DNS update failed */
    DDNS_EVENT_RECORD_CREATED,   /**< Record created */
    DDNS_EVENT_RECORD_DELETED,   /**< Record deleted */
    DDNS_EVENT_ERROR,            /**< Error occurred */
    DDNS_EVENT_ZONE_FOUND,       /**< Zone found */
    DDNS_EVENT_RECORD_FOUND,     /**< Record found */
} ddns_event_type_t;

/* ========== DDNS Event ========== */

typedef struct {
    ddns_event_type_t type;      /**< Event type */
    const char *message;         /**< Event message */
    cfddns_error_t error;        /**< Error code (if applicable) */
    const dns_record_t *record;  /**< Related record (if applicable) */
    const char *old_ip;          /**< Old IP address (for IP_CHANGED) */
    const char *new_ip;          /**< New IP address (for IP_CHANGED) */
    uint64_t timestamp;          /**< Event timestamp */
} ddns_event_t;

/* ========== DDNS Event Callback ========== */

/**
 * @brief DDNS event callback function type
 * @param event Event information
 * @param user_data User data
 */
typedef void (*ddns_event_callback_t)(const ddns_event_t *event, void *user_data);

/* ========== CloudFlare Zone ========== */

typedef struct {
    char id[64];             /**< Zone ID */
    char name[128];          /**< Zone name */
    char status[32];         /**< Zone status */
    bool paused;             /**< Whether zone is paused */
    char plan[32];           /**< Zone plan */
} cf_zone_t;

/* ========== CloudFlare Configuration ========== */

typedef struct {
    char api_token[256];     /**< CloudFlare API token */
    char zone_id[64];        /**< Zone ID (optional, can be auto-detected) */
    char zone_name[128];     /**< Zone name (e.g., "example.com") */
    char record_name[256];   /**< DNS record name (e.g., "ddns.example.com") */
    dns_record_type_t record_type; /**< Record type (A or AAAA) */
    int ttl;                 /**< TTL in seconds (1 = automatic, 3600 = 1 hour) */
    bool proxied;            /**< Enable CloudFlare proxy */
    int check_interval;      /**< IP check interval in seconds */
    bool update_on_change;   /**< Update DNS when IP changes */
    bool create_if_missing;  /**< Create record if it doesn't exist */
    bool delete_on_exit;     /**< Delete record on service exit */
} cf_config_t;

/* ========== Record Binding Mode (Multi-WAN) ========== */

typedef enum {
    RECORD_BINDING_FIXED = 0,    /**< Fixed binding to single WAN */
    RECORD_BINDING_DYNAMIC = 1,  /**< Dynamic priority-based failover */
    RECORD_BINDING_AUTO = 2,     /**< Auto-select first available WAN */
} record_binding_mode_t;

/* ========== DNS Record Configuration (Multi-Record) ========== */

typedef struct {
    char id[64];                /**< Unique identifier for this record config */
    char name[128];             /**< Record name for display/debugging */

    /* CloudFlare API settings */
    char api_token[256];        /**< CloudFlare API token (can be shared) */
    char zone_id[64];           /**< Zone ID */
    char zone_name[128];        /**< Zone name */
    char record_name[256];      /**< DNS record name */
    dns_record_type_t record_type; /**< Record type (A or AAAA - configured separately) */

    /* WAN binding (Hybrid mode) */
    record_binding_mode_t binding_mode;
    char wan_interface[32];     /**< Fixed: specific WAN name/alias */
    int wan_priority;           /**< Dynamic: priority level for this record */

    /* Forced IP (optional) */
    char forced_ip[128];        /**< Override auto-detection if set */

    /* Record settings */
    int ttl;                    /**< TTL in seconds */
    bool proxied;               /**< Enable CloudFlare proxy */

    /* Update behavior */
    int check_interval;         /**< IP check interval for this record */
    bool update_on_change;      /**< Update DNS when IP changes */
    bool create_if_missing;     /**< Create record if not exists */
    bool delete_on_exit;        /**< Delete record on service exit */

    /* Status tracking (runtime) */
    ddns_status_t status;       /**< Current status */
    char record_id[64];         /**< CloudFlare DNS record ID (runtime) */
    char current_ip[128];       /**< Current detected IP */
    char dns_ip[128];           /**< Current DNS record IP */
    uint64_t last_update;       /**< Last successful update timestamp */
    int failed_attempts;        /**< Consecutive failed attempts */
} dns_record_config_t;

/* ========== Global DDNS Configuration (Multi-Record) ========== */

#define DDNS_MAX_RECORDS 16

typedef struct {
    char api_token[256];        /**< Default API token (shared by all records) */
    bool use_shared_token;      /**< Whether to use shared API token */

    /* DNS record configurations */
    dns_record_config_t records[DDNS_MAX_RECORDS];
    int record_count;

    /* Global settings */
    int default_check_interval; /**< Default check interval */
    int max_concurrent_updates; /**< Maximum concurrent DNS updates */
    bool parallel_updates;      /**< Enable parallel DNS updates */

    /* Logging and events */
    ddns_event_callback_t event_callback;
    void *event_user_data;
} ddns_global_config_t;

/* ========== Config Parsing ========== */

CFDDNS_API int dns_record_config_from_json(config_t *cfg, int index, dns_record_config_t *rec, const char *shared_token);
CFDDNS_API int ddns_global_config_from_json(config_t *cfg, ddns_global_config_t *config);

/* ========== DDNS Statistics ========== */

typedef struct {
    uint64_t start_time;         /**< Service start time */
    uint64_t last_check_time;    /**< Last IP check time */
    uint64_t last_update_time;   /**< Last DNS update time */
    int total_checks;            /**< Total IP checks performed */
    int total_updates;           /**< Total DNS updates performed */
    int successful_updates;      /**< Successful updates */
    int failed_updates;          /**< Failed updates */
    char current_ip[48];         /**< Current detected IP */
    char dns_ip[48];             /**< Current DNS record IP */
    ddns_status_t status;        /**< Current service status */
} ddns_stats_t;

/* ========== DDNS Service Interface ========== */

typedef struct ddns_service {
    void *handle;                /**< Internal handle */
    http_client_t *http_client;  /**< HTTP client */
    json_parser_t *json_parser;  /**< JSON parser */
    logger_t *logger;            /**< Logger */
    ip_provider_t *ip_provider;  /**< IP provider */
    config_t *config;            /**< Configuration */
    wan_manager_t *wan_manager;  /**< WAN manager (multi-WAN) */

    /* ========== Lifecycle ========== */

    /**
     * @brief Initialize DDNS service
     * @param self DDNS service
     * @param config CloudFlare configuration
     * @return 0 on success, error code on failure
     */
    int (*init)(struct ddns_service *self, const cf_config_t *config);

    /**
     * @brief Start DDNS service
     * @param self DDNS service
     * @return 0 on success, error code on failure
     */
    int (*start)(struct ddns_service *self);

    /**
     * @brief Stop DDNS service
     * @param self DDNS service
     * @return 0 on success, error code on failure
     */
    int (*stop)(struct ddns_service *self);

    /**
     * @brief Destroy DDNS service
     * @param self DDNS service
     */
    void (*destroy)(struct ddns_service *self);

    /* ========== Manual Operations ========== */

    /**
     * @brief Check IP immediately
     * @param self DDNS service
     * @return 0 on success, error code on failure
     */
    int (*check_now)(struct ddns_service *self);

    /**
     * @brief Force update DNS record
     * @param self DDNS service
     * @param ip IP address (NULL to auto-detect)
     * @return 0 on success, error code on failure
     */
    int (*force_update)(struct ddns_service *self, const char *ip);

    /**
     * @brief Create DNS record
     * @param self DDNS service
     * @param ip IP address
     * @return 0 on success, error code on failure
     */
    int (*create_record)(struct ddns_service *self, const char *ip);

    /**
     * @brief Update DNS record
     * @param self DDNS service
     * @param ip New IP address
     * @return 0 on success, error code on failure
     */
    int (*update_record)(struct ddns_service *self, const char *ip);

    /**
     * @brief Delete DNS record
     * @param self DDNS service
     * @return 0 on success, error code on failure
     */
    int (*delete_record)(struct ddns_service *self);

    /* ========== Configuration ========== */

    /**
     * @brief Set check interval
     * @param self DDNS service
     * @param seconds Interval in seconds
     * @return 0 on success, error code on failure
     */
    int (*set_interval)(struct ddns_service *self, int seconds);

    /**
     * @brief Set event callback
     * @param self DDNS service
     * @param callback Event callback
     * @param user_data User data
     * @return 0 on success, error code on failure
     */
    int (*set_event_callback)(struct ddns_service *self,
                              ddns_event_callback_t callback,
                              void *user_data);

    /**
     * @brief Update configuration
     * @param self DDNS service
     * @param config New configuration
     * @return 0 on success, error code on failure
     */
    int (*update_config)(struct ddns_service *self, const cf_config_t *config);

    /* ========== Query ========== */

    /**
     * @brief Get current service status
     * @param self DDNS service
     * @return Service status
     */
    ddns_status_t (*get_status)(struct ddns_service *self);

    /**
     * @brief Get service statistics
     * @param self DDNS service
     * @param stats Statistics output
     * @return 0 on success, error code on failure
     */
    int (*get_stats)(struct ddns_service *self, ddns_stats_t *stats);

    /**
     * @brief Get current IP address
     * @param self DDNS service
     * @param buf Buffer to store IP
     * @param len Buffer length
     * @return 0 on success, error code on failure
     */
    int (*get_current_ip)(struct ddns_service *self, char *buf, size_t len);

    /**
     * @brief Get DNS record information
     * @param self DDNS service
     * @param record Record output
     * @return 0 on success, error code on failure
     */
    int (*get_record_info)(struct ddns_service *self, dns_record_t *record);

    /* ========== Dependencies ========== */

    /**
     * @brief Set HTTP client
     * @param self DDNS service
     * @param client HTTP client
     */
    void (*set_http_client)(struct ddns_service *self, http_client_t *client);

    /**
     * @brief Set JSON parser
     * @param self DDNS service
     * @param parser JSON parser
     */
    void (*set_json_parser)(struct ddns_service *self, json_parser_t *parser);

    /**
     * @brief Set logger
     * @param self DDNS service
     * @param logger Logger
     */
    void (*set_logger)(struct ddns_service *self, logger_t *logger);

    /**
     * @brief Set IP provider
     * @param self DDNS service
     * @param provider IP provider
     */
    void (*set_ip_provider)(struct ddns_service *self, ip_provider_t *provider);

    /**
     * @brief Set WAN manager
     * @param self DDNS service
     * @param manager WAN manager
     */
    void (*set_wan_manager)(struct ddns_service *self, wan_manager_t *manager);

    /* ========== Multi-Record Management ========== */

    /**
     * @brief Initialize with multi-record configuration
     * @param self DDNS service
     * @param config Global configuration with multiple records
     * @return 0 on success, error code on failure
     */
    int (*init_multi)(struct ddns_service *self, const ddns_global_config_t *config);

    /**
     * @brief Add DNS record configuration
     * @param self DDNS service
     * @param record Record configuration
     * @return 0 on success, error code on failure
     */
    int (*add_record)(struct ddns_service *self, const dns_record_config_t *record);

    /**
     * @brief Remove DNS record configuration
     * @param self DDNS service
     * @param record_id Record ID to remove
     * @return 0 on success, error code on failure
     */
    int (*remove_record)(struct ddns_service *self, const char *record_id);

    /**
     * @brief Update single DNS record configuration
     * @param self DDNS service
     * @param record_id Record ID to update
     * @param record New record configuration
     * @return 0 on success, error code on failure
     */
    int (*update_record_config)(struct ddns_service *self,
                                const char *record_id,
                                const dns_record_config_t *record);

    /**
     * @brief Check single DNS record
     * @param self DDNS service
     * @param record_id Record ID to check
     * @return 0 on success, error code on failure
     */
    int (*check_record)(struct ddns_service *self, const char *record_id);

    /**
     * @brief Update single DNS record now
     * @param self DDNS service
     * @param record_id Record ID to update
     * @return 0 on success, error code on failure
     */
    int (*update_record_now)(struct ddns_service *self, const char *record_id);

    /**
     * @brief Force update single DNS record with specific IP
     * @param self DDNS service
     * @param record_id Record ID to update
     * @param ip IP address to set
     * @return 0 on success, error code on failure
     */
    int (*force_update_record)(struct ddns_service *self,
                               const char *record_id,
                               const char *ip);

    /**
     * @brief Check all DNS records
     * @param self DDNS service
     * @return 0 on success, error code on failure
     */
    int (*check_all_records)(struct ddns_service *self);

    /**
     * @brief Update all DNS records
     * @param self DDNS service
     * @return 0 on success, error code on failure
     */
    int (*update_all_records)(struct ddns_service *self);

    /**
     * @brief Get DNS record configuration
     * @param self DDNS service
     * @param record_id Record ID
     * @return Record configuration, or NULL if not found
     */
    dns_record_config_t* (*get_record_config)(struct ddns_service *self, const char *record_id);

    /**
     * @brief Get all DNS record configurations
     * @param self DDNS service
     * @param records Buffer to store record configurations
     * @param max_count Maximum records to return
     * @return Number of records returned
     */
    int (*get_all_record_configs)(struct ddns_service *self,
                                  dns_record_config_t *records,
                                  int max_count);

    /**
     * @brief Get DNS record status
     * @param self DDNS service
     * @param record_id Record ID
     * @param stats Statistics output
     * @return 0 on success, error code on failure
     */
    int (*get_record_status)(struct ddns_service *self,
                             const char *record_id,
                             ddns_stats_t *stats);

} ddns_service_t;

/* ========== DDNS Service Creation ========== */

/**
 * @brief Create DDNS service
 * @return DDNS service on success, NULL on failure
 */
CFDDNS_API ddns_service_t *ddns_service_create(void);

/**
 * @brief Create DDNS service with dependencies
 * @param http_client HTTP client (NULL for default)
 * @param json_parser JSON parser (NULL for default)
 * @param logger Logger (NULL for default)
 * @param ip_provider IP provider (NULL for default)
 * @return DDNS service on success, NULL on failure
 */
CFDDNS_API ddns_service_t *ddns_service_create_full(http_client_t *http_client,
                                                     json_parser_t *json_parser,
                                                     logger_t *logger,
                                                     ip_provider_t *ip_provider);

/**
 * @brief Destroy DDNS service
 * @param service DDNS service
 */
CFDDNS_API void ddns_service_destroy(ddns_service_t *service);

/* ========== Lifecycle ========== */

CFDDNS_API int ddns_service_init(ddns_service_t *service, const cf_config_t *config);
CFDDNS_API int ddns_service_start(ddns_service_t *service);
CFDDNS_API int ddns_service_stop(ddns_service_t *service);

/* ========== Manual Operations ========== */

CFDDNS_API int ddns_service_check_now(ddns_service_t *service);
CFDDNS_API int ddns_service_force_update(ddns_service_t *service, const char *ip);
CFDDNS_API int ddns_service_create_record(ddns_service_t *service, const char *ip);
CFDDNS_API int ddns_service_update_record(ddns_service_t *service, const char *ip);
CFDDNS_API int ddns_service_delete_record(ddns_service_t *service);

/* ========== Configuration ========== */

CFDDNS_API int ddns_service_set_interval(ddns_service_t *service, int seconds);
CFDDNS_API int ddns_service_set_event_callback(ddns_service_t *service,
                                                ddns_event_callback_t callback,
                                                void *user_data);
CFDDNS_API int ddns_service_update_config(ddns_service_t *service, const cf_config_t *config);

/* ========== Query ========== */

CFDDNS_API ddns_status_t ddns_service_get_status(ddns_service_t *service);
CFDDNS_API int ddns_service_get_stats(ddns_service_t *service, ddns_stats_t *stats);
CFDDNS_API int ddns_service_get_current_ip(ddns_service_t *service, char *buf, size_t len);
CFDDNS_API int ddns_service_get_record_info(ddns_service_t *service, dns_record_t *record);
CFDDNS_API int ddns_service_query_record_server(ddns_service_t *service, dns_record_t *record);

/* ========== Dependencies ========== */

CFDDNS_API void ddns_service_set_http_client(ddns_service_t *service, http_client_t *client);
CFDDNS_API void ddns_service_set_json_parser(ddns_service_t *service, json_parser_t *parser);
CFDDNS_API void ddns_service_set_logger(ddns_service_t *service, logger_t *logger);
CFDDNS_API void ddns_service_set_ip_provider(ddns_service_t *service, ip_provider_t *provider);

/* ========== Utility Functions ========== */

/**
 * @brief Get DNS record type name
 * @param type Record type
 * @return Type name string
 */
CFDDNS_API const char *dns_record_type_name(dns_record_type_t type);

/**
 * @brief Parse DNS record type from string
 * @param str Type string ("A", "AAAA", etc.)
 * @return Record type
 */
CFDDNS_API dns_record_type_t dns_record_type_parse(const char *str);

/**
 * @brief Get default CloudFlare configuration
 * @return Default configuration
 */
CFDDNS_API cf_config_t cf_config_default(void);

/**
 * @brief Load CloudFlare configuration from config
 * @param cfg Config module
 * @param cf_config CloudFlare configuration output
 * @return 0 on success, error code on failure
 */
CFDDNS_API int cf_config_from_config(config_t *cfg, cf_config_t *cf_config);

/**
 * @brief Validate CloudFlare configuration
 * @param config CloudFlare configuration
 * @return 0 if valid, error code if invalid
 */
CFDDNS_API int cf_config_validate(const cf_config_t *config);

/**
 * @brief Get status name
 * @param status Service status
 * @return Status name string
 */
CFDDNS_API const char *ddns_status_name(ddns_status_t status);

/**
 * @brief Get event name
 * @param event Event type
 * @return Event name string
 */
CFDDNS_API const char *ddns_event_name(ddns_event_type_t event);

/**
 * @brief Get binding mode name
 * @param mode Binding mode
 * @return Binding mode name string
 */
CFDDNS_API const char *record_binding_mode_name(record_binding_mode_t mode);

/**
 * @brief Parse binding mode from string
 * @param str Mode string ("fixed", "dynamic", "auto")
 * @return Binding mode
 */
CFDDNS_API record_binding_mode_t record_binding_mode_parse(const char *str);

/* ========== Multi-Record API ========== */

/**
 * @brief Initialize DDNS service with multi-record configuration
 * @param service DDNS service
 * @param config Global configuration
 * @return 0 on success, error code on failure
 */
CFDDNS_API int ddns_service_init_multi(ddns_service_t *service,
                                        const ddns_global_config_t *config);

/**
 * @brief Add DNS record to service
 * @param service DDNS service
 * @param record Record configuration
 * @return 0 on success, error code on failure
 */
CFDDNS_API int ddns_service_add_record(ddns_service_t *service,
                                        const dns_record_config_t *record);

/**
 * @brief Remove DNS record from service
 * @param service DDNS service
 * @param record_id Record ID to remove
 * @return 0 on success, error code on failure
 */
CFDDNS_API int ddns_service_remove_record(ddns_service_t *service,
                                           const char *record_id);

/**
 * @brief Check and update all DNS records
 * @param service DDNS service
 * @return 0 on success, error code on failure
 */
CFDDNS_API int ddns_service_check_all_records(ddns_service_t *service);

/**
 * @brief Get DNS record configuration by ID
 * @param service DDNS service
 * @param record_id Record ID
 * @return Record configuration, or NULL if not found
 */
CFDDNS_API dns_record_config_t *ddns_service_get_record_config(ddns_service_t *service,
                                                                const char *record_id);

/**
 * @brief Get all DNS record configurations
 * @param service DDNS service
 * @param records Buffer to store configurations
 * @param max_count Maximum number to return
 * @return Number of records returned
 */
CFDDNS_API int ddns_service_get_all_record_configs(ddns_service_t *service,
                                                    dns_record_config_t *records,
                                                    int max_count);

/**
 * @brief Set WAN manager for DDNS service
 * @param service DDNS service
 * @param manager WAN manager
 */
CFDDNS_API void ddns_service_set_wan_manager(ddns_service_t *service,
                                              wan_manager_t *manager);

/**
 * @brief Get default global configuration
 * @return Default global configuration
 */
CFDDNS_API ddns_global_config_t ddns_global_config_default(void);

/**
 * @brief Validate global configuration
 * @param config Global configuration
 * @return 0 if valid, error code if invalid
 */
CFDDNS_API int ddns_global_config_validate(const ddns_global_config_t *config);

/* ========== CLI Show Commands ========== */

/**
 * @brief Show command options
 */
typedef enum {
    DDNS_SHOW_ALL = 0,          /**< Show all configurations and status */
    DDNS_SHOW_CONFIG = 1,       /**< Show global configuration */
    DDNS_SHOW_RECORDS = 2,      /**< Show all DNS records */
    DDNS_SHOW_STATUS = 3,       /**< Show service status summary */
    DDNS_SHOW_RECORD = 4,       /**< Show specific record by ID */
    DDNS_SHOW_STATS = 5,        /**< Show statistics */
    DDNS_SHOW_WAN = 6,          /**< Show WAN bindings */
} ddns_show_mode_t;

/**
 * @brief Execute show command for DDNS service
 * @param service DDNS service
 * @param mode Show mode
 * @param arg Optional argument (record ID for DDNS_SHOW_RECORD)
 */
CFDDNS_API void ddns_service_show(ddns_service_t *service, ddns_show_mode_t mode, const char *arg);

/**
 * @brief Print service status summary (table format)
 * @param service DDNS service
 */
CFDDNS_API void ddns_service_show_status(ddns_service_t *service);

/**
 * @brief Print global configuration
 * @param service DDNS service
 */
CFDDNS_API void ddns_service_show_config(ddns_service_t *service);

/**
 * @brief Print all DNS records (table format)
 * @param service DDNS service
 */
CFDDNS_API void ddns_service_show_records(ddns_service_t *service);

/**
 * @brief Print specific DNS record details
 * @param service DDNS service
 * @param record_id Record ID
 */
CFDDNS_API void ddns_service_show_record(ddns_service_t *service, const char *record_id);

/**
 * @brief Print service statistics
 * @param service DDNS service
 */
CFDDNS_API void ddns_service_show_stats(ddns_service_t *service);

/**
 * @brief Print WAN bindings summary
 * @param service DDNS service
 */
CFDDNS_API void ddns_service_show_wan_bindings(ddns_service_t *service);

/**
 * @brief Format all status as string
 * @param service DDNS service
 * @param buf Buffer to store formatted string
 * @param len Buffer length
 * @return Number of characters written
 */
CFDDNS_API int ddns_service_format_status(ddns_service_t *service, char *buf, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* CFDDNS_SERVICE_DDNS_SERVICE_H */