/**
 * @file http_client.c
 * @brief HTTP client abstraction layer implementation
 */

#include "hal/http_client.h"
#include "hal/platform.h"
#include "common/macros.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <pthread.h>

/* ========== Implementation Registry ========== */

#define MAX_HTTP_IMPLS 8

static const http_client_impl_t *g_http_impls[MAX_HTTP_IMPLS] = {NULL};
static int g_http_impl_count = 0;
static const http_client_impl_t *g_default_impl = NULL;
static pthread_mutex_t g_http_registry_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_once_t g_http_auto_register_once = PTHREAD_ONCE_INIT;

static void http_client_auto_register_impls(void) {
    extern int http_client_register_libcurl(void);
    extern int http_client_register_mbedtls(void);

#ifdef HTTP_IMPL_LIBCURL
    http_client_register_libcurl();
#endif
#ifdef HTTP_IMPL_MBEDTLS
    http_client_register_mbedtls();
#endif
#ifndef HTTP_IMPL_MBEDTLS
    http_client_register_libcurl();
#endif
}

static int set_option_string(const char **target, const char *value) {
    char *copy = NULL;

    if (value != NULL) {
        copy = strdup(value);
        if (copy == NULL) return CFDDNS_ERR_OUT_OF_MEMORY;
    }

    free((void *)*target);
    *target = copy;
    return CFDDNS_OK;
}

/* ========== Header Utilities ========== */

http_header_t *http_header_list_create(void) {
    return NULL;
}

http_header_t *http_header_list_add(http_header_t *headers,
                                     const char *key,
                                     const char *value) {
    if (key == NULL || value == NULL) return headers;

    http_header_t *header = malloc(sizeof(http_header_t));
    if (header == NULL) return headers;

    header->key = strdup(key);
    header->value = strdup(value);
    if (header->key == NULL || header->value == NULL) {
        CFDDNS_FREE(header->key);
        CFDDNS_FREE(header->value);
        free(header);
        return headers;
    }
    header->next = headers;

    return header;
}

void http_header_list_free(http_header_t *headers) {
    while (headers != NULL) {
        http_header_t *next = headers->next;
        CFDDNS_FREE(headers->key);
        CFDDNS_FREE(headers->value);
        CFDDNS_FREE(headers);
        headers = next;
    }
}

/* Find header in linked list */
static const char *http_header_find(const http_header_t *headers, const char *key) {
    if (headers == NULL || key == NULL) return NULL;

    const http_header_t *h = headers;
    while (h != NULL) {
        if (strcasecmp(h->key, key) == 0) {
            return h->value;
        }
        h = h->next;
    }
    return NULL;
}

/* Deep copy header list */
static http_header_t *http_header_list_copy(const http_header_t *headers) {
    http_header_t *copy = NULL;
    const http_header_t *h = headers;
    while (h != NULL) {
        copy = http_header_list_add(copy, h->key, h->value);
        h = h->next;
    }
    return copy;
}

/* ========== Response Utilities ========== */

void http_response_init(http_response_t *response) {
    if (response == NULL) return;
    memset(response, 0, sizeof(http_response_t));
}

void http_response_free(http_response_t *response) {
    if (response == NULL) return;
    CFDDNS_FREE(response->body);
    CFDDNS_FREE(response->content_type);
    CFDDNS_FREE(response->error_msg);
    http_header_list_free(response->headers);
    memset(response, 0, sizeof(http_response_t));
}

bool http_response_is_success(const http_response_t *response) {
    if (response == NULL) return false;
    return (response->status_code >= 200 && response->status_code < 300);
}

const char *http_response_get_header(const http_response_t *response, const char *key) {
    if (response == NULL || key == NULL) return NULL;

    http_header_t *h = response->headers;
    while (h != NULL) {
        if (strcasecmp(h->key, key) == 0) {
            return h->value;
        }
        h = h->next;
    }
    return NULL;
}

/* ========== Implementation Registration ========== */

int http_client_register_impl(const http_client_impl_t *impl) {
    if (impl == NULL) return CFDDNS_ERR_NULL_POINTER;

    pthread_mutex_lock(&g_http_registry_mutex);

    for (int i = 0; i < g_http_impl_count; i++) {
        if (strcasecmp(g_http_impls[i]->name, impl->name) == 0) {
            pthread_mutex_unlock(&g_http_registry_mutex);
            return CFDDNS_OK;
        }
    }

    if (g_http_impl_count >= MAX_HTTP_IMPLS) {
        pthread_mutex_unlock(&g_http_registry_mutex);
        return CFDDNS_ERR_OUT_OF_MEMORY;
    }

    g_http_impls[g_http_impl_count++] = impl;

    /* Set first registered implementation as default */
    if (g_default_impl == NULL) {
        g_default_impl = impl;
    }

    pthread_mutex_unlock(&g_http_registry_mutex);

    return CFDDNS_OK;
}

int http_client_get_impl_names(char buf[][32], int max_count) {
    pthread_mutex_lock(&g_http_registry_mutex);
    int count = CFDDNS_MIN(g_http_impl_count, max_count);
    for (int i = 0; i < count; i++) {
        CFDDNS_STRNCPY(buf[i], g_http_impls[i]->name, 32);
    }
    pthread_mutex_unlock(&g_http_registry_mutex);
    return count;
}

static const http_client_impl_t *find_impl(const char *name) {
    pthread_mutex_lock(&g_http_registry_mutex);

    if (name == NULL) {
        const http_client_impl_t *impl = g_default_impl;
        pthread_mutex_unlock(&g_http_registry_mutex);
        return impl;
    }

    for (int i = 0; i < g_http_impl_count; i++) {
        if (strcasecmp(g_http_impls[i]->name, name) == 0) {
            const http_client_impl_t *impl = g_http_impls[i];
            pthread_mutex_unlock(&g_http_registry_mutex);
            return impl;
        }
    }

    const http_client_impl_t *impl = g_default_impl;
    pthread_mutex_unlock(&g_http_registry_mutex);
    return impl;
}

/* ========== Client Creation/Destruction ========== */

http_client_t *http_client_create(void) {
    return http_client_create_with_impl(NULL);
}

http_client_t *http_client_create_with_impl(const char *impl_name) {
    /* Auto-register implementations exactly once in a thread-safe way. */
    pthread_once(&g_http_auto_register_once, http_client_auto_register_impls);

    const http_client_impl_t *impl = find_impl(impl_name);
    if (impl == NULL) return NULL;

    http_client_t *client = malloc(sizeof(http_client_t));
    if (client == NULL) return NULL;

    memset(client, 0, sizeof(http_client_t));
    client->impl = impl;

    /* Create implementation handle */
    client->handle = impl->create();
    if (client->handle == NULL) {
        free(client);
        return NULL;
    }

    /* Set default options */
    client->options.timeout_ms = HTTP_DEFAULT_TIMEOUT_MS;
    client->options.max_redirects = HTTP_MAX_REDIRECTS;
    client->options.follow_redirects = true;
    client->options.verify_ssl = true;

    return client;
}

void http_client_destroy(http_client_t *client) {
    if (client == NULL) return;

    if (client->impl != NULL && client->impl->destroy != NULL) {
        client->impl->destroy(client->handle);
    }

    CFDDNS_FREE(client->auth_token);
    http_header_list_free(client->default_headers);
    free((void *)client->options.ca_cert_path);
    free((void *)client->options.proxy);
    free((void *)client->options.proxy_user);
    free((void *)client->options.proxy_pass);
    free((void *)client->options.bind_interface);
    free((void *)client->options.bind_address);
    free(client);
}

/* ========== Request Methods ========== */

int http_client_request(http_client_t *client,
                        const char *method,
                        const char *url,
                        const char *body,
                        const http_header_t *headers,
                        http_response_t *response) {
    if (client == NULL || url == NULL || response == NULL) {
        return CFDDNS_ERR_NULL_POINTER;
    }

    if (client->impl == NULL || client->impl->request == NULL) {
        return CFDDNS_ERR_NOT_IMPLEMENTED;
    }

    http_response_init(response);

    /* Build header list - start by copying default headers (deep copy) */
    http_header_t *req_headers = http_header_list_copy(client->default_headers);

    /* Add additional headers from caller first */
    const http_header_t *h = headers;
    while (h != NULL) {
        req_headers = http_header_list_add(req_headers, h->key, h->value);
        h = h->next;
    }

    /* Add auth header if token is set and Authorization not already provided by caller */
    if (client->auth_token != NULL) {
        const char *existing_auth = http_header_find(req_headers, "Authorization");
        if (existing_auth == NULL) {
            /* Add "Bearer " prefix */
            char auth_header[512];
            snprintf(auth_header, sizeof(auth_header), "Bearer %s", client->auth_token);
            req_headers = http_header_list_add(req_headers, "Authorization", auth_header);
        }
    }

    /* Add Content-Type if body is provided and not already set */
    if (body != NULL) {
        const char *ct = http_header_find(req_headers, "Content-Type");
        if (ct == NULL) {
            req_headers = http_header_list_add(req_headers, "Content-Type", "application/json");
        }
    }

    size_t body_len = (body != NULL) ? strlen(body) : 0;

    int result = client->impl->request(client->handle, method, url,
                                        body, body_len, req_headers,
                                        &client->options, response);

    /* Free only the temporary header list we built (not client->default_headers) */
    http_header_list_free(req_headers);
    return result;
}

int http_client_get(http_client_t *client,
                    const char *url,
                    const http_header_t *headers,
                    http_response_t *response) {
    return http_client_request(client, HTTP_METHOD_GET, url, NULL, headers, response);
}

int http_client_post(http_client_t *client,
                     const char *url,
                     const char *body,
                     const char *content_type,
                     const http_header_t *headers,
                     http_response_t *response) {
    /* Merge caller headers with Content-Type */
    http_header_t *merged_headers = http_header_list_copy(headers);
    if (content_type != NULL) {
        merged_headers = http_header_list_add(merged_headers, "Content-Type", content_type);
    }

    int result = http_client_request(client, HTTP_METHOD_POST, url, body, merged_headers, response);

    http_header_list_free(merged_headers);
    return result;
}

int http_client_put(http_client_t *client,
                    const char *url,
                    const char *body,
                    const char *content_type,
                    const http_header_t *headers,
                    http_response_t *response) {
    /* Merge caller headers with Content-Type */
    http_header_t *merged_headers = http_header_list_copy(headers);
    if (content_type != NULL) {
        merged_headers = http_header_list_add(merged_headers, "Content-Type", content_type);
    }

    int result = http_client_request(client, HTTP_METHOD_PUT, url, body, merged_headers, response);

    http_header_list_free(merged_headers);
    return result;
}

int http_client_delete(http_client_t *client,
                       const char *url,
                       const http_header_t *headers,
                       http_response_t *response) {
    return http_client_request(client, HTTP_METHOD_DELETE, url, NULL, headers, response);
}

/* ========== Configuration Methods ========== */

int http_client_set_auth_token(http_client_t *client, const char *token) {
    if (client == NULL) return CFDDNS_ERR_NULL_POINTER;

    CFDDNS_FREE(client->auth_token);
    if (token != NULL) {
        client->auth_token = strdup(token);
        if (client->auth_token == NULL) return CFDDNS_ERR_OUT_OF_MEMORY;
    }
    return CFDDNS_OK;
}

int http_client_set_timeout(http_client_t *client, int timeout_ms) {
    if (client == NULL) return CFDDNS_ERR_NULL_POINTER;

    client->options.timeout_ms = timeout_ms;

    if (client->impl != NULL && client->impl->set_timeout != NULL) {
        return client->impl->set_timeout(client->handle, timeout_ms);
    }
    return CFDDNS_OK;
}

int http_client_set_ssl_verify(http_client_t *client, bool verify) {
    if (client == NULL) return CFDDNS_ERR_NULL_POINTER;

    client->options.verify_ssl = verify;
    return CFDDNS_OK;
}

int http_client_set_proxy(http_client_t *client,
                          const char *proxy,
                          const char *username,
                          const char *password) {
    if (client == NULL) return CFDDNS_ERR_NULL_POINTER;

    int result = set_option_string(&client->options.proxy, proxy);
    if (CFDDNS_FAILED(result)) return result;

    result = set_option_string(&client->options.proxy_user, username);
    if (CFDDNS_FAILED(result)) return result;

    result = set_option_string(&client->options.proxy_pass, password);
    if (CFDDNS_FAILED(result)) return result;

    return CFDDNS_OK;
}

int http_client_set_bind_interface(http_client_t *client, const char *interface) {
    if (client == NULL) return CFDDNS_ERR_NULL_POINTER;

    return set_option_string(&client->options.bind_interface, interface);
}

int http_client_set_bind_address(http_client_t *client, const char *address) {
    if (client == NULL) return CFDDNS_ERR_NULL_POINTER;

    return set_option_string(&client->options.bind_address, address);
}