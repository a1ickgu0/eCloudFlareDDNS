/**
 * @file ddns_app.h
 * @brief DDNS application interface
 */

#ifndef CFDDNS_APP_DDNS_APP_H
#define CFDDNS_APP_DDNS_APP_H

#include "service/ddns_service.h"
#include "service/wan_manager.h"
#include "hal/config.h"
#include "hal/logger.h"
#include "common/types.h"
#include "common/errors.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ========== Application Configuration ========== */

typedef struct {
    char config_path[256];         /**< Configuration file path */
    char log_path[256];            /**< Log file path (empty for console) */
    log_level_t log_level;         /**< Log level */
    bool daemon_mode;              /**< Run as daemon */
    bool verbose;                  /**< Verbose output */
    bool once;                     /**< Run once and exit */
    bool force_update;             /**< Force update on start */
    char forced_ip[48];            /**< Forced IP address (empty for auto) */

    /* Show command options */
    bool show_mode;                /**< Execute show command instead of running */
    ddns_show_mode_t show_type;    /**< Type of show command */
    char show_arg[128];            /**< Optional argument for show command */
} app_config_t;

/* ========== Application Context ========== */

typedef struct ddns_app {
    void *handle;
    config_t *config;
    logger_t *logger;
    ddns_service_t *ddns_service;
    wan_manager_t *wan_manager;
    ip_provider_t *ip_provider;

    /**
     * @brief Initialize application
     */
    int (*init)(struct ddns_app *self, const app_config_t *config);

    /**
     * @brief Run application
     */
    int (*run)(struct ddns_app *self);

    /**
     * @brief Stop application
     */
    int (*stop)(struct ddns_app *self);

    /**
     * @brief Destroy application
     */
    void (*destroy)(struct ddns_app *self);

    /**
     * @brief Execute show command
     */
    int (*show)(struct ddns_app *self, ddns_show_mode_t mode, const char *arg);

} ddns_app_t;

/* ========== Application Creation ========== */

CFDDNS_API ddns_app_t *ddns_app_create(void);
CFDDNS_API void ddns_app_destroy(ddns_app_t *app);

/* ========== Application Operations ========== */

CFDDNS_API int ddns_app_init(ddns_app_t *app, const app_config_t *config);
CFDDNS_API int ddns_app_run(ddns_app_t *app);
CFDDNS_API int ddns_app_stop(ddns_app_t *app);
CFDDNS_API int ddns_app_show(ddns_app_t *app, ddns_show_mode_t mode, const char *arg);

/* ========== Configuration Helpers ========== */

CFDDNS_API app_config_t app_config_default(void);
CFDDNS_API int app_config_parse_args(app_config_t *config, int argc, char *argv[]);
CFDDNS_API int app_config_from_file(app_config_t *config, const char *path);

/* ========== Utility Functions ========== */

CFDDNS_API void app_print_version(void);
CFDDNS_API void app_print_usage(const char *program_name);
CFDDNS_API int app_setup_signal_handlers(ddns_app_t *app);

#ifdef __cplusplus
}
#endif

#endif /* CFDDNS_APP_DDNS_APP_H */