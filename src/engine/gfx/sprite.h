#ifndef GFX_SPRITE_H
#define GFX_SPRITE_H

#include "engine/gfx/draw2d.h"
#include "engine/gfx/texture.h"

#ifndef GFX_MAX_SPRITES
#define GFX_MAX_SPRITES 128
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef int gfx_sprite_id_t;

int  gfx_sprite_system_init(void);
void gfx_sprite_system_shutdown(void);

int  gfx_sprite_create(gfx_texture_handle_t texture,
                       const gfx_draw_params_t *params,
                       gfx_sprite_id_t *out_sprite_id);

int  gfx_sprite_create_region(gfx_texture_handle_t texture,
                              const gfx_draw_params_t *params,
                              const gfx_rect_t *src_rect,
                              gfx_sprite_id_t *out_sprite_id);

int  gfx_sprite_update(gfx_sprite_id_t sprite_id,
                       const gfx_draw_params_t *params);

int  gfx_sprite_get_draw_params(gfx_sprite_id_t sprite_id,
                                gfx_draw_params_t *out_params);

int  gfx_sprite_set_texture(gfx_sprite_id_t sprite_id,
                            gfx_texture_handle_t texture);

int  gfx_sprite_get_texture(gfx_sprite_id_t sprite_id,
                            gfx_texture_handle_t *out_texture);

int  gfx_sprite_set_region(gfx_sprite_id_t sprite_id,
                           const gfx_rect_t *src_rect);

int  gfx_sprite_clear_region(gfx_sprite_id_t sprite_id);

int  gfx_sprite_get_region(gfx_sprite_id_t sprite_id,
                           gfx_rect_t *out_rect,
                           int *out_has_region);

int  gfx_sprite_set_visible(gfx_sprite_id_t sprite_id,
                            int visible);

int  gfx_sprite_is_visible(gfx_sprite_id_t sprite_id);

void gfx_sprite_remove(gfx_sprite_id_t sprite_id);
void gfx_sprite_clear_all(void);

void gfx_sprite_draw_all(void);

#ifdef __cplusplus
}
#endif

#endif /* GFX_SPRITE_H */