/**
 * @file platform.h
 * @brief Platform abstraction layer for OS-specific operations
 *
 * This module provides a unified interface for platform-specific operations,
 * enabling easy porting to different operating systems.
 */

#ifndef CFDDNS_HAL_PLATFORM_H
#define CFDDNS_HAL_PLATFORM_H

#include "common/types.h"
#include "common/errors.h"
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ========== Forward declarations ========== */
typedef struct fs_ops fs_ops_t;
typedef struct timer_ops timer_ops_t;
typedef struct thread_ops thread_ops_t;
typedef struct mutex_ops mutex_ops_t;
typedef struct atomic_ops atomic_ops_t;
typedef struct platform platform_t;

/* ========== Thread ========== */
typedef struct cf_thread cf_thread_t;
typedef void (*thread_func_t)(void *arg);

/* ========== Mutex ========== */
typedef struct cf_mutex cf_mutex_t;

/* ========== File System Operations ========== */

/**
 * @brief File system operation interface
 */
struct fs_ops {
    /**
     * @brief Check if a file exists
     * @param path File path
     * @return 1 if exists, 0 if not exists, negative on error
     */
    int (*file_exists)(const char *path);

    /**
     * @brief Check if a directory exists
     * @param path Directory path
     * @return 1 if exists, 0 if not exists, negative on error
     */
    int (*dir_exists)(const char *path);

    /**
     * @brief Create directory recursively (like mkdir -p)
     * @param path Directory path
     * @return 0 on success, error code on failure
     */
    int (*mkdir_recursive)(const char *path);

    /**
     * @brief Remove a file
     * @param path File path
     * @return 0 on success, error code on failure
     */
    int (*remove_file)(const char *path);

    /**
     * @brief Get user's home directory
     * @param buf Buffer to store path
     * @param len Buffer length
     * @return 0 on success, error code on failure
     */
    int (*get_home_dir)(char *buf, size_t len);

    /**
     * @brief Get configuration directory
     * @param buf Buffer to store path
     * @param len Buffer length
     * @return 0 on success, error code on failure
     *
     * Returns:
     * - Linux/macOS: ~/.config/cfddns/
     * - Windows: %APPDATA%/cfddns/
     */
    int (*get_config_dir)(char *buf, size_t len);

    /**
     * @brief Get absolute path
     * @param path Relative or absolute path
     * @param buf Buffer to store absolute path
     * @param len Buffer length
     * @return 0 on success, error code on failure
     */
    int (*get_absolute_path)(const char *path, char *buf, size_t len);
};

/* ========== Timer Operations ========== */

/**
 * @brief Timer operation interface
 */
struct timer_ops {
    /**
     * @brief Sleep for specified milliseconds
     * @param milliseconds Milliseconds to sleep
     * @return 0 on success, error code on failure
     */
    int (*sleep_ms)(uint32_t milliseconds);

    /**
     * @brief Sleep for specified seconds
     * @param seconds Seconds to sleep
     * @return 0 on success, error code on failure
     */
    int (*sleep_s)(uint32_t seconds);

    /**
     * @brief Get monotonic time in milliseconds
     * @return Monotonic time in milliseconds
     *
     * This should use a monotonic clock that is not affected by
     * system time changes.
     */
    uint64_t (*get_time_ms)(void);

    /**
     * @brief Get monotonic time in seconds
     * @return Monotonic time in seconds
     */
    uint64_t (*get_time_s)(void);

    /**
     * @brief Get local time components
     * @param year Year output
     * @param month Month output (1-12)
     * @param day Day output (1-31)
     * @param hour Hour output (0-23)
     * @param min Minute output (0-59)
     * @param sec Second output (0-59)
     * @return 0 on success, error code on failure
     */
    int (*get_localtime)(int *year, int *month, int *day,
                         int *hour, int *min, int *sec);

    /**
     * @brief Format time to string
     * @param buf Buffer to store formatted string
     * @param len Buffer length
     * @param format Format string (strftime format)
     * @param timestamp Unix timestamp (0 for current time)
     * @return Number of bytes written, negative on error
     */
    int (*format_time)(char *buf, size_t len, const char *format, time_t timestamp);
};

/* ========== Thread Operations ========== */

/**
 * @brief Thread operation interface
 */
struct thread_ops {
    /**
     * @brief Create a new thread
     * @param func Thread function
     * @param arg Argument passed to thread function
     * @return Thread handle on success, NULL on failure
     */
    cf_thread_t* (*create)(thread_func_t func, void *arg);

    /**
     * @brief Wait for thread to complete
     * @param thread Thread handle
     * @return 0 on success, error code on failure
     */
    int (*join)(cf_thread_t *thread);

    /**
     * @brief Detach thread
     * @param thread Thread handle
     * @return 0 on success, error code on failure
     */
    int (*detach)(cf_thread_t *thread);

    /**
     * @brief Exit current thread
     */
    void (*exit)(void) CFDDNS_NORETURN;

    /**
     * @brief Get current thread ID
     * @return Thread ID
     */
    uint64_t (*self)(void);

    /**
     * @brief Check if threads are equal
     * @param t1 Thread ID 1
     * @param t2 Thread ID 2
     * @return 1 if equal, 0 if not equal
     */
    int (*equal)(uint64_t t1, uint64_t t2);
};

/* ========== Mutex Operations ========== */

/**
 * @brief Mutex operation interface
 */
struct mutex_ops {
    /**
     * @brief Create a mutex
     * @return Mutex handle on success, NULL on failure
     */
    cf_mutex_t* (*create)(void);

    /**
     * @brief Lock mutex (blocking)
     * @param mutex Mutex handle
     * @return 0 on success, error code on failure
     */
    int (*lock)(cf_mutex_t *mutex);

    /**
     * @brief Try to lock mutex (non-blocking)
     * @param mutex Mutex handle
     * @return 0 on success, CFDDNS_ERR_TIMEOUT if already locked
     */
    int (*trylock)(cf_mutex_t *mutex);

    /**
     * @brief Unlock mutex
     * @param mutex Mutex handle
     * @return 0 on success, error code on failure
     */
    int (*unlock)(cf_mutex_t *mutex);

    /**
     * @brief Destroy mutex
     * @param mutex Mutex handle
     */
    void (*destroy)(cf_mutex_t *mutex);
};

/* ========== Atomic Operations ========== */

/**
 * @brief Atomic operation interface
 */
struct atomic_ops {
    /**
     * @brief Atomically load an integer
     * @param ptr Pointer to integer
     * @return Loaded value
     */
    int (*load_int)(volatile int *ptr);

    /**
     * @brief Atomically store an integer
     * @param ptr Pointer to integer
     * @param val Value to store
     */
    void (*store_int)(volatile int *ptr, int val);

    /**
     * @brief Atomically compare and exchange an integer
     * @param ptr Pointer to integer
     * @param expected Expected value
     * @param desired Desired value
     * @return Original value
     *
     * If *ptr == expected, sets *ptr = desired and returns expected.
     * Otherwise, returns *ptr.
     */
    int (*compare_exchange_int)(volatile int *ptr, int expected, int desired);

    /**
     * @brief Atomically increment an integer
     * @param ptr Pointer to integer
     * @return Value after increment
     */
    int (*increment_int)(volatile int *ptr);

    /**
     * @brief Atomically decrement an integer
     * @param ptr Pointer to integer
     * @return Value after decrement
     */
    int (*decrement_int)(volatile int *ptr);
};

/* ========== Platform Structure ========== */

/**
 * @brief Platform abstraction structure
 */
struct platform {
    const char *name;           /**< Platform name (e.g., "posix", "windows") */
    const char *description;    /**< Platform description */

    fs_ops_t     fs;            /**< File system operations */
    timer_ops_t  timer;         /**< Timer operations */
    thread_ops_t thread;        /**< Thread operations */
    mutex_ops_t  mutex;         /**< Mutex operations */
    atomic_ops_t atomic;        /**< Atomic operations */

    void *reserved[4];          /**< Reserved for future use */
};

/* ========== Platform Registration ========== */

/**
 * @brief Register a platform implementation
 * @param platform Platform implementation
 * @return 0 on success, error code on failure
 */
CFDDNS_API int platform_register(const platform_t *platform);

/**
 * @brief Get current platform implementation
 * @return Platform implementation, or NULL if not initialized
 */
CFDDNS_API const platform_t *platform_get(void);

/**
 * @brief Initialize platform layer
 * @return 0 on success, error code on failure
 */
CFDDNS_API int platform_init(void);

/**
 * @brief Cleanup platform layer
 */
CFDDNS_API void platform_cleanup(void);

/* ========== Convenience Macros ========== */

/**
 * @brief Get file system operations
 */
#define fs_ops() (platform_get()->fs)

/**
 * @brief Get timer operations
 */
#define timer_ops() (platform_get()->timer)

/**
 * @brief Get thread operations
 */
#define thread_ops() (platform_get()->thread)

/**
 * @brief Get mutex operations
 */
#define mutex_ops() (platform_get()->mutex)

/**
 * @brief Get atomic operations
 */
#define atomic_ops() (platform_get()->atomic)

/* ========== File System Convenience Functions ========== */

CFDDNS_API int fs_file_exists(const char *path);
CFDDNS_API int fs_dir_exists(const char *path);
CFDDNS_API int fs_mkdir_recursive(const char *path);
CFDDNS_API int fs_remove_file(const char *path);
CFDDNS_API int fs_get_home_dir(char *buf, size_t len);
CFDDNS_API int fs_get_config_dir(char *buf, size_t len);
CFDDNS_API int fs_get_absolute_path(const char *path, char *buf, size_t len);

/* ========== Timer Convenience Functions ========== */

CFDDNS_API int timer_sleep_ms(uint32_t milliseconds);
CFDDNS_API int timer_sleep_s(uint32_t seconds);
CFDDNS_API uint64_t timer_get_time_ms(void);
CFDDNS_API uint64_t timer_get_time_s(void);
CFDDNS_API int timer_get_localtime(int *year, int *month, int *day,
                                    int *hour, int *min, int *sec);
CFDDNS_API int timer_format_time(char *buf, size_t len, const char *format, time_t timestamp);

/* ========== Thread Convenience Functions ========== */

CFDDNS_API cf_thread_t *thread_create(thread_func_t func, void *arg);
CFDDNS_API int thread_join(cf_thread_t *thread);
CFDDNS_API int thread_detach(cf_thread_t *thread);
CFDDNS_API void thread_exit(void);
CFDDNS_API uint64_t thread_self(void);
CFDDNS_API int thread_equal(uint64_t t1, uint64_t t2);

/* ========== Mutex Convenience Functions ========== */

CFDDNS_API cf_mutex_t *mutex_create(void);
CFDDNS_API int mutex_lock(cf_mutex_t *mutex);
CFDDNS_API int mutex_trylock(cf_mutex_t *mutex);
CFDDNS_API int mutex_unlock(cf_mutex_t *mutex);
CFDDNS_API void mutex_destroy(cf_mutex_t *mutex);

/* ========== Atomic Convenience Functions ========== */

CFDDNS_API int atomic_load_int(volatile int *ptr);
CFDDNS_API void atomic_store_int(volatile int *ptr, int val);
CFDDNS_API int atomic_compare_exchange_int(volatile int *ptr, int expected, int desired);
CFDDNS_API int atomic_increment_int(volatile int *ptr);
CFDDNS_API int atomic_decrement_int(volatile int *ptr);

#ifdef __cplusplus
}
#endif

#endif /* CFDDNS_HAL_PLATFORM_H */