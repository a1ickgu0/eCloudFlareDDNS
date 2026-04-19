/**
 * @file posix_platform.c
 * @brief POSIX platform implementation (Linux, macOS, FreeBSD)
 */

#include "hal/platform.h"
#include "common/types.h"
#include "common/errors.h"
#include "common/macros.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/time.h>
#include <time.h>
#include <pthread.h>
#include <errno.h>
#include <limits.h>
#include <fcntl.h>

#ifdef __APPLE__
#include <mach/mach_time.h>
#endif

/* ========== File System Operations ========== */

static int posix_file_exists(const char *path) {
    if (path == NULL) return CFDDNS_ERR_NULL_POINTER;
    struct stat st;
    return (stat(path, &st) == 0 && S_ISREG(st.st_mode)) ? 1 : 0;
}

static int posix_dir_exists(const char *path) {
    if (path == NULL) return CFDDNS_ERR_NULL_POINTER;
    struct stat st;
    return (stat(path, &st) == 0 && S_ISDIR(st.st_mode)) ? 1 : 0;
}

static int posix_mkdir_recursive(const char *path) {
    if (path == NULL) return CFDDNS_ERR_NULL_POINTER;

    char tmp[PATH_MAX];
    char *p = NULL;
    size_t len;

    CFDDNS_STRNCPY(tmp, path, sizeof(tmp));
    len = strlen(tmp);

    /* Remove trailing slash */
    if (len > 0 && tmp[len - 1] == '/') {
        tmp[len - 1] = '\0';
    }

    for (p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            /* Try to create directory */
            if (mkdir(tmp, 0755) != 0 && errno != EEXIST) {
                return CFDDNS_ERR_DIR_CREATE_FAILED;
            }
            *p = '/';
        }
    }

    /* Create final directory */
    if (mkdir(tmp, 0755) != 0 && errno != EEXIST) {
        return CFDDNS_ERR_DIR_CREATE_FAILED;
    }

    return CFDDNS_OK;
}

static int posix_remove_file(const char *path) {
    if (path == NULL) return CFDDNS_ERR_NULL_POINTER;
    if (unlink(path) != 0) {
        if (errno == ENOENT) return CFDDNS_ERR_FILE_NOT_FOUND;
        return CFDDNS_ERR_FILE_PERMISSION;
    }
    return CFDDNS_OK;
}

static int posix_get_home_dir(char *buf, size_t len) {
    if (buf == NULL || len == 0) return CFDDNS_ERR_NULL_POINTER;

    const char *home = getenv("HOME");
    if (home == NULL || strlen(home) == 0) {
        return CFDDNS_ERR_NOT_FOUND;
    }

    if (strlen(home) >= len) {
        return CFDDNS_ERR_BUFFER_TOO_SMALL;
    }

    CFDDNS_STRNCPY(buf, home, len);
    return CFDDNS_OK;
}

static int posix_get_config_dir(char *buf, size_t len) {
    if (buf == NULL || len == 0) return CFDDNS_ERR_NULL_POINTER;

    char home[PATH_MAX];
    int ret = posix_get_home_dir(home, sizeof(home));
    if (CFDDNS_FAILED(ret)) return ret;

    /* Build config directory path: ~/.config/cfddns/ */
    size_t needed = strlen(home) + strlen("/.config/cfddns") + 1;
    if (needed > len) {
        return CFDDNS_ERR_BUFFER_TOO_SMALL;
    }

    snprintf(buf, len, "%s/.config/cfddns", home);

    /* Create directory if not exists */
    if (!posix_dir_exists(buf)) {
        ret = posix_mkdir_recursive(buf);
        if (CFDDNS_FAILED(ret)) return ret;
    }

    return CFDDNS_OK;
}

static int posix_get_absolute_path(const char *path, char *buf, size_t len) {
    if (path == NULL || buf == NULL || len == 0) {
        return CFDDNS_ERR_NULL_POINTER;
    }

    if (realpath(path, buf) == NULL) {
        if (errno == ENOENT) return CFDDNS_ERR_FILE_NOT_FOUND;
        return CFDDNS_ERR_OPERATION_FAILED;
    }

    return CFDDNS_OK;
}

/* ========== Timer Operations ========== */

static int posix_sleep_ms(uint32_t milliseconds) {
    struct timespec ts;
    ts.tv_sec = milliseconds / 1000;
    ts.tv_nsec = (milliseconds % 1000) * 1000000L;
    nanosleep(&ts, NULL);
    return CFDDNS_OK;
}

static int posix_sleep_s(uint32_t seconds) {
    sleep(seconds);
    return CFDDNS_OK;
}

static uint64_t posix_get_time_ms(void) {
#ifdef __APPLE__
    /* macOS: use mach_absolute_time */
    static mach_timebase_info_data_t timebase = {0};
    if (timebase.denom == 0) {
        mach_timebase_info(&timebase);
    }
    uint64_t time = mach_absolute_time();
    return (time * timebase.numer / timebase.denom) / 1000000;
#else
    /* Linux: use clock_gettime with CLOCK_MONOTONIC */
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
#endif
}

static uint64_t posix_get_time_s(void) {
    return posix_get_time_ms() / 1000;
}

static int posix_get_localtime(int *year, int *month, int *day,
                                int *hour, int *min, int *sec) {
    time_t now = time(NULL);
    struct tm tm;

    if (localtime_r(&now, &tm) == NULL) {
        return CFDDNS_ERR_OPERATION_FAILED;
    }

    if (year)  *year  = tm.tm_year + 1900;
    if (month) *month = tm.tm_mon + 1;
    if (day)   *day   = tm.tm_mday;
    if (hour)  *hour  = tm.tm_hour;
    if (min)   *min   = tm.tm_min;
    if (sec)   *sec   = tm.tm_sec;

    return CFDDNS_OK;
}

static int posix_format_time(char *buf, size_t len, const char *format, time_t timestamp) {
    if (buf == NULL || len == 0) return CFDDNS_ERR_NULL_POINTER;
    if (format == NULL) format = "%Y-%m-%d %H:%M:%S";

    if (timestamp == 0) timestamp = time(NULL);

    struct tm tm;
    localtime_r(&timestamp, &tm);

    size_t result = strftime(buf, len, format, &tm);
    if (result == 0) {
        return CFDDNS_ERR_BUFFER_TOO_SMALL;
    }

    return (int)result;
}

/* ========== Thread Operations ========== */

struct posix_thread {
    pthread_t thread;
    thread_func_t func;
    void *arg;
    bool running;
    bool detached;
};

static void *posix_thread_entry(void *arg) {
    struct posix_thread *pt = (struct posix_thread *)arg;
    pt->running = true;
    pt->func(pt->arg);
    pt->running = false;
    if (pt->detached) {
        free(pt);
    }
    return NULL;
}

static cf_thread_t *posix_thread_create(thread_func_t func, void *arg) {
    if (func == NULL) return NULL;

    struct posix_thread *pt = malloc(sizeof(struct posix_thread));
    if (pt == NULL) return NULL;

    pt->func = func;
    pt->arg = arg;
    pt->running = false;
    pt->detached = false;

    if (pthread_create(&pt->thread, NULL, posix_thread_entry, pt) != 0) {
        free(pt);
        return NULL;
    }

    return (cf_thread_t *)pt;
}

static int posix_thread_join(cf_thread_t *thread) {
    if (thread == NULL) return CFDDNS_ERR_NULL_POINTER;
    struct posix_thread *pt = (struct posix_thread *)thread;

    int result = pthread_join(pt->thread, NULL);
    free(pt);

    return (result == 0) ? CFDDNS_OK : CFDDNS_ERR_OPERATION_FAILED;
}

static int posix_thread_detach(cf_thread_t *thread) {
    if (thread == NULL) return CFDDNS_ERR_NULL_POINTER;
    struct posix_thread *pt = (struct posix_thread *)thread;

    pt->detached = true;
    int result = pthread_detach(pt->thread);
    if (result != 0) {
        pt->detached = false;
    }

    return (result == 0) ? CFDDNS_OK : CFDDNS_ERR_OPERATION_FAILED;
}

static void CFDDNS_NORETURN posix_thread_exit(void) {
    pthread_exit(NULL);
}

static uint64_t posix_thread_self(void) {
    return (uint64_t)pthread_self();
}

static int posix_thread_equal(uint64_t t1, uint64_t t2) {
    return pthread_equal((pthread_t)t1, (pthread_t)t2) != 0;
}

/* ========== Mutex Operations ========== */

struct posix_mutex {
    pthread_mutex_t mutex;
};

static cf_mutex_t *posix_mutex_create(void) {
    struct posix_mutex *pm = malloc(sizeof(struct posix_mutex));
    if (pm == NULL) return NULL;

    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);

    if (pthread_mutex_init(&pm->mutex, &attr) != 0) {
        pthread_mutexattr_destroy(&attr);
        free(pm);
        return NULL;
    }

    pthread_mutexattr_destroy(&attr);
    return (cf_mutex_t *)pm;
}

static int posix_mutex_lock(cf_mutex_t *mutex) {
    if (mutex == NULL) return CFDDNS_ERR_NULL_POINTER;
    struct posix_mutex *pm = (struct posix_mutex *)mutex;

    int result = pthread_mutex_lock(&pm->mutex);
    return (result == 0) ? CFDDNS_OK : CFDDNS_ERR_OPERATION_FAILED;
}

static int posix_mutex_trylock(cf_mutex_t *mutex) {
    if (mutex == NULL) return CFDDNS_ERR_NULL_POINTER;
    struct posix_mutex *pm = (struct posix_mutex *)mutex;

    int result = pthread_mutex_trylock(&pm->mutex);
    if (result == EBUSY) return CFDDNS_ERR_TIMEOUT;
    return (result == 0) ? CFDDNS_OK : CFDDNS_ERR_OPERATION_FAILED;
}

static int posix_mutex_unlock(cf_mutex_t *mutex) {
    if (mutex == NULL) return CFDDNS_ERR_NULL_POINTER;
    struct posix_mutex *pm = (struct posix_mutex *)mutex;

    int result = pthread_mutex_unlock(&pm->mutex);
    return (result == 0) ? CFDDNS_OK : CFDDNS_ERR_OPERATION_FAILED;
}

static void posix_mutex_destroy(cf_mutex_t *mutex) {
    if (mutex == NULL) return;
    struct posix_mutex *pm = (struct posix_mutex *)mutex;

    pthread_mutex_destroy(&pm->mutex);
    free(pm);
}

/* ========== Atomic Operations ========== */

static int posix_atomic_load_int(volatile int *ptr) {
    return __atomic_load_n(ptr, __ATOMIC_SEQ_CST);
}

static void posix_atomic_store_int(volatile int *ptr, int val) {
    __atomic_store_n(ptr, val, __ATOMIC_SEQ_CST);
}

static int posix_atomic_compare_exchange_int(volatile int *ptr, int expected, int desired) {
    __atomic_compare_exchange_n(ptr, &expected, desired, 0,
                                 __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
    return expected;
}

static int posix_atomic_increment_int(volatile int *ptr) {
    return __atomic_add_fetch(ptr, 1, __ATOMIC_SEQ_CST);
}

static int posix_atomic_decrement_int(volatile int *ptr) {
    return __atomic_sub_fetch(ptr, 1, __ATOMIC_SEQ_CST);
}

/* ========== Platform Registration ========== */

static platform_t posix_platform = {
    .name        = "posix",
    .description = "POSIX platform (Linux, macOS, FreeBSD)",
    .fs          = {
        .file_exists       = posix_file_exists,
        .dir_exists        = posix_dir_exists,
        .mkdir_recursive   = posix_mkdir_recursive,
        .remove_file       = posix_remove_file,
        .get_home_dir      = posix_get_home_dir,
        .get_config_dir    = posix_get_config_dir,
        .get_absolute_path = posix_get_absolute_path,
    },
    .timer       = {
        .sleep_ms       = posix_sleep_ms,
        .sleep_s        = posix_sleep_s,
        .get_time_ms    = posix_get_time_ms,
        .get_time_s     = posix_get_time_s,
        .get_localtime  = posix_get_localtime,
        .format_time    = posix_format_time,
    },
    .thread      = {
        .create  = posix_thread_create,
        .join    = posix_thread_join,
        .detach  = posix_thread_detach,
        .exit    = posix_thread_exit,
        .self    = posix_thread_self,
        .equal   = posix_thread_equal,
    },
    .mutex       = {
        .create  = posix_mutex_create,
        .lock    = posix_mutex_lock,
        .trylock = posix_mutex_trylock,
        .unlock  = posix_mutex_unlock,
        .destroy = posix_mutex_destroy,
    },
    .atomic      = {
        .load_int           = posix_atomic_load_int,
        .store_int          = posix_atomic_store_int,
        .compare_exchange_int = posix_atomic_compare_exchange_int,
        .increment_int      = posix_atomic_increment_int,
        .decrement_int      = posix_atomic_decrement_int,
    },
};

int platform_register_posix(void) {
    return platform_register(&posix_platform);
}