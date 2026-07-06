#ifndef SCREEN_CONSOLE_H
#define SCREEN_CONSOLE_H

#ifdef __cplusplus
extern "C" {
#endif

int  screen_console_enter(void);
void screen_console_exit(void);
void screen_console_clear(void);
void screen_console_begin(const char *title, const char *controls);
void screen_console_printf(const char *fmt, ...);

#ifdef __cplusplus
}
#endif

#endif /* SCREEN_CONSOLE_H */