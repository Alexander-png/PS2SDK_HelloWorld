#ifndef GFX_SPRITE_ANIM_H
#define GFX_SPRITE_ANIM_H

#include "engine/gfx/sprite.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef GFX_SPRITE_ANIM_INVALID_CLIP
#define GFX_SPRITE_ANIM_INVALID_CLIP (-1)
#endif

typedef enum gfx_sprite_anim_play_mode {
    GFX_SPRITE_ANIM_PLAY_ONCE = 0,
    GFX_SPRITE_ANIM_PLAY_LOOP = 1
} gfx_sprite_anim_play_mode_t;

typedef struct gfx_sprite_anim_frame {
    gfx_rect_t rect;
    float duration;
} gfx_sprite_anim_frame_t;

typedef struct gfx_sprite_anim_clip {
    const char *name;
    int first_frame;
    int frame_count;
    gfx_sprite_anim_play_mode_t play_mode;
} gfx_sprite_anim_clip_t;

typedef struct gfx_sprite_anim_asset {
    gfx_texture_handle_t texture;

    gfx_sprite_anim_frame_t *frames;
    int frame_capacity;
    int frame_count;

    gfx_sprite_anim_clip_t *clips;
    int clip_capacity;
    int clip_count;
} gfx_sprite_anim_asset_t;

typedef struct gfx_sprite_anim_player {
    const gfx_sprite_anim_asset_t *asset;
    gfx_sprite_id_t sprite_id;

    int current_clip;
    int current_frame_in_clip;
    float time_in_frame;
    float speed;

    int playing;
    int finished;
} gfx_sprite_anim_player_t;

/* asset */
void gfx_sprite_anim_asset_init(gfx_sprite_anim_asset_t *asset,
                                gfx_texture_handle_t texture,
                                gfx_sprite_anim_frame_t *frames,
                                int frame_capacity,
                                gfx_sprite_anim_clip_t *clips,
                                int clip_capacity);

void gfx_sprite_anim_asset_reset(gfx_sprite_anim_asset_t *asset);

int gfx_sprite_anim_asset_add_frame(gfx_sprite_anim_asset_t *asset,
                                    const gfx_rect_t *rect,
                                    float duration);

int gfx_sprite_anim_asset_add_clip(gfx_sprite_anim_asset_t *asset,
                                   const char *name,
                                   int first_frame,
                                   int frame_count,
                                   gfx_sprite_anim_play_mode_t play_mode);

int gfx_sprite_anim_asset_find_clip(const gfx_sprite_anim_asset_t *asset,
                                    const char *name);

int gfx_sprite_anim_asset_add_grid_strip(gfx_sprite_anim_asset_t *asset,
                                         float start_x,
                                         float start_y,
                                         float frame_w,
                                         float frame_h,
                                         int frame_count,
                                         float duration);

/* player */
void gfx_sprite_anim_player_init(gfx_sprite_anim_player_t *player,
                                 const gfx_sprite_anim_asset_t *asset,
                                 gfx_sprite_id_t sprite_id);

void gfx_sprite_anim_player_reset(gfx_sprite_anim_player_t *player);

int gfx_sprite_anim_player_set_sprite(gfx_sprite_anim_player_t *player,
                                      gfx_sprite_id_t sprite_id);

int gfx_sprite_anim_player_play(gfx_sprite_anim_player_t *player,
                                const char *clip_name,
                                int restart_if_same);

void gfx_sprite_anim_player_stop(gfx_sprite_anim_player_t *player);

int gfx_sprite_anim_player_apply(gfx_sprite_anim_player_t *player);

int gfx_sprite_anim_player_update(gfx_sprite_anim_player_t *player,
                                  float dt);

int gfx_sprite_anim_player_is_playing(const gfx_sprite_anim_player_t *player);
int gfx_sprite_anim_player_is_finished(const gfx_sprite_anim_player_t *player);

#ifdef __cplusplus
}
#endif

#endif /* GFX_SPRITE_ANIM_H */