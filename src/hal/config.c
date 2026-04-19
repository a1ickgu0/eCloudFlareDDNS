/**
 * @file config.c
 * @brief Configuration abstraction layer implementation
 */

#include "hal/config.h"
#include "hal/platform.h"
#include "common/macros.h"
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

/* ========== Implementation Registry ========== */

#define MAX_CONFIG_IMPLS 4

static const config_impl_t *g_config_impls[MAX_CONFIG_IMPLS] = {NULL};
static int g_config_impl_count = 0;
static const config_impl_t *g_default_impl = NULL;
static pthread_mutex_t g_config_registry_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_once_t g_config_auto_register_once = PTHREAD_ONCE_INIT;

static void config_auto_register_impls(void) {
    extern int config_register_json(void);
    config_register_json();
}

/* ========== Implementation Registration ========== */

int config_register_impl(const config_impl_t *impl) {
    if (impl == NULL) return CFDDNS_ERR_NULL_POINTER;

    pthread_mutex_lock(&g_config_registry_mutex);

    for (int i = 0; i < g_config_impl_count; i++) {
        if (strcasecmp(g_config_impls[i]->name, impl->name) == 0) {
            pthread_mutex_unlock(&g_config_registry_mutex);
            return CFDDNS_OK;
        }
    }

    if (g_config_impl_count >= MAX_CONFIG_IMPLS) {
        pthread_mutex_unlock(&g_config_registry_mutex);
        return CFDDNS_ERR_OUT_OF_MEMORY;
    }

    g_config_impls[g_config_impl_count++] = impl;

    if (g_default_impl == NULL) {
        g_default_impl = impl;
    }

    pthread_mutex_unlock(&g_config_registry_mutex);

    return CFDDNS_OK;
}

static const config_impl_t *find_impl(const char *name) {
    pthread_mutex_lock(&g_config_registry_mutex);

    if (name == NULL) {
        const config_impl_t *impl = g_default_impl;
        pthread_mutex_unlock(&g_config_registry_mutex);
        return impl;
    }

    for (int i = 0; i < g_config_impl_count; i++) {
        if (strcasecmp(g_config_impls[i]->name, name) == 0) {
            const config_impl_t *impl = g_config_impls[i];
            pthread_mutex_unlock(&g_config_registry_mutex);
            return impl;
        }
    }

    const config_impl_t *impl = g_default_impl;
    pthread_mutex_unlock(&g_config_registry_mutex);
    return impl;
}

static const config_impl_t *find_impl_by_format(config_format_t format) {
    const char *name = NULL;

    switch (format) {
        case CONFIG_FORMAT_JSON:  name = "json";  break;
        case CONFIG_FORMAT_INI:   name = "ini";   break;
        case CONFIG_FORMAT_TOML:  name = "toml";  break;
        default: break;
    }

    return find_impl(name);
}

/* ========== Config Creation/Destruction ========== */

config_t *config_create(void) {
    return config_create_with_format(CONFIG_FORMAT_JSON);
}

config_t *config_create_with_format(config_format_t format) {
    /* Auto-register implementations once in a thread-safe way. */
    pthread_once(&g_config_auto_register_once, config_auto_register_impls);

    const config_impl_t *impl = find_impl_by_format(format);
    if (impl == NULL) impl = g_default_impl;
    if (impl == NULL) return NULL;

    config_t *cfg = malloc(sizeof(config_t));
    if (cfg == NULL) return NULL;

    memset(cfg, 0, sizeof(config_t));
    cfg->impl = impl;
    cfg->options = config_get_default_options();

    /* Create implementation handle */
    cfg->handle = impl->create();
    if (cfg->handle == NULL) {
        free(cfg);
        return NULL;
    }

    return cfg;
}

config_t *config_create_with_impl(const char *impl_name) {
    /* Auto-register implementations once in a thread-safe way. */
    pthread_once(&g_config_auto_register_once, config_auto_register_impls);

    const config_impl_t *impl = find_impl(impl_name);
    if (impl == NULL) return NULL;

    config_t *cfg = malloc(sizeof(config_t));
    if (cfg == NULL) return NULL;

    memset(cfg, 0, sizeof(config_t));
    cfg->impl = impl;
    cfg->options = config_get_default_options();

    cfg->handle = impl->create();
    if (cfg->handle == NULL) {
        free(cfg);
        return NULL;
    }

    return cfg;
}

void config_destroy(config_t *cfg) {
    if (cfg == NULL) return;

    if (cfg->impl != NULL && cfg->impl->destroy != NULL) {
        cfg->impl->destroy(cfg->handle);
    }

    CFDDNS_FREE(cfg->path);
    free(cfg);
}

/* ========== Load/Save ========== */

int config_load_string(config_t *cfg, const char *str) {
    return config_load_string_len(cfg, str, 0);
}

int config_load_string_len(config_t *cfg, const char *str, size_t len) {
    if (cfg == NULL || str == NULL) return CFDDNS_ERR_NULL_POINTER;
    if (cfg->impl == NULL || cfg->impl->load_string == NULL) {
        return CFDDNS_ERR_NOT_IMPLEMENTED;
    }

    cfg->modified = false;
    return cfg->impl->load_string(cfg->handle, str, len);
}

int config_load_file(config_t *cfg, const char *path) {
    if (cfg == NULL || path == NULL) return CFDDNS_ERR_NULL_POINTER;
    if (cfg->impl == NULL || cfg->impl->load_file == NULL) {
        return CFDDNS_ERR_NOT_IMPLEMENTED;
    }

    int result = cfg->impl->load_file(cfg->handle, path);
    if (CFDDNS_SUCCEEDED(result)) {
        cfg->path = strdup(path);
        cfg->modified = false;
    }

    return result;
}

int config_save_string(config_t *cfg, char **str) {
    if (cfg == NULL || str == NULL) return CFDDNS_ERR_NULL_POINTER;
    if (cfg->impl == NULL || cfg->impl->save_string == NULL) {
        return CFDDNS_ERR_NOT_IMPLEMENTED;
    }

    size_t len = 0;
    return cfg->impl->save_string(cfg->handle, str, &len);
}

int config_save_file(config_t *cfg, const char *path) {
    if (cfg == NULL) return CFDDNS_ERR_NULL_POINTER;
    if (cfg->impl == NULL || cfg->impl->save_file == NULL) {
        return CFDDNS_ERR_NOT_IMPLEMENTED;
    }

    const char *save_path = (path != NULL) ? path : cfg->path;
    if (save_path == NULL) return CFDDNS_ERR_INVALID_ARG;

    int result = cfg->impl->save_file(cfg->handle, save_path);
    if (CFDDNS_SUCCEEDED(result)) {
        cfg->modified = false;
    }

    return result;
}

/* ========== Query Operations ========== */

int config_has(config_t *cfg, const char *key) {
    if (cfg == NULL || key == NULL) return CFDDNS_ERR_NULL_POINTER;
    if (cfg->impl == NULL || cfg->impl->has == NULL) {
        return CFDDNS_ERR_NOT_IMPLEMENTED;
    }

    return cfg->impl->has(cfg->handle, key);
}

config_type_t config_get_type(config_t *cfg, const char *key) {
    if (cfg == NULL || key == NULL) return CONFIG_TYPE_NULL;
    if (cfg->impl == NULL || cfg->impl->get_type == NULL) {
        return CONFIG_TYPE_NULL;
    }

    return cfg->impl->get_type(cfg->handle, key);
}

/* ========== Get Operations ========== */

bool config_get_bool(config_t *cfg, const char *key, bool default_val) {
    if (cfg == NULL || key == NULL) return default_val;
    if (cfg->impl == NULL || cfg->impl->get_bool == NULL) {
        return default_val;
    }

    return cfg->impl->get_bool(cfg->handle, key, default_val);
}

int64_t config_get_int(config_t *cfg, const char *key, int64_t default_val) {
    if (cfg == NULL || key == NULL) return default_val;
    if (cfg->impl == NULL || cfg->impl->get_int == NULL) {
        return default_val;
    }

    return cfg->impl->get_int(cfg->handle, key, default_val);
}

double config_get_float(config_t *cfg, const char *key, double default_val) {
    if (cfg == NULL || key == NULL) return default_val;
    if (cfg->impl == NULL || cfg->impl->get_float == NULL) {
        return default_val;
    }

    return cfg->impl->get_float(cfg->handle, key, default_val);
}

const char *config_get_string(config_t *cfg, const char *key, const char *default_val) {
    if (cfg == NULL || key == NULL) return default_val;
    if (cfg->impl == NULL || cfg->impl->get_string == NULL) {
        return default_val;
    }

    return cfg->impl->get_string(cfg->handle, key, default_val);
}

int config_get_string_buf(config_t *cfg, const char *key, char *buf, size_t len, const char *default_val) {
    if (cfg == NULL || key == NULL || buf == NULL) return CFDDNS_ERR_NULL_POINTER;
    if (cfg->impl == NULL || cfg->impl->get_string_buf == NULL) {
        if (default_val != NULL) {
            CFDDNS_STRNCPY(buf, default_val, len);
            return CFDDNS_OK;
        }
        return CFDDNS_ERR_NOT_IMPLEMENTED;
    }

    return cfg->impl->get_string_buf(cfg->handle, key, buf, len, default_val);
}

/* ========== Set Operations ========== */

int config_set_bool(config_t *cfg, const char *key, bool val) {
    if (cfg == NULL || key == NULL) return CFDDNS_ERR_NULL_POINTER;
    if (cfg->impl == NULL || cfg->impl->set_bool == NULL) {
        return CFDDNS_ERR_NOT_IMPLEMENTED;
    }

    cfg->modified = true;
    return cfg->impl->set_bool(cfg->handle, key, val);
}

int config_set_int(config_t *cfg, const char *key, int64_t val) {
    if (cfg == NULL || key == NULL) return CFDDNS_ERR_NULL_POINTER;
    if (cfg->impl == NULL || cfg->impl->set_int == NULL) {
        return CFDDNS_ERR_NOT_IMPLEMENTED;
    }

    cfg->modified = true;
    return cfg->impl->set_int(cfg->handle, key, val);
}

int config_set_float(config_t *cfg, const char *key, double val) {
    if (cfg == NULL || key == NULL) return CFDDNS_ERR_NULL_POINTER;
    if (cfg->impl == NULL || cfg->impl->set_float == NULL) {
        return CFDDNS_ERR_NOT_IMPLEMENTED;
    }

    cfg->modified = true;
    return cfg->impl->set_float(cfg->handle, key, val);
}

int config_set_string(config_t *cfg, const char *key, const char *val) {
    if (cfg == NULL || key == NULL) return CFDDNS_ERR_NULL_POINTER;
    if (cfg->impl == NULL || cfg->impl->set_string == NULL) {
        return CFDDNS_ERR_NOT_IMPLEMENTED;
    }

    cfg->modified = true;
    return cfg->impl->set_string(cfg->handle, key, val);
}

int config_delete(config_t *cfg, const char *key) {
    if (cfg == NULL || key == NULL) return CFDDNS_ERR_NULL_POINTER;
    if (cfg->impl == NULL || cfg->impl->delete_key == NULL) {
        return CFDDNS_ERR_NOT_IMPLEMENTED;
    }

    cfg->modified = true;
    return cfg->impl->delete_key(cfg->handle, key);
}

/* ========== Array Operations ========== */

int config_get_array_size(config_t *cfg, const char *key) {
    if (cfg == NULL || key == NULL) return 0;
    if (cfg->impl == NULL || cfg->impl->get_array_size == NULL) {
        return 0;
    }

    return cfg->impl->get_array_size(cfg->handle, key);
}

const config_item_t *config_get_array_item(config_t *cfg, const char *key, int index) {
    if (cfg == NULL || key == NULL) return NULL;
    if (cfg->impl == NULL || cfg->impl->get_array_item == NULL) {
        return NULL;
    }

    return cfg->impl->get_array_item(cfg->handle, key, index);
}

/* ========== Utility Functions ========== */

int config_get_default_path(char *buf, size_t len) {
    return fs_get_config_dir(buf, len);
}

config_options_t config_get_default_options(void) {
    config_options_t options;
    memset(&options, 0, sizeof(options));
    options.allow_include = true;
    options.allow_env_vars = false;
    options.allow_comments = true;
    options.strict_mode = false;
    options.auto_save = false;
    CFDDNS_STRNCPY(options.env_prefix, "CFDDNS_", sizeof(options.env_prefix));
    return options;
}

int config_set_options(config_t *cfg, const config_options_t *options) {
    if (cfg == NULL || options == NULL) return CFDDNS_ERR_NULL_POINTER;
    cfg->options = *options;
    return CFDDNS_OK;
}