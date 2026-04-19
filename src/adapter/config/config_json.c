/**
 * @file config_json.c
 * @brief JSON configuration implementation
 */

#include "hal/config.h"
#include "hal/json_parser.h"
#include "common/types.h"
#include "common/errors.h"
#include "common/macros.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* Forward declaration */
static config_impl_t json_config_impl;

/* ========== JSON Config Handle ========== */

typedef struct {
    json_value_t root;      /* Root JSON object */
    json_parser_t *parser;  /* JSON parser instance */
} json_config_handle_t;

/* ========== Internal Helpers ========== */

static json_value_t get_nested_item(json_parser_t *parser, json_value_t root, const char *key) {
    if (root == NULL || key == NULL) return NULL;

    /* Split key by '.' */
    json_value_t current = root;
    const char *start = key;
    const char *dot;

    while ((dot = strchr(start, '.')) != NULL) {
        char segment[256];
        size_t len = dot - start;
        if (len >= sizeof(segment)) return NULL;

        memcpy(segment, start, len);
        segment[len] = '\0';

        if (!json_is_object(parser, current)) return NULL;
        current = json_get_object_item(parser, current, segment);
        if (current == NULL) return NULL;

        start = dot + 1;
    }

    /* Final segment */
    if (!json_is_object(parser, current)) return NULL;
    return json_get_object_item(parser, current, start);
}

static json_value_t create_nested_path(json_parser_t *parser, json_value_t root, const char *key) {
    if (root == NULL || key == NULL) return NULL;

    json_value_t current = root;
    const char *start = key;
    const char *dot;

    while ((dot = strchr(start, '.')) != NULL) {
        char segment[256];
        size_t len = dot - start;
        if (len >= sizeof(segment)) return NULL;

        memcpy(segment, start, len);
        segment[len] = '\0';

        json_value_t child = json_get_object_item(parser, current, segment);
        if (child == NULL) {
            child = json_create_object(parser);
            if (child == NULL) return NULL;
            json_add_item_to_object(parser, current, segment, child);
        }

        current = child;
        start = dot + 1;
    }

    return current;
}

static int load_file_content(const char *path, char **content, size_t *len) {
    FILE *fp = fopen(path, "r");
    if (fp == NULL) return CFDDNS_ERR_FILE_OPEN_FAILED;

    /* Get file size */
    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    if (size <= 0) {
        fclose(fp);
        return CFDDNS_ERR_FILE_READ_FAILED;
    }

    *content = malloc(size + 1);
    if (*content == NULL) {
        fclose(fp);
        return CFDDNS_ERR_OUT_OF_MEMORY;
    }

    *len = fread(*content, 1, size, fp);
    (*content)[*len] = '\0';

    fclose(fp);
    return CFDDNS_OK;
}

/* ========== Implementation Functions ========== */

static void *json_config_create(void) {
    json_config_handle_t *handle = malloc(sizeof(json_config_handle_t));
    if (handle == NULL) return NULL;

    memset(handle, 0, sizeof(json_config_handle_t));

    handle->parser = json_parser_create();
    if (handle->parser == NULL) {
        free(handle);
        return NULL;
    }

    handle->root = json_create_object(handle->parser);
    if (handle->root == NULL) {
        json_parser_destroy(handle->parser);
        free(handle);
        return NULL;
    }

    return handle;
}

static void json_config_destroy(void *handle) {
    if (handle == NULL) return;

    json_config_handle_t *jh = (json_config_handle_t *)handle;

    if (jh->root != NULL) {
        json_free(jh->parser, jh->root);
    }

    if (jh->parser != NULL) {
        json_parser_destroy(jh->parser);
    }

    free(jh);
}

static int json_config_load_string(void *handle, const char *str, size_t len) {
    json_config_handle_t *jh = (json_config_handle_t *)handle;
    if (jh == NULL || str == NULL) return CFDDNS_ERR_NULL_POINTER;

    /* Free old root */
    if (jh->root != NULL) {
        json_free(jh->parser, jh->root);
    }

    jh->root = json_parse_len(jh->parser, str, len);
    if (jh->root == NULL) {
        return CFDDNS_ERR_CONFIG_PARSE_FAILED;
    }

    return CFDDNS_OK;
}

static int json_config_load_file(void *handle, const char *path) {
    json_config_handle_t *jh = (json_config_handle_t *)handle;
    if (jh == NULL || path == NULL) return CFDDNS_ERR_NULL_POINTER;

    char *content = NULL;
    size_t len = 0;

    int result = load_file_content(path, &content, &len);
    if (CFDDNS_FAILED(result)) return result;

    result = json_config_load_string(handle, content, len);

    CFDDNS_FREE(content);
    return result;
}

static int json_config_save_string(void *handle, char **str, size_t *len) {
    json_config_handle_t *jh = (json_config_handle_t *)handle;
    if (jh == NULL || str == NULL) return CFDDNS_ERR_NULL_POINTER;

    *str = json_stringify_pretty(jh->parser, jh->root, "  ");
    if (*str == NULL) return CFDDNS_ERR_OUT_OF_MEMORY;

    *len = strlen(*str);
    return CFDDNS_OK;
}

static int json_config_save_file(void *handle, const char *path) {
    json_config_handle_t *jh = (json_config_handle_t *)handle;
    if (jh == NULL || path == NULL) return CFDDNS_ERR_NULL_POINTER;

    char *str = NULL;
    size_t len = 0;

    int result = json_config_save_string(handle, &str, &len);
    if (CFDDNS_FAILED(result)) return result;

    FILE *fp = fopen(path, "w");
    if (fp == NULL) {
        CFDDNS_FREE(str);
        return CFDDNS_ERR_FILE_OPEN_FAILED;
    }

    fwrite(str, 1, len, fp);
    fclose(fp);

    CFDDNS_FREE(str);
    return CFDDNS_OK;
}

static int json_config_has(void *handle, const char *key) {
    json_config_handle_t *jh = (json_config_handle_t *)handle;
    if (jh == NULL || key == NULL) return CFDDNS_ERR_NULL_POINTER;

    json_value_t item = get_nested_item(jh->parser, jh->root, key);
    return (item != NULL) ? 1 : 0;
}

static config_type_t json_config_get_type(void *handle, const char *key) {
    json_config_handle_t *jh = (json_config_handle_t *)handle;
    if (jh == NULL || key == NULL) return CONFIG_TYPE_NULL;

    json_value_t item = get_nested_item(jh->parser, jh->root, key);
    if (item == NULL) return CONFIG_TYPE_NULL;

    json_type_t type = json_get_type(jh->parser, item);
    switch (type) {
        case JSON_TYPE_NULL:    return CONFIG_TYPE_NULL;
        case JSON_TYPE_BOOL:    return CONFIG_TYPE_BOOL;
        case JSON_TYPE_NUMBER:  return CONFIG_TYPE_INT;
        case JSON_TYPE_STRING:  return CONFIG_TYPE_STRING;
        case JSON_TYPE_ARRAY:   return CONFIG_TYPE_ARRAY;
        case JSON_TYPE_OBJECT:  return CONFIG_TYPE_OBJECT;
        default:                return CONFIG_TYPE_NULL;
    }
}

static bool json_config_get_bool(void *handle, const char *key, bool default_val) {
    json_config_handle_t *jh = (json_config_handle_t *)handle;
    if (jh == NULL || key == NULL) return default_val;

    json_value_t item = get_nested_item(jh->parser, jh->root, key);
    if (item == NULL || !json_is_bool(jh->parser, item)) return default_val;

    return json_get_bool(jh->parser, item);
}

static int64_t json_config_get_int(void *handle, const char *key, int64_t default_val) {
    json_config_handle_t *jh = (json_config_handle_t *)handle;
    if (jh == NULL || key == NULL) return default_val;

    json_value_t item = get_nested_item(jh->parser, jh->root, key);
    if (item == NULL || !json_is_number(jh->parser, item)) return default_val;

    return json_get_int(jh->parser, item);
}

static double json_config_get_float(void *handle, const char *key, double default_val) {
    json_config_handle_t *jh = (json_config_handle_t *)handle;
    if (jh == NULL || key == NULL) return default_val;

    json_value_t item = get_nested_item(jh->parser, jh->root, key);
    if (item == NULL || !json_is_number(jh->parser, item)) return default_val;

    return json_get_number(jh->parser, item);
}

static const char *json_config_get_string(void *handle, const char *key, const char *default_val) {
    json_config_handle_t *jh = (json_config_handle_t *)handle;
    if (jh == NULL || key == NULL) return default_val;

    json_value_t item = get_nested_item(jh->parser, jh->root, key);
    if (item == NULL || !json_is_string(jh->parser, item)) return default_val;

    return json_get_string(jh->parser, item);
}

static int json_config_get_string_buf(void *handle, const char *key, char *buf, size_t len, const char *default_val) {
    json_config_handle_t *jh = (json_config_handle_t *)handle;
    if (jh == NULL || key == NULL || buf == NULL) return CFDDNS_ERR_NULL_POINTER;

    json_value_t item = get_nested_item(jh->parser, jh->root, key);
    if (item == NULL || !json_is_string(jh->parser, item)) {
        if (default_val != NULL) {
            CFDDNS_STRNCPY(buf, default_val, len);
            return CFDDNS_OK;
        }
        return CFDDNS_ERR_CONFIG_MISSING_KEY;
    }

    const char *val = json_get_string(jh->parser, item);
    if (val == NULL) return CFDDNS_ERR_CONFIG_INVALID_VALUE;

    if (strlen(val) >= len) return CFDDNS_ERR_BUFFER_TOO_SMALL;

    CFDDNS_STRNCPY(buf, val, len);
    return CFDDNS_OK;
}

static int json_config_set_bool(void *handle, const char *key, bool val) {
    json_config_handle_t *jh = (json_config_handle_t *)handle;
    if (jh == NULL || key == NULL) return CFDDNS_ERR_NULL_POINTER;

    json_value_t parent = create_nested_path(jh->parser, jh->root, key);
    if (parent == NULL) return CFDDNS_ERR_CONFIG_PARSE_FAILED;

    /* Extract final key */
    const char *last_dot = strrchr(key, '.');
    const char *final_key = (last_dot != NULL) ? last_dot + 1 : key;

    json_value_t item = json_create_bool(jh->parser, val);
    if (item == NULL) return CFDDNS_ERR_OUT_OF_MEMORY;

    if (!json_add_item_to_object(jh->parser, parent, final_key, item)) {
        json_free(jh->parser, item);
        return CFDDNS_ERR_OPERATION_FAILED;
    }

    return CFDDNS_OK;
}

static int json_config_set_int(void *handle, const char *key, int64_t val) {
    json_config_handle_t *jh = (json_config_handle_t *)handle;
    if (jh == NULL || key == NULL) return CFDDNS_ERR_NULL_POINTER;

    json_value_t parent = create_nested_path(jh->parser, jh->root, key);
    if (parent == NULL) return CFDDNS_ERR_CONFIG_PARSE_FAILED;

    const char *last_dot = strrchr(key, '.');
    const char *final_key = (last_dot != NULL) ? last_dot + 1 : key;

    json_value_t item = json_create_int(jh->parser, val);
    if (item == NULL) return CFDDNS_ERR_OUT_OF_MEMORY;

    if (!json_add_item_to_object(jh->parser, parent, final_key, item)) {
        json_free(jh->parser, item);
        return CFDDNS_ERR_OPERATION_FAILED;
    }

    return CFDDNS_OK;
}

static int json_config_set_float(void *handle, const char *key, double val) {
    json_config_handle_t *jh = (json_config_handle_t *)handle;
    if (jh == NULL || key == NULL) return CFDDNS_ERR_NULL_POINTER;

    json_value_t parent = create_nested_path(jh->parser, jh->root, key);
    if (parent == NULL) return CFDDNS_ERR_CONFIG_PARSE_FAILED;

    const char *last_dot = strrchr(key, '.');
    const char *final_key = (last_dot != NULL) ? last_dot + 1 : key;

    json_value_t item = json_create_number(jh->parser, val);
    if (item == NULL) return CFDDNS_ERR_OUT_OF_MEMORY;

    if (!json_add_item_to_object(jh->parser, parent, final_key, item)) {
        json_free(jh->parser, item);
        return CFDDNS_ERR_OPERATION_FAILED;
    }

    return CFDDNS_OK;
}

static int json_config_set_string(void *handle, const char *key, const char *val) {
    json_config_handle_t *jh = (json_config_handle_t *)handle;
    if (jh == NULL || key == NULL) return CFDDNS_ERR_NULL_POINTER;

    json_value_t parent = create_nested_path(jh->parser, jh->root, key);
    if (parent == NULL) return CFDDNS_ERR_CONFIG_PARSE_FAILED;

    const char *last_dot = strrchr(key, '.');
    const char *final_key = (last_dot != NULL) ? last_dot + 1 : key;

    json_value_t item = json_create_string(jh->parser, val);
    if (item == NULL) return CFDDNS_ERR_OUT_OF_MEMORY;

    if (!json_add_item_to_object(jh->parser, parent, final_key, item)) {
        json_free(jh->parser, item);
        return CFDDNS_ERR_OPERATION_FAILED;
    }

    return CFDDNS_OK;
}

static int json_config_delete_key(void *handle, const char *key) {
    json_config_handle_t *jh = (json_config_handle_t *)handle;
    if (jh == NULL || key == NULL) return CFDDNS_ERR_NULL_POINTER;

    const char *last_dot = strrchr(key, '.');
    const char *final_key = (last_dot != NULL) ? last_dot + 1 : key;

    json_value_t parent;
    if (last_dot != NULL) {
        char parent_key[256];
        size_t len = last_dot - key;
        memcpy(parent_key, key, len);
        parent_key[len] = '\0';
        parent = get_nested_item(jh->parser, jh->root, parent_key);
    } else {
        parent = jh->root;
    }

    if (parent == NULL) return CFDDNS_ERR_CONFIG_NOT_FOUND;

    json_delete_item_from_object(jh->parser, parent, final_key);
    return CFDDNS_OK;
}

/* ========== Array Operations ========== */

static int json_config_get_array_size(void *handle, const char *key) {
    json_config_handle_t *jh = (json_config_handle_t *)handle;
    if (jh == NULL || key == NULL) return 0;

    json_value_t item = get_nested_item(jh->parser, jh->root, key);
    if (item == NULL || !json_is_array(jh->parser, item)) return 0;

    return json_get_array_size(jh->parser, item);
}

/* API function: returns const config_item_t* per config.h interface */
static const config_item_t *json_config_get_array_item_api(void *handle, const char *key, int index) {
    /* For now, return NULL as config_item_t structure is different from config_t.
     * The internal parsing uses the helper function below. */
    (void)handle;
    (void)key;
    (void)index;
    return NULL;
}

/* Internal helper: returns config_t* for parsing nested array objects */
static void json_config_release_sub_config(config_t *sub_cfg) {
    if (sub_cfg == NULL) return;
    if (sub_cfg->handle != NULL) free(sub_cfg->handle);
    free(sub_cfg);
}

static config_t *json_config_get_array_item_internal(config_t *parent_cfg, const char *key, int index) {
    if (parent_cfg == NULL || key == NULL || index < 0) return NULL;

    json_config_handle_t *jh = (json_config_handle_t *)parent_cfg->handle;
    if (jh == NULL) return NULL;

    json_value_t array = get_nested_item(jh->parser, jh->root, key);
    if (array == NULL || !json_is_array(jh->parser, array)) return NULL;

    int size = json_get_array_size(jh->parser, array);
    if (index >= size) return NULL;

    json_value_t item = json_get_array_item(jh->parser, array, index);
    if (item == NULL) return NULL;

    /* Create a new config_t for this array item */
    config_t *sub_config = malloc(sizeof(config_t));
    if (sub_config == NULL) return NULL;

    json_config_handle_t *sub_handle = malloc(sizeof(json_config_handle_t));
    if (sub_handle == NULL) {
        free(sub_config);
        return NULL;
    }

    /* Share the parser, use the array item as root */
    sub_handle->parser = jh->parser;
    sub_handle->root = item;

    sub_config->handle = sub_handle;
    sub_config->impl = &json_config_impl;
    sub_config->path = NULL;
    sub_config->options = parent_cfg->options;
    sub_config->modified = false;

    return sub_config;
}

__attribute__((unused))
static int json_config_add_array_item(void *handle, const char *key) {
    json_config_handle_t *jh = (json_config_handle_t *)handle;
    if (jh == NULL || key == NULL) return CFDDNS_ERR_NULL_POINTER;

    json_value_t parent = create_nested_path(jh->parser, jh->root, key);
    if (parent == NULL) return CFDDNS_ERR_CONFIG_PARSE_FAILED;

    const char *last_dot = strrchr(key, '.');
    const char *final_key = (last_dot != NULL) ? last_dot + 1 : key;

    /* Get or create array */
    json_value_t array = json_get_object_item(jh->parser, parent, final_key);
    if (array == NULL) {
        array = json_create_array(jh->parser);
        if (array == NULL) return CFDDNS_ERR_OUT_OF_MEMORY;
        json_add_item_to_object(jh->parser, parent, final_key, array);
    }

    if (!json_is_array(jh->parser, array)) {
        return CFDDNS_ERR_CONFIG_INVALID_VALUE;
    }

    /* Add new empty object to array */
    json_value_t new_item = json_create_object(jh->parser);
    if (new_item == NULL) return CFDDNS_ERR_OUT_OF_MEMORY;

    json_add_item_to_array(jh->parser, array, new_item);

    return json_get_array_size(jh->parser, array) - 1;  /* Return index of new item */
}

/* ========== Implementation Registration ========== */

static config_impl_t json_config_impl = {
    .name            = "json",
    .format          = CONFIG_FORMAT_JSON,
    .create          = json_config_create,
    .destroy         = json_config_destroy,
    .load_string     = json_config_load_string,
    .load_file       = json_config_load_file,
    .save_string     = json_config_save_string,
    .save_file       = json_config_save_file,
    .has             = json_config_has,
    .get_type        = json_config_get_type,
    .get_bool        = json_config_get_bool,
    .get_int         = json_config_get_int,
    .get_float       = json_config_get_float,
    .get_string      = json_config_get_string,
    .get_string_buf  = json_config_get_string_buf,
    .set_bool        = json_config_set_bool,
    .set_int         = json_config_set_int,
    .set_float       = json_config_set_float,
    .set_string      = json_config_set_string,
    .delete_key      = json_config_delete_key,
    .get_array_size  = json_config_get_array_size,
    .get_array_item  = json_config_get_array_item_api,
};

int config_register_json(void) {
    return config_register_impl(&json_config_impl);
}

/* ========== Multi-WAN Configuration Parsing ========== */

#include "service/wan_manager.h"

int wan_config_from_json(config_t *cfg, int index, wan_config_t *wan) {
    if (cfg == NULL || wan == NULL) return CFDDNS_ERR_NULL_POINTER;

    memset(wan, 0, sizeof(wan_config_t));

    /* Get array item using internal helper */
    config_t *item = json_config_get_array_item_internal(cfg, "wan.interfaces", index);
    if (item == NULL) return CFDDNS_ERR_NOT_FOUND;

    /* Read fields */
    config_get_string_buf(item, "name", wan->name, sizeof(wan->name), "");
    config_get_string_buf(item, "alias", wan->alias, sizeof(wan->alias), "");

    /* WAN type */
    const char *type_str = config_get_string(item, "type", "dhcp");
    if (strcasecmp(type_str, "dhcp") == 0) wan->type = WAN_TYPE_DHCP;
    else if (strcasecmp(type_str, "static") == 0) wan->type = WAN_TYPE_STATIC;
    else if (strcasecmp(type_str, "pppoe") == 0) wan->type = WAN_TYPE_PPPOE;
    else wan->type = WAN_TYPE_DHCP;

    wan->enabled = config_get_bool(item, "enabled", true);
    wan->detect_ipv4 = config_get_bool(item, "detect_ipv4", true);
    wan->detect_ipv6 = config_get_bool(item, "detect_ipv6", false);
    wan->use_external_detect = config_get_bool(item, "use_external_detect", true);
    wan->use_local_read = config_get_bool(item, "use_local_read", false);

    config_get_string_buf(item, "ipv4_endpoint", wan->ipv4_endpoint, sizeof(wan->ipv4_endpoint), "");
    config_get_string_buf(item, "ipv6_endpoint", wan->ipv6_endpoint, sizeof(wan->ipv6_endpoint), "");

    wan->priority = (int)config_get_int(item, "priority", index + 1);
    wan->failover_enabled = config_get_bool(item, "failover_enabled", true);
    wan->check_interval = (int)config_get_int(item, "check_interval", 30);

    /* Release item config */
    json_config_release_sub_config(item);

    return (wan->name[0] != '\0') ? CFDDNS_OK : CFDDNS_ERR_CONFIG_MISSING_KEY;
}

int wan_manager_config_from_json(config_t *cfg, wan_manager_config_t *config) {
    if (cfg == NULL || config == NULL) return CFDDNS_ERR_NULL_POINTER;

    memset(config, 0, sizeof(wan_manager_config_t));

    /* Global WAN settings */
    config_get_string_buf(cfg, "wan.default_interface", config->default_wan, sizeof(config->default_wan), "");
    config->failover_enabled = config_get_bool(cfg, "wan.failover_enabled", true);
    config->failover_threshold = (int)config_get_int(cfg, "wan.failover_threshold", 3);
    config->check_interval = (int)config_get_int(cfg, "wan.check_interval", 30);

    /* Parse WAN interfaces array */
    int count = config_get_array_size(cfg, "wan.interfaces");
    config->wan_count = 0;

    for (int i = 0; i < count && i < WAN_MAX_COUNT; i++) {
        int result = wan_config_from_json(cfg, i, &config->wans[config->wan_count]);
        if (CFDDNS_SUCCEEDED(result)) {
            config->wan_count++;
        }
    }

    return (config->wan_count > 0) ? CFDDNS_OK : CFDDNS_ERR_CONFIG_MISSING_KEY;
}

/* ========== Multi-Record Configuration Parsing ========== */

#include "service/ddns_service.h"

int dns_record_config_from_json(config_t *cfg, int index, dns_record_config_t *rec, const char *shared_token) {
    if (cfg == NULL || rec == NULL) return CFDDNS_ERR_NULL_POINTER;

    memset(rec, 0, sizeof(dns_record_config_t));

    /* Get array item using internal helper */
    config_t *item = json_config_get_array_item_internal(cfg, "records", index);
    if (item == NULL) return CFDDNS_ERR_NOT_FOUND;

    /* Read fields */
    config_get_string_buf(item, "id", rec->id, sizeof(rec->id), "");
    config_get_string_buf(item, "name", rec->name, sizeof(rec->name), "");

    /* API token - use shared or per-record */
    const char *token = config_get_string(item, "api_token", NULL);
    if (token != NULL && strlen(token) > 0) {
        CFDDNS_STRNCPY(rec->api_token, token, sizeof(rec->api_token));
    } else if (shared_token != NULL && strlen(shared_token) > 0) {
        CFDDNS_STRNCPY(rec->api_token, shared_token, sizeof(rec->api_token));
    }

    config_get_string_buf(item, "zone_id", rec->zone_id, sizeof(rec->zone_id), "");
    config_get_string_buf(item, "zone_name", rec->zone_name, sizeof(rec->zone_name), "");
    config_get_string_buf(item, "record_name", rec->record_name, sizeof(rec->record_name), "");

    /* Record type */
    const char *type_str = config_get_string(item, "record_type", "A");
    rec->record_type = dns_record_type_parse(type_str);

    /* Binding mode */
    const char *mode_str = config_get_string(item, "binding_mode", "fixed");
    rec->binding_mode = record_binding_mode_parse(mode_str);

    config_get_string_buf(item, "wan_interface", rec->wan_interface, sizeof(rec->wan_interface), "");
    rec->wan_priority = (int)config_get_int(item, "wan_priority", 0);
    config_get_string_buf(item, "forced_ip", rec->forced_ip, sizeof(rec->forced_ip), "");

    rec->ttl = (int)config_get_int(item, "ttl", 300);
    rec->proxied = config_get_bool(item, "proxied", false);
    rec->check_interval = (int)config_get_int(item, "check_interval", 300);
    rec->update_on_change = config_get_bool(item, "update_on_change", true);
    rec->create_if_missing = config_get_bool(item, "create_if_missing", true);
    rec->delete_on_exit = config_get_bool(item, "delete_on_exit", false);

    /* Initial status */
    rec->status = DDNS_STATUS_STOPPED;

    /* Release item config */
    json_config_release_sub_config(item);

    return (rec->id[0] != '\0' && rec->record_name[0] != '\0') ? CFDDNS_OK : CFDDNS_ERR_CONFIG_MISSING_KEY;
}

int ddns_global_config_from_json(config_t *cfg, ddns_global_config_t *config) {
    if (cfg == NULL || config == NULL) return CFDDNS_ERR_NULL_POINTER;

    memset(config, 0, sizeof(ddns_global_config_t));

    /* Shared API token */
    config_get_string_buf(cfg, "cloudflare.api_token", config->api_token, sizeof(config->api_token), "");
    config->use_shared_token = config_get_bool(cfg, "cloudflare.use_shared_token", true);

    /* Global settings */
    config->default_check_interval = (int)config_get_int(cfg, "global_settings.default_check_interval", 300);
    config->max_concurrent_updates = (int)config_get_int(cfg, "global_settings.max_concurrent_updates", 5);
    config->parallel_updates = config_get_bool(cfg, "global_settings.parallel_updates", true);

    /* Parse DNS records array */
    int count = config_get_array_size(cfg, "records");
    config->record_count = 0;

    for (int i = 0; i < count && i < DDNS_MAX_RECORDS; i++) {
        int result = dns_record_config_from_json(cfg, i, &config->records[config->record_count],
                                                  config->use_shared_token ? config->api_token : NULL);
        if (CFDDNS_SUCCEEDED(result)) {
            config->record_count++;
        }
    }

    return ddns_global_config_validate(config);
}