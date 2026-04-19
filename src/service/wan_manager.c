/**
 * @file wan_manager.c
 * @brief WAN interface manager implementation
 */

#include "service/wan_manager.h"
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
#include <ifaddrs.h>
#include <net/if.h>
#include <arpa/inet.h>

/* ========== WAN Manager Handle ========== */

typedef struct {
    wan_manager_config_t config;
    ip_provider_t *ip_provider;
    http_client_t *http_client;
    logger_t *logger;
    char active_wan[WAN_MAX_NAME_LEN];
} wan_manager_handle_t;

/* ========== Default Configuration ========== */

wan_manager_config_t wan_manager_config_default(void) {
    wan_manager_config_t config;
    memset(&config, 0, sizeof(config));
    config.failover_enabled = true;
    config.failover_threshold = 3;
    config.check_interval = 30;
    return config;
}

/* ========== Local IP Detection ========== */

static int get_local_interface_ip(const char *interface_name,
                                  ip_type_t type,
                                  char *buf, size_t len) {
    struct ifaddrs *ifaddr, *ifa;
    int result = CFDDNS_ERR_NOT_FOUND;

    if (getifaddrs(&ifaddr) == -1) {
        return CFDDNS_ERR_NETWORK_FAILED;
    }

    for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
        if (ifa->ifa_addr == NULL) continue;

        /* Match interface name */
        if (strcmp(ifa->ifa_name, interface_name) != 0) continue;

        /* Check address family */
        int family = ifa->ifa_addr->sa_family;

        if (type == IP_TYPE_IPV4 && family == AF_INET) {
            struct sockaddr_in *addr = (struct sockaddr_in *)ifa->ifa_addr;
            const char *ip = inet_ntop(AF_INET, &addr->sin_addr, buf, len);
            if (ip != NULL) {
                result = CFDDNS_OK;
                break;
            }
        } else if (type == IP_TYPE_IPV6 && family == AF_INET6) {
            struct sockaddr_in6 *addr = (struct sockaddr_in6 *)ifa->ifa_addr;
            const char *ip = inet_ntop(AF_INET6, &addr->sin6_addr, buf, len);
            if (ip != NULL) {
                result = CFDDNS_OK;
                break;
            }
        }
    }

    freeifaddrs(ifaddr);
    return result;
}

/* ========== Alias Mapping ========== */

static wan_config_t *find_wan_by_name_or_alias(wan_manager_handle_t *h,
                                                const char *name_or_alias) {
    if (name_or_alias == NULL) return NULL;

    for (int i = 0; i < h->config.wan_count; i++) {
        wan_config_t *wan = &h->config.wans[i];

        /* Match by system name */
        if (strcmp(wan->name, name_or_alias) == 0) {
            return wan;
        }

        /* Match by alias */
        if (wan->alias[0] != '\0' && strcmp(wan->alias, name_or_alias) == 0) {
            return wan;
        }
    }

    return NULL;
}

/* ========== Implementation Functions ========== */

static int wan_manager_init_impl(wan_manager_t *self, const wan_manager_config_t *config) {
    wan_manager_handle_t *h = (wan_manager_handle_t *)self->handle;
    if (h == NULL) return CFDDNS_ERR_NULL_POINTER;

    if (config != NULL) {
        h->config = *config;
    } else {
        h->config = wan_manager_config_default();
    }

    /* Set active WAN to default or first enabled WAN */
    if (h->config.default_wan[0] != '\0') {
        CFDDNS_STRNCPY(h->active_wan, h->config.default_wan, WAN_MAX_NAME_LEN);
    } else {
        for (int i = 0; i < h->config.wan_count; i++) {
            if (h->config.wans[i].enabled) {
                CFDDNS_STRNCPY(h->active_wan, h->config.wans[i].name, WAN_MAX_NAME_LEN);
                break;
            }
        }
    }

    return CFDDNS_OK;
}

static void wan_manager_destroy_impl(wan_manager_t *self) {
    if (self == NULL) return;

    wan_manager_handle_t *h = (wan_manager_handle_t *)self->handle;
    if (h != NULL) {
        free(h);
        self->handle = NULL;
    }

    free(self);
}

static int wan_manager_detect_ip_external_impl(wan_manager_t *self,
                                                 const char *wan_name,
                                                 ip_type_t type,
                                                 char *buf, size_t len) {
    wan_manager_handle_t *h = (wan_manager_handle_t *)self->handle;
    if (h == NULL || wan_name == NULL || buf == NULL) return CFDDNS_ERR_NULL_POINTER;

    wan_config_t *wan = find_wan_by_name_or_alias(h, wan_name);
    if (wan == NULL) return CFDDNS_ERR_NOT_FOUND;

    if (!wan->enabled ||
        (type == IP_TYPE_IPV4 && !wan->detect_ipv4) ||
        (type == IP_TYPE_IPV6 && !wan->detect_ipv6)) {
        return CFDDNS_ERR_INVALID_ARG;
    }

    /* Use IP provider with interface binding */
    if (h->ip_provider != NULL) {
        return ip_provider_get_ip_from_interface(h->ip_provider, type, wan->name, buf, len);
    }

    return CFDDNS_ERR_NULL_POINTER;
}

static int wan_manager_detect_ip_local_impl(wan_manager_t *self,
                                             const char *wan_name,
                                             ip_type_t type,
                                             char *buf, size_t len) {
    wan_manager_handle_t *h = (wan_manager_handle_t *)self->handle;
    if (h == NULL || wan_name == NULL || buf == NULL) return CFDDNS_ERR_NULL_POINTER;

    wan_config_t *wan = find_wan_by_name_or_alias(h, wan_name);
    if (wan == NULL) return CFDDNS_ERR_NOT_FOUND;

    /* Get IP from local interface */
    return get_local_interface_ip(wan->name, type, buf, len);
}

static int wan_manager_detect_ip_impl(wan_manager_t *self,
                                       const char *wan_name_or_alias,
                                       ip_type_t type,
                                       char *buf, size_t len) {
    wan_manager_handle_t *h = (wan_manager_handle_t *)self->handle;
    if (h == NULL || wan_name_or_alias == NULL || buf == NULL) return CFDDNS_ERR_NULL_POINTER;

    wan_config_t *wan = find_wan_by_name_or_alias(h, wan_name_or_alias);
    if (wan == NULL) return CFDDNS_ERR_NOT_FOUND;

    int result = CFDDNS_ERR_NETWORK_FAILED;

    /* Try local detection first if enabled */
    if (wan->use_local_read) {
        result = wan_manager_detect_ip_local_impl(self, wan->name, type, buf, len);
        if (CFDDNS_SUCCEEDED(result)) {
            return result;
        }
    }

    /* Try external detection if enabled or local failed */
    if (wan->use_external_detect || !wan->use_local_read) {
        result = wan_manager_detect_ip_external_impl(self, wan->name, type, buf, len);
    }

    return result;
}

static int wan_manager_detect_all_ips_impl(wan_manager_t *self,
                                            const char *wan_name,
                                            wan_ip_result_t *result) {
    wan_manager_handle_t *h = (wan_manager_handle_t *)self->handle;
    if (h == NULL || wan_name == NULL || result == NULL) return CFDDNS_ERR_NULL_POINTER;

    wan_config_t *wan = find_wan_by_name_or_alias(h, wan_name);
    if (wan == NULL) return CFDDNS_ERR_NOT_FOUND;

    memset(result, 0, sizeof(wan_ip_result_t));
    CFDDNS_STRNCPY(result->wan_name, wan->name, WAN_MAX_NAME_LEN);
    result->timestamp = timer_get_time_ms();

    bool ipv4_success = false;
    bool ipv6_success = false;

    /* Detect IPv4 */
    if (wan->detect_ipv4) {
        int ipv4_result = wan_manager_detect_ip_impl(self, wan->name, IP_TYPE_IPV4,
                                                      result->ipv4, sizeof(result->ipv4));
        ipv4_success = CFDDNS_SUCCEEDED(ipv4_result);
    }

    /* Detect IPv6 */
    if (wan->detect_ipv6) {
        int ipv6_result = wan_manager_detect_ip_impl(self, wan->name, IP_TYPE_IPV6,
                                    result->ipv6, sizeof(result->ipv6));
        ipv6_success = CFDDNS_SUCCEEDED(ipv6_result);
    }

    /* Success if any enabled protocol succeeded */
    result->success = false;
    if (wan->detect_ipv4 && wan->detect_ipv6) {
        result->success = ipv4_success || ipv6_success;
    } else if (wan->detect_ipv4) {
        result->success = ipv4_success;
    } else if (wan->detect_ipv6) {
        result->success = ipv6_success;
    }

    return result->success ? CFDDNS_OK : CFDDNS_ERR_NETWORK_FAILED;
}

static int wan_manager_map_alias_impl(wan_manager_t *self,
                                       const char *alias,
                                       char *buf, size_t len) {
    wan_manager_handle_t *h = (wan_manager_handle_t *)self->handle;
    if (h == NULL || alias == NULL || buf == NULL) return CFDDNS_ERR_NULL_POINTER;

    wan_config_t *wan = find_wan_by_name_or_alias(h, alias);
    if (wan == NULL) return CFDDNS_ERR_NOT_FOUND;

    CFDDNS_STRNCPY(buf, wan->name, len);
    return CFDDNS_OK;
}

static int wan_manager_health_check_impl(wan_manager_t *self, const char *wan_name) {
    wan_manager_handle_t *h = (wan_manager_handle_t *)self->handle;
    if (h == NULL || wan_name == NULL) return CFDDNS_ERR_NULL_POINTER;

    wan_config_t *wan = find_wan_by_name_or_alias(h, wan_name);
    if (wan == NULL) return CFDDNS_ERR_NOT_FOUND;

    /* Simple health check: try to detect IP */
    char ip_buf[48];
    int result = wan_manager_detect_ip_impl(self, wan->name, IP_TYPE_IPV4, ip_buf, sizeof(ip_buf));

    wan->last_check_time = timer_get_time_ms();

    if (CFDDNS_SUCCEEDED(result)) {
        wan->is_online = true;
        wan->failed_checks = 0;
        CFDDNS_STRNCPY(wan->current_ipv4, ip_buf, sizeof(wan->current_ipv4));
        return CFDDNS_OK;
    } else {
        wan->failed_checks++;
        if (wan->failed_checks >= h->config.failover_threshold) {
            wan->is_online = false;
        }
        return CFDDNS_ERR_NETWORK_FAILED;
    }
}

static int wan_manager_check_all_health_impl(wan_manager_t *self) {
    wan_manager_handle_t *h = (wan_manager_handle_t *)self->handle;
    if (h == NULL) return 0;

    int online_count = 0;

    for (int i = 0; i < h->config.wan_count; i++) {
        wan_config_t *wan = &h->config.wans[i];
        if (!wan->enabled) continue;

        int result = wan_manager_health_check_impl(self, wan->name);
        if (CFDDNS_SUCCEEDED(result)) {
            online_count++;
        }
    }

    return online_count;
}

static int wan_manager_add_wan_impl(wan_manager_t *self, const wan_config_t *wan) {
    wan_manager_handle_t *h = (wan_manager_handle_t *)self->handle;
    if (h == NULL || wan == NULL) return CFDDNS_ERR_NULL_POINTER;
    if (h->config.wan_count >= WAN_MAX_COUNT) return CFDDNS_ERR_OUT_OF_MEMORY;

    h->config.wans[h->config.wan_count++] = *wan;
    return CFDDNS_OK;
}

static int wan_manager_remove_wan_impl(wan_manager_t *self, const char *wan_name) {
    wan_manager_handle_t *h = (wan_manager_handle_t *)self->handle;
    if (h == NULL || wan_name == NULL) return CFDDNS_ERR_NULL_POINTER;

    for (int i = 0; i < h->config.wan_count; i++) {
        if (strcmp(h->config.wans[i].name, wan_name) == 0) {
            /* Shift remaining WANs */
            for (int j = i; j < h->config.wan_count - 1; j++) {
                h->config.wans[j] = h->config.wans[j + 1];
            }
            h->config.wan_count--;
            return CFDDNS_OK;
        }
    }

    return CFDDNS_ERR_NOT_FOUND;
}

static int wan_manager_enable_wan_impl(wan_manager_t *self, const char *wan_name) {
    wan_manager_handle_t *h = (wan_manager_handle_t *)self->handle;
    if (h == NULL || wan_name == NULL) return CFDDNS_ERR_NULL_POINTER;

    wan_config_t *wan = find_wan_by_name_or_alias(h, wan_name);
    if (wan == NULL) return CFDDNS_ERR_NOT_FOUND;

    wan->enabled = true;
    return CFDDNS_OK;
}

static int wan_manager_disable_wan_impl(wan_manager_t *self, const char *wan_name) {
    wan_manager_handle_t *h = (wan_manager_handle_t *)self->handle;
    if (h == NULL || wan_name == NULL) return CFDDNS_ERR_NULL_POINTER;

    wan_config_t *wan = find_wan_by_name_or_alias(h, wan_name);
    if (wan == NULL) return CFDDNS_ERR_NOT_FOUND;

    wan->enabled = false;
    wan->is_online = false;
    return CFDDNS_OK;
}

static wan_config_t *wan_manager_get_wan_impl(wan_manager_t *self,
                                                const char *wan_name_or_alias) {
    wan_manager_handle_t *h = (wan_manager_handle_t *)self->handle;
    if (h == NULL || wan_name_or_alias == NULL) return NULL;

    return find_wan_by_name_or_alias(h, wan_name_or_alias);
}

static int wan_manager_get_all_wans_impl(wan_manager_t *self,
                                          wan_config_t *wans, int max_count) {
    wan_manager_handle_t *h = (wan_manager_handle_t *)self->handle;
    if (h == NULL || wans == NULL) return 0;

    int count = (h->config.wan_count < max_count) ? h->config.wan_count : max_count;
    memcpy(wans, h->config.wans, count * sizeof(wan_config_t));

    return count;
}

static int wan_manager_get_active_wan_impl(wan_manager_t *self, char *buf, size_t len) {
    wan_manager_handle_t *h = (wan_manager_handle_t *)self->handle;
    if (h == NULL || buf == NULL) return CFDDNS_ERR_NULL_POINTER;

    /* Find highest priority online WAN */
    wan_config_t *best_wan = NULL;

    for (int i = 0; i < h->config.wan_count; i++) {
        wan_config_t *wan = &h->config.wans[i];
        if (!wan->enabled || !wan->is_online) continue;

        if (best_wan == NULL || wan->priority < best_wan->priority) {
            best_wan = wan;
        }
    }

    if (best_wan == NULL) return CFDDNS_ERR_NOT_FOUND;

    CFDDNS_STRNCPY(buf, best_wan->name, len);
    return CFDDNS_OK;
}

static int wan_manager_get_online_wans_impl(wan_manager_t *self,
                                             wan_config_t *wans, int max_count) {
    wan_manager_handle_t *h = (wan_manager_handle_t *)self->handle;
    if (h == NULL || wans == NULL) return 0;

    int count = 0;
    for (int i = 0; i < h->config.wan_count && count < max_count; i++) {
        if (h->config.wans[i].enabled && h->config.wans[i].is_online) {
            wans[count++] = h->config.wans[i];
        }
    }

    return count;
}

static int wan_manager_check_failover_impl(wan_manager_t *self) {
    wan_manager_handle_t *h = (wan_manager_handle_t *)self->handle;
    if (h == NULL) return 0;

    if (!h->config.failover_enabled) return 0;

    /* Check if active WAN is still online */
    wan_config_t *active = find_wan_by_name_or_alias(h, h->active_wan);

    if (active != NULL && active->is_online) {
        return 0;  /* No failover needed */
    }

    /* Find new active WAN */
    char new_active[WAN_MAX_NAME_LEN];
    int result = wan_manager_get_active_wan_impl(self, new_active, sizeof(new_active));

    if (CFDDNS_SUCCEEDED(result) && strcmp(h->active_wan, new_active) != 0) {
        CFDDNS_STRNCPY(h->active_wan, new_active, WAN_MAX_NAME_LEN);
        return 1;  /* Failover occurred */
    }

    return CFDDNS_ERR_NOT_FOUND;  /* No available WAN */
}

static int wan_manager_switch_wan_impl(wan_manager_t *self, const char *wan_name) {
    wan_manager_handle_t *h = (wan_manager_handle_t *)self->handle;
    if (h == NULL || wan_name == NULL) return CFDDNS_ERR_NULL_POINTER;

    wan_config_t *wan = find_wan_by_name_or_alias(h, wan_name);
    if (wan == NULL) return CFDDNS_ERR_NOT_FOUND;

    if (!wan->enabled || !wan->is_online) {
        return CFDDNS_ERR_INVALID_ARG;
    }

    CFDDNS_STRNCPY(h->active_wan, wan->name, WAN_MAX_NAME_LEN);
    return CFDDNS_OK;
}

static void wan_manager_set_ip_provider_impl(wan_manager_t *self, ip_provider_t *provider) {
    wan_manager_handle_t *h = (wan_manager_handle_t *)self->handle;
    if (h != NULL) {
        h->ip_provider = provider;
    }
}

static void wan_manager_set_http_client_impl(wan_manager_t *self, http_client_t *client) {
    wan_manager_handle_t *h = (wan_manager_handle_t *)self->handle;
    if (h != NULL) {
        h->http_client = client;
    }
}

/* ========== Public API ========== */

wan_manager_t *wan_manager_create(void) {
    return wan_manager_create_with_deps(NULL, NULL);
}

wan_manager_t *wan_manager_create_with_deps(ip_provider_t *ip_provider,
                                             http_client_t *http_client) {
    wan_manager_t *manager = malloc(sizeof(wan_manager_t));
    if (manager == NULL) return NULL;

    memset(manager, 0, sizeof(wan_manager_t));

    wan_manager_handle_t *handle = malloc(sizeof(wan_manager_handle_t));
    if (handle == NULL) {
        free(manager);
        return NULL;
    }

    memset(handle, 0, sizeof(wan_manager_handle_t));
    handle->config = wan_manager_config_default();
    handle->ip_provider = ip_provider;
    handle->http_client = http_client;

    manager->handle = handle;
    manager->ip_provider = ip_provider;
    manager->http_client = http_client;

    /* Set function pointers */
    manager->init = wan_manager_init_impl;
    manager->destroy = wan_manager_destroy_impl;
    manager->detect_ip_external = wan_manager_detect_ip_external_impl;
    manager->detect_ip_local = wan_manager_detect_ip_local_impl;
    manager->detect_ip = wan_manager_detect_ip_impl;
    manager->detect_all_ips = wan_manager_detect_all_ips_impl;
    manager->map_alias_to_interface = wan_manager_map_alias_impl;
    manager->health_check = wan_manager_health_check_impl;
    manager->check_all_health = wan_manager_check_all_health_impl;
    manager->add_wan = wan_manager_add_wan_impl;
    manager->remove_wan = wan_manager_remove_wan_impl;
    manager->enable_wan = wan_manager_enable_wan_impl;
    manager->disable_wan = wan_manager_disable_wan_impl;
    manager->get_wan = wan_manager_get_wan_impl;
    manager->get_all_wans = wan_manager_get_all_wans_impl;
    manager->get_active_wan = wan_manager_get_active_wan_impl;
    manager->get_online_wans = wan_manager_get_online_wans_impl;
    manager->check_failover = wan_manager_check_failover_impl;
    manager->switch_wan = wan_manager_switch_wan_impl;
    manager->set_ip_provider = wan_manager_set_ip_provider_impl;
    manager->set_http_client = wan_manager_set_http_client_impl;

    return manager;
}

void wan_manager_destroy(wan_manager_t *manager) {
    if (manager != NULL && manager->destroy != NULL) {
        manager->destroy(manager);
    }
}

int wan_manager_init(wan_manager_t *manager, const wan_manager_config_t *config) {
    if (manager != NULL && manager->init != NULL) {
        return manager->init(manager, config);
    }
    return CFDDNS_ERR_NULL_POINTER;
}

int wan_manager_detect_ip(wan_manager_t *manager,
                           const char *wan_name_or_alias,
                           ip_type_t type,
                           char *buf, size_t len) {
    if (manager != NULL && manager->detect_ip != NULL) {
        return manager->detect_ip(manager, wan_name_or_alias, type, buf, len);
    }
    return CFDDNS_ERR_NULL_POINTER;
}

int wan_manager_detect_ip_external(wan_manager_t *manager,
                                    const char *wan_name,
                                    ip_type_t type,
                                    char *buf, size_t len) {
    if (manager != NULL && manager->detect_ip_external != NULL) {
        return manager->detect_ip_external(manager, wan_name, type, buf, len);
    }
    return CFDDNS_ERR_NULL_POINTER;
}

int wan_manager_detect_ip_local(wan_manager_t *manager,
                                 const char *wan_name,
                                 ip_type_t type,
                                 char *buf, size_t len) {
    if (manager != NULL && manager->detect_ip_local != NULL) {
        return manager->detect_ip_local(manager, wan_name, type, buf, len);
    }
    return CFDDNS_ERR_NULL_POINTER;
}

int wan_manager_detect_all_ips(wan_manager_t *manager,
                                const char *wan_name,
                                wan_ip_result_t *result) {
    if (manager != NULL && manager->detect_all_ips != NULL) {
        return manager->detect_all_ips(manager, wan_name, result);
    }
    return CFDDNS_ERR_NULL_POINTER;
}

int wan_manager_map_alias(wan_manager_t *manager,
                           const char *alias,
                           char *buf, size_t len) {
    if (manager != NULL && manager->map_alias_to_interface != NULL) {
        return manager->map_alias_to_interface(manager, alias, buf, len);
    }
    return CFDDNS_ERR_NULL_POINTER;
}

int wan_manager_health_check(wan_manager_t *manager, const char *wan_name) {
    if (manager != NULL && manager->health_check != NULL) {
        return manager->health_check(manager, wan_name);
    }
    return CFDDNS_ERR_NULL_POINTER;
}

int wan_manager_check_all_health(wan_manager_t *manager) {
    if (manager != NULL && manager->check_all_health != NULL) {
        return manager->check_all_health(manager);
    }
    return 0;
}

int wan_manager_add_wan(wan_manager_t *manager, const wan_config_t *wan) {
    if (manager != NULL && manager->add_wan != NULL) {
        return manager->add_wan(manager, wan);
    }
    return CFDDNS_ERR_NULL_POINTER;
}

int wan_manager_remove_wan(wan_manager_t *manager, const char *wan_name) {
    if (manager != NULL && manager->remove_wan != NULL) {
        return manager->remove_wan(manager, wan_name);
    }
    return CFDDNS_ERR_NULL_POINTER;
}

int wan_manager_enable_wan(wan_manager_t *manager, const char *wan_name) {
    if (manager != NULL && manager->enable_wan != NULL) {
        return manager->enable_wan(manager, wan_name);
    }
    return CFDDNS_ERR_NULL_POINTER;
}

int wan_manager_disable_wan(wan_manager_t *manager, const char *wan_name) {
    if (manager != NULL && manager->disable_wan != NULL) {
        return manager->disable_wan(manager, wan_name);
    }
    return CFDDNS_ERR_NULL_POINTER;
}

wan_config_t *wan_manager_get_wan(wan_manager_t *manager,
                                   const char *wan_name_or_alias) {
    if (manager != NULL && manager->get_wan != NULL) {
        return manager->get_wan(manager, wan_name_or_alias);
    }
    return NULL;
}

int wan_manager_get_all_wans(wan_manager_t *manager,
                              wan_config_t *wans, int max_count) {
    if (manager != NULL && manager->get_all_wans != NULL) {
        return manager->get_all_wans(manager, wans, max_count);
    }
    return 0;
}

int wan_manager_get_active_wan(wan_manager_t *manager, char *buf, size_t len) {
    if (manager != NULL && manager->get_active_wan != NULL) {
        return manager->get_active_wan(manager, buf, len);
    }
    return CFDDNS_ERR_NULL_POINTER;
}

int wan_manager_get_online_wans(wan_manager_t *manager,
                                 wan_config_t *wans, int max_count) {
    if (manager != NULL && manager->get_online_wans != NULL) {
        return manager->get_online_wans(manager, wans, max_count);
    }
    return 0;
}

int wan_manager_check_failover(wan_manager_t *manager) {
    if (manager != NULL && manager->check_failover != NULL) {
        return manager->check_failover(manager);
    }
    return CFDDNS_ERR_NULL_POINTER;
}

int wan_manager_switch_wan(wan_manager_t *manager, const char *wan_name) {
    if (manager != NULL && manager->switch_wan != NULL) {
        return manager->switch_wan(manager, wan_name);
    }
    return CFDDNS_ERR_NULL_POINTER;
}

void wan_manager_set_ip_provider(wan_manager_t *manager, ip_provider_t *provider) {
    if (manager != NULL && manager->set_ip_provider != NULL) {
        manager->set_ip_provider(manager, provider);
    }
}

void wan_manager_set_http_client(wan_manager_t *manager, http_client_t *client) {
    if (manager != NULL && manager->set_http_client != NULL) {
        manager->set_http_client(manager, client);
    }
}

/* ========== WAN Type Name ========== */

static const char *wan_type_names[] = {
    "DHCP", "Static", "PPPoE", "Auto"
};

const char *wan_type_name(wan_type_t type) {
    if (type >= 0 && type < (int)(sizeof(wan_type_names) / sizeof(wan_type_names[0]))) {
        return wan_type_names[type];
    }
    return "Unknown";
}

/* ========== CLI Show Commands ========== */

void wan_manager_show_status(wan_manager_t *manager) {
    wan_manager_handle_t *h = (wan_manager_handle_t *)manager->handle;
    if (h == NULL) {
        printf("WAN Manager: Not initialized\n");
        return;
    }

    /* Header */
    printf("\n");
    printf("WAN Interface Status\n");
    printf("====================\n");

    /* Table header */
    printf("%-12s %-20s %-8s %-6s %-12s %-15s %-15s\n",
           "Interface", "Alias", "Type", "Status", "Priority", "IPv4", "IPv6");
    printf("%-12s %-20s %-8s %-6s %-12s %-15s %-15s\n",
           "--------", "-----", "----", "------", "--------", "----", "----");

    /* WAN entries */
    for (int i = 0; i < h->config.wan_count; i++) {
        wan_config_t *wan = &h->config.wans[i];
        printf("%-12s %-20s %-8s %-6s %-12d %-15s %-15s\n",
               wan->name,
               wan->alias,
               wan_type_name(wan->type),
               wan->is_online ? "Online" : (wan->enabled ? "Offline" : "Disabled"),
               wan->priority,
               wan->current_ipv4[0] ? wan->current_ipv4 : "--",
               wan->current_ipv6[0] ? wan->current_ipv6 : "--");
    }

    printf("\n");
    printf("Active WAN: %s\n", h->active_wan[0] ? h->active_wan : "None");
    printf("Failover: %s\n", h->config.failover_enabled ? "Enabled" : "Disabled");
    printf("Total WANs: %d\n", h->config.wan_count);
    printf("\n");
}

void wan_manager_show_config(wan_manager_t *manager, const char *wan_name) {
    wan_manager_handle_t *h = (wan_manager_handle_t *)manager->handle;
    if (h == NULL) {
        printf("WAN Manager: Not initialized\n");
        return;
    }

    if (wan_name != NULL) {
        /* Show specific WAN */
        wan_config_t *wan = find_wan_by_name_or_alias(h, wan_name);
        if (wan == NULL) {
            printf("WAN '%s' not found\n", wan_name);
            return;
        }

        printf("\n");
        printf("WAN Interface: %s\n", wan->name);
        printf("===============%.*s\n", (int)strlen(wan->name), "===============");
        printf("  Alias:              %s\n", wan->alias);
        printf("  Type:               %s\n", wan_type_name(wan->type));
        printf("  Enabled:            %s\n", wan->enabled ? "Yes" : "No");
        printf("  Priority:           %d\n", wan->priority);
        printf("  Status:             %s\n", wan->is_online ? "Online" : "Offline");
        printf("\n");
        printf("  IP Detection:\n");
        printf("    IPv4 Detect:      %s\n", wan->detect_ipv4 ? "Yes" : "No");
        printf("    IPv6 Detect:      %s\n", wan->detect_ipv6 ? "Yes" : "No");
        printf("    External Detect:  %s\n", wan->use_external_detect ? "Yes" : "No");
        printf("    Local Read:       %s\n", wan->use_local_read ? "Yes" : "No");
        printf("\n");
        printf("  Current IPs:\n");
        printf("    IPv4:             %s\n", wan->current_ipv4[0] ? wan->current_ipv4 : "Not detected");
        printf("    IPv6:             %s\n", wan->current_ipv6[0] ? wan->current_ipv6 : "Not detected");
        printf("\n");
        printf("  Endpoints:\n");
        printf("    IPv4 Endpoint:    %s\n", wan->ipv4_endpoint[0] ? wan->ipv4_endpoint : "Default");
        printf("    IPv6 Endpoint:    %s\n", wan->ipv6_endpoint[0] ? wan->ipv6_endpoint : "Default");
        printf("\n");
        printf("  Health:\n");
        printf("    Check Interval:   %d seconds\n", wan->check_interval);
        printf("    Failover:         %s\n", wan->failover_enabled ? "Enabled" : "Disabled");
        printf("    Failed Checks:    %d\n", wan->failed_checks);
        printf("\n");
    } else {
        /* Show all WANs */
        printf("\n");
        printf("WAN Configuration Summary\n");
        printf("========================\n");
        printf("  Total Interfaces: %d\n", h->config.wan_count);
        printf("  Default WAN:      %s\n", h->config.default_wan[0] ? h->config.default_wan : "None");
        printf("  Failover:         %s\n", h->config.failover_enabled ? "Enabled" : "Disabled");
        printf("  Threshold:        %d failed checks\n", h->config.failover_threshold);
        printf("  Check Interval:   %d seconds\n", h->config.check_interval);
        printf("\n");

        for (int i = 0; i < h->config.wan_count; i++) {
            wan_config_t *wan = &h->config.wans[i];
            printf("  [%d] %s (%s)\n", i + 1, wan->name, wan->alias);
            printf("      Type: %s, Priority: %d, Status: %s\n",
                   wan_type_name(wan->type), wan->priority,
                   wan->is_online ? "Online" : "Offline");
        }
        printf("\n");
    }
}

int wan_manager_format_status(wan_manager_t *manager, char *buf, size_t len) {
    wan_manager_handle_t *h = (wan_manager_handle_t *)manager->handle;
    if (h == NULL || buf == NULL) return 0;

    int written = 0;
    written += snprintf(buf + written, len - written, "WAN Status:\n");

    for (int i = 0; i < h->config.wan_count && written < (int)len; i++) {
        wan_config_t *wan = &h->config.wans[i];
        written += snprintf(buf + written, len - written,
                           "  %s: %s (IPv4: %s, IPv6: %s)\n",
                           wan->name,
                           wan->is_online ? "Online" : "Offline",
                           wan->current_ipv4[0] ? wan->current_ipv4 : "N/A",
                           wan->current_ipv6[0] ? wan->current_ipv6 : "N/A");
    }

    return written;
}

void wan_manager_show(wan_manager_t *manager, wan_show_mode_t mode, const char *arg) {
    switch (mode) {
        case WAN_SHOW_ALL:
            wan_manager_show_status(manager);
            wan_manager_show_config(manager, NULL);
            break;
        case WAN_SHOW_STATUS:
            wan_manager_show_status(manager);
            break;
        case WAN_SHOW_CONFIG:
            wan_manager_show_config(manager, arg);
            break;
        case WAN_SHOW_IP:
            /* Show IP addresses only */
            {
                wan_manager_handle_t *h = (wan_manager_handle_t *)manager->handle;
                if (h == NULL) {
                    printf("WAN Manager: Not initialized\n");
                    return;
                }
                printf("\n");
                printf("WAN IP Addresses\n");
                printf("================\n");
                printf("%-12s %-15s %-40s\n", "Interface", "IPv4", "IPv6");
                printf("%-12s %-15s %-40s\n", "--------", "----", "----");
                for (int i = 0; i < h->config.wan_count; i++) {
                    wan_config_t *wan = &h->config.wans[i];
                    printf("%-12s %-15s %-40s\n",
                           wan->name,
                           wan->current_ipv4[0] ? wan->current_ipv4 : "--",
                           wan->current_ipv6[0] ? wan->current_ipv6 : "--");
                }
                printf("\n");
            }
            break;
        case WAN_SHOW_INTERFACE:
            wan_manager_show_config(manager, arg);
            break;
        default:
            wan_manager_show_status(manager);
            break;
    }
}