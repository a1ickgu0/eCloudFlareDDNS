/**
 * @file json_parser.c
 * @brief JSON parser abstraction layer implementation
 */

#include "hal/json_parser.h"
#include "common/macros.h"
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

/* ========== Implementation Registry ========== */

#define MAX_JSON_IMPLS 4

static const json_parser_impl_t *g_json_impls[MAX_JSON_IMPLS] = {NULL};
static int g_json_impl_count = 0;
static const json_parser_impl_t *g_default_impl = NULL;
static pthread_mutex_t g_json_registry_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_once_t g_json_auto_register_once = PTHREAD_ONCE_INIT;

static void json_parser_auto_register_impls(void) {
    extern int json_parser_register_cjson(void);
    json_parser_register_cjson();
}

/* ========== Implementation Registration ========== */

int json_parser_register_impl(const json_parser_impl_t *impl) {
    if (impl == NULL) return CFDDNS_ERR_NULL_POINTER;

    pthread_mutex_lock(&g_json_registry_mutex);

    for (int i = 0; i < g_json_impl_count; i++) {
        if (strcasecmp(g_json_impls[i]->name, impl->name) == 0) {
            pthread_mutex_unlock(&g_json_registry_mutex);
            return CFDDNS_OK;
        }
    }

    if (g_json_impl_count >= MAX_JSON_IMPLS) {
        pthread_mutex_unlock(&g_json_registry_mutex);
        return CFDDNS_ERR_OUT_OF_MEMORY;
    }

    g_json_impls[g_json_impl_count++] = impl;

    if (g_default_impl == NULL) {
        g_default_impl = impl;
    }

    pthread_mutex_unlock(&g_json_registry_mutex);

    return CFDDNS_OK;
}

static const json_parser_impl_t *find_impl(const char *name) {
    pthread_mutex_lock(&g_json_registry_mutex);

    if (name == NULL) {
        const json_parser_impl_t *impl = g_default_impl;
        pthread_mutex_unlock(&g_json_registry_mutex);
        return impl;
    }

    for (int i = 0; i < g_json_impl_count; i++) {
        if (strcasecmp(g_json_impls[i]->name, name) == 0) {
            const json_parser_impl_t *impl = g_json_impls[i];
            pthread_mutex_unlock(&g_json_registry_mutex);
            return impl;
        }
    }

    const json_parser_impl_t *impl = g_default_impl;
    pthread_mutex_unlock(&g_json_registry_mutex);
    return impl;
}

/* ========== Parser Creation/Destruction ========== */

json_parser_t *json_parser_create(void) {
    return json_parser_create_with_impl(NULL);
}

json_parser_t *json_parser_create_with_impl(const char *impl_name) {
    /* Auto-register cJSON once in a thread-safe way. */
    pthread_once(&g_json_auto_register_once, json_parser_auto_register_impls);

    const json_parser_impl_t *impl = find_impl(impl_name);
    if (impl == NULL) return NULL;

    json_parser_t *parser = malloc(sizeof(json_parser_t));
    if (parser == NULL) return NULL;

    memset(parser, 0, sizeof(json_parser_t));
    parser->impl = impl;

    return parser;
}

void json_parser_destroy(json_parser_t *parser) {
    if (parser == NULL) return;
    free(parser);
}

/* ========== Internal Helper ========== */

static const json_parser_impl_t *get_impl(json_parser_t *parser) {
    if (parser != NULL && parser->impl != NULL) {
        return parser->impl;
    }
    return g_default_impl;
}

/* ========== Parsing Functions ========== */

json_value_t json_parse(json_parser_t *parser, const char *str) {
    return json_parse_len(parser, str, 0);
}

json_value_t json_parse_len(json_parser_t *parser, const char *str, size_t len) {
    const json_parser_impl_t *impl = get_impl(parser);
    if (impl == NULL || impl->parse == NULL) return NULL;

    return impl->parse(str, len);
}

json_value_t json_parse_with_error(json_parser_t *parser, const char *str, size_t len,
                                    int *error_pos, char *error_msg, size_t error_msg_len) {
    const json_parser_impl_t *impl = get_impl(parser);
    if (impl == NULL || impl->parse_with_error == NULL) return NULL;

    return impl->parse_with_error(str, len, error_pos, error_msg, error_msg_len);
}

/* ========== Stringify Functions ========== */

char *json_stringify(json_parser_t *parser, json_value_t value) {
    const json_parser_impl_t *impl = get_impl(parser);
    if (impl == NULL || impl->stringify == NULL) return NULL;

    return impl->stringify(value);
}

char *json_stringify_pretty(json_parser_t *parser, json_value_t value, const char *indent) {
    const json_parser_impl_t *impl = get_impl(parser);
    if (impl == NULL || impl->stringify_pretty == NULL) return NULL;

    if (indent == NULL) indent = "  ";
    return impl->stringify_pretty(value, indent);
}

/* ========== Type Query ========== */

json_type_t json_get_type(json_parser_t *parser, json_value_t value) {
    const json_parser_impl_t *impl = get_impl(parser);
    if (impl == NULL || impl->get_type == NULL) return JSON_TYPE_INVALID;

    return impl->get_type(value);
}

bool json_is_null(json_parser_t *parser, json_value_t value) {
    return json_get_type(parser, value) == JSON_TYPE_NULL;
}

bool json_is_bool(json_parser_t *parser, json_value_t value) {
    return json_get_type(parser, value) == JSON_TYPE_BOOL;
}

bool json_is_number(json_parser_t *parser, json_value_t value) {
    return json_get_type(parser, value) == JSON_TYPE_NUMBER;
}

bool json_is_string(json_parser_t *parser, json_value_t value) {
    return json_get_type(parser, value) == JSON_TYPE_STRING;
}

bool json_is_array(json_parser_t *parser, json_value_t value) {
    return json_get_type(parser, value) == JSON_TYPE_ARRAY;
}

bool json_is_object(json_parser_t *parser, json_value_t value) {
    return json_get_type(parser, value) == JSON_TYPE_OBJECT;
}

/* ========== Value Getters ========== */

bool json_get_bool(json_parser_t *parser, json_value_t value) {
    const json_parser_impl_t *impl = get_impl(parser);
    if (impl == NULL || impl->get_bool == NULL) return false;

    return impl->get_bool(value);
}

double json_get_number(json_parser_t *parser, json_value_t value) {
    const json_parser_impl_t *impl = get_impl(parser);
    if (impl == NULL || impl->get_number == NULL) return 0.0;

    return impl->get_number(value);
}

int64_t json_get_int(json_parser_t *parser, json_value_t value) {
    const json_parser_impl_t *impl = get_impl(parser);
    if (impl == NULL || impl->get_int == NULL) return 0;

    return impl->get_int(value);
}

const char *json_get_string(json_parser_t *parser, json_value_t value) {
    const json_parser_impl_t *impl = get_impl(parser);
    if (impl == NULL || impl->get_string == NULL) return NULL;

    return impl->get_string(value);
}

const char *json_get_string_len(json_parser_t *parser, json_value_t value, size_t *len) {
    const json_parser_impl_t *impl = get_impl(parser);
    if (impl == NULL || impl->get_string_len == NULL) return NULL;

    return impl->get_string_len(value, len);
}

/* ========== Object Operations ========== */

json_value_t json_get_object_item(json_parser_t *parser, json_value_t obj, const char *key) {
    const json_parser_impl_t *impl = get_impl(parser);
    if (impl == NULL || impl->get_object_item == NULL) return NULL;

    return impl->get_object_item(obj, key);
}

json_value_t json_get_object_item_case(json_parser_t *parser, json_value_t obj, const char *key) {
    const json_parser_impl_t *impl = get_impl(parser);
    if (impl == NULL || impl->get_object_item_case == NULL) return NULL;

    return impl->get_object_item_case(obj, key);
}

bool json_has_object_item(json_parser_t *parser, json_value_t obj, const char *key) {
    const json_parser_impl_t *impl = get_impl(parser);
    if (impl == NULL || impl->has_object_item == NULL) return false;

    return impl->has_object_item(obj, key);
}

int json_get_object_size(json_parser_t *parser, json_value_t obj) {
    const json_parser_impl_t *impl = get_impl(parser);
    if (impl == NULL || impl->get_object_size == NULL) return 0;

    return impl->get_object_size(obj);
}

json_value_t json_get_object_item_at(json_parser_t *parser, json_value_t obj, int index, const char **key) {
    const json_parser_impl_t *impl = get_impl(parser);
    if (impl == NULL || impl->get_object_item_at == NULL) return NULL;

    return impl->get_object_item_at(obj, index, key);
}

/* ========== Array Operations ========== */

int json_get_array_size(json_parser_t *parser, json_value_t arr) {
    const json_parser_impl_t *impl = get_impl(parser);
    if (impl == NULL || impl->get_array_size == NULL) return 0;

    return impl->get_array_size(arr);
}

json_value_t json_get_array_item(json_parser_t *parser, json_value_t arr, int index) {
    const json_parser_impl_t *impl = get_impl(parser);
    if (impl == NULL || impl->get_array_item == NULL) return NULL;

    return impl->get_array_item(arr, index);
}

/* ========== Value Creation ========== */

json_value_t json_create_null(json_parser_t *parser) {
    const json_parser_impl_t *impl = get_impl(parser);
    if (impl == NULL || impl->create_null == NULL) return NULL;

    return impl->create_null();
}

json_value_t json_create_bool(json_parser_t *parser, bool val) {
    const json_parser_impl_t *impl = get_impl(parser);
    if (impl == NULL || impl->create_bool == NULL) return NULL;

    return impl->create_bool(val);
}

json_value_t json_create_number(json_parser_t *parser, double val) {
    const json_parser_impl_t *impl = get_impl(parser);
    if (impl == NULL || impl->create_number == NULL) return NULL;

    return impl->create_number(val);
}

json_value_t json_create_int(json_parser_t *parser, int64_t val) {
    const json_parser_impl_t *impl = get_impl(parser);
    if (impl == NULL || impl->create_int == NULL) return NULL;

    return impl->create_int(val);
}

json_value_t json_create_string(json_parser_t *parser, const char *val) {
    const json_parser_impl_t *impl = get_impl(parser);
    if (impl == NULL || impl->create_string == NULL) return NULL;

    return impl->create_string(val);
}

json_value_t json_create_string_len(json_parser_t *parser, const char *val, size_t len) {
    const json_parser_impl_t *impl = get_impl(parser);
    if (impl == NULL || impl->create_string_len == NULL) return NULL;

    return impl->create_string_len(val, len);
}

json_value_t json_create_array(json_parser_t *parser) {
    const json_parser_impl_t *impl = get_impl(parser);
    if (impl == NULL || impl->create_array == NULL) return NULL;

    return impl->create_array();
}

json_value_t json_create_object(json_parser_t *parser) {
    const json_parser_impl_t *impl = get_impl(parser);
    if (impl == NULL || impl->create_object == NULL) return NULL;

    return impl->create_object();
}

/* ========== Value Modification ========== */

bool json_add_item_to_object(json_parser_t *parser, json_value_t obj, const char *key, json_value_t value) {
    const json_parser_impl_t *impl = get_impl(parser);
    if (impl == NULL || impl->add_item_to_object == NULL) return false;

    return impl->add_item_to_object(obj, key, value);
}

bool json_add_item_to_array(json_parser_t *parser, json_value_t arr, json_value_t value) {
    const json_parser_impl_t *impl = get_impl(parser);
    if (impl == NULL || impl->add_item_to_array == NULL) return false;

    return impl->add_item_to_array(arr, value);
}

bool json_replace_item_in_object(json_parser_t *parser, json_value_t obj, const char *key, json_value_t value) {
    const json_parser_impl_t *impl = get_impl(parser);
    if (impl == NULL || impl->replace_item_in_object == NULL) return false;

    return impl->replace_item_in_object(obj, key, value);
}

void json_delete_item_from_object(json_parser_t *parser, json_value_t obj, const char *key) {
    const json_parser_impl_t *impl = get_impl(parser);
    if (impl == NULL || impl->delete_item_from_object == NULL) return;

    impl->delete_item_from_object(obj, key);
}

void json_delete_item_from_array(json_parser_t *parser, json_value_t arr, int index) {
    const json_parser_impl_t *impl = get_impl(parser);
    if (impl == NULL || impl->delete_item_from_array == NULL) return;

    impl->delete_item_from_array(arr, index);
}

/* ========== Reference Management ========== */

json_value_t json_incref(json_parser_t *parser, json_value_t value) {
    const json_parser_impl_t *impl = get_impl(parser);
    if (impl == NULL || impl->incref == NULL) return NULL;

    return impl->incref(value);
}

void json_decref(json_parser_t *parser, json_value_t value) {
    const json_parser_impl_t *impl = get_impl(parser);
    if (impl == NULL || impl->decref == NULL) return;

    impl->decref(value);
}

void json_free(json_parser_t *parser, json_value_t value) {
    const json_parser_impl_t *impl = get_impl(parser);
    if (impl == NULL || impl->free == NULL) return;

    impl->free(value);
}