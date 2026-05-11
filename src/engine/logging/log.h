#ifndef LOG_H
#define LOG_H

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Lightweight logging for PS2 homebrew.
 *
 * Routes messages to:
 *   - sio_write
 *   - printf, unless LOG_NO_LIBC is defined
 *   - scr_printf only when LOG_SCREEN is defined and enabled at runtime
 *
 * Runtime screen logging is disabled by default.
 * This makes log_init() and early LOGLN() safe before init_scr().
 */

void log_init(void);
void log_enable_screen(int enabled);
int  log_is_screen_enabled(void);

void log_printf(const char *fmt, ...)
    __attribute__((format(printf, 1, 2)));

#if defined(LOG_DISABLE)
  #define LOG(...)   ((void)0)
  #define LOGLN(...) ((void)0)
#else
  #define LOG(...)   log_printf(__VA_ARGS__)
  #define LOGLN(...) do { log_printf(__VA_ARGS__); log_printf("\n"); } while (0)
#endif

#ifdef __cplusplus
}
#endif

#if defined(DEBUG)
  #define DEBUG_ONLY(stmt) do { stmt; } while (0)
#else
  #define DEBUG_ONLY(stmt) ((void)0)
#endif

#endif /* LOG_H */