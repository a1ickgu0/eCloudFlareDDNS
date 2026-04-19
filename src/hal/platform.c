/**
 * @file platform.c
 * @brief Platform abstraction layer implementation
 */

#include "hal/platform.h"
#include "common/macros.h"
#include <stdlib.h>
#include <string.h>

/* ========== Global Platform Registry ========== */

static const platform_t *g_platform = NULL;
static bool g_initialized = false;

/* ========== Platform Registry ========== */

int platform_register(const platform_t *platform) {
    if (platform == NULL) {
        return CFDDNS_ERR_NULL_POINTER;
    }

    if (platform->name == NULL) {
        return CFDDNS_ERR_INVALID_ARG;
    }

    /* Validate required operations */
    if (platform->fs.file_exists == NULL ||
        platform->timer.sleep_ms == NULL ||
        platform->timer.get_time_ms == NULL) {
        return CFDDNS_ERR_INVALID_ARG;
    }

    g_platform = platform;
    return CFDDNS_OK;
}

const platform_t *platform_get(void) {
    if (g_platform == NULL) {
        /* Auto-initialize if not done */
        platform_init();
    }
    return g_platform;
}

int platform_init(void) {
    if (g_initialized) {
        return CFDDNS_OK;
    }

    if (g_platform == NULL) {
        /* Try to auto-detect and register platform */
        extern int platform_register_posix(void);
        extern int platform_register_windows(void);

        int result = CFDDNS_ERR_PLATFORM_NOT_SUPPORTED;

#if defined(CFDDNS_PLATFORM_LINUX) || defined(CFDDNS_PLATFORM_MACOS) || defined(CFDDNS_PLATFORM_FREEBSD)
        result = platform_register_posix();
#elif defined(CFDDNS_PLATFORM_WINDOWS)
        result = platform_register_windows();
#endif

        if (CFDDNS_FAILED(result)) {
            return result;
        }
    }

    g_initialized = true;
    return CFDDNS_OK;
}

void platform_cleanup(void) {
    g_platform = NULL;
    g_initialized = false;
}

/* ========== File System Operations (Convenience Functions) ========== */

int fs_file_exists(const char *path) {
    const platform_t *p = platform_get();
    if (p == NULL) return CFDDNS_ERR_NOT_INITIALIZED;
    return p->fs.file_exists(path);
}

int fs_dir_exists(const char *path) {
    const platform_t *p = platform_get();
    if (p == NULL) return CFDDNS_ERR_NOT_INITIALIZED;
    return p->fs.dir_exists(path);
}

int fs_mkdir_recursive(const char *path) {
    const platform_t *p = platform_get();
    if (p == NULL) return CFDDNS_ERR_NOT_INITIALIZED;
    return p->fs.mkdir_recursive(path);
}

int fs_remove_file(const char *path) {
    const platform_t *p = platform_get();
    if (p == NULL) return CFDDNS_ERR_NOT_INITIALIZED;
    return p->fs.remove_file(path);
}

int fs_get_home_dir(char *buf, size_t len) {
    const platform_t *p = platform_get();
    if (p == NULL) return CFDDNS_ERR_NOT_INITIALIZED;
    return p->fs.get_home_dir(buf, len);
}

int fs_get_config_dir(char *buf, size_t len) {
    const platform_t *p = platform_get();
    if (p == NULL) return CFDDNS_ERR_NOT_INITIALIZED;
    return p->fs.get_config_dir(buf, len);
}

int fs_get_absolute_path(const char *path, char *buf, size_t len) {
    const platform_t *p = platform_get();
    if (p == NULL) return CFDDNS_ERR_NOT_INITIALIZED;
    return p->fs.get_absolute_path(path, buf, len);
}

/* ========== Timer Operations (Convenience Functions) ========== */

int timer_sleep_ms(uint32_t milliseconds) {
    const platform_t *p = platform_get();
    if (p == NULL) return CFDDNS_ERR_NOT_INITIALIZED;
    return p->timer.sleep_ms(milliseconds);
}

int timer_sleep_s(uint32_t seconds) {
    const platform_t *p = platform_get();
    if (p == NULL) return CFDDNS_ERR_NOT_INITIALIZED;
    return p->timer.sleep_s(seconds);
}

uint64_t timer_get_time_ms(void) {
    const platform_t *p = platform_get();
    if (p == NULL) return 0;
    return p->timer.get_time_ms();
}

uint64_t timer_get_time_s(void) {
    const platform_t *p = platform_get();
    if (p == NULL) return 0;
    return p->timer.get_time_s();
}

int timer_get_localtime(int *year, int *month, int *day, int *hour, int *min, int *sec) {
    const platform_t *p = platform_get();
    if (p == NULL) return CFDDNS_ERR_NOT_INITIALIZED;
    return p->timer.get_localtime(year, month, day, hour, min, sec);
}

int timer_format_time(char *buf, size_t len, const char *format, time_t timestamp) {
    const platform_t *p = platform_get();
    if (p == NULL) return CFDDNS_ERR_NOT_INITIALIZED;
    return p->timer.format_time(buf, len, format, timestamp);
}

/* ========== Thread Operations (Convenience Functions) ========== */

cf_thread_t *thread_create(thread_func_t func, void *arg) {
    const platform_t *p = platform_get();
    if (p == NULL) return NULL;
    return p->thread.create(func, arg);
}

int thread_join(cf_thread_t *thread) {
    const platform_t *p = platform_get();
    if (p == NULL) return CFDDNS_ERR_NOT_INITIALIZED;
    return p->thread.join(thread);
}

int thread_detach(cf_thread_t *thread) {
    const platform_t *p = platform_get();
    if (p == NULL) return CFDDNS_ERR_NOT_INITIALIZED;
    return p->thread.detach(thread);
}

void thread_exit(void) {
    const platform_t *p = platform_get();
    if (p != NULL) {
        p->thread.exit();
    }
}

uint64_t thread_self(void) {
    const platform_t *p = platform_get();
    if (p == NULL) return 0;
    return p->thread.self();
}

/* ========== Mutex Operations (Convenience Functions) ========== */

cf_mutex_t *mutex_create(void) {
    const platform_t *p = platform_get();
    if (p == NULL) return NULL;
    return p->mutex.create();
}

int mutex_lock(cf_mutex_t *mutex) {
    const platform_t *p = platform_get();
    if (p == NULL) return CFDDNS_ERR_NOT_INITIALIZED;
    return p->mutex.lock(mutex);
}

int mutex_trylock(cf_mutex_t *mutex) {
    const platform_t *p = platform_get();
    if (p == NULL) return CFDDNS_ERR_NOT_INITIALIZED;
    return p->mutex.trylock(mutex);
}

int mutex_unlock(cf_mutex_t *mutex) {
    const platform_t *p = platform_get();
    if (p == NULL) return CFDDNS_ERR_NOT_INITIALIZED;
    return p->mutex.unlock(mutex);
}

void mutex_destroy(cf_mutex_t *mutex) {
    const platform_t *p = platform_get();
    if (p != NULL) {
        p->mutex.destroy(mutex);
    }
}

/* ========== Atomic Operations (Convenience Functions) ========== */

int atomic_load_int(volatile int *ptr) {
    const platform_t *p = platform_get();
    if (p == NULL) return 0;
    return p->atomic.load_int(ptr);
}

void atomic_store_int(volatile int *ptr, int val) {
    const platform_t *p = platform_get();
    if (p != NULL) {
        p->atomic.store_int(ptr, val);
    }
}

int atomic_compare_exchange_int(volatile int *ptr, int expected, int desired) {
    const platform_t *p = platform_get();
    if (p == NULL) return 0;
    return p->atomic.compare_exchange_int(ptr, expected, desired);
}

int atomic_increment_int(volatile int *ptr) {
    const platform_t *p = platform_get();
    if (p == NULL) return 0;
    return p->atomic.increment_int(ptr);
}

int atomic_decrement_int(volatile int *ptr) {
    const platform_t *p = platform_get();
    if (p == NULL) return 0;
    return p->atomic.decrement_int(ptr);
}