/**
 * @file macros.h
 * @brief Common macros for CloudFlare DDNS client
 */

#ifndef CFDDNS_COMMON_MACROS_H
#define CFDDNS_COMMON_MACROS_H

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ========== Memory helpers ========== */

/**
 * @brief Safe free macro (set pointer to NULL after free)
 */
#define CFDDNS_FREE(ptr) do { \
    if (ptr) { \
        free((void*)(ptr)); \
        (ptr) = NULL; \
    } \
} while(0)

/**
 * @brief Safe realloc with error check
 */
#define CFDDNS_REALLOC(ptr, size) ({ \
    void *__ptr = realloc((ptr), (size)); \
    if (!__ptr && (size) > 0) { \
        free(ptr); \
        (ptr) = NULL; \
    } else { \
        (ptr) = __ptr; \
    } \
    __ptr; \
})

/* ========== String helpers ========== */

/**
 * @brief Safe string copy with size check
 */
#define CFDDNS_STRNCPY(dst, src, size) do { \
    strncpy((dst), (src), (size) - 1); \
    (dst)[(size) - 1] = '\0'; \
} while(0)

/**
 * @brief String length with NULL check
 */
#define CFDDNS_STRLEN_OR_ZERO(s) ((s) ? strlen(s) : 0)

/**
 * @brief Check if string is empty or NULL
 */
#define CFDDNS_STR_EMPTY(s) (!(s) || *(s) == '\0')

/**
 * @brief Check if strings are equal
 */
#define CFDDNS_STR_EQ(a, b) (strcmp((a), (b)) == 0)

/**
 * @brief Check if strings are equal (case insensitive)
 */
#define CFDDNS_STR_IEQ(a, b) (strcasecmp((a), (b)) == 0)

/* ========== Container helpers ========== */

/**
 * @brief Get struct pointer from member pointer
 */
#define CFDDNS_CONTAINER_OF(ptr, type, member) \
    ((type *)((char *)(ptr) - offsetof(type, member)))

/* ========== Resource cleanup ========== */

/**
 * @brief Auto-cleanup attribute for GCC/Clang
 */
#ifdef __GNUC__
#define CFDDNS_AUTO_CLEANUP(func) __attribute__((cleanup(func)))
#else
#define CFDDNS_AUTO_CLEANUP(func)
#endif

/**
 * @brief Declare auto-cleanup function for pointer type
 */
#define CFDDNS_DEFINE_AUTOFREE(type, func) \
    static inline void func(type **ptr) { \
        if (ptr && *ptr) { \
            free(*ptr); \
            *ptr = NULL; \
        } \
    }

/* ========== Bit operations ========== */

#define CFDDNS_BIT_SET(val, bit)    ((val) |= (1U << (bit)))
#define CFDDNS_BIT_CLEAR(val, bit)  ((val) &= ~(1U << (bit)))
#define CFDDNS_BIT_TOGGLE(val, bit) ((val) ^= (1U << (bit)))
#define CFDDNS_BIT_TEST(val, bit)   (((val) >> (bit)) & 1U)

/* ========== Compile-time assertions ========== */

#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
    #define CFDDNS_STATIC_ASSERT(cond, msg) _Static_assert((cond), msg)
#else
    #define CFDDNS_STATIC_ASSERT(cond, msg) \
        typedef char cfddns_static_assert_##msg[(cond) ? 1 : -1] CFDDNS_UNUSED
#endif

/* ========== Branch prediction hints ========== */

#ifdef __GNUC__
    #define CFDDNS_LIKELY(x)   __builtin_expect(!!(x), 1)
    #define CFDDNS_UNLIKELY(x) __builtin_expect(!!(x), 0)
#else
    #define CFDDNS_LIKELY(x)   (x)
    #define CFDDNS_UNLIKELY(x) (x)
#endif

/* ========== Defer statement (C23 or GCC) ========== */

#if defined(__GNUC__) || defined(__clang__)
    #define CFDDNS_DEFER __attribute__((cleanup(cfddns_defer_cleanup)))
    static inline void cfddns_defer_cleanup(void (**func)(void)) {
        if (*func) (*func)();
    }
#endif

/* ========== Debug helpers ========== */

#ifdef CFDDNS_DEBUG
    #define CFDDNS_DEBUG_PRINT(fmt, ...) \
        fprintf(stderr, "[DEBUG] %s:%d: " fmt "\n", __FILE__, __LINE__, ##__VA_ARGS__)
#else
    #define CFDDNS_DEBUG_PRINT(fmt, ...) ((void)0)
#endif

/* ========== Unused variable ========== */

#define CFDDNS_UNUSED_VAR(x) (void)(x)

/* ========== Countof for arrays ========== */

#ifndef countof
#define countof(arr) (sizeof(arr) / sizeof((arr)[0]))
#endif

#ifdef __cplusplus
}
#endif

#endif /* CFDDNS_COMMON_MACROS_H */