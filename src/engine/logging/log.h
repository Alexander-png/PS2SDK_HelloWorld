#ifndef LOG_H
#define LOG_H

#include <tamtypes.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Lightweight logging with runtime category filtering.
 *
 * Routes messages to:
 *   - sio_write
 *   - printf, unless LOG_NO_LIBC is defined
 *   - scr_printf only when LOG_SCREEN is defined and enabled at runtime
 *
 * Runtime screen logging is disabled by default.
 * Runtime category mask is enabled for all categories by default.
 */

typedef u32 log_category_t;

enum {
    LOGCAT_APP       = 1u << 0,
    LOGCAT_PLATFORM  = 1u << 1,
    LOGCAT_AUDIO     = 1u << 2,
    LOGCAT_INPUT     = 1u << 3,
    LOGCAT_MEMORY    = 1u << 4,
    LOGCAT_STREAMING = 1u << 5,
    LOGCAT_RESOURCES = 1u << 6,
    LOGCAT_GFX       = 1u << 7,
    LOGCAT_TEXT      = 1u << 8,
    LOGCAT_DEBUG     = 1u << 9,
    LOGCAT_STATE     = 1u << 10,

    LOGCAT_ALL       = 0xFFFFFFFFu
};

void log_init(void);
void log_enable_screen(int enabled);
int  log_is_screen_enabled(void);

void log_set_mask(log_category_t mask);
log_category_t log_get_mask(void);
void log_enable_categories(log_category_t mask);
void log_disable_categories(log_category_t mask);
int  log_category_enabled(log_category_t cat);

void log_printf(const char *fmt, ...)
    __attribute__((format(printf, 1, 2)));

void log_printf_cat(log_category_t cat, const char *fmt, ...)
    __attribute__((format(printf, 2, 3)));

#if defined(LOG_DISABLE)

  #define LOG(...)           ((void)0)
  #define LOGLN(...)         ((void)0)
  #define LOGC(cat, ...)     ((void)0)
  #define LOGLNC(cat, ...)   ((void)0)

#else

  #define LOG(...)           log_printf(__VA_ARGS__)
  #define LOGLN(...)         do { log_printf(__VA_ARGS__); log_printf("\n"); } while (0)

  #define LOGC(cat, ...)     do { log_printf_cat((cat), __VA_ARGS__); } while (0)
  #define LOGLNC(cat, ...)   do { log_printf_cat((cat), __VA_ARGS__); log_printf_cat((cat), "\n"); } while (0)

#endif

#if defined(DEBUG)
  #define DEBUG_ONLY(stmt) do { stmt; } while (0)
#else
  #define DEBUG_ONLY(stmt) ((void)0)
#endif

#ifdef __cplusplus
}
#endif

#endif /* LOG_H */