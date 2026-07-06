#include "engine/debug/screen_console.h"

#include "engine/gfx/gfx2d.h"
#include "engine/logging/log.h"

#include <debug.h>
#include <stdarg.h>

int screen_console_enter(void)
{
    gfx2d_shutdown();
    log_enable_screen(1);
    init_scr();
    scr_clear();
    return 0;
}

void screen_console_exit(void)
{
    scr_clear();
    log_enable_screen(0);

    if (gfx2d_init() < 0)
        LOGLN("[screen_console] gfx2d reinit failed");
    else
        LOGLN("[screen_console] gfx2d reinit ok");
}

void screen_console_clear(void)
{
    scr_clear();
}

void screen_console_begin(const char *title, const char *controls)
{
    scr_setXY(0, 0);
    scr_clear();

    screen_console_printf("=== %s ===\n\n", title ? title : "Screen Console");

    if (controls && controls[0] != '\0') {
        screen_console_printf("%s", controls);
        screen_console_printf("\n\n");
    }
}

void screen_console_printf(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    scr_vprintf(fmt, args);
    va_end(args);
}