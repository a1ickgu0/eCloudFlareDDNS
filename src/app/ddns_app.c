/**
 * @file ddns_app.c
 * @brief DDNS application implementation
 */

#include "app/ddns_app.h"
#include "service/ddns_service.h"
#include "service/ip_provider.h"
#include "service/wan_manager.h"
#include "hal/config.h"
#include "hal/logger.h"
#include "hal/http_client.h"
#include "hal/json_parser.h"
#include "hal/platform.h"
#include "common/types.h"
#include "common/errors.h"
#include "common/macros.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <signal.h>
#include <getopt.h>
#include <sys/stat.h>
#include <fcntl.h>

/* ========== Forward declarations ========== */

/* ========== Application Handle ========== */

typedef struct {
    app_config_t config;
    cf_config_t cf_config;

    /* Multi-record configuration */
    ddns_global_config_t ddns_global_config;
    wan_manager_config_t wan_config;

    config_t *cfg;
    logger_t *logger;
    http_client_t *http_client;
    json_parser_t *json_parser;
    ip_provider_t *ip_provider;
    wan_manager_t *wan_manager;
    ddns_service_t *ddns_service;

    volatile sig_atomic_t stop_requested;
} app_handle_t;

/* ========== Version/Usage ========== */

void app_print_version(void) {
    printf("cfddns - CloudFlare DDNS Client\n");
    printf("Version: %s\n", CFDDNS_VERSION_STRING);
}

void app_print_usage(const char *program_name) {
    printf("Usage: %s [OPTIONS]\n\n", program_name);
    printf("Options:\n");
    printf("  -c, --config <file>     Configuration file path\n");
    printf("  -s, --show <type>       Show configuration/status:\n");
    printf("                          all     - Show all configurations and status\n");
    printf("                          config  - Show global configuration\n");
    printf("                          records - Show all DNS records\n");
    printf("                          status  - Show service status summary\n");
    printf("                          wan     - Show WAN bindings\n");
    printf("                          stats   - Show statistics\n");
    printf("  -r, --record <id>       Show specific record details\n");
    printf("  -h, --help              Show this help\n");
    printf("  -V, --version           Show version\n");
}

/* ========== Default Configuration ========== */

app_config_t app_config_default(void) {
    app_config_t config;
    memset(&config, 0, sizeof(config));
    config.log_level = LOG_LEVEL_INFO;
    config.daemon_mode = false;
    config.verbose = false;
    config.once = false;
    config.force_update = false;
    config.show_mode = false;
    config.show_type = DDNS_SHOW_ALL;
    return config;
}

/* ========== Argument Parsing ========== */

static ddns_show_mode_t parse_show_type(const char *type) {
    if (type == NULL) return DDNS_SHOW_ALL;

    if (strcmp(type, "all") == 0) return DDNS_SHOW_ALL;
    if (strcmp(type, "config") == 0) return DDNS_SHOW_CONFIG;
    if (strcmp(type, "records") == 0) return DDNS_SHOW_RECORDS;
    if (strcmp(type, "status") == 0) return DDNS_SHOW_STATUS;
    if (strcmp(type, "wan") == 0) return DDNS_SHOW_WAN;
    if (strcmp(type, "stats") == 0) return DDNS_SHOW_STATS;

    return DDNS_SHOW_ALL;
}

int app_config_parse_args(app_config_t *config, int argc, char *argv[]) {
    if (config == NULL) return CFDDNS_ERR_NULL_POINTER;

    *config = app_config_default();

    static struct option long_options[] = {
        {"config",     required_argument, NULL, 'c'},
        {"show",       required_argument, NULL, 's'},
        {"record",     required_argument, NULL, 'r'},
        {"help",       no_argument,       NULL, 'h'},
        {"version",    no_argument,       NULL, 'V'},
        {NULL,         0,                 NULL, 0}
    };

    int opt;
    while ((opt = getopt_long(argc, argv, "c:s:r:hV", long_options, NULL)) != -1) {
        switch (opt) {
            case 'c':
                CFDDNS_STRNCPY(config->config_path, optarg, sizeof(config->config_path));
                break;
            case 's':
                config->show_mode = true;
                config->show_type = parse_show_type(optarg);
                break;
            case 'r':
                config->show_mode = true;
                config->show_type = DDNS_SHOW_RECORD;
                CFDDNS_STRNCPY(config->show_arg, optarg, sizeof(config->show_arg));
                break;
            case 'h':
                app_print_usage(argv[0]);
                exit(0);
            case 'V':
                app_print_version();
                exit(0);
            default:
                app_print_usage(argv[0]);
                return CFDDNS_ERR_INVALID_ARG;
        }
    }

    return CFDDNS_OK;
}

int app_config_from_file(app_config_t *config, const char *path) {
    if (config == NULL || path == NULL) return CFDDNS_ERR_NULL_POINTER;

    if (!fs_file_exists(path)) {
        return CFDDNS_ERR_FILE_NOT_FOUND;
    }

    CFDDNS_STRNCPY(config->config_path, path, sizeof(config->config_path));
    return CFDDNS_OK;
}

/* ========== Signal Handler ========== */

static app_handle_t *g_app_handle = NULL;

static void signal_handler(int sig) {
    (void)sig;
    if (g_app_handle != NULL) {
        g_app_handle->stop_requested = 1;
    }
}

int app_setup_signal_handlers(ddns_app_t *app) {
    app_handle_t *h = (app_handle_t *)app->handle;
    if (h == NULL) return CFDDNS_ERR_NULL_POINTER;

    g_app_handle = h;

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;

    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGINT, &sa, NULL);

    return CFDDNS_OK;
}

/* ========== Application Implementation ========== */

static int ddns_app_init_impl(ddns_app_t *self, const app_config_t *config) {
    app_handle_t *h = (app_handle_t *)self->handle;
    if (h == NULL) return CFDDNS_ERR_NULL_POINTER;

    h->config = *config;

    /* Initialize logger */
    log_config_t log_cfg = log_config_default();
    log_cfg.level = config->log_level;

    int result = logger_init(&log_cfg);
    if (CFDDNS_FAILED(result)) return result;

    h->logger = logger_get();
    self->logger = h->logger;

    /* Initialize HTTP client */
    h->http_client = http_client_create();
    if (h->http_client == NULL) {
        return CFDDNS_ERR_OUT_OF_MEMORY;
    }

    /* Initialize JSON parser */
    h->json_parser = json_parser_create();
    if (h->json_parser == NULL) {
        return CFDDNS_ERR_OUT_OF_MEMORY;
    }

    /* Initialize IP provider */
    h->ip_provider = ip_provider_create_with_http(h->http_client);
    if (h->ip_provider == NULL) {
        return CFDDNS_ERR_OUT_OF_MEMORY;
    }
    self->ip_provider = h->ip_provider;

    /* Initialize WAN manager */
    h->wan_manager = wan_manager_create();
    if (h->wan_manager == NULL) {
        return CFDDNS_ERR_OUT_OF_MEMORY;
    }
    self->wan_manager = h->wan_manager;
    wan_manager_set_ip_provider(h->wan_manager, h->ip_provider);
    wan_manager_set_http_client(h->wan_manager, h->http_client);

    /* Initialize DDNS service */
    h->ddns_service = ddns_service_create_full(h->http_client, h->json_parser,
                                                h->logger, h->ip_provider);
    if (h->ddns_service == NULL) {
        return CFDDNS_ERR_OUT_OF_MEMORY;
    }
    self->ddns_service = h->ddns_service;
    ddns_service_set_wan_manager(h->ddns_service, h->wan_manager);

    /* Load configuration if provided */
    if (strlen(config->config_path) > 0) {
        h->cfg = config_create();
        if (h->cfg == NULL) {
            return CFDDNS_ERR_OUT_OF_MEMORY;
        }
        self->config = h->cfg;

        result = config_load_file(h->cfg, config->config_path);
        if (CFDDNS_FAILED(result)) {
            return result;
        }

        /* Parse WAN manager configuration */
        result = wan_manager_config_from_json(h->cfg, &h->wan_config);
        if (CFDDNS_FAILED(result)) {
            /* WAN config may be optional, continue anyway */
        } else {
            wan_manager_init(h->wan_manager, &h->wan_config);
        }

        /* Parse DDNS global configuration */
        result = ddns_global_config_from_json(h->cfg, &h->ddns_global_config);
        if (CFDDNS_FAILED(result)) {
            return result;
        }

        /* Initialize DDNS service with multi-record config */
        result = ddns_service_init_multi(h->ddns_service, &h->ddns_global_config);
        if (CFDDNS_FAILED(result)) {
            return result;
        }
    }

    return CFDDNS_OK;
}

static int ddns_app_show_impl(ddns_app_t *self, ddns_show_mode_t mode, const char *arg) {
    app_handle_t *h = (app_handle_t *)self->handle;
    if (h == NULL) return CFDDNS_ERR_NULL_POINTER;

    /* Show WAN status */
    if (mode == DDNS_SHOW_ALL || mode == DDNS_SHOW_WAN) {
        wan_manager_show(h->wan_manager, WAN_SHOW_STATUS, NULL);
        if (mode == DDNS_SHOW_WAN) return CFDDNS_OK;
    }

    /* Show DDNS service status */
    ddns_service_show(h->ddns_service, mode, arg);

    return CFDDNS_OK;
}

static int ddns_app_run_impl(ddns_app_t *self) {
    app_handle_t *h = (app_handle_t *)self->handle;
    if (h == NULL) return CFDDNS_ERR_NULL_POINTER;

    /* Handle show mode */
    if (h->config.show_mode) {
        return ddns_app_show_impl(self, h->config.show_type, h->config.show_arg);
    }

    app_setup_signal_handlers(self);

    /* Start DDNS service if configured */
    if (h->ddns_service != NULL && h->ddns_global_config.record_count > 0) {
        int result = ddns_service_start(h->ddns_service);
        if (CFDDNS_FAILED(result)) {
            return result;
        }

        /* Run once mode: wait for first check cycle and exit */
        if (h->config.once) {
            /* Wait for the worker thread to complete first check */
            timer_sleep_ms(5000);  /* Give time for first check cycle */
            ddns_service_stop(h->ddns_service);
            return CFDDNS_OK;
        }
    }

    /* Wait for stop signal (continuous mode) */
    while (!h->stop_requested) {
        timer_sleep_ms(1000);
    }

    return CFDDNS_OK;
}

static int ddns_app_stop_impl(ddns_app_t *self) {
    app_handle_t *h = (app_handle_t *)self->handle;
    if (h == NULL) return CFDDNS_ERR_NULL_POINTER;

    h->stop_requested = 1;

    if (h->ddns_service != NULL) {
        return ddns_service_stop(h->ddns_service);
    }

    return CFDDNS_OK;
}

static void ddns_app_destroy_impl(ddns_app_t *self) {
    if (self == NULL) return;

    app_handle_t *h = (app_handle_t *)self->handle;
    if (h != NULL) {
        if (h->ddns_service != NULL) {
            ddns_service_stop(h->ddns_service);
            ddns_service_destroy(h->ddns_service);
        }

        if (h->ip_provider != NULL) {
            ip_provider_destroy(h->ip_provider);
        }

        if (h->json_parser != NULL) {
            json_parser_destroy(h->json_parser);
        }

        if (h->http_client != NULL) {
            http_client_destroy(h->http_client);
        }

        logger_cleanup();

        free(h);
        self->handle = NULL;
    }

    free(self);
}

/* ========== Public API ========== */

ddns_app_t *ddns_app_create(void) {
    ddns_app_t *app = malloc(sizeof(ddns_app_t));
    if (app == NULL) return NULL;

    memset(app, 0, sizeof(ddns_app_t));

    app_handle_t *handle = malloc(sizeof(app_handle_t));
    if (handle == NULL) {
        free(app);
        return NULL;
    }

    memset(handle, 0, sizeof(app_handle_t));

    app->handle = handle;

    app->init = ddns_app_init_impl;
    app->run = ddns_app_run_impl;
    app->stop = ddns_app_stop_impl;
    app->destroy = ddns_app_destroy_impl;
    app->show = ddns_app_show_impl;

    return app;
}

void ddns_app_destroy(ddns_app_t *app) {
    if (app != NULL && app->destroy != NULL) {
        app->destroy(app);
    }
}

int ddns_app_init(ddns_app_t *app, const app_config_t *config) {
    if (app != NULL && app->init != NULL) {
        return app->init(app, config);
    }
    return CFDDNS_ERR_NULL_POINTER;
}

int ddns_app_run(ddns_app_t *app) {
    if (app != NULL && app->run != NULL) {
        return app->run(app);
    }
    return CFDDNS_ERR_NULL_POINTER;
}

int ddns_app_stop(ddns_app_t *app) {
    if (app != NULL && app->stop != NULL) {
        return app->stop(app);
    }
    return CFDDNS_ERR_NULL_POINTER;
}

int ddns_app_show(ddns_app_t *app, ddns_show_mode_t mode, const char *arg) {
    if (app != NULL && app->show != NULL) {
        return app->show(app, mode, arg);
    }
    return CFDDNS_ERR_NULL_POINTER;
}