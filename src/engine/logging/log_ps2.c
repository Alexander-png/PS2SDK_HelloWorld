#include "engine/logging/log.h"

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
static log_category_t s_log_mask = LOGCAT_ALL;

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

static void log_write_buffer(const char *buf, int n)
{
    if (!buf || n <= 0)
        return;

    log_lock();

    // sio_write expects void*, that is not marked as const
    // so we need to cast buf to (void *) to avoid warning
    void *sio_buf = (void *)buf;
    sio_write(sio_buf, (size_t)n);

#if !defined(LOG_NO_LIBC)
    fputs(buf, stdout);
#endif

#if defined(LOG_SCREEN)
    if (s_screen_enabled)
        scr_printf("%s", buf);
#endif

    log_unlock();
}

void log_init(void)
{
#if !defined(LOG_NO_LIBC)
    setbuf(stdout, NULL);
#endif

    sio_init(38400, 0, 0, 0, 0);
    s_screen_enabled = 0;
    s_log_mask = LOGCAT_ALL;

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

void log_set_mask(log_category_t mask)
{
    s_log_mask = mask;
}

log_category_t log_get_mask(void)
{
    return s_log_mask;
}

void log_enable_categories(log_category_t mask)
{
    s_log_mask |= mask;
}

void log_disable_categories(log_category_t mask)
{
    s_log_mask &= ~mask;
}

int log_category_enabled(log_category_t cat)
{
    return (s_log_mask & cat) != 0;
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

    log_write_buffer(buf, n);
}

void log_printf_cat(log_category_t cat, const char *fmt, ...)
{
    char buf[LOG_BUF_SIZE];
    va_list ap;
    int n;

    if (!log_category_enabled(cat))
        return;

    va_start(ap, fmt);
    n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    if (n < 0)
        return;

    if (n >= (int)sizeof(buf)) {
        n = (int)sizeof(buf) - 1;
        buf[n] = '\0';
    }

    log_write_buffer(buf, n);
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

void log_set_mask(log_category_t mask)
{
    (void)mask;
}

log_category_t log_get_mask(void)
{
    return 0;
}

void log_enable_categories(log_category_t mask)
{
    (void)mask;
}

void log_disable_categories(log_category_t mask)
{
    (void)mask;
}

int log_category_enabled(log_category_t cat)
{
    (void)cat;
    return 0;
}

void log_printf(const char *fmt, ...)
{
    (void)fmt;
}

void log_printf_cat(log_category_t cat, const char *fmt, ...)
{
    (void)cat;
    (void)fmt;
}

#endif