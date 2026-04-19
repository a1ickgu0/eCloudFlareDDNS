/**
 * @file json_cjson.c
 * @brief cJSON implementation for JSON parser
 */

#include "hal/json_parser.h"
#include "common/types.h"
#include "common/errors.h"
#include "common/macros.h"

#include <cjson/cJSON.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ========== Type Conversion ========== */

static json_type_t cJSON_type_to_json_type(cJSON *item) {
    if (item == NULL) return JSON_TYPE_INVALID;

    switch (item->type) {
        case cJSON_NULL:    return JSON_TYPE_NULL;
        case cJSON_True:    return JSON_TYPE_BOOL;
        case cJSON_False:   return JSON_TYPE_BOOL;
        case cJSON_Number:  return JSON_TYPE_NUMBER;
        case cJSON_String:  return JSON_TYPE_STRING;
        case cJSON_Array:   return JSON_TYPE_ARRAY;
        case cJSON_Object:  return JSON_TYPE_OBJECT;
        default:            return JSON_TYPE_INVALID;
    }
}

/* ========== Parsing ========== */

static json_value_t cjson_parse(const char *str, size_t len) {
    if (str == NULL) return NULL;

    if (len == 0) {
        return cJSON_Parse(str);
    }
    return cJSON_ParseWithLength(str, len);
}

static json_value_t cjson_parse_with_error(const char *str, size_t len,
                                            int *error_pos, char *error_msg, size_t error_msg_len) {
    if (str == NULL) return NULL;

    cJSON *result = NULL;
    const char *error_ptr = NULL;

    if (len == 0) {
        result = cJSON_ParseWithOpts(str, &error_ptr, false);
    } else {
        result = cJSON_ParseWithLengthOpts(str, len, &error_ptr, false);
    }

    if (result == NULL && error_ptr != NULL) {
        if (error_pos != NULL) {
            *error_pos = (int)(error_ptr - str);
        }
        if (error_msg != NULL && error_msg_len > 0) {
            snprintf(error_msg, error_msg_len, "JSON parse error at position %d",
                     (int)(error_ptr - str));
        }
    }

    return result;
}

/* ========== Stringify ========== */

static char *cjson_stringify(json_value_t value) {
    return cJSON_PrintUnformatted((cJSON *)value);
}

static char *cjson_stringify_pretty(json_value_t value, const char *indent) {
    /* cJSON doesn't support custom indent, use default */
    (void)indent;
    return cJSON_Print((cJSON *)value);
}

/* ========== Type Query ========== */

static json_type_t cjson_get_type(json_value_t value) {
    return cJSON_type_to_json_type((cJSON *)value);
}

/* ========== Value Getters ========== */

static bool cjson_get_bool(json_value_t value) {
    cJSON *item = (cJSON *)value;
    if (item == NULL || !cJSON_IsBool(item)) return false;
    return cJSON_IsTrue(item);
}

static double cjson_get_number(json_value_t value) {
    cJSON *item = (cJSON *)value;
    if (item == NULL || !cJSON_IsNumber(item)) return 0.0;
    return item->valuedouble;
}

static int64_t cjson_get_int(json_value_t value) {
    cJSON *item = (cJSON *)value;
    if (item == NULL || !cJSON_IsNumber(item)) return 0;
    return (int64_t)item->valueint;
}

static const char *cjson_get_string(json_value_t value) {
    cJSON *item = (cJSON *)value;
    if (item == NULL || !cJSON_IsString(item)) return NULL;
    return item->valuestring;
}

static const char *cjson_get_string_len(json_value_t value, size_t *len) {
    cJSON *item = (cJSON *)value;
    if (item == NULL || !cJSON_IsString(item)) {
        if (len) *len = 0;
        return NULL;
    }
    if (len) *len = item->valuestring ? strlen(item->valuestring) : 0;
    return item->valuestring;
}

/* ========== Object Operations ========== */

static json_value_t cjson_get_object_item(json_value_t obj, const char *key) {
    return cJSON_GetObjectItem((cJSON *)obj, key);
}

static json_value_t cjson_get_object_item_case(json_value_t obj, const char *key) {
    return cJSON_GetObjectItemCaseSensitive((cJSON *)obj, key);
}

static bool cjson_has_object_item(json_value_t obj, const char *key) {
    return cJSON_HasObjectItem((cJSON *)obj, key);
}

static int cjson_get_object_size(json_value_t obj) {
    cJSON *item = (cJSON *)obj;
    if (item == NULL || !cJSON_IsObject(item)) return 0;
    return cJSON_GetArraySize(item);
}

static json_value_t cjson_get_object_item_at(json_value_t obj, int index, const char **key) {
    cJSON *item = (cJSON *)obj;
    if (item == NULL || !cJSON_IsObject(item)) return NULL;

    cJSON *child = item->child;
    int i = 0;

    while (child != NULL && i < index) {
        child = child->next;
        i++;
    }

    if (child == NULL) return NULL;

    if (key != NULL) *key = child->string;
    return child;
}

/* ========== Array Operations ========== */

static int cjson_get_array_size(json_value_t arr) {
    return cJSON_GetArraySize((cJSON *)arr);
}

static json_value_t cjson_get_array_item(json_value_t arr, int index) {
    return cJSON_GetArrayItem((cJSON *)arr, index);
}

/* ========== Value Creation ========== */

static json_value_t cjson_create_null(void) {
    return cJSON_CreateNull();
}

static json_value_t cjson_create_bool(bool val) {
    return cJSON_CreateBool(val);
}

static json_value_t cjson_create_number(double val) {
    return cJSON_CreateNumber(val);
}

static json_value_t cjson_create_int(int64_t val) {
    return cJSON_CreateNumber((double)val);
}

static json_value_t cjson_create_string(const char *val) {
    return cJSON_CreateString(val);
}

static json_value_t cjson_create_string_len(const char *val, size_t len) {
    (void)len;  /* cJSON_CreateStringReference doesn't use length */
    return cJSON_CreateStringReference(val);  /* Note: doesn't copy */
}

static json_value_t cjson_create_array(void) {
    return cJSON_CreateArray();
}

static json_value_t cjson_create_object(void) {
    return cJSON_CreateObject();
}

/* ========== Value Modification ========== */

static bool cjson_add_item_to_object(json_value_t obj, const char *key, json_value_t value) {
    return cJSON_AddItemToObject((cJSON *)obj, key, (cJSON *)value);
}

static bool cjson_add_item_to_array(json_value_t arr, json_value_t value) {
    return cJSON_AddItemToArray((cJSON *)arr, (cJSON *)value);
}

static bool cjson_replace_item_in_object(json_value_t obj, const char *key, json_value_t value) {
    return cJSON_ReplaceItemInObject((cJSON *)obj, key, (cJSON *)value);
}

static void cjson_delete_item_from_object(json_value_t obj, const char *key) {
    cJSON_DeleteItemFromObject((cJSON *)obj, key);
}

static void cjson_delete_item_from_array(json_value_t arr, int index) {
    cJSON_DeleteItemFromArray((cJSON *)arr, index);
}

/* ========== Reference Management ========== */

static json_value_t cjson_incref(json_value_t value) {
    /* cJSON doesn't have reference counting, just return the value */
    return value;
}

static void cjson_decref(json_value_t value) {
    (void)value;  /* cJSON doesn't have reference counting, do nothing */
}

static void cjson_free(json_value_t value) {
    cJSON_Delete((cJSON *)value);
}

/* ========== Implementation Registration ========== */

static json_parser_impl_t cjson_impl = {
    .name                = "cjson",
    .parse               = cjson_parse,
    .parse_with_error    = cjson_parse_with_error,
    .stringify           = cjson_stringify,
    .stringify_pretty    = cjson_stringify_pretty,
    .get_type            = cjson_get_type,
    .get_bool            = cjson_get_bool,
    .get_number          = cjson_get_number,
    .get_int             = cjson_get_int,
    .get_string          = cjson_get_string,
    .get_string_len      = cjson_get_string_len,
    .get_object_item     = cjson_get_object_item,
    .get_object_item_case = cjson_get_object_item_case,
    .has_object_item     = cjson_has_object_item,
    .get_object_size     = cjson_get_object_size,
    .get_object_item_at  = cjson_get_object_item_at,
    .get_array_size      = cjson_get_array_size,
    .get_array_item      = cjson_get_array_item,
    .create_null         = cjson_create_null,
    .create_bool         = cjson_create_bool,
    .create_number       = cjson_create_number,
    .create_int          = cjson_create_int,
    .create_string       = cjson_create_string,
    .create_string_len   = cjson_create_string_len,
    .create_array        = cjson_create_array,
    .create_object       = cjson_create_object,
    .add_item_to_object  = cjson_add_item_to_object,
    .add_item_to_array   = cjson_add_item_to_array,
    .replace_item_in_object = cjson_replace_item_in_object,
    .delete_item_from_object = cjson_delete_item_from_object,
    .delete_item_from_array = cjson_delete_item_from_array,
    .incref              = cjson_incref,
    .decref              = cjson_decref,
    .free                = cjson_free,
};

int json_parser_register_cjson(void) {
    return json_parser_register_impl(&cjson_impl);
}