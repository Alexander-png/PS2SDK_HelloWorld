#include "log.h"

#if !defined(LOG_DISABLE)

#include <stdio.h>
#include <stdarg.h>
#include <kernel.h>
#include <sio.h>
#include <string.h>

#if defined(LOG_SCREEN)
  #include <debug.h>
#endif

#define LOG_BUF_SIZE 512

static int s_screen_enabled = 0;
static int s_log_sema = -1;

static void log_lock(void)
{
    if (s_log_sema >= 0)
        WaitSema(s_log_sema);
}

static void log_unlock(void)
{
    if (s_log_sema >= 0)
        SignalSema(s_log_sema);
}

void log_init(void)
{
#if !defined(LOG_NO_LIBC)
    setbuf(stdout, NULL);
#endif

    sio_init(38400, 0, 0, 0, 0);
    s_screen_enabled = 0;

    if (s_log_sema < 0) {
        ee_sema_t sema;
        memset(&sema, 0, sizeof(sema));
        sema.max_count = 1;
        sema.init_count = 1;
        s_log_sema = CreateSema(&sema);
    }
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
    char buf[LOG_BUF_SIZE];
    va_list ap;
    int n;

    va_start(ap, fmt);
    n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    if (n < 0)
        return;

    if (n >= (int)sizeof(buf)) {
        n = (int)sizeof(buf) - 1;
        buf[n] = '\0';
    }

    log_lock();

    sio_write(buf, (size_t)n);

#if !defined(LOG_NO_LIBC)
    fputs(buf, stdout);
#endif

#if defined(LOG_SCREEN)
    if (s_screen_enabled)
        scr_printf("%s", buf);
#endif

    log_unlock();
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