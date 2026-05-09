/*
 * log.h — lightweight logging for PS2 homebrew.
 *
 * Routes messages to:
 *   - Kprintf  (kernel log; PCSX2 IOP Console / ps2client on hardware)
 *   - printf   (libc stdout; PCSX2 EE Console)
 *   - scr_printf (on-screen framebuffer text) when LOG_SCREEN is enabled
 *
 * Compile-time switches (define before including, or via -D):
 *   LOG_SCREEN   — also draw to screen via scr_printf
 *   LOG_NO_LIBC  — skip printf (use only Kprintf + optional screen)
 *   LOG_DISABLE  — compile out all log calls (zero overhead)
 */

#ifndef LOG_H
#define LOG_H

#ifdef __cplusplus
extern "C" {
#endif

/* Call once at program start. Safe to call before init_scr(). */
void log_init(void);

/* Core logging entry point. Newline is NOT auto-appended. */
void log_printf(const char *fmt, ...)
    __attribute__((format(printf, 1, 2)));

/* Convenience macros */
#if defined(LOG_DISABLE)
  #define LOG(...)   ((void)0)
  #define LOGLN(...) ((void)0)
#else
  #define LOG(...)   log_printf(__VA_ARGS__)
  #define LOGLN(...) log_printf(__VA_ARGS__), log_printf("\n")
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