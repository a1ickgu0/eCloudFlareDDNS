/**
 * @file main.c
 * @brief Main entry point for CloudFlare DDNS client
 */

#include "app/ddns_app.h"
#include "service/ddns_service.h"
#include "hal/platform.h"
#include "hal/logger.h"
#include "common/errors.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ========== Platform Initialization ========== */

static int init_platform(void) {
    /* Register platform implementations */
    extern int platform_register_posix(void);
    extern int http_client_register_libcurl(void);
    extern int json_parser_register_cjson(void);
    extern int config_register_json(void);
    extern int logger_register_console(void);
    extern int logger_register_file(void);

    int result;

    result = platform_register_posix();
    if (result < 0) {
        fprintf(stderr, "Failed to register POSIX platform\n");
        return result;
    }

    result = http_client_register_libcurl();
    if (result < 0) {
        fprintf(stderr, "Failed to register libcurl HTTP client\n");
        return result;
    }

    result = json_parser_register_cjson();
    if (result < 0) {
        fprintf(stderr, "Failed to register cJSON parser\n");
        return result;
    }

    result = config_register_json();
    if (result < 0) {
        fprintf(stderr, "Failed to register JSON config\n");
        return result;
    }

    result = logger_register_console();
    if (result < 0) {
        fprintf(stderr, "Failed to register console logger\n");
        return result;
    }

    result = logger_register_file();
    if (result < 0) {
        fprintf(stderr, "Failed to register file logger\n");
        return result;
    }

    return 0;
}

/* ========== Main Function ========== */

int main(int argc, char *argv[]) {
    int result;

    /* Initialize platform adapters */
    result = init_platform();
    if (result < 0) {
        return 1;
    }

    /* Create application */
    ddns_app_t *app = ddns_app_create();
    if (app == NULL) {
        fprintf(stderr, "Failed to create application\n");
        return 1;
    }

    /* Parse command-line arguments */
    app_config_t config;
    result = app_config_parse_args(&config, argc, argv);
    if (result < 0) {
        ddns_app_destroy(app);
        return 1;
    }

    bool has_direct_once_args =
        config.once &&
        strlen(config.api_token) > 0 &&
        strlen(config.record_name) > 0 &&
        (strlen(config.zone_id) > 0 || strlen(config.zone_name) > 0);

    /* Check for required configuration */
    if (strlen(config.config_path) == 0 && getenv("CFDDNS_API_TOKEN") == NULL && !has_direct_once_args) {
        fprintf(stderr, "Error: No configuration provided.\n");
        fprintf(stderr, "Use -c <config_file> or set CFDDNS_API_TOKEN environment variable.\n\n");
        app_print_usage(argv[0]);
        ddns_app_destroy(app);
        return 1;
    }

    /* Initialize application */
    result = ddns_app_init(app, &config);
    if (result < 0) {
        fprintf(stderr, "Failed to initialize application: %s\n", cfddns_strerror(result));
        ddns_app_destroy(app);
        return 1;
    }

    /* Run application */
    result = ddns_app_run(app);
    if (result < 0) {
        fprintf(stderr, "Application error: %s\n", cfddns_strerror(result));
        ddns_app_destroy(app);
        return 1;
    }

    /* Cleanup */
    ddns_app_destroy(app);

    return 0;
}