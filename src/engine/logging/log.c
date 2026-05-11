#include "log.h"

#if !defined(LOG_DISABLE)

#include <stdio.h>
#include <stdarg.h>
#include <kernel.h>
#include <sio.h>

#if defined(LOG_SCREEN)
  #include <debug.h>
#endif

#define LOG_BUF_SIZE 512

static char s_log_buf[LOG_BUF_SIZE];
static int  s_screen_enabled = 0;

void log_init(void)
{
#if !defined(LOG_NO_LIBC)
    setbuf(stdout, NULL);
#endif

    sio_init(38400, 0, 0, 0, 0);
    s_screen_enabled = 0;
}

void log_enable_screen(int enabled)
{
#if defined(LOG_SCREEN)
    s_screen_enabled = enabled ? 1 : 0;
#else
    (void)enabled;
    s_screen_enabled = 0;
#endif
}

int log_is_screen_enabled(void)
{
    return s_screen_enabled;
}

void log_printf(const char *fmt, ...)
{
    va_list ap;
    int n;

    va_start(ap, fmt);
    n = vsnprintf(s_log_buf, sizeof(s_log_buf), fmt, ap);
    va_end(ap);

    if (n < 0)
        return;

    if (n >= (int)sizeof(s_log_buf)) {
        n = (int)sizeof(s_log_buf) - 1;
        s_log_buf[n] = '\0';
    }

    sio_write(s_log_buf, (size_t)n);

#if !defined(LOG_NO_LIBC)
    fputs(s_log_buf, stdout);
#endif

#if defined(LOG_SCREEN)
    if (s_screen_enabled)
        scr_printf("%s", s_log_buf);
#endif
}

#else

void log_init(void)
{
}

void log_enable_screen(int enabled)
{
    (void)enabled;
}

int log_is_screen_enabled(void)
{
    return 0;
}

void log_printf(const char *fmt, ...)
{
    (void)fmt;
}

#endif
