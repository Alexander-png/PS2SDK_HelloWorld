#include "engine/input/input.h"
#include "engine/logging/log.h"
#include "engine/platform/platform.h"

#include <string.h>
#include <kernel.h>
#include <libpad.h>

#ifndef INPUT_PAD_PORT
#define INPUT_PAD_PORT 0
#endif

#ifndef INPUT_PAD_SLOT
#define INPUT_PAD_SLOT 0
#endif

#ifndef INPUT_PAD_READY_TRIES
#define INPUT_PAD_READY_TRIES 2000
#endif

#ifndef INPUT_PAD_READY_DELAY_US
#define INPUT_PAD_READY_DELAY_US 1000
#endif

typedef struct input_system {
    int initialized;
    int available;
    int port;
    int slot;

    u16 current;
    u16 previous;
    u16 pressed;
    u16 released;
} input_system_t;

static input_system_t g_input;
static char g_pad_dma_buf[256] __attribute__((aligned(64)));

static int input_wait_pad_ready(void)
{
    int i;
    int state;

    for (i = 0; i < INPUT_PAD_READY_TRIES; i++) {
        state = padGetState(INPUT_PAD_PORT, INPUT_PAD_SLOT);

        if (state == PAD_STATE_STABLE || state == PAD_STATE_FINDCTP1)
            return 0;

        platform_delay_us(INPUT_PAD_READY_DELAY_US);
    }

    return -1;
}

static u16 input_convert_pad_buttons(u16 pad_btns)
{
    /*
     * libpad returns 0 bits for pressed buttons.
     * Convert to the engine convention: 1 bit means pressed.
     *
     * The INPUT_BUTTON_* values intentionally match PS2SDK's PAD_* values.
     */
    return (u16)(~pad_btns);
}

int input_init(void)
{
    int ret;

    memset(&g_input, 0, sizeof(g_input));
    memset(g_pad_dma_buf, 0, sizeof(g_pad_dma_buf));

    g_input.port = INPUT_PAD_PORT;
    g_input.slot = INPUT_PAD_SLOT;

    LOGLNC(LOGCAT_INPUT, "[input] init");

    /*
     * These may already be loaded, depending on your platform boot.
     * Negative return is logged but not fatal here; padInit/padPortOpen
     * below decides whether input is actually available.
     */
    ret = platform_load_module("rom0:SIO2MAN", 0, NULL);
    LOGLNC(LOGCAT_INPUT, "[input] SIO2MAN load: %d", ret);

    ret = platform_load_module("rom0:PADMAN", 0, NULL);
    LOGLNC(LOGCAT_INPUT, "[input] PADMAN load: %d", ret);

    ret = padInit(0);
    LOGLNC(LOGCAT_INPUT, "[input] padInit: %d", ret);

    if (ret == 0) {
        LOGLNC(LOGCAT_INPUT, "[input] padInit failed");
        g_input.initialized = 1;
        g_input.available = 0;
        return 0;
    }

    ret = padPortOpen(g_input.port, g_input.slot, g_pad_dma_buf);
    LOGLNC(LOGCAT_INPUT, "[input] padPortOpen(%d,%d): %d",
          g_input.port, g_input.slot, ret);

    if (ret == 0) {
        LOGLNC(LOGCAT_INPUT, "[input] padPortOpen failed");
        g_input.initialized = 1;
        g_input.available = 0;
        return 0;
    }

    // First wait for basic ready
    if (input_wait_pad_ready() < 0) {
        LOGLNC(LOGCAT_INPUT, "[input] pad not ready after open");
        g_input.initialized = 1;
        g_input.available = 0;
        return 0;
    }

    // Set DualShock mode and wait again
    ret = padSetMainMode(g_input.port, g_input.slot,
                         PAD_MMODE_DUALSHOCK, PAD_MMODE_LOCK);
    LOGLNC(LOGCAT_INPUT, "[input] padSetMainMode: %d", ret);

    if (input_wait_pad_ready() < 0) {
        LOGLNC(LOGCAT_INPUT, "[input] pad not ready after mode set");
        g_input.initialized = 1;
        g_input.available = 0;
        return 0;
    }

    g_input.initialized = 1;
    g_input.available = 1;
    LOGLNC(LOGCAT_INPUT, "[input] ready");
    return 0;
}

void input_shutdown(void)
{
    if (!g_input.initialized)
        return;

    LOGLNC(LOGCAT_INPUT, "[input] shutdown");

    if (g_input.available)
        padPortClose(g_input.port, g_input.slot);

    memset(&g_input, 0, sizeof(g_input));
}

void input_update(void)
{
    struct padButtonStatus pad_status;
    int state;
    int len;
    u16 next;

    if (!g_input.initialized || !g_input.available) {
        g_input.previous = g_input.current;
        g_input.current = 0;
        g_input.pressed = 0;
        g_input.released = 0;
        return;
    }

    g_input.previous = g_input.current;

    state = padGetState(g_input.port, g_input.slot);
    if (state != PAD_STATE_STABLE && state != PAD_STATE_FINDCTP1) {
        g_input.current = 0;
        g_input.pressed = 0;
        g_input.released = 0;
        return;
    }

    memset(&pad_status, 0, sizeof(pad_status));
    len = padRead(g_input.port, g_input.slot, &pad_status);
    if (len == 0) {
        g_input.current = 0;
        g_input.pressed = 0;
        g_input.released = 0;
        return;
    }

    next = input_convert_pad_buttons(pad_status.btns);

    g_input.current = next;
    g_input.pressed = (u16)(g_input.current & ~g_input.previous);
    g_input.released = (u16)(~g_input.current & g_input.previous);
}

void input_consume(void)
{
    if (!g_input.initialized)
        return;

    g_input.previous = g_input.current;
    g_input.pressed = 0;
    g_input.released = 0;
}

int input_is_available(void)
{
    return g_input.available;
}

u16 input_buttons_down(void)
{
    return g_input.current;
}

u16 input_buttons_pressed(void)
{
    return g_input.pressed;
}

u16 input_buttons_released(void)
{
    return g_input.released;
}

int input_button_down(u16 button)
{
    return (g_input.current & button) != 0;
}

int input_button_pressed(u16 button)
{
    return (g_input.pressed & button) != 0;
}

int input_button_released(u16 button)
{
    return (g_input.released & button) != 0;
}
