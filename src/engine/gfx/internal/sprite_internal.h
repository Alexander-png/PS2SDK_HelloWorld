#ifndef GFX_SPRITE_INTERNAL_H
#define GFX_SPRITE_INTERNAL_H

#include "engine/gfx/sprite.h"

typedef struct gfx_sprite_slot {
    int used;
    u16 generation;
    unsigned int order;

    gfx_texture_handle_t texture;
    gfx_draw_params_t params;

    gfx_rect_t src_rect;
    int use_src_rect;
} gfx_sprite_slot_t;

#endif