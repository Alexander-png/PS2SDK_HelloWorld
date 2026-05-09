// /*
//  * log.c — implementation of log.h
//  */

// #include "log.h"

// #if !defined(LOG_DISABLE)

// #include <stdio.h>
// #include <stdarg.h>
// #include <string.h>     /* strlen */
// #include <kernel.h>
// #include <sio.h>        /* sio_init, sio_write — EE serial I/O */

// #if defined(LOG_SCREEN)
//   #include <debug.h>    /* scr_printf */
// #endif

// #define LOG_BUF_SIZE 512
// static char s_buf[LOG_BUF_SIZE];

// #if !defined(LOG_DISABLE)

// void log_init(void)
// {
// #if !defined(LOG_NO_LIBC)
//     setbuf(stdout, NULL);   /* unbuffered stdout for PCSX2 EE Console */
// #endif
//     sio_init(38400, 0, 0, 0, 0);
// }

// void log_printf(const char *fmt, ...)
// {
//     va_list ap;

//     va_start(ap, fmt);
//     int n = vsnprintf(s_buf, sizeof(s_buf), fmt, ap);
//     va_end(ap);

//     if (n < 0) return;
//     if (n >= (int)sizeof(s_buf)) {
//         n = sizeof(s_buf) - 1;
//         s_buf[n] = '\0';
//     }

//     /* Kernel/serial channel — captured by PCSX2 console logging
//      * and by ps2client on real hardware. */
//     sio_write(s_buf, (size_t)n);

// #else

// /* Stubs so anything that calls log_init() directly still links. */
// void log_init(void) {}

// #endif

// #if !defined(LOG_NO_LIBC)
//     fputs(s_buf, stdout);
// #endif

// #if defined(LOG_SCREEN)
//     scr_printf("%s", s_buf);
// #endif
// }

// #endif /* LOG_DISABLE */

/*
 * log.c — implementation of log.h
 */

#include "log.h"

#if !defined(LOG_DISABLE)

#include <stdio.h>
#include <stdarg.h>
#include <string.h>     /* strlen */
#include <kernel.h>
#include <sio.h>        /* sio_init, sio_write — EE serial I/O */

#if defined(LOG_SCREEN)
  #include <debug.h>    /* scr_printf */
#endif

#define LOG_BUF_SIZE 512
static char s_buf[LOG_BUF_SIZE];

void log_init(void)
{
#if !defined(LOG_NO_LIBC)
    setbuf(stdout, NULL);   /* unbuffered stdout for PCSX2 EE Console */
#endif
    sio_init(38400, 0, 0, 0, 0);
}

void log_printf(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(s_buf, sizeof(s_buf), fmt, ap);
    va_end(ap);

    if (n < 0) return;
    if (n >= (int)sizeof(s_buf)) {
        n = sizeof(s_buf) - 1;
        s_buf[n] = '\0';
    }

    /* Kernel/serial channel — captured by PCSX2 console logging
     * and by ps2client on real hardware. */
    sio_write(s_buf, (size_t)n);

#if !defined(LOG_NO_LIBC)
    fputs(s_buf, stdout);
#endif

#if defined(LOG_SCREEN)
    scr_printf("%s", s_buf);
#endif
}

#else  /* LOG_DISABLE — release build: empty stubs */

/* Stubs so anything that calls log_init() directly still links. */
void log_init(void) {}

void log_printf(const char *fmt, ...)
{
    (void)fmt;
}

#endif /* LOG_DISABLE */