/**
 * @file types.h
 * @brief Common type definitions for CloudFlare DDNS client
 */

#ifndef CFDDNS_COMMON_TYPES_H
#define CFDDNS_COMMON_TYPES_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ========== Boolean type ========== */
/* bool, true, false are provided by <stdbool.h> */

/* ========== String buffer ========== */
typedef struct {
    char *data;
    size_t len;
    size_t capacity;
} string_buffer_t;

/* ========== Key-value pair ========== */
typedef struct {
    char *key;
    char *value;
} key_value_t;

/* ========== Dynamic array ========== */
typedef struct {
    void **items;
    size_t count;
    size_t capacity;
    void (*item_free)(void *item);
} array_t;

/* ========== Version info ========== */
typedef struct {
    int major;
    int minor;
    int patch;
    const char *suffix;
} version_t;

/* ========== Current version ========== */
#define CFDDNS_VERSION_MAJOR 2
#define CFDDNS_VERSION_MINOR 0
#define CFDDNS_VERSION_PATCH 0
#define CFDDNS_VERSION_SUFFIX ""

/* Version string for display */
#define CFDDNS_VERSION_STRING "2.0.0"

/* ========== Platform detection ========== */
#if defined(_WIN32) || defined(_WIN64)
    #define CFDDNS_PLATFORM_WINDOWS 1
#elif defined(__linux__)
    #define CFDDNS_PLATFORM_LINUX 1
#elif defined(__APPLE__)
    #define CFDDNS_PLATFORM_MACOS 1
#elif defined(__FreeBSD__)
    #define CFDDNS_PLATFORM_FREEBSD 1
#else
    #define CFDDNS_PLATFORM_UNKNOWN 1
#endif

/* ========== Compiler detection ========== */
#if defined(__GNUC__)
    #define CFDDNS_COMPILER_GCC 1
    #define CFDDNS_LIKELY(x)   __builtin_expect(!!(x), 1)
    #define CFDDNS_UNLIKELY(x) __builtin_expect(!!(x), 0)
#else
    #define CFDDNS_LIKELY(x)   (x)
    #define CFDDNS_UNLIKELY(x) (x)
#endif

/* ========== Attribute macros ========== */
#ifdef __GNUC__
    #define CFDDNS_UNUSED     __attribute__((unused))
    #define CFDDNS_NORETURN   __attribute__((noreturn))
    #define CFDDNS_MALLOC     __attribute__((malloc))
    #define CFDDNS_PRINTF(fmt, args) __attribute__((format(printf, fmt, args)))
#else
    #define CFDDNS_UNUSED
    #define CFDDNS_NORETURN
    #define CFDDNS_MALLOC
    #define CFDDNS_PRINTF(fmt, args)
#endif

/* ========== Export macros ========== */
#ifdef CFDDNS_BUILD_SHARED
    #ifdef _WIN32
        #ifdef CFDDNS_EXPORTS
            #define CFDDNS_API __declspec(dllexport)
        #else
            #define CFDDNS_API __declspec(dllimport)
        #endif
    #else
        #define CFDDNS_API __attribute__((visibility("default")))
    #endif
#else
    #define CFDDNS_API
#endif

/* ========== Inline hint ========== */
#ifdef __GNUC__
    #define CFDDNS_INLINE static inline __attribute__((always_inline))
#else
    #define CFDDNS_INLINE static inline
#endif

/* ========== Min/Max ========== */
#define CFDDNS_MIN(a, b) ((a) < (b) ? (a) : (b))
#define CFDDNS_MAX(a, b) ((a) > (b) ? (a) : (b))

/* ========== Array size ========== */
#define CFDDNS_ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))

/* ========== String helpers ========== */
#define CFDDNS_STRINGIFY_IMPL(x) #x
#define CFDDNS_STRINGIFY(x) CFDDNS_STRINGIFY_IMPL(x)

#ifdef __cplusplus
}
#endif

#endif /* CFDDNS_COMMON_TYPES_H */