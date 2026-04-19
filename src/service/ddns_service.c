/**
 * @file ddns_service.c
 * @brief DDNS service implementation for CloudFlare
 *        Supports multi-WAN, multi-domain, IPv4/v6 dual-stack
 */

#include "service/ddns_service.h"
#include "service/ip_provider.h"
#include "service/wan_manager.h"
#include "hal/config.h"
#include "hal/platform.h"
#include "hal/http_client.h"
#include "hal/json_parser.h"
#include "hal/logger.h"
#include "common/types.h"
#include "common/errors.h"
#include "common/macros.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <pthread.h>
#include <unistd.h>

/* ========== DNS Record Type Mapping ========== */

static const char *dns_type_names[] = {
    "A", "AAAA", "CNAME", "TXT", "MX"
};

const char *dns_record_type_name(dns_record_type_t type) {
    if (type >= 0 && type < (int)(sizeof(dns_type_names) / sizeof(dns_type_names[0]))) {
        return dns_type_names[type];
    }
    return "UNKNOWN";
}

dns_record_type_t dns_record_type_parse(const char *str) {
    if (str == NULL) return DNS_RECORD_A;

    for (int i = 0; i < (int)(sizeof(dns_type_names) / sizeof(dns_type_names[0])); i++) {
        if (strcasecmp(str, dns_type_names[i]) == 0) {
            return (dns_record_type_t)i;
        }
    }
    return DNS_RECORD_A;
}

/* ========== Status/Event Names ========== */

static const char *status_names[] = {
    "STOPPED", "RUNNING", "UPDATING", "ERROR", "PAUSED"
};

const char *ddns_status_name(ddns_status_t status) {
    if (status >= 0 && status < (int)(sizeof(status_names) / sizeof(status_names[0]))) {
        return status_names[status];
    }
    return "UNKNOWN";
}

static const char *event_names[] = {
    "STARTED", "STOPPED", "IP_CHECK", "IP_CHANGED",
    "UPDATE_START", "UPDATE_SUCCESS", "UPDATE_FAILED",
    "RECORD_CREATED", "RECORD_DELETED", "ERROR",
    "ZONE_FOUND", "RECORD_FOUND"
};

const char *ddns_event_name(ddns_event_type_t event) {
    if (event >= 0 && event < (int)(sizeof(event_names) / sizeof(event_names[0]))) {
        return event_names[event];
    }
    return "UNKNOWN";
}

/* ========== Binding Mode Names ========== */

static const char *binding_mode_names[] = {
    "fixed", "dynamic", "auto"
};

const char *record_binding_mode_name(record_binding_mode_t mode) {
    if (mode >= 0 && mode < (int)(sizeof(binding_mode_names) / sizeof(binding_mode_names[0]))) {
        return binding_mode_names[mode];
    }
    return "unknown";
}

record_binding_mode_t record_binding_mode_parse(const char *str) {
    if (str == NULL) return RECORD_BINDING_FIXED;

    for (int i = 0; i < (int)(sizeof(binding_mode_names) / sizeof(binding_mode_names[0])); i++) {
        if (strcasecmp(str, binding_mode_names[i]) == 0) {
            return (record_binding_mode_t)i;
        }
    }
    return RECORD_BINDING_FIXED;
}

/* ========== Default Configuration ========== */

cf_config_t cf_config_default(void) {
    cf_config_t config;
    memset(&config, 0, sizeof(config));
    config.record_type = DNS_RECORD_A;
    config.ttl = 1;  /* Automatic TTL */
    config.proxied = false;
    config.check_interval = 300;  /* 5 minutes */
    config.update_on_change = true;
    config.create_if_missing = true;
    config.delete_on_exit = false;
    return config;
}

int cf_config_validate(const cf_config_t *config) {
    if (config == NULL) return CFDDNS_ERR_NULL_POINTER;

    /* API token is required */
    if (strlen(config->api_token) == 0) {
        return CFDDNS_ERR_CONFIG_MISSING_KEY;
    }

    /* Zone ID or zone name is required */
    if (strlen(config->zone_id) == 0 && strlen(config->zone_name) == 0) {
        return CFDDNS_ERR_CONFIG_MISSING_KEY;
    }

    /* Record name is required */
    if (strlen(config->record_name) == 0) {
        return CFDDNS_ERR_CONFIG_MISSING_KEY;
    }

    /* Record type must be A or AAAA for DDNS */
    if (config->record_type != DNS_RECORD_A && config->record_type != DNS_RECORD_AAAA) {
        return CFDDNS_ERR_INVALID_ARG;
    }

    return CFDDNS_OK;
}

int cf_config_from_config(config_t *cfg, cf_config_t *cf_config) {
    if (cfg == NULL || cf_config == NULL) return CFDDNS_ERR_NULL_POINTER;

    *cf_config = cf_config_default();

    /* Read API token */
    config_get_string_buf(cfg, "cloudflare.api_token",
                          cf_config->api_token, sizeof(cf_config->api_token), NULL);

    /* Read zone ID */
    config_get_string_buf(cfg, "cloudflare.zone_id",
                          cf_config->zone_id, sizeof(cf_config->zone_id), NULL);

    /* Read zone name */
    config_get_string_buf(cfg, "cloudflare.zone_name",
                          cf_config->zone_name, sizeof(cf_config->zone_name), NULL);

    /* Read record name */
    config_get_string_buf(cfg, "cloudflare.record_name",
                          cf_config->record_name, sizeof(cf_config->record_name), NULL);

    /* Read record type */
    const char *type_str = config_get_string(cfg, "cloudflare.record_type", "A");
    cf_config->record_type = dns_record_type_parse(type_str);

    /* Read TTL */
    cf_config->ttl = (int)config_get_int(cfg, "cloudflare.ttl", 1);

    /* Read proxied */
    cf_config->proxied = config_get_bool(cfg, "cloudflare.proxied", false);

    /* Read check interval */
    cf_config->check_interval = (int)config_get_int(cfg, "cloudflare.check_interval", 300);

    /* Read other options */
    cf_config->update_on_change = config_get_bool(cfg, "cloudflare.update_on_change", true);
    cf_config->create_if_missing = config_get_bool(cfg, "cloudflare.create_if_missing", true);
    cf_config->delete_on_exit = config_get_bool(cfg, "cloudflare.delete_on_exit", false);

    return cf_config_validate(cf_config);
}

/* ========== DDNS Service Handle ========== */

typedef struct {
    cf_config_t config;                    /**< Single-record config (legacy) */
    ddns_global_config_t global_config;    /**< Multi-record config */
    bool use_multi_record;                 /**< Whether using multi-record mode */

    ddns_status_t status;
    ddns_stats_t stats;
    dns_record_t current_record;

    http_client_t *http_client;
    json_parser_t *json_parser;
    logger_t *logger;
    ip_provider_t *ip_provider;
    wan_manager_t *wan_manager;            /**< WAN manager (multi-WAN) */

    ddns_event_callback_t event_callback;
    void *event_user_data;

    pthread_t worker_thread;
    pthread_mutex_t mutex;
    bool mutex_initialized;                 /**< Mutex has been initialized */
    bool running;
    bool stop_requested;

    char last_ip[48];
    char detected_ip[48];
} ddns_service_handle_t;

/* ========== CloudFlare API Helpers ========== */

static int cf_api_request(ddns_service_handle_t *h, const char *method,
                          const char *path, const char *body,
                          char **response, size_t *response_len) {
    if (h == NULL || h->http_client == NULL) return CFDDNS_ERR_NULL_POINTER;

    /* Build full URL */
    char url[512];
    snprintf(url, sizeof(url), "%s%s", CF_API_BASE_URL, path);


    /* Build headers using linked list (not array) */
    http_header_t *headers = NULL;
    char auth_buf[256];

    const char *token = h->global_config.use_shared_token ?
                        h->global_config.api_token :
                        (strlen(h->config.api_token) > 0 ? h->config.api_token : "");


    snprintf(auth_buf, sizeof(auth_buf), "Bearer %s", token);
    headers = http_header_list_add(headers, "Authorization", auth_buf);

    if (body != NULL && strlen(body) > 0) {
        headers = http_header_list_add(headers, "Content-Type", "application/json");
    }

    http_response_t resp;
    memset(&resp, 0, sizeof(resp));

    int result = http_client_request(h->http_client, method, url, body,
                                      headers, &resp);

    /* Free headers list */
    http_header_list_free(headers);

    if (CFDDNS_FAILED(result)) {
        return result;
    }

    /* Copy response body */
    if (resp.body != NULL && resp.body_len > 0) {
        *response = malloc(resp.body_len + 1);
        if (*response == NULL) {
            http_response_free(&resp);
            return CFDDNS_ERR_OUT_OF_MEMORY;
        }
        memcpy(*response, resp.body, resp.body_len);
        (*response)[resp.body_len] = '\0';
        *response_len = resp.body_len;
    }

    /* Save status code before freeing response */
    int status_code = resp.status_code;
    http_response_free(&resp);

    return (status_code >= 200 && status_code < 300) ?
           CFDDNS_OK : CFDDNS_ERR_NETWORK_FAILED;
}

static json_value_t cf_parse_response(ddns_service_handle_t *h, const char *response,
                                        bool *success) {
    if (h == NULL || h->json_parser == NULL || response == NULL) {
        if (success) *success = false;
        return NULL;
    }

    json_value_t root = json_parse(h->json_parser, response);
    if (root == NULL) {
        if (success) *success = false;
        return NULL;
    }

    /* Check success flag */
    json_value_t success_val = json_get_object_item(h->json_parser, root, "success");
    *success = false;
    if (success_val != NULL && json_is_bool(h->json_parser, success_val)) {
        *success = json_get_bool(h->json_parser, success_val);
    }

    /* Return root - caller must call json_free when done */
    return root;
}

/* ========== Zone Lookup API ========== */

static int cf_find_zone(ddns_service_handle_t *h, const char *zone_name,
                        char *zone_id, size_t zone_id_len) {
    if (h == NULL || zone_name == NULL) return CFDDNS_ERR_NULL_POINTER;

    char path[256];
    snprintf(path, sizeof(path), "/zones?name=%s", zone_name);

    char *response = NULL;
    size_t response_len = 0;

    int result = cf_api_request(h, "GET", path, NULL, &response, &response_len);
    if (CFDDNS_FAILED(result)) return result;

    bool success = false;
    json_value_t root = cf_parse_response(h, response, &success);
    free(response);  /* Free response string after parsing */

    if (root == NULL) return CFDDNS_ERR_CONFIG_PARSE_FAILED;
    if (!success) {
        json_free(h->json_parser, root);
        return CFDDNS_ERR_CF_API_ERROR;
    }

    json_value_t result_arr = json_get_object_item(h->json_parser, root, "result");
    if (!json_is_array(h->json_parser, result_arr)) {
        json_free(h->json_parser, root);
        return CFDDNS_ERR_NOT_FOUND;
    }

    int arr_size = json_get_array_size(h->json_parser, result_arr);
    if (arr_size == 0) {
        json_free(h->json_parser, root);
        return CFDDNS_ERR_NOT_FOUND;
    }

    json_value_t zone_obj = json_get_array_item(h->json_parser, result_arr, 0);
    json_value_t id_val = json_get_object_item(h->json_parser, zone_obj, "id");

    if (json_is_string(h->json_parser, id_val)) {
        const char *id_str = json_get_string(h->json_parser, id_val);
        CFDDNS_STRNCPY(zone_id, id_str, zone_id_len);
        json_free(h->json_parser, root);
        return CFDDNS_OK;
    }

    json_free(h->json_parser, root);
    return CFDDNS_ERR_CONFIG_PARSE_FAILED;
}

/* ========== DNS Record Lookup API ========== */

static int cf_find_record(ddns_service_handle_t *h, const char *zone_id,
                          const char *record_name, dns_record_type_t type,
                          dns_record_t *record) {
    if (h == NULL || zone_id == NULL || record_name == NULL) return CFDDNS_ERR_NULL_POINTER;

    char path[512];
    snprintf(path, sizeof(path), "/zones/%s/dns_records?name=%s&type=%s",
             zone_id, record_name, dns_record_type_name(type));

    char *response = NULL;
    size_t response_len = 0;

    int result = cf_api_request(h, "GET", path, NULL, &response, &response_len);
    if (CFDDNS_FAILED(result)) return result;

    bool success = false;
    json_value_t root = cf_parse_response(h, response, &success);
    free(response);  /* Free response string after parsing */

    if (root == NULL) return CFDDNS_ERR_CONFIG_PARSE_FAILED;
    if (!success) {
        json_free(h->json_parser, root);
        return CFDDNS_ERR_CF_API_ERROR;
    }

    json_value_t result_arr = json_get_object_item(h->json_parser, root, "result");
    if (!json_is_array(h->json_parser, result_arr)) {
        json_free(h->json_parser, root);
        return CFDDNS_ERR_NOT_FOUND;
    }

    int arr_size = json_get_array_size(h->json_parser, result_arr);
    if (arr_size == 0) {
        json_free(h->json_parser, root);
        return CFDDNS_ERR_NOT_FOUND;
    }

    json_value_t rec_obj = json_get_array_item(h->json_parser, result_arr, 0);

    /* Parse record fields */
    json_value_t id_val = json_get_object_item(h->json_parser, rec_obj, "id");
    json_value_t name_val = json_get_object_item(h->json_parser, rec_obj, "name");
    json_value_t content_val = json_get_object_item(h->json_parser, rec_obj, "content");
    json_value_t ttl_val = json_get_object_item(h->json_parser, rec_obj, "ttl");
    json_value_t proxied_val = json_get_object_item(h->json_parser, rec_obj, "proxied");
    json_value_t zone_name_val = json_get_object_item(h->json_parser, rec_obj, "zone_name");

    if (json_is_string(h->json_parser, id_val)) {
        CFDDNS_STRNCPY(record->id, json_get_string(h->json_parser, id_val), sizeof(record->id));
    }
    if (json_is_string(h->json_parser, name_val)) {
        CFDDNS_STRNCPY(record->name, json_get_string(h->json_parser, name_val), sizeof(record->name));
    }
    if (json_is_string(h->json_parser, content_val)) {
        CFDDNS_STRNCPY(record->content, json_get_string(h->json_parser, content_val), sizeof(record->content));
    }
    if (json_is_string(h->json_parser, zone_name_val)) {
        CFDDNS_STRNCPY(record->zone_name, json_get_string(h->json_parser, zone_name_val), sizeof(record->zone_name));
    }
    if (json_is_number(h->json_parser, ttl_val)) {
        record->ttl = (int)json_get_int(h->json_parser, ttl_val);
    }
    if (json_is_bool(h->json_parser, proxied_val)) {
        record->proxied = json_get_bool(h->json_parser, proxied_val);
    }

    record->type = type;
    CFDDNS_STRNCPY(record->zone_id, zone_id, sizeof(record->zone_id));

    json_free(h->json_parser, root);
    return CFDDNS_OK;
}

/* ========== DNS Record Create API ========== */

static int cf_create_record(ddns_service_handle_t *h, const char *zone_id,
                            dns_record_config_t *rec, const char *ip) {
    if (h == NULL || zone_id == NULL || rec == NULL || ip == NULL) {
        return CFDDNS_ERR_NULL_POINTER;
    }

    char body[512];
    snprintf(body, sizeof(body),
             "{\"type\":\"%s\",\"name\":\"%s\",\"content\":\"%s\",\"ttl\":%d,\"proxied\":%s}",
             dns_record_type_name(rec->record_type),
             rec->record_name,
             ip,
             rec->ttl,
             rec->proxied ? "true" : "false");

    char path[256];
    snprintf(path, sizeof(path), "/zones/%s/dns_records", zone_id);

    char *response = NULL;
    size_t response_len = 0;

    int result = cf_api_request(h, "POST", path, body, &response, &response_len);
    if (CFDDNS_FAILED(result)) return result;

    bool success = false;
    json_value_t root = cf_parse_response(h, response, &success);
    free(response);  /* Free response string after parsing */

    if (root == NULL) return CFDDNS_ERR_CONFIG_PARSE_FAILED;
    if (!success) {
        json_free(h->json_parser, root);
        return CFDDNS_ERR_CF_API_ERROR;
    }

    /* Parse created record ID */
    json_value_t result_obj = json_get_object_item(h->json_parser, root, "result");
    json_value_t id_val = json_get_object_item(h->json_parser, result_obj, "id");
    if (json_is_string(h->json_parser, id_val)) {
        CFDDNS_STRNCPY(rec->record_id, json_get_string(h->json_parser, id_val), sizeof(rec->record_id));
    }

    json_free(h->json_parser, root);
    return CFDDNS_OK;
}

/* ========== DNS Record Update API ========== */

static int cf_update_record(ddns_service_handle_t *h, const char *zone_id,
                            dns_record_config_t *rec, const char *ip) {
    if (h == NULL || zone_id == NULL || rec == NULL || ip == NULL) {
        return CFDDNS_ERR_NULL_POINTER;
    }

    if (strlen(rec->record_id) == 0) {
        return CFDDNS_ERR_INVALID_ARG;
    }

    char body[512];
    snprintf(body, sizeof(body),
             "{\"type\":\"%s\",\"name\":\"%s\",\"content\":\"%s\",\"ttl\":%d,\"proxied\":%s}",
             dns_record_type_name(rec->record_type),
             rec->record_name,
             ip,
             rec->ttl,
             rec->proxied ? "true" : "false");

    char path[256];
    snprintf(path, sizeof(path), "/zones/%s/dns_records/%s", zone_id, rec->record_id);

    char *response = NULL;
    size_t response_len = 0;

    int result = cf_api_request(h, "PUT", path, body, &response, &response_len);
    if (CFDDNS_FAILED(result)) return result;

    bool success = false;
    json_value_t root = cf_parse_response(h, response, &success);
    free(response);  /* Free response string after parsing */

    if (root != NULL) {
        json_free(h->json_parser, root);
    }

    return success ? CFDDNS_OK : CFDDNS_ERR_CF_API_ERROR;
}

/* ========== DNS Record Delete API ========== */

static int cf_delete_record(ddns_service_handle_t *h, const char *zone_id,
                            const char *record_id) {
    if (h == NULL || zone_id == NULL || record_id == NULL) {
        return CFDDNS_ERR_NULL_POINTER;
    }

    char path[256];
    snprintf(path, sizeof(path), "/zones/%s/dns_records/%s", zone_id, record_id);

    char *response = NULL;
    size_t response_len = 0;

    int result = cf_api_request(h, "DELETE", path, NULL, &response, &response_len);
    if (CFDDNS_FAILED(result)) return result;

    bool success = false;
    json_value_t root = cf_parse_response(h, response, &success);
    free(response);  /* Free response string after parsing */

    if (root != NULL) {
        json_free(h->json_parser, root);
    }

    return success ? CFDDNS_OK : CFDDNS_ERR_CF_API_ERROR;
}

/* ========== Public API - Simple Wrapper ========== */

ddns_service_t *ddns_service_create(void) {
    return ddns_service_create_full(NULL, NULL, NULL, NULL);
}

ddns_service_t *ddns_service_create_full(http_client_t *http_client,
                                          json_parser_t *json_parser,
                                          logger_t *logger,
                                          ip_provider_t *ip_provider) {
    ddns_service_t *service = malloc(sizeof(ddns_service_t));
    if (service == NULL) return NULL;

    memset(service, 0, sizeof(ddns_service_t));

    ddns_service_handle_t *handle = malloc(sizeof(ddns_service_handle_t));
    if (handle == NULL) {
        free(service);
        return NULL;
    }

    memset(handle, 0, sizeof(ddns_service_handle_t));
    handle->status = DDNS_STATUS_STOPPED;
    handle->http_client = http_client;
    handle->json_parser = json_parser;
    handle->logger = logger;
    handle->ip_provider = ip_provider;
    handle->wan_manager = NULL;
    handle->use_multi_record = false;

    service->handle = handle;
    service->http_client = http_client;
    service->json_parser = json_parser;
    service->logger = logger;
    service->ip_provider = ip_provider;
    service->wan_manager = NULL;

    return service;
}

void ddns_service_destroy(ddns_service_t *service) {
    if (service == NULL) return;

    ddns_service_handle_t *h = (ddns_service_handle_t *)service->handle;
    if (h != NULL) {
        /* Stop worker thread if running */
        if (h->running) {
            h->stop_requested = true;
            pthread_join(h->worker_thread, NULL);
            h->running = false;
        }

        /* Destroy mutex only if initialized */
        if (h->mutex_initialized) {
            pthread_mutex_destroy(&h->mutex);
            h->mutex_initialized = false;
        }

        if (h->ip_provider != NULL && service->ip_provider == NULL) {
            ip_provider_destroy(h->ip_provider);
        }

        if (h->json_parser != NULL && service->json_parser == NULL) {
            json_parser_destroy(h->json_parser);
        }

        if (h->http_client != NULL && service->http_client == NULL) {
            http_client_destroy(h->http_client);
        }

        free(h);
    }

    free(service);
}

int ddns_service_init(ddns_service_t *service, const cf_config_t *config) {
    if (service == NULL || config == NULL) return CFDDNS_ERR_NULL_POINTER;

    ddns_service_handle_t *h = (ddns_service_handle_t *)service->handle;
    if (h == NULL) return CFDDNS_ERR_NULL_POINTER;

    int result = cf_config_validate(config);
    if (CFDDNS_FAILED(result)) return result;

    h->config = *config;
    h->status = DDNS_STATUS_STOPPED;

    pthread_mutex_init(&h->mutex, NULL);

    /* Create dependencies if not provided */
    if (h->http_client == NULL) {
        h->http_client = http_client_create();
        if (h->http_client == NULL) return CFDDNS_ERR_OUT_OF_MEMORY;
    }

    if (h->json_parser == NULL) {
        h->json_parser = json_parser_create();
        if (h->json_parser == NULL) return CFDDNS_ERR_OUT_OF_MEMORY;
    }

    if (h->ip_provider == NULL) {
        h->ip_provider = ip_provider_create_with_http(h->http_client);
        if (h->ip_provider == NULL) return CFDDNS_ERR_OUT_OF_MEMORY;
    }

    return CFDDNS_OK;
}

/* ========== Worker Thread ========== */

static void *ddns_worker_thread(void *arg) {
    ddns_service_t *service = (ddns_service_t *)arg;
    if (service == NULL) return NULL;

    ddns_service_handle_t *h = (ddns_service_handle_t *)service->handle;
    if (h == NULL) return NULL;


    h->status = DDNS_STATUS_RUNNING;
    h->running = true;

    /* Log service start */
    if (h->logger != NULL) {
        LOG_INFO(h->logger, "DDNS service started");
    }

    /* Main monitoring loop */
    while (!h->stop_requested) {

        /* Check and update all records */
        int result = ddns_service_check_all_records(service);
        if (CFDDNS_FAILED(result) && h->logger != NULL) {
            LOG_ERROR(h->logger, "DDNS check failed: %s", cfddns_strerror(result));
        }

        /* Wait for next check interval */
        int interval = h->global_config.default_check_interval;
        if (interval <= 0) interval = 300;  /* Default 5 minutes */

        /* Sleep in smaller increments to respond to stop signal quickly */
        int sleep_remaining = interval * 1000;  /* Convert to milliseconds */
        while (sleep_remaining > 0 && !h->stop_requested) {
            int sleep_chunk = (sleep_remaining > 1000) ? 1000 : sleep_remaining;
            timer_sleep_ms(sleep_chunk);
            sleep_remaining -= sleep_chunk;
        }
    }

    h->status = DDNS_STATUS_STOPPED;
    h->running = false;

    if (h->logger != NULL) {
        LOG_INFO(h->logger, "DDNS service stopped");
    }

    return NULL;
}

int ddns_service_start(ddns_service_t *service) {
    if (service == NULL) return CFDDNS_ERR_NULL_POINTER;

    ddns_service_handle_t *h = (ddns_service_handle_t *)service->handle;
    if (h == NULL) return CFDDNS_ERR_NULL_POINTER;

    /* Already running */
    if (h->running) return CFDDNS_OK;

    /* Initialize mutex if not already initialized */
    if (!h->mutex_initialized) {
        if (pthread_mutex_init(&h->mutex, NULL) != 0) {
            return CFDDNS_ERR_MUTEX_CREATE_FAILED;
        }
        h->mutex_initialized = true;
    }

    h->stop_requested = false;
    h->status = DDNS_STATUS_RUNNING;

    /* Create worker thread */
    int result = pthread_create(&h->worker_thread, NULL, ddns_worker_thread, service);
    if (result != 0) {
        h->status = DDNS_STATUS_ERROR;
        return CFDDNS_ERR_THREAD_CREATE_FAILED;
    }

    h->running = true;
    return CFDDNS_OK;
}

int ddns_service_stop(ddns_service_t *service) {
    if (service == NULL) return CFDDNS_ERR_NULL_POINTER;

    ddns_service_handle_t *h = (ddns_service_handle_t *)service->handle;
    if (h == NULL) return CFDDNS_ERR_NULL_POINTER;

    /* Not running */
    if (!h->running) return CFDDNS_OK;

    h->stop_requested = true;

    /* Wait for thread to finish */
    if (h->worker_thread != 0) {
        pthread_join(h->worker_thread, NULL);
        h->worker_thread = 0;
    }

    /* Destroy mutex */
    pthread_mutex_destroy(&h->mutex);

    h->status = DDNS_STATUS_STOPPED;
    h->running = false;

    return CFDDNS_OK;
}

int ddns_service_check_now(ddns_service_t *service) {
    if (service == NULL) return CFDDNS_ERR_NULL_POINTER;

    ddns_service_handle_t *h = (ddns_service_handle_t *)service->handle;
    if (h == NULL) return CFDDNS_ERR_NULL_POINTER;

    h->stats.total_checks++;
    h->stats.last_check_time = timer_get_time_ms();

    /* Legacy single-record mode */
    if (!h->use_multi_record) {
        /* Detect IP */
        char detected_ip[128] = {0};
        int result = CFDDNS_ERR_NETWORK_FAILED;

        if (h->ip_provider != NULL) {
            ip_type_t type = (h->config.record_type == DNS_RECORD_AAAA) ?
                             IP_TYPE_IPV6 : IP_TYPE_IPV4;
            result = ip_provider_get_ip(h->ip_provider, type, detected_ip, sizeof(detected_ip));
        }

        if (CFDDNS_FAILED(result)) {
            h->stats.failed_updates++;
            return result;
        }

        /* Update DNS record */
        result = ddns_service_update_record(service, detected_ip);
        return result;
    }

    /* Multi-record mode: delegate to check_all_records */
    return ddns_service_check_all_records(service);
}

int ddns_service_force_update(ddns_service_t *service, const char *ip) {
    if (service == NULL || ip == NULL) return CFDDNS_ERR_NULL_POINTER;

    ddns_service_handle_t *h = (ddns_service_handle_t *)service->handle;
    if (h == NULL) return CFDDNS_ERR_NULL_POINTER;

    /* Legacy single-record mode */
    if (!h->use_multi_record) {
        return ddns_service_update_record(service, ip);
    }

    /* Multi-record mode: update all records with forced IP */
    int success_count = 0;
    for (int i = 0; i < h->global_config.record_count; i++) {
        dns_record_config_t *rec = &h->global_config.records[i];

        /* Skip if zone_id not cached */
        if (strlen(rec->zone_id) == 0) {
            int result = cf_find_zone(h, rec->zone_name, rec->zone_id, sizeof(rec->zone_id));
            if (CFDDNS_FAILED(result)) continue;
        }

        rec->status = DDNS_STATUS_UPDATING;

        /* Create or update record */
        int result;
        if (strlen(rec->record_id) == 0 && rec->create_if_missing) {
            result = cf_create_record(h, rec->zone_id, rec, ip);
        } else {
            result = cf_update_record(h, rec->zone_id, rec, ip);
        }

        if (CFDDNS_SUCCEEDED(result)) {
            CFDDNS_STRNCPY(rec->dns_ip, ip, sizeof(rec->dns_ip));
            CFDDNS_STRNCPY(rec->current_ip, ip, sizeof(rec->current_ip));
            rec->status = DDNS_STATUS_RUNNING;
            h->stats.successful_updates++;
            success_count++;
        } else {
            rec->status = DDNS_STATUS_ERROR;
            rec->failed_attempts++;
            h->stats.failed_updates++;
        }
    }

    h->stats.total_updates++;
    h->stats.last_update_time = timer_get_time_ms();

    return (success_count > 0) ? CFDDNS_OK : CFDDNS_ERR_NETWORK_FAILED;
}

int ddns_service_create_record(ddns_service_t *service, const char *ip) {
    if (service == NULL || ip == NULL) return CFDDNS_ERR_NULL_POINTER;

    ddns_service_handle_t *h = (ddns_service_handle_t *)service->handle;
    if (h == NULL) return CFDDNS_ERR_NULL_POINTER;

    /* Legacy single-record mode */
    if (!h->use_multi_record) {
        char zone_id[64] = {0};

        /* Find zone */
        if (strlen(h->config.zone_id) > 0) {
            CFDDNS_STRNCPY(zone_id, h->config.zone_id, sizeof(zone_id));
        } else {
            int result = cf_find_zone(h, h->config.zone_name, zone_id, sizeof(zone_id));
            if (CFDDNS_FAILED(result)) return result;
        }

        /* Build temporary record config */
        dns_record_config_t temp_rec;
        memset(&temp_rec, 0, sizeof(temp_rec));
        CFDDNS_STRNCPY(temp_rec.record_name, h->config.record_name, sizeof(temp_rec.record_name));
        temp_rec.record_type = h->config.record_type;
        temp_rec.ttl = h->config.ttl;
        temp_rec.proxied = h->config.proxied;

        return cf_create_record(h, zone_id, &temp_rec, ip);
    }

    return CFDDNS_ERR_INVALID_ARG;
}

int ddns_service_update_record(ddns_service_t *service, const char *ip) {
    if (service == NULL || ip == NULL) return CFDDNS_ERR_NULL_POINTER;

    ddns_service_handle_t *h = (ddns_service_handle_t *)service->handle;
    if (h == NULL) return CFDDNS_ERR_NULL_POINTER;

    /* Legacy single-record mode */
    if (!h->use_multi_record) {
        char zone_id[64] = {0};
        dns_record_t record;

        /* Find zone */
        if (strlen(h->config.zone_id) > 0) {
            CFDDNS_STRNCPY(zone_id, h->config.zone_id, sizeof(zone_id));
        } else {
            int result = cf_find_zone(h, h->config.zone_name, zone_id, sizeof(zone_id));
            if (CFDDNS_FAILED(result)) return result;
        }

        /* Find existing record */
        int result = cf_find_record(h, zone_id, h->config.record_name, h->config.record_type, &record);
        if (CFDDNS_FAILED(result)) {
            /* Record not found, create if configured */
            if (h->config.create_if_missing) {
                dns_record_config_t temp_rec;
                memset(&temp_rec, 0, sizeof(temp_rec));
                CFDDNS_STRNCPY(temp_rec.record_name, h->config.record_name, sizeof(temp_rec.record_name));
                temp_rec.record_type = h->config.record_type;
                temp_rec.ttl = h->config.ttl;
                temp_rec.proxied = h->config.proxied;

                result = cf_create_record(h, zone_id, &temp_rec, ip);
                if (CFDDNS_FAILED(result)) return result;

                CFDDNS_STRNCPY(h->current_record.id, temp_rec.record_id, sizeof(h->current_record.id));
            } else {
                return result;
            }
        } else {
            /* Record exists, check if IP changed */
            if (strcmp(record.content, ip) != 0) {
                /* Build temporary record config for update */
                dns_record_config_t temp_rec;
                memset(&temp_rec, 0, sizeof(temp_rec));
                CFDDNS_STRNCPY(temp_rec.record_id, record.id, sizeof(temp_rec.record_id));
                CFDDNS_STRNCPY(temp_rec.record_name, h->config.record_name, sizeof(temp_rec.record_name));
                temp_rec.record_type = h->config.record_type;
                temp_rec.ttl = h->config.ttl;
                temp_rec.proxied = h->config.proxied;

                result = cf_update_record(h, zone_id, &temp_rec, ip);
                if (CFDDNS_FAILED(result)) return result;
            }

            h->current_record = record;
        }

        CFDDNS_STRNCPY(h->current_record.content, ip, sizeof(h->current_record.content));
        CFDDNS_STRNCPY(h->stats.current_ip, ip, sizeof(h->stats.current_ip));
        CFDDNS_STRNCPY(h->stats.dns_ip, ip, sizeof(h->stats.dns_ip));
        h->stats.total_updates++;
        h->stats.successful_updates++;
        h->stats.last_update_time = timer_get_time_ms();

        return CFDDNS_OK;
    }

    return CFDDNS_ERR_INVALID_ARG;
}

int ddns_service_delete_record(ddns_service_t *service) {
    if (service == NULL) return CFDDNS_ERR_NULL_POINTER;

    ddns_service_handle_t *h = (ddns_service_handle_t *)service->handle;
    if (h == NULL) return CFDDNS_ERR_NULL_POINTER;

    /* Legacy single-record mode */
    if (!h->use_multi_record) {
        if (strlen(h->current_record.id) == 0) {
            return CFDDNS_ERR_NOT_FOUND;
        }

        return cf_delete_record(h, h->current_record.zone_id, h->current_record.id);
    }

    /* Multi-record mode - delete all records marked for deletion */
    int success_count = 0;
    for (int i = 0; i < h->global_config.record_count; i++) {
        dns_record_config_t *rec = &h->global_config.records[i];

        if (rec->delete_on_exit && strlen(rec->record_id) > 0 && strlen(rec->zone_id) > 0) {
            int result = cf_delete_record(h, rec->zone_id, rec->record_id);
            if (CFDDNS_SUCCEEDED(result)) {
                memset(rec->record_id, 0, sizeof(rec->record_id));
                rec->status = DDNS_STATUS_STOPPED;
                success_count++;
            }
        }
    }

    return (success_count > 0) ? CFDDNS_OK : CFDDNS_ERR_NOT_FOUND;
}

int ddns_service_set_interval(ddns_service_t *service, int seconds) {
    (void)service;
    (void)seconds;
    return CFDDNS_OK;
}

int ddns_service_set_event_callback(ddns_service_t *service,
                                     ddns_event_callback_t callback,
                                     void *user_data) {
    if (service == NULL) return CFDDNS_ERR_NULL_POINTER;

    ddns_service_handle_t *h = (ddns_service_handle_t *)service->handle;
    if (h == NULL) return CFDDNS_ERR_NULL_POINTER;

    h->event_callback = callback;
    h->event_user_data = user_data;

    return CFDDNS_OK;
}

int ddns_service_update_config(ddns_service_t *service, const cf_config_t *config) {
    if (service == NULL || config == NULL) return CFDDNS_ERR_NULL_POINTER;

    ddns_service_handle_t *h = (ddns_service_handle_t *)service->handle;
    if (h == NULL) return CFDDNS_ERR_NULL_POINTER;

    h->config = *config;

    return CFDDNS_OK;
}

ddns_status_t ddns_service_get_status(ddns_service_t *service) {
    if (service == NULL) return DDNS_STATUS_STOPPED;

    ddns_service_handle_t *h = (ddns_service_handle_t *)service->handle;
    if (h == NULL) return DDNS_STATUS_STOPPED;

    return h->status;
}

int ddns_service_get_stats(ddns_service_t *service, ddns_stats_t *stats) {
    if (service == NULL || stats == NULL) return CFDDNS_ERR_NULL_POINTER;

    ddns_service_handle_t *h = (ddns_service_handle_t *)service->handle;
    if (h == NULL) return CFDDNS_ERR_NULL_POINTER;

    *stats = h->stats;
    stats->status = h->status;

    return CFDDNS_OK;
}

int ddns_service_get_current_ip(ddns_service_t *service, char *buf, size_t len) {
    if (service == NULL || buf == NULL) return CFDDNS_ERR_NULL_POINTER;

    ddns_service_handle_t *h = (ddns_service_handle_t *)service->handle;
    if (h == NULL) return CFDDNS_ERR_NULL_POINTER;

    CFDDNS_STRNCPY(buf, h->stats.current_ip, len);

    return (strlen(buf) > 0) ? CFDDNS_OK : CFDDNS_ERR_NOT_FOUND;
}

int ddns_service_get_record_info(ddns_service_t *service, dns_record_t *record) {
    if (service == NULL || record == NULL) return CFDDNS_ERR_NULL_POINTER;

    ddns_service_handle_t *h = (ddns_service_handle_t *)service->handle;
    if (h == NULL) return CFDDNS_ERR_NULL_POINTER;

    *record = h->current_record;

    return (strlen(record->id) > 0) ? CFDDNS_OK : CFDDNS_ERR_NOT_FOUND;
}

void ddns_service_set_http_client(ddns_service_t *service, http_client_t *client) {
    if (service == NULL) return;

    ddns_service_handle_t *h = (ddns_service_handle_t *)service->handle;
    if (h != NULL) {
        h->http_client = client;
        service->http_client = client;
    }
}

void ddns_service_set_json_parser(ddns_service_t *service, json_parser_t *parser) {
    if (service == NULL) return;

    ddns_service_handle_t *h = (ddns_service_handle_t *)service->handle;
    if (h != NULL) {
        h->json_parser = parser;
        service->json_parser = parser;
    }
}

void ddns_service_set_logger(ddns_service_t *service, logger_t *logger) {
    if (service == NULL) return;

    ddns_service_handle_t *h = (ddns_service_handle_t *)service->handle;
    if (h != NULL) {
        h->logger = logger;
        service->logger = logger;
    }
}

void ddns_service_set_ip_provider(ddns_service_t *service, ip_provider_t *provider) {
    if (service == NULL) return;

    ddns_service_handle_t *h = (ddns_service_handle_t *)service->handle;
    if (h != NULL) {
        h->ip_provider = provider;
        service->ip_provider = provider;
    }
}

void ddns_service_set_wan_manager(ddns_service_t *service, wan_manager_t *manager) {
    if (service == NULL) return;

    ddns_service_handle_t *h = (ddns_service_handle_t *)service->handle;
    if (h != NULL) {
        h->wan_manager = manager;
        service->wan_manager = manager;
    }
}

/* ========== Global Configuration ========== */

ddns_global_config_t ddns_global_config_default(void) {
    ddns_global_config_t config;
    memset(&config, 0, sizeof(config));
    config.use_shared_token = true;
    config.default_check_interval = 300;
    config.max_concurrent_updates = 5;
    config.parallel_updates = true;
    return config;
}

int ddns_global_config_validate(const ddns_global_config_t *config) {
    if (config == NULL) return CFDDNS_ERR_NULL_POINTER;

    /* If using shared token, it must be set */
    if (config->use_shared_token && strlen(config->api_token) == 0) {
        return CFDDNS_ERR_CONFIG_MISSING_KEY;
    }

    /* At least one record must be configured */
    if (config->record_count == 0) {
        return CFDDNS_ERR_CONFIG_MISSING_KEY;
    }

    /* Validate each record */
    for (int i = 0; i < config->record_count; i++) {
        const dns_record_config_t *rec = &config->records[i];

        /* If not using shared token, record must have its own API token */
        if (!config->use_shared_token && strlen(rec->api_token) == 0) {
            return CFDDNS_ERR_CONFIG_MISSING_KEY;
        }

        /* Zone name and record name are required */
        if (strlen(rec->zone_name) == 0) {
            return CFDDNS_ERR_CONFIG_MISSING_KEY;
        }
        if (strlen(rec->record_name) == 0) {
            return CFDDNS_ERR_CONFIG_MISSING_KEY;
        }

        /* Record type must be A or AAAA */
        if (rec->record_type != DNS_RECORD_A && rec->record_type != DNS_RECORD_AAAA) {
            return CFDDNS_ERR_INVALID_ARG;
        }
    }

    return CFDDNS_OK;
}

/* ========== Multi-Record API ========== */

int ddns_service_init_multi(ddns_service_t *service, const ddns_global_config_t *config) {
    if (service == NULL || config == NULL) return CFDDNS_ERR_NULL_POINTER;

    ddns_service_handle_t *h = (ddns_service_handle_t *)service->handle;
    if (h == NULL) return CFDDNS_ERR_NULL_POINTER;

    int result = ddns_global_config_validate(config);
    if (CFDDNS_FAILED(result)) return result;

    h->global_config = *config;
    h->use_multi_record = true;
    h->status = DDNS_STATUS_STOPPED;

    /* Create dependencies if not provided */
    if (h->http_client == NULL) {
        h->http_client = http_client_create();
        if (h->http_client == NULL) return CFDDNS_ERR_OUT_OF_MEMORY;
    }

    if (h->json_parser == NULL) {
        h->json_parser = json_parser_create();
        if (h->json_parser == NULL) return CFDDNS_ERR_OUT_OF_MEMORY;
    }

    if (h->ip_provider == NULL) {
        h->ip_provider = ip_provider_create_with_http(h->http_client);
        if (h->ip_provider == NULL) return CFDDNS_ERR_OUT_OF_MEMORY;
    }

    /* Set shared API token */
    if (config->use_shared_token) {
        http_client_set_auth_token(h->http_client, config->api_token);
    }

    return CFDDNS_OK;
}

int ddns_service_add_record(ddns_service_t *service, const dns_record_config_t *record) {
    if (service == NULL || record == NULL) return CFDDNS_ERR_NULL_POINTER;

    ddns_service_handle_t *h = (ddns_service_handle_t *)service->handle;
    if (h == NULL) return CFDDNS_ERR_NULL_POINTER;

    if (h->global_config.record_count >= DDNS_MAX_RECORDS) {
        return CFDDNS_ERR_OUT_OF_MEMORY;
    }

    h->global_config.records[h->global_config.record_count++] = *record;
    return CFDDNS_OK;
}

int ddns_service_remove_record(ddns_service_t *service, const char *record_id) {
    if (service == NULL || record_id == NULL) return CFDDNS_ERR_NULL_POINTER;

    ddns_service_handle_t *h = (ddns_service_handle_t *)service->handle;
    if (h == NULL) return CFDDNS_ERR_NULL_POINTER;

    for (int i = 0; i < h->global_config.record_count; i++) {
        if (strcmp(h->global_config.records[i].id, record_id) == 0) {
            /* Shift remaining records */
            for (int j = i; j < h->global_config.record_count - 1; j++) {
                h->global_config.records[j] = h->global_config.records[j + 1];
            }
            h->global_config.record_count--;
            return CFDDNS_OK;
        }
    }

    return CFDDNS_ERR_NOT_FOUND;
}

int ddns_service_check_all_records(ddns_service_t *service) {
    if (service == NULL) return CFDDNS_ERR_NULL_POINTER;

    ddns_service_handle_t *h = (ddns_service_handle_t *)service->handle;
    if (h == NULL) return CFDDNS_ERR_NULL_POINTER;

    h->stats.total_checks++;
    h->stats.last_check_time = timer_get_time_ms();

    if (!h->use_multi_record) {
        /* Legacy single-record mode */
        return ddns_service_check_now(service);
    }

    /* Check each record */
    int success_count = 0;
    int update_count = 0;


    for (int i = 0; i < h->global_config.record_count; i++) {
        dns_record_config_t *rec = &h->global_config.records[i];


        /* Get IP for this record based on binding mode */
        char detected_ip[128] = {0};
        int result = CFDDNS_ERR_NETWORK_FAILED;

        if (strlen(rec->forced_ip) > 0) {
            /* Use forced IP */
            CFDDNS_STRNCPY(detected_ip, rec->forced_ip, sizeof(detected_ip));
            result = CFDDNS_OK;
        } else if (h->wan_manager != NULL && rec->binding_mode == RECORD_BINDING_AUTO) {
            /* Auto mode: use default/active WAN */
            ip_type_t type = (rec->record_type == DNS_RECORD_AAAA) ? IP_TYPE_IPV6 : IP_TYPE_IPV4;
            char default_wan[32] = {0};
            int wan_result = wan_manager_get_active_wan(h->wan_manager, default_wan, sizeof(default_wan));
            if (CFDDNS_SUCCEEDED(wan_result) && strlen(default_wan) > 0) {
                result = wan_manager_detect_ip(h->wan_manager, default_wan,
                                                type, detected_ip, sizeof(detected_ip));
                if (h->logger != NULL) {
                    LOG_INFO(h->logger, "WAN IP detection result for %s: %d, IP: %s",
                             default_wan, result, detected_ip);
                }
            }
            if (CFDDNS_FAILED(result) && h->ip_provider != NULL) {
                /* Fallback to default IP provider */
                result = ip_provider_get_ip(h->ip_provider, type, detected_ip, sizeof(detected_ip));
                if (h->logger != NULL) {
                    LOG_INFO(h->logger, "IP provider fallback result: %d, IP: %s",
                             result, detected_ip);
                }
            }
        } else if (h->wan_manager != NULL && strlen(rec->wan_interface) > 0) {
            /* Fixed mode: Get IP from specific WAN */
            ip_type_t type = (rec->record_type == DNS_RECORD_AAAA) ? IP_TYPE_IPV6 : IP_TYPE_IPV4;
            result = wan_manager_detect_ip(h->wan_manager, rec->wan_interface,
                                            type, detected_ip, sizeof(detected_ip));
        } else if (h->ip_provider != NULL) {
            /* Use default IP provider */
            ip_type_t type = (rec->record_type == DNS_RECORD_AAAA) ? IP_TYPE_IPV6 : IP_TYPE_IPV4;
            result = ip_provider_get_ip(h->ip_provider, type, detected_ip, sizeof(detected_ip));
        }

        if (CFDDNS_FAILED(result)) {
            rec->failed_attempts++;
            rec->status = DDNS_STATUS_ERROR;
            continue;
        }

        /* IP detection successful */
        CFDDNS_STRNCPY(rec->current_ip, detected_ip, sizeof(rec->current_ip));
        success_count++;

        if (h->logger != NULL) {
            LOG_INFO(h->logger, "IP detected: %s for record %s", detected_ip, rec->record_name);
        }

        /* Find zone if not cached */
        if (strlen(rec->zone_id) == 0) {
            result = cf_find_zone(h, rec->zone_name, rec->zone_id, sizeof(rec->zone_id));
            if (h->logger != NULL) {
                LOG_INFO(h->logger, "Zone lookup for %s: result=%d, zone_id=%s",
                         rec->zone_name, result, rec->zone_id);
            }
            if (CFDDNS_FAILED(result)) {
                rec->failed_attempts++;
                rec->status = DDNS_STATUS_ERROR;
                continue;
            }
        }

        /* Check current DNS record */
        dns_record_t dns_rec;
        result = cf_find_record(h, rec->zone_id, rec->record_name, rec->record_type, &dns_rec);

        if (h->logger != NULL) {
            LOG_INFO(h->logger, "Record lookup: result=%d", result);
        }

        if (CFDDNS_SUCCEEDED(result)) {
            /* Record exists, cache the ID and DNS IP */
            CFDDNS_STRNCPY(rec->record_id, dns_rec.id, sizeof(rec->record_id));
            CFDDNS_STRNCPY(rec->dns_ip, dns_rec.content, sizeof(rec->dns_ip));

            /* Check if IP changed */
            if (rec->update_on_change && strcmp(rec->current_ip, rec->dns_ip) != 0) {
                /* IP changed, update DNS */
                rec->status = DDNS_STATUS_UPDATING;

                result = cf_update_record(h, rec->zone_id, rec, rec->current_ip);

                if (CFDDNS_SUCCEEDED(result)) {
                    CFDDNS_STRNCPY(rec->dns_ip, rec->current_ip, sizeof(rec->dns_ip));
                    rec->status = DDNS_STATUS_RUNNING;
                    rec->failed_attempts = 0;
                    h->stats.successful_updates++;
                    update_count++;
                } else {
                    rec->failed_attempts++;
                    rec->status = DDNS_STATUS_ERROR;
                    h->stats.failed_updates++;
                }
            } else {
                rec->status = DDNS_STATUS_RUNNING;
            }
        } else if (rec->create_if_missing) {
            /* Record not found, create it */
            rec->status = DDNS_STATUS_UPDATING;

            result = cf_create_record(h, rec->zone_id, rec, rec->current_ip);

            if (CFDDNS_SUCCEEDED(result)) {
                CFDDNS_STRNCPY(rec->dns_ip, rec->current_ip, sizeof(rec->dns_ip));
                rec->status = DDNS_STATUS_RUNNING;
                rec->failed_attempts = 0;
                h->stats.successful_updates++;
                update_count++;
            } else {
                rec->failed_attempts++;
                rec->status = DDNS_STATUS_ERROR;
                h->stats.failed_updates++;
            }
        } else {
            /* Record not found and create_if_missing is false */
            rec->status = DDNS_STATUS_ERROR;
        }
    }

    if (update_count > 0) {
        h->stats.total_updates++;
        h->stats.last_update_time = timer_get_time_ms();
    }

    return (success_count > 0) ? CFDDNS_OK : CFDDNS_ERR_NETWORK_FAILED;
}

dns_record_config_t *ddns_service_get_record_config(ddns_service_t *service, const char *record_id) {
    if (service == NULL || record_id == NULL) return NULL;

    ddns_service_handle_t *h = (ddns_service_handle_t *)service->handle;
    if (h == NULL) return NULL;

    for (int i = 0; i < h->global_config.record_count; i++) {
        if (strcmp(h->global_config.records[i].id, record_id) == 0) {
            return &h->global_config.records[i];
        }
    }

    return NULL;
}

int ddns_service_get_all_record_configs(ddns_service_t *service,
                                          dns_record_config_t *records,
                                          int max_count) {
    if (service == NULL || records == NULL) return 0;

    ddns_service_handle_t *h = (ddns_service_handle_t *)service->handle;
    if (h == NULL) return 0;

    int count = (h->global_config.record_count < max_count) ?
                h->global_config.record_count : max_count;
    memcpy(records, h->global_config.records, count * sizeof(dns_record_config_t));

    return count;
}

/* ========== CLI Show Commands ========== */

void ddns_service_show_status(ddns_service_t *service) {
    ddns_service_handle_t *h = (ddns_service_handle_t *)service->handle;
    if (h == NULL) {
        printf("DDNS Service: Not initialized\n");
        return;
    }

    printf("\n");
    printf("DDNS Service Status\n");
    printf("===================\n");
    printf("  Service Status:    %s\n", ddns_status_name(h->status));
    printf("  Mode:              %s\n", h->use_multi_record ? "Multi-Record" : "Single-Record");
    printf("  Total Records:     %d\n", h->global_config.record_count);
    printf("  WAN Manager:       %s\n", h->wan_manager ? "Active" : "Not configured");
    printf("\n");

    /* Summary table */
    int online = 0, updating = 0, error = 0;
    for (int i = 0; i < h->global_config.record_count; i++) {
        switch (h->global_config.records[i].status) {
            case DDNS_STATUS_RUNNING: online++; break;
            case DDNS_STATUS_UPDATING: updating++; break;
            case DDNS_STATUS_ERROR: error++; break;
            default: break;
        }
    }

    printf("  Record Summary:\n");
    printf("    Running:   %d\n", online);
    printf("    Updating:  %d\n", updating);
    printf("    Error:     %d\n", error);
    printf("    Stopped:   %d\n", h->global_config.record_count - online - updating - error);
    printf("\n");
}

void ddns_service_show_config(ddns_service_t *service) {
    ddns_service_handle_t *h = (ddns_service_handle_t *)service->handle;
    if (h == NULL) {
        printf("DDNS Service: Not initialized\n");
        return;
    }

    printf("\n");
    printf("DDNS Global Configuration\n");
    printf("=========================\n");

    /* API Token (masked) */
    printf("  API Token:         %s\n",
           h->global_config.use_shared_token ? "Shared (configured)" : "Per-record");

    if (h->global_config.use_shared_token && h->global_config.api_token[0]) {
        char masked[20];
        int len = strlen(h->global_config.api_token);
        if (len > 10) {
            snprintf(masked, sizeof(masked), "%.*s...%s", 4, h->global_config.api_token,
                     h->global_config.api_token + len - 4);
            printf("    Token (masked):  %s\n", masked);
        }
    }

    printf("\n");
    printf("  Global Settings:\n");
    printf("    Check Interval:    %d seconds\n", h->global_config.default_check_interval);
    printf("    Max Concurrent:    %d updates\n", h->global_config.max_concurrent_updates);
    printf("    Parallel Updates:  %s\n", h->global_config.parallel_updates ? "Yes" : "No");
    printf("\n");

    /* WAN binding summary */
    printf("  WAN Bindings:\n");
    if (h->wan_manager != NULL) {
        char active_wan[32];
        if (wan_manager_get_active_wan(h->wan_manager, active_wan, sizeof(active_wan)) == 0) {
            printf("    Active WAN:       %s\n", active_wan);
        }
        wan_manager_show(h->wan_manager, WAN_SHOW_IP, NULL);
    } else {
        printf("    WAN Manager:      Not configured\n");
    }
    printf("\n");
}

void ddns_service_show_records(ddns_service_t *service) {
    ddns_service_handle_t *h = (ddns_service_handle_t *)service->handle;
    if (h == NULL) {
        printf("DDNS Service: Not initialized\n");
        return;
    }

    printf("\n");
    printf("DNS Records Status\n");
    printf("==================\n");

    /* Table header */
    printf("%-16s %-30s %-6s %-8s %-12s %-15s %-15s\n",
           "ID", "Record Name", "Type", "Status", "WAN Bind", "Current IP", "DNS IP");
    printf("%-16s %-30s %-6s %-8s %-12s %-15s %-15s\n",
           "--", "-----------", "----", "------", "--------", "----------", "-------");

    /* Records */
    for (int i = 0; i < h->global_config.record_count; i++) {
        dns_record_config_t *rec = &h->global_config.records[i];
        printf("%-16s %-30s %-6s %-8s %-12s %-15s %-15s\n",
               rec->id,
               rec->record_name,
               dns_record_type_name(rec->record_type),
               ddns_status_name(rec->status),
               rec->wan_interface[0] ? rec->wan_interface : (rec->binding_mode == RECORD_BINDING_DYNAMIC ? "Dynamic" : "Auto"),
               rec->current_ip[0] ? rec->current_ip : "--",
               rec->dns_ip[0] ? rec->dns_ip : "--");
    }

    printf("\n");
    printf("Total: %d records\n", h->global_config.record_count);
    printf("\n");
}

void ddns_service_show_record(ddns_service_t *service, const char *record_id) {
    ddns_service_handle_t *h = (ddns_service_handle_t *)service->handle;
    if (h == NULL) {
        printf("DDNS Service: Not initialized\n");
        return;
    }

    dns_record_config_t *rec = ddns_service_get_record_config(service, record_id);
    if (rec == NULL) {
        printf("Record '%s' not found\n", record_id);
        return;
    }

    printf("\n");
    printf("DNS Record: %s\n", rec->id);
    printf("===========%.*s\n", (int)strlen(rec->id), "=");
    printf("\n");
    printf("  General:\n");
    printf("    Name:           %s\n", rec->name[0] ? rec->name : rec->record_name);
    printf("    Zone:           %s\n", rec->zone_name);
    printf("    Record Name:    %s\n", rec->record_name);
    printf("    Type:           %s\n", dns_record_type_name(rec->record_type));
    printf("    TTL:            %d seconds\n", rec->ttl);
    printf("    Proxied:        %s\n", rec->proxied ? "Yes" : "No");
    printf("\n");
    printf("  Status:\n");
    printf("    Service Status: %s\n", ddns_status_name(rec->status));
    printf("    Current IP:     %s\n", rec->current_ip[0] ? rec->current_ip : "Not detected");
    printf("    DNS IP:         %s\n", rec->dns_ip[0] ? rec->dns_ip : "Not set");
    printf("    Failed Attempts:%d\n", rec->failed_attempts);
    printf("\n");
    printf("  WAN Binding:\n");
    printf("    Mode:           %s\n", record_binding_mode_name(rec->binding_mode));
    printf("    Interface:      %s\n", rec->wan_interface[0] ? rec->wan_interface : "None");
    printf("    Priority:       %d\n", rec->wan_priority);
    printf("    Forced IP:      %s\n", rec->forced_ip[0] ? rec->forced_ip : "None");
    printf("\n");
    printf("  Update Settings:\n");
    printf("    Check Interval: %d seconds\n", rec->check_interval);
    printf("    Update on Change: %s\n", rec->update_on_change ? "Yes" : "No");
    printf("    Create Missing: %s\n", rec->create_if_missing ? "Yes" : "No");
    printf("    Delete on Exit: %s\n", rec->delete_on_exit ? "Yes" : "No");
    printf("\n");

    /* API Token info */
    if (!h->global_config.use_shared_token) {
        printf("  Authentication:\n");
        if (rec->api_token[0]) {
            char masked[20];
            int len = strlen(rec->api_token);
            if (len > 10) {
                snprintf(masked, sizeof(masked), "%.*s...%s", 4, rec->api_token,
                         rec->api_token + len - 4);
                printf("    API Token:      %s (masked)\n", masked);
            } else {
                printf("    API Token:      Configured\n");
            }
        }
        printf("\n");
    }
}

void ddns_service_show_stats(ddns_service_t *service) {
    ddns_service_handle_t *h = (ddns_service_handle_t *)service->handle;
    if (h == NULL) {
        printf("DDNS Service: Not initialized\n");
        return;
    }

    printf("\n");
    printf("DDNS Service Statistics\n");
    printf("=======================\n");
    printf("\n");
    printf("  Service:\n");
    printf("    Status:         %s\n", ddns_status_name(h->stats.status));
    printf("    Start Time:     %llu\n", (unsigned long long)h->stats.start_time);
    printf("    Last Check:     %llu\n", (unsigned long long)h->stats.last_check_time);
    printf("    Last Update:    %llu\n", (unsigned long long)h->stats.last_update_time);
    printf("\n");
    printf("  Counters:\n");
    printf("    Total Checks:   %d\n", h->stats.total_checks);
    printf("    Total Updates:  %d\n", h->stats.total_updates);
    printf("    Successful:     %d\n", h->stats.successful_updates);
    printf("    Failed:         %d\n", h->stats.failed_updates);
    printf("\n");
    printf("  Current IPs:\n");
    printf("    Detected IP:    %s\n", h->stats.current_ip[0] ? h->stats.current_ip : "None");
    printf("    DNS Record IP:  %s\n", h->stats.dns_ip[0] ? h->stats.dns_ip : "None");
    printf("\n");

    /* Record-specific stats */
    if (h->use_multi_record) {
        printf("  Per-Record Statistics:\n");
        printf("%-16s %-8s %-12s %-15s\n", "Record ID", "Status", "Failed", "Current IP");
        printf("%-16s %-8s %-12s %-15s\n", "--------", "------", "------", "----------");
        for (int i = 0; i < h->global_config.record_count; i++) {
            dns_record_config_t *rec = &h->global_config.records[i];
            printf("%-16s %-8s %-12d %-15s\n",
                   rec->id,
                   ddns_status_name(rec->status),
                   rec->failed_attempts,
                   rec->current_ip[0] ? rec->current_ip : "--");
        }
        printf("\n");
    }
}

void ddns_service_show_wan_bindings(ddns_service_t *service) {
    ddns_service_handle_t *h = (ddns_service_handle_t *)service->handle;
    if (h == NULL) {
        printf("DDNS Service: Not initialized\n");
        return;
    }

    printf("\n");
    printf("DNS Record - WAN Binding Summary\n");
    printf("================================\n");
    printf("\n");

    /* Group by WAN interface */
    printf("%-16s %-30s %-6s %-12s %-15s\n",
           "Record ID", "Record Name", "Type", "WAN Interface", "Binding Mode");
    printf("%-16s %-30s %-6s %-12s %-15s\n",
           "--------", "-----------", "----", "------------", "------------");

    for (int i = 0; i < h->global_config.record_count; i++) {
        dns_record_config_t *rec = &h->global_config.records[i];
        printf("%-16s %-30s %-6s %-12s %-15s\n",
               rec->id,
               rec->record_name,
               dns_record_type_name(rec->record_type),
               rec->wan_interface[0] ? rec->wan_interface : "--",
               record_binding_mode_name(rec->binding_mode));
    }

    printf("\n");

    /* Show WAN IP mapping */
    if (h->wan_manager != NULL) {
        printf("WAN IP Addresses:\n");
        wan_manager_show(h->wan_manager, WAN_SHOW_IP, NULL);
    }
}

int ddns_service_format_status(ddns_service_t *service, char *buf, size_t len) {
    ddns_service_handle_t *h = (ddns_service_handle_t *)service->handle;
    if (h == NULL || buf == NULL) return 0;

    int written = 0;

    written += snprintf(buf + written, len - written,
                       "DDNS Service Status: %s\n", ddns_status_name(h->status));
    written += snprintf(buf + written, len - written,
                       "Records: %d configured\n", h->global_config.record_count);

    for (int i = 0; i < h->global_config.record_count && written < (int)len; i++) {
        dns_record_config_t *rec = &h->global_config.records[i];
        written += snprintf(buf + written, len - written,
                           "  %s [%s]: %s -> %s\n",
                           rec->id,
                           dns_record_type_name(rec->record_type),
                           rec->record_name,
                           rec->current_ip[0] ? rec->current_ip : "pending");
    }

    return written;
}

void ddns_service_show(ddns_service_t *service, ddns_show_mode_t mode, const char *arg) {
    switch (mode) {
        case DDNS_SHOW_ALL:
            ddns_service_show_status(service);
            ddns_service_show_config(service);
            ddns_service_show_records(service);
            if (service != NULL && service->wan_manager != NULL) {
                wan_manager_show(service->wan_manager, WAN_SHOW_STATUS, NULL);
            }
            break;
        case DDNS_SHOW_CONFIG:
            ddns_service_show_config(service);
            break;
        case DDNS_SHOW_RECORDS:
            ddns_service_show_records(service);
            break;
        case DDNS_SHOW_STATUS:
            ddns_service_show_status(service);
            break;
        case DDNS_SHOW_RECORD:
            ddns_service_show_record(service, arg);
            break;
        case DDNS_SHOW_STATS:
            ddns_service_show_stats(service);
            break;
        case DDNS_SHOW_WAN:
            ddns_service_show_wan_bindings(service);
            break;
        default:
            ddns_service_show_status(service);
            break;
    }
}