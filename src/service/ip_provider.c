/**
 * @file ip_provider.c
 * @brief IP address provider service implementation
 */

#include "service/ip_provider.h"
#include "hal/platform.h"
#include "hal/http_client.h"
#include "hal/logger.h"
#include "common/types.h"
#include "common/errors.h"
#include "common/macros.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <regex.h>
#include <ctype.h>

/* ========== Default IP Detection Endpoints ========== */

static ip_provider_endpoint_t default_endpoints[] = {
    {
        .name = "cloudflare",
        .url_ipv4 = "https://1.1.1.1/cdn-cgi/trace",
        .url_ipv6 = "https://[2606:4700:4700::1111]/cdn-cgi/trace",
        .priority = 1,
    },
    {
        .name = "ipify",
        .url_ipv4 = "https://api.ipify.org?format=text",
        .url_ipv6 = "https://api6.ipify.org?format=text",
        .priority = 2,
    },
    {
        .name = "icanhazip",
        .url_ipv4 = "https://icanhazip.com",
        .url_ipv6 = "https://ipv6.icanhazip.com",
        .priority = 3,
    },
    {
        .name = "ident.me",
        .url_ipv4 = "https://ident.me",
        .url_ipv6 = "https://ident.me",
        .priority = 4,
    },
    { NULL, NULL, NULL, 0 }  /* Sentinel */
};

/* ========== IP Provider Handle ========== */

#define MAX_CUSTOM_ENDPOINTS 8

typedef struct {
    ip_provider_config_t config;
    char *bind_interface_owned;
    http_client_t *http_client;
    ip_provider_endpoint_t endpoints[MAX_CUSTOM_ENDPOINTS + 4];  /* Default + custom */
    int endpoint_count;
} ip_provider_handle_t;

static int ip_provider_set_bind_interface_owned(ip_provider_handle_t *h, const char *interface_name) {
    char *copy = NULL;

    if (interface_name != NULL) {
        copy = strdup(interface_name);
        if (copy == NULL) return CFDDNS_ERR_OUT_OF_MEMORY;
    }

    CFDDNS_FREE(h->bind_interface_owned);
    h->bind_interface_owned = copy;
    h->config.bind_interface = h->bind_interface_owned;
    return CFDDNS_OK;
}

/* ========== IP Validation Patterns ========== */

static const char *ipv4_pattern =
    "^([0-9]{1,3}\\.){3}[0-9]{1,3}$";

static const char *ipv6_pattern =
    "^([0-9a-fA-F]{0,4}:){2,7}[0-9a-fA-F]{0,4}$|^::$|^([0-9a-fA-F]{0,4}:){1,7}:$|^:([0-9a-fA-F]{0,4}:){1,7}$";

/* ========== Utility Functions ========== */

int ip_validate(const char *ip, ip_type_t type) {
    if (ip == NULL || strlen(ip) == 0) return 0;

    regex_t regex;
    int result;

    const char *pattern = (type == IP_TYPE_IPV6) ? ipv6_pattern : ipv4_pattern;

    if (regcomp(&regex, pattern, REG_EXTENDED | REG_NOSUB) != 0) {
        return 0;
    }

    result = (regexec(&regex, ip, 0, NULL, 0) == 0) ? 1 : 0;
    regfree(&regex);

    /* Additional IPv4 validation: each octet <= 255 */
    if (result && type == IP_TYPE_IPV4) {
        char buf[64];
        CFDDNS_STRNCPY(buf, ip, sizeof(buf));

        char *token = strtok(buf, ".");
        while (token != NULL) {
            int octet = atoi(token);
            if (octet < 0 || octet > 255) {
                return 0;
            }
            token = strtok(NULL, ".");
        }
    }

    return result;
}

ip_type_t ip_detect_type(const char *ip) {
    if (ip == NULL) return IP_TYPE_IPV4;

    /* IPv6 contains colons */
    if (strchr(ip, ':') != NULL) {
        return IP_TYPE_IPV6;
    }

    /* IPv4 contains dots */
    if (strchr(ip, '.') != NULL) {
        return IP_TYPE_IPV4;
    }

    return IP_TYPE_IPV4;
}

int ip_compare(const char *ip1, const char *ip2) {
    if (ip1 == NULL && ip2 == NULL) return 0;
    if (ip1 == NULL || ip2 == NULL) return 1;

    return strcmp(ip1, ip2);
}

/* ========== Default Configuration ========== */

ip_provider_config_t ip_provider_config_default(void) {
    ip_provider_config_t config;
    memset(&config, 0, sizeof(config));
    config.timeout_ms = 5000;
    config.retry_count = 3;
    config.retry_delay_ms = 1000;
    config.verify_response = true;
    config.preferred_type = IP_TYPE_IPV4;
    return config;
}

const ip_provider_endpoint_t *ip_provider_default_endpoints(void) {
    return default_endpoints;
}

/* ========== Internal Helpers ========== */

static void sort_endpoints_by_priority(ip_provider_handle_t *h) {
    /* Simple bubble sort by priority */
    for (int i = 0; i < h->endpoint_count - 1; i++) {
        for (int j = 0; j < h->endpoint_count - i - 1; j++) {
            if (h->endpoints[j].priority > h->endpoints[j + 1].priority) {
                ip_provider_endpoint_t tmp = h->endpoints[j];
                h->endpoints[j] = h->endpoints[j + 1];
                h->endpoints[j + 1] = tmp;
            }
        }
    }
}

static int extract_ip_from_response(const char *response, char *buf, size_t len) {
    if (response == NULL || buf == NULL) return CFDDNS_ERR_NULL_POINTER;

    /* Clean the response: remove whitespace, newlines */
    const char *start = response;
    while (*start && isspace(*start)) start++;

    /* Check for empty response after trimming */
    size_t trimmed_len = strlen(start);
    if (trimmed_len == 0) {
        return CFDDNS_ERR_INVALID_RESPONSE;
    }

    const char *end = start + trimmed_len - 1;
    while (end > start && isspace(*end)) end--;

    size_t ip_len = end - start + 1;
    if (ip_len == 0 || ip_len >= len) {
        return CFDDNS_ERR_INVALID_RESPONSE;
    }

    /* Copy IP address */
    memcpy(buf, start, ip_len);
    buf[ip_len] = '\0';

    /* Cloudflare trace format: "ip=1.2.3.4" */
    const char *ip_prefix = "ip=";
    const char *trace_ip = strstr(buf, ip_prefix);
    if (trace_ip != NULL) {
        const char *ip_start = trace_ip + strlen(ip_prefix);
        /* Find end of IP */
        const char *ip_end = strchr(ip_start, '\n');
        if (ip_end == NULL) ip_end = ip_start + strlen(ip_start);

        size_t actual_len = ip_end - ip_start;
        if (actual_len >= len) return CFDDNS_ERR_BUFFER_TOO_SMALL;

        memmove(buf, ip_start, actual_len);
        buf[actual_len] = '\0';
    }

    return CFDDNS_OK;
}

static int fetch_ip_from_endpoint(ip_provider_handle_t *h,
                                   const ip_provider_endpoint_t *endpoint,
                                   ip_type_t type,
                                   const char *bind_interface,
                                   char *buf, size_t len) {
    if (h == NULL || endpoint == NULL || buf == NULL) return CFDDNS_ERR_NULL_POINTER;
    if (h->http_client == NULL) return CFDDNS_ERR_NULL_POINTER;

    const char *url = (type == IP_TYPE_IPV6) ? endpoint->url_ipv6 : endpoint->url_ipv4;
    if (url == NULL) return CFDDNS_ERR_INVALID_ARG;

    http_response_t response;
    memset(&response, 0, sizeof(response));

    int result = CFDDNS_ERR_NETWORK_FAILED;

    /* Set timeout */
    http_client_set_timeout(h->http_client, h->config.timeout_ms);

    /* Set interface binding if specified */
    if (bind_interface != NULL) {
        http_client_set_bind_interface(h->http_client, bind_interface);
    } else if (h->config.bind_interface != NULL) {
        http_client_set_bind_interface(h->http_client, h->config.bind_interface);
    } else {
        http_client_set_bind_interface(h->http_client, NULL);
    }

    for (int retry = 0; retry <= h->config.retry_count; retry++) {
        result = http_client_get(h->http_client, url, NULL, &response);

        if (CFDDNS_SUCCEEDED(result) && response.status_code == 200) {
            result = extract_ip_from_response(response.body, buf, len);
            if (CFDDNS_SUCCEEDED(result)) {
                /* Validate IP */
                if (h->config.verify_response && !ip_validate(buf, type)) {
                    result = CFDDNS_ERR_INVALID_RESPONSE;
                    continue;
                }
                break;
            }
        }

        /* Retry delay */
        if (retry < h->config.retry_count) {
            timer_sleep_ms(h->config.retry_delay_ms);
        }

        /* Ensure all response fields are released before retry. */
        http_response_free(&response);
    }

    http_response_free(&response);

    return result;
}

/* ========== Implementation Functions ========== */

static int ip_provider_init_impl(ip_provider_t *self, const ip_provider_config_t *config) {
    ip_provider_handle_t *h = (ip_provider_handle_t *)self->handle;
    if (h == NULL) return CFDDNS_ERR_NULL_POINTER;

    int result;
    if (config != NULL) {
        h->config = *config;
        result = ip_provider_set_bind_interface_owned(h, config->bind_interface);
    } else {
        h->config = ip_provider_config_default();
        result = ip_provider_set_bind_interface_owned(h, NULL);
    }
    if (CFDDNS_FAILED(result)) return result;

    /* Initialize endpoints with defaults */
    h->endpoint_count = 0;
    for (int i = 0; default_endpoints[i].name != NULL; i++) {
        h->endpoints[h->endpoint_count++] = default_endpoints[i];
    }

    sort_endpoints_by_priority(h);

    return CFDDNS_OK;
}

static int ip_provider_get_ip_impl(ip_provider_t *self, ip_type_t type, char *buf, size_t len) {
    ip_provider_handle_t *h = (ip_provider_handle_t *)self->handle;
    if (h == NULL || buf == NULL) return CFDDNS_ERR_NULL_POINTER;
    if (len < 16) return CFDDNS_ERR_BUFFER_TOO_SMALL;

    /* Try each endpoint in priority order */
    for (int i = 0; i < h->endpoint_count; i++) {
        int result = fetch_ip_from_endpoint(h, &h->endpoints[i], type, NULL, buf, len);
        if (CFDDNS_SUCCEEDED(result)) {
            return CFDDNS_OK;
        }
    }

    return CFDDNS_ERR_NETWORK_FAILED;
}

static int ip_provider_get_ip_from_interface_impl(ip_provider_t *self, ip_type_t type,
                                                   const char *interface_name,
                                                   char *buf, size_t len) {
    ip_provider_handle_t *h = (ip_provider_handle_t *)self->handle;
    if (h == NULL || buf == NULL) return CFDDNS_ERR_NULL_POINTER;
    if (len < 16) return CFDDNS_ERR_BUFFER_TOO_SMALL;

    /* Try each endpoint in priority order with interface binding */
    for (int i = 0; i < h->endpoint_count; i++) {
        int result = fetch_ip_from_endpoint(h, &h->endpoints[i], type, interface_name, buf, len);
        if (CFDDNS_SUCCEEDED(result)) {
            return CFDDNS_OK;
        }
    }

    return CFDDNS_ERR_NETWORK_FAILED;
}

static int ip_provider_set_bind_interface_impl(ip_provider_t *self, const char *interface_name) {
    ip_provider_handle_t *h = (ip_provider_handle_t *)self->handle;
    if (h == NULL) return CFDDNS_ERR_NULL_POINTER;

    return ip_provider_set_bind_interface_owned(h, interface_name);
}

static int ip_provider_get_all_ips_impl(ip_provider_t *self, ip_result_t *result) {
    ip_provider_handle_t *h = (ip_provider_handle_t *)self->handle;
    if (h == NULL || result == NULL) return CFDDNS_ERR_NULL_POINTER;

    memset(result, 0, sizeof(ip_result_t));

    int ipv4_result = ip_provider_get_ip_impl(self, IP_TYPE_IPV4, result->ipv4, sizeof(result->ipv4));
    int ipv6_result = ip_provider_get_ip_impl(self, IP_TYPE_IPV6, result->ipv6, sizeof(result->ipv6));

    result->timestamp = timer_get_time_ms();

    /* Return success if at least one IP was obtained */
    if (CFDDNS_SUCCEEDED(ipv4_result) || CFDDNS_SUCCEEDED(ipv6_result)) {
        return CFDDNS_OK;
    }

    return CFDDNS_ERR_NETWORK_FAILED;
}

static void ip_provider_destroy_impl(ip_provider_t *self) {
    if (self == NULL) return;

    ip_provider_handle_t *h = (ip_provider_handle_t *)self->handle;
    if (h != NULL) {
        CFDDNS_FREE(h->bind_interface_owned);
        free(h);
        self->handle = NULL;
    }

    free(self);
}

static void ip_provider_set_http_client_impl(ip_provider_t *self, http_client_t *client) {
    ip_provider_handle_t *h = (ip_provider_handle_t *)self->handle;
    if (h != NULL) {
        h->http_client = client;
    }
}

/* ========== Public API ========== */

ip_provider_t *ip_provider_create(void) {
    return ip_provider_create_with_http(NULL);
}

ip_provider_t *ip_provider_create_with_http(http_client_t *http_client) {
    ip_provider_t *provider = malloc(sizeof(ip_provider_t));
    if (provider == NULL) return NULL;

    memset(provider, 0, sizeof(ip_provider_t));

    ip_provider_handle_t *handle = malloc(sizeof(ip_provider_handle_t));
    if (handle == NULL) {
        free(provider);
        return NULL;
    }

    memset(handle, 0, sizeof(ip_provider_handle_t));
    handle->config = ip_provider_config_default();
    handle->http_client = http_client;

    /* Initialize endpoints with defaults */
    for (int i = 0; default_endpoints[i].name != NULL; i++) {
        handle->endpoints[handle->endpoint_count++] = default_endpoints[i];
    }
    sort_endpoints_by_priority(handle);

    provider->handle = handle;
    provider->http_client = http_client;

    /* Set function pointers */
    provider->init = ip_provider_init_impl;
    provider->get_ip = ip_provider_get_ip_impl;
    provider->get_all_ips = ip_provider_get_all_ips_impl;
    provider->destroy = ip_provider_destroy_impl;
    provider->set_http_client = ip_provider_set_http_client_impl;
    provider->get_ip_from_interface = ip_provider_get_ip_from_interface_impl;
    provider->set_bind_interface = ip_provider_set_bind_interface_impl;

    return provider;
}

void ip_provider_destroy(ip_provider_t *provider) {
    if (provider != NULL && provider->destroy != NULL) {
        provider->destroy(provider);
    }
}

int ip_provider_get_ip(ip_provider_t *provider, ip_type_t type, char *buf, size_t len) {
    if (provider != NULL && provider->get_ip != NULL) {
        return provider->get_ip(provider, type, buf, len);
    }
    return CFDDNS_ERR_NULL_POINTER;
}

int ip_provider_get_ipv4(ip_provider_t *provider, char *buf, size_t len) {
    return ip_provider_get_ip(provider, IP_TYPE_IPV4, buf, len);
}

int ip_provider_get_ipv6(ip_provider_t *provider, char *buf, size_t len) {
    return ip_provider_get_ip(provider, IP_TYPE_IPV6, buf, len);
}

int ip_provider_get_all(ip_provider_t *provider, ip_result_t *result) {
    if (provider != NULL && provider->get_all_ips != NULL) {
        return provider->get_all_ips(provider, result);
    }
    return CFDDNS_ERR_NULL_POINTER;
}

int ip_provider_set_config(ip_provider_t *provider, const ip_provider_config_t *config) {
    if (provider == NULL || config == NULL) return CFDDNS_ERR_NULL_POINTER;

    if (provider->init != NULL) {
        return provider->init(provider, config);
    }

    return CFDDNS_ERR_NOT_IMPLEMENTED;
}

void ip_provider_set_http_client(ip_provider_t *provider, http_client_t *client) {
    if (provider != NULL && provider->set_http_client != NULL) {
        provider->set_http_client(provider, client);
    }
}

int ip_provider_add_endpoint(ip_provider_t *provider, const ip_provider_endpoint_t *endpoint) {
    ip_provider_handle_t *h = (ip_provider_handle_t *)provider->handle;
    if (h == NULL || endpoint == NULL) return CFDDNS_ERR_NULL_POINTER;
    if (h->endpoint_count >= MAX_CUSTOM_ENDPOINTS + 4) return CFDDNS_ERR_OUT_OF_MEMORY;

    h->endpoints[h->endpoint_count++] = *endpoint;
    sort_endpoints_by_priority(h);

    return CFDDNS_OK;
}

int ip_provider_remove_endpoint(ip_provider_t *provider, const char *name) {
    ip_provider_handle_t *h = (ip_provider_handle_t *)provider->handle;
    if (h == NULL || name == NULL) return CFDDNS_ERR_NULL_POINTER;

    for (int i = 0; i < h->endpoint_count; i++) {
        if (strcasecmp(h->endpoints[i].name, name) == 0) {
            /* Shift remaining endpoints */
            for (int j = i; j < h->endpoint_count - 1; j++) {
                h->endpoints[j] = h->endpoints[j + 1];
            }
            h->endpoint_count--;
            return CFDDNS_OK;
        }
    }

    return CFDDNS_ERR_NOT_FOUND;
}

int ip_provider_get_endpoints(ip_provider_t *provider,
                               ip_provider_endpoint_t *endpoints,
                               int max_count) {
    ip_provider_handle_t *h = (ip_provider_handle_t *)provider->handle;
    if (h == NULL || endpoints == NULL) return 0;

    int count = (h->endpoint_count < max_count) ? h->endpoint_count : max_count;
    memcpy(endpoints, h->endpoints, count * sizeof(ip_provider_endpoint_t));

    return count;
}

/* ========== Interface Binding Functions (Multi-WAN) ========== */

int ip_provider_get_ip_from_interface(ip_provider_t *provider,
                                       ip_type_t type,
                                       const char *interface_name,
                                       char *buf, size_t len) {
    if (provider != NULL && provider->get_ip_from_interface != NULL) {
        return provider->get_ip_from_interface(provider, type, interface_name, buf, len);
    }
    return CFDDNS_ERR_NULL_POINTER;
}

int ip_provider_set_bind_interface(ip_provider_t *provider, const char *interface_name) {
    if (provider != NULL && provider->set_bind_interface != NULL) {
        return provider->set_bind_interface(provider, interface_name);
    }
    return CFDDNS_ERR_NULL_POINTER;
}