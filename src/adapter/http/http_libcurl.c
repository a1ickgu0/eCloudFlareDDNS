/**
 * @file http_libcurl.c
 * @brief libcurl HTTP client implementation
 */

#include "hal/http_client.h"
#include "hal/platform.h"
#include "common/types.h"
#include "common/errors.h"
#include "common/macros.h"

#include <curl/curl.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <pthread.h>

/* ========== libcurl Context ========== */

typedef struct {
    CURL *curl;
    char error_buffer[CURL_ERROR_SIZE];
    struct curl_slist *headers;
} libcurl_handle_t;

/* ========== Response Buffer ========== */

typedef struct {
    char *data;
    size_t size;
    size_t capacity;
} response_buffer_t;

static pthread_once_t g_curl_global_init_once = PTHREAD_ONCE_INIT;
static int g_curl_global_init_rc = CURLE_OK;

static void libcurl_global_init_once(void) {
    g_curl_global_init_rc = curl_global_init(CURL_GLOBAL_DEFAULT);
}

static size_t write_callback(void *contents, size_t size, size_t nmemb, void *userp) {
    size_t total_size = size * nmemb;
    response_buffer_t *buf = (response_buffer_t *)userp;
    size_t required_size = buf->size + total_size + 1;

    /* Check if we need to expand buffer */
    if (required_size > buf->capacity) {
        size_t new_capacity = buf->capacity == 0 ? HTTP_MAX_RESPONSE_SIZE :
                              CFDDNS_MIN(buf->capacity * 2, HTTP_MAX_RESPONSE_SIZE);
        if (required_size > new_capacity) {
            new_capacity = required_size;
        }
        if (new_capacity > HTTP_MAX_RESPONSE_SIZE) {
            return 0;  /* Too large */
        }
        char *new_data = realloc(buf->data, new_capacity);
        if (new_data == NULL) {
            return 0;  /* Out of memory */
        }
        buf->data = new_data;
        buf->capacity = new_capacity;
    }

    memcpy(buf->data + buf->size, contents, total_size);
    buf->size += total_size;
    buf->data[buf->size] = '\0';

    return total_size;
}

static size_t header_callback(char *buffer, size_t size, size_t nitems, void *userp) {
    size_t total_size = size * nitems;
    http_response_t *response = (http_response_t *)userp;

    /* Parse header */
    char *colon = strchr(buffer, ':');
    if (colon != NULL) {
        *colon = '\0';
        char *key = buffer;
        char *value = colon + 1;

        /* Trim whitespace */
        while (*value == ' ' || *value == '\t') value++;
        size_t value_len = strlen(value);
        while (value_len > 0 && (value[value_len - 1] == '\r' ||
                                 value[value_len - 1] == '\n')) {
            value[--value_len] = '\0';
        }

        response->headers = http_header_list_add(response->headers, key, value);

        /* Store content-type separately */
        if (strcasecmp(key, "Content-Type") == 0) {
            response->content_type = strdup(value);
        }
    }

    return total_size;
}

/* ========== Implementation Functions ========== */

static void *libcurl_create(void) {
    libcurl_handle_t *handle = malloc(sizeof(libcurl_handle_t));
    if (handle == NULL) return NULL;

    memset(handle, 0, sizeof(libcurl_handle_t));

    /* Initialize curl globally exactly once per process. */
    pthread_once(&g_curl_global_init_once, libcurl_global_init_once);
    if (g_curl_global_init_rc != CURLE_OK) {
        free(handle);
        return NULL;
    }

    handle->curl = curl_easy_init();
    if (handle->curl == NULL) {
        free(handle);
        return NULL;
    }

    return handle;
}

static void libcurl_destroy(void *handle) {
    if (handle == NULL) return;

    libcurl_handle_t *lh = (libcurl_handle_t *)handle;

    if (lh->headers != NULL) {
        curl_slist_free_all(lh->headers);
    }

    if (lh->curl != NULL) {
        curl_easy_cleanup(lh->curl);
    }

    free(lh);
}

static int libcurl_request(void *handle,
                           const char *method,
                           const char *url,
                           const char *body,
                           size_t body_len,
                           const http_header_t *headers,
                           const http_request_options_t *options,
                           http_response_t *response) {
    libcurl_handle_t *lh = (libcurl_handle_t *)handle;
    if (lh == NULL || lh->curl == NULL) return CFDDNS_ERR_NULL_POINTER;

    CURL *curl = lh->curl;

    /* Reset curl handle */
    curl_easy_reset(curl);

    /* Set error buffer */
    curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, lh->error_buffer);

    /* Set URL */
    curl_easy_setopt(curl, CURLOPT_URL, url);

    /* Set method */
    if (strcmp(method, HTTP_METHOD_GET) == 0) {
        curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    } else if (strcmp(method, HTTP_METHOD_POST) == 0) {
        curl_easy_setopt(curl, CURLOPT_POST, 1L);
    } else if (strcmp(method, HTTP_METHOD_PUT) == 0) {
        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, HTTP_METHOD_PUT);
    } else if (strcmp(method, HTTP_METHOD_DELETE) == 0) {
        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, HTTP_METHOD_DELETE);
    } else if (strcmp(method, HTTP_METHOD_PATCH) == 0) {
        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, HTTP_METHOD_PATCH);
    }

    /* Set body */
    if (body != NULL && body_len > 0) {
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, body_len);
    }

    /* Set headers */
    struct curl_slist *curl_headers = NULL;
    const http_header_t *h = headers;
    while (h != NULL) {
        char header_line[512];
        snprintf(header_line, sizeof(header_line), "%s: %s", h->key, h->value);
        curl_headers = curl_slist_append(curl_headers, header_line);
        h = h->next;
    }
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, curl_headers);

    /* Set options */
    if (options != NULL) {
        curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, options->timeout_ms);
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION,
                         options->follow_redirects ? 1L : 0L);
        curl_easy_setopt(curl, CURLOPT_MAXREDIRS, options->max_redirects);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER,
                         options->verify_ssl ? 1L : 0L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST,
                         options->verify_ssl ? 2L : 0L);

        if (options->ca_cert_path != NULL) {
            curl_easy_setopt(curl, CURLOPT_CAINFO, options->ca_cert_path);
        }

        if (options->proxy != NULL) {
            curl_easy_setopt(curl, CURLOPT_PROXY, options->proxy);
            if (options->proxy_user != NULL) {
                curl_easy_setopt(curl, CURLOPT_PROXYUSERNAME, options->proxy_user);
            }
            if (options->proxy_pass != NULL) {
                curl_easy_setopt(curl, CURLOPT_PROXYPASSWORD, options->proxy_pass);
            }
        }

        /* Interface binding for multi-WAN support */
        if (options->bind_interface != NULL) {
            /* Bind to specific network interface (e.g., "eth0", "wan1") */
            curl_easy_setopt(curl, CURLOPT_INTERFACE, options->bind_interface);
        }
        if (options->bind_address != NULL) {
            /* Bind to specific source IP address */
            curl_easy_setopt(curl, CURLOPT_INTERFACE, options->bind_address);
        }
    }

    /* Set response callbacks */
    response_buffer_t resp_buf = {0};
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &resp_buf);

    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, header_callback);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, response);

    /* Perform request */
    CURLcode res = curl_easy_perform(curl);

    /* Free headers */
    if (curl_headers != NULL) {
        curl_slist_free_all(curl_headers);
    }

    /* Handle result */
    if (res != CURLE_OK) {
        response->error_msg = strdup(lh->error_buffer);
        CFDDNS_FREE(resp_buf.data);

        switch (res) {
            case CURLE_COULDNT_RESOLVE_HOST:
                return CFDDNS_ERR_NETWORK_DNS_FAILED;
            case CURLE_COULDNT_CONNECT:
                return CFDDNS_ERR_NETWORK_CONNECT_FAILED;
            case CURLE_OPERATION_TIMEDOUT:
                return CFDDNS_ERR_TIMEOUT;
            case CURLE_SSL_CONNECT_ERROR:
                return CFDDNS_ERR_NETWORK_SSL_FAILED;
            case CURLE_SSL_CACERT:
                return CFDDNS_ERR_NETWORK_SSL_CERT;
            default:
                return CFDDNS_ERR_HTTP_REQUEST_FAILED;
        }
    }

    /* Get response code */
    long status_code;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status_code);
    response->status_code = (int)status_code;
    response->body = resp_buf.data;
    response->body_len = resp_buf.size;

    /* Check for HTTP errors */
    if (status_code >= 400 && status_code < 500) {
        return CFDDNS_ERR_HTTP_4XX;
    }
    if (status_code >= 500) {
        return CFDDNS_ERR_HTTP_5XX;
    }

    return CFDDNS_OK;
}

static int libcurl_set_auth_token(void *handle, const char *token) {
    libcurl_handle_t *lh = (libcurl_handle_t *)handle;
    if (lh == NULL || lh->curl == NULL) return CFDDNS_ERR_NULL_POINTER;

    /* Build Bearer header */
    char auth_header[512];
    snprintf(auth_header, sizeof(auth_header), "Authorization: Bearer %s", token);

    /* Add to headers list */
    lh->headers = curl_slist_append(lh->headers, auth_header);

    return CFDDNS_OK;
}

static int libcurl_set_timeout(void *handle, int timeout_ms) {
    libcurl_handle_t *lh = (libcurl_handle_t *)handle;
    if (lh == NULL || lh->curl == NULL) return CFDDNS_ERR_NULL_POINTER;

    curl_easy_setopt(lh->curl, CURLOPT_TIMEOUT_MS, timeout_ms);
    return CFDDNS_OK;
}

static int libcurl_set_default_headers(void *handle, const http_header_t *headers) {
    libcurl_handle_t *lh = (libcurl_handle_t *)handle;
    if (lh == NULL) return CFDDNS_ERR_NULL_POINTER;

    /* Clear existing headers */
    if (lh->headers != NULL) {
        curl_slist_free_all(lh->headers);
        lh->headers = NULL;
    }

    /* Add new headers */
    const http_header_t *h = headers;
    while (h != NULL) {
        char header_line[512];
        snprintf(header_line, sizeof(header_line), "%s: %s", h->key, h->value);
        lh->headers = curl_slist_append(lh->headers, header_line);
        h = h->next;
    }

    return CFDDNS_OK;
}

static uint32_t libcurl_get_capabilities(void *handle) {
    (void)handle;
    return 0;  /* No special capabilities */
}

/* ========== Implementation Registration ========== */

static http_client_impl_t libcurl_impl = {
    .name              = "libcurl",
    .create            = libcurl_create,
    .destroy           = libcurl_destroy,
    .request           = libcurl_request,
    .set_auth_token    = libcurl_set_auth_token,
    .set_default_headers = libcurl_set_default_headers,
    .set_timeout       = libcurl_set_timeout,
    .get_capabilities  = libcurl_get_capabilities,
};

int http_client_register_libcurl(void) {
    return http_client_register_impl(&libcurl_impl);
}