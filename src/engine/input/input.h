#ifndef INPUT_H
#define INPUT_H

#include <tamtypes.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    INPUT_BUTTON_SELECT   = 0x0001,
    INPUT_BUTTON_L3       = 0x0002,
    INPUT_BUTTON_R3       = 0x0004,
    INPUT_BUTTON_START    = 0x0008,
    INPUT_BUTTON_UP       = 0x0010,
    INPUT_BUTTON_RIGHT    = 0x0020,
    INPUT_BUTTON_DOWN     = 0x0040,
    INPUT_BUTTON_LEFT     = 0x0080,
    INPUT_BUTTON_L2       = 0x0100,
    INPUT_BUTTON_R2       = 0x0200,
    INPUT_BUTTON_L1       = 0x0400,
    INPUT_BUTTON_R1       = 0x0800,
    INPUT_BUTTON_TRIANGLE = 0x1000,
    INPUT_BUTTON_CIRCLE   = 0x2000,
    INPUT_BUTTON_CROSS    = 0x4000,
    INPUT_BUTTON_SQUARE   = 0x8000
};

int  input_init(void);
void input_shutdown(void);
void input_update(void);

void input_consume(void);

int  input_is_available(void);
u16  input_buttons_down(void);
u16  input_buttons_pressed(void);
u16  input_buttons_released(void);

int  input_button_down(u16 button);
int  input_button_pressed(u16 button);
int  input_button_released(u16 button);

#ifdef __cplusplus
}
#endif

#endif /* INPUT_H */
