#include "engine/gfx/sprite_anim.h"

#include <string.h>

static int gfx_sprite_anim_valid_asset(const gfx_sprite_anim_asset_t *asset)
{
    if (!asset)
        return 0;

    if (!gfx_texture_is_valid(asset->texture))
        return 0;

    if (!asset->frames || asset->frame_capacity <= 0)
        return 0;

    if (!asset->clips || asset->clip_capacity <= 0)
        return 0;

    return 1;
}

static int gfx_sprite_anim_valid_player(const gfx_sprite_anim_player_t *player)
{
    if (!player || !player->asset)
        return 0;

    if (player->sprite_id < 0)
        return 0;

    return 1;
}

static const gfx_sprite_anim_clip_t *gfx_sprite_anim_player_clip(
    const gfx_sprite_anim_player_t *player)
{
    if (!gfx_sprite_anim_valid_player(player))
        return NULL;

    if (player->current_clip < 0 || player->current_clip >= player->asset->clip_count)
        return NULL;

    return &player->asset->clips[player->current_clip];
}

static const gfx_sprite_anim_frame_t *gfx_sprite_anim_player_frame(
    const gfx_sprite_anim_player_t *player)
{
    const gfx_sprite_anim_clip_t *clip;
    int frame_index;

    clip = gfx_sprite_anim_player_clip(player);
    if (!clip)
        return NULL;

    if (player->current_frame_in_clip < 0 ||
        player->current_frame_in_clip >= clip->frame_count)
        return NULL;

    frame_index = clip->first_frame + player->current_frame_in_clip;
    if (frame_index < 0 || frame_index >= player->asset->frame_count)
        return NULL;

    return &player->asset->frames[frame_index];
}

void gfx_sprite_anim_asset_init(gfx_sprite_anim_asset_t *asset,
                                gfx_texture_handle_t texture,
                                gfx_sprite_anim_frame_t *frames,
                                int frame_capacity,
                                gfx_sprite_anim_clip_t *clips,
                                int clip_capacity)
{
    if (!asset)
        return;

    memset(asset, 0, sizeof(*asset));
    asset->texture = texture;
    asset->frames = frames;
    asset->frame_capacity = frame_capacity;
    asset->clips = clips;
    asset->clip_capacity = clip_capacity;
}

void gfx_sprite_anim_asset_reset(gfx_sprite_anim_asset_t *asset)
{
    if (!asset)
        return;

    asset->frame_count = 0;
    asset->clip_count = 0;
}

int gfx_sprite_anim_asset_add_frame(gfx_sprite_anim_asset_t *asset,
                                    const gfx_rect_t *rect,
                                    float duration)
{
    if (!gfx_sprite_anim_valid_asset(asset) || !rect)
        return -1;

    if (asset->frame_count >= asset->frame_capacity)
        return -1;

    if (rect->w <= 0.0f || rect->h <= 0.0f || duration <= 0.0f)
        return -1;

    asset->frames[asset->frame_count].rect = *rect;
    asset->frames[asset->frame_count].duration = duration;
    asset->frame_count++;
    return asset->frame_count - 1;
}

int gfx_sprite_anim_asset_add_clip(gfx_sprite_anim_asset_t *asset,
                                   const char *name,
                                   int first_frame,
                                   int frame_count,
                                   gfx_sprite_anim_play_mode_t play_mode)
{
    if (!gfx_sprite_anim_valid_asset(asset) || !name)
        return -1;

    if (asset->clip_count >= asset->clip_capacity)
        return -1;

    if (first_frame < 0 || frame_count <= 0)
        return -1;

    if (first_frame + frame_count > asset->frame_count)
        return -1;

    asset->clips[asset->clip_count].name = name;
    asset->clips[asset->clip_count].first_frame = first_frame;
    asset->clips[asset->clip_count].frame_count = frame_count;
    asset->clips[asset->clip_count].play_mode = play_mode;
    asset->clip_count++;
    return asset->clip_count - 1;
}

int gfx_sprite_anim_asset_find_clip(const gfx_sprite_anim_asset_t *asset,
                                    const char *name)
{
    int i;

    if (!gfx_sprite_anim_valid_asset(asset) || !name)
        return -1;

    for (i = 0; i < asset->clip_count; ++i) {
        if (asset->clips[i].name && strcmp(asset->clips[i].name, name) == 0)
            return i;
    }

    return -1;
}

int gfx_sprite_anim_asset_add_grid_strip(gfx_sprite_anim_asset_t *asset,
                                         float start_x,
                                         float start_y,
                                         float frame_w,
                                         float frame_h,
                                         int frame_count,
                                         float duration)
{
    int i;

    if (!gfx_sprite_anim_valid_asset(asset))
        return -1;

    if (frame_w <= 0.0f || frame_h <= 0.0f || frame_count <= 0 || duration <= 0.0f)
        return -1;

    for (i = 0; i < frame_count; ++i) {
        gfx_rect_t rect;
        rect.x = start_x + frame_w * (float)i;
        rect.y = start_y;
        rect.w = frame_w;
        rect.h = frame_h;

        if (gfx_sprite_anim_asset_add_frame(asset, &rect, duration) < 0)
            return -1;
    }

    return 0;
}

void gfx_sprite_anim_player_init(gfx_sprite_anim_player_t *player,
                                 const gfx_sprite_anim_asset_t *asset,
                                 gfx_sprite_id_t sprite_id)
{
    if (!player)
        return;

    memset(player, 0, sizeof(*player));
    player->asset = asset;
    player->sprite_id = sprite_id;
    player->current_clip = GFX_SPRITE_ANIM_INVALID_CLIP;
    player->speed = 1.0f;
}

void gfx_sprite_anim_player_reset(gfx_sprite_anim_player_t *player)
{
    const gfx_sprite_anim_asset_t *asset;
    gfx_sprite_id_t sprite_id;

    if (!player)
        return;

    asset = player->asset;
    sprite_id = player->sprite_id;

    memset(player, 0, sizeof(*player));
    player->asset = asset;
    player->sprite_id = sprite_id;
    player->current_clip = GFX_SPRITE_ANIM_INVALID_CLIP;
    player->speed = 1.0f;
}

int gfx_sprite_anim_player_set_sprite(gfx_sprite_anim_player_t *player,
                                      gfx_sprite_id_t sprite_id)
{
    if (!player || sprite_id < 0)
        return -1;

    player->sprite_id = sprite_id;
    return 0;
}

int gfx_sprite_anim_player_play(gfx_sprite_anim_player_t *player,
                                const char *clip_name,
                                int restart_if_same)
{
    int clip_index;

    if (!gfx_sprite_anim_valid_player(player) || !clip_name)
        return -1;

    clip_index = gfx_sprite_anim_asset_find_clip(player->asset, clip_name);
    if (clip_index < 0)
        return -1;

    if (!restart_if_same && player->current_clip == clip_index)
        return 0;

    player->current_clip = clip_index;
    player->current_frame_in_clip = 0;
    player->time_in_frame = 0.0f;
    player->playing = 1;
    player->finished = 0;

    return gfx_sprite_anim_player_apply(player);
}

void gfx_sprite_anim_player_stop(gfx_sprite_anim_player_t *player)
{
    if (!player)
        return;

    player->playing = 0;
}

int gfx_sprite_anim_player_apply(gfx_sprite_anim_player_t *player)
{
    const gfx_sprite_anim_frame_t *frame;

    if (!gfx_sprite_anim_valid_player(player))
        return -1;

    frame = gfx_sprite_anim_player_frame(player);
    if (!frame)
        return -1;

    return gfx_sprite_set_region(player->sprite_id, &frame->rect);
}

int gfx_sprite_anim_player_update(gfx_sprite_anim_player_t *player,
                                  float dt)
{
    const gfx_sprite_anim_clip_t *clip;
    const gfx_sprite_anim_frame_t *frame;

    if (!gfx_sprite_anim_valid_player(player))
        return -1;

    if (!player->playing)
        return 0;

    if (dt < 0.0f)
        dt = 0.0f;

    clip = gfx_sprite_anim_player_clip(player);
    if (!clip)
        return -1;

    player->time_in_frame += dt * player->speed;

    for (;;) {
        frame = gfx_sprite_anim_player_frame(player);
        if (!frame)
            return -1;

        if (player->time_in_frame < frame->duration)
            break;

        player->time_in_frame -= frame->duration;
        player->current_frame_in_clip++;

        if (player->current_frame_in_clip >= clip->frame_count) {
            if (clip->play_mode == GFX_SPRITE_ANIM_PLAY_LOOP) {
                player->current_frame_in_clip = 0;
            } else {
                player->current_frame_in_clip = clip->frame_count - 1;
                player->playing = 0;
                player->finished = 1;
                break;
            }
        }
    }

    return gfx_sprite_anim_player_apply(player);
}

int gfx_sprite_anim_player_is_playing(const gfx_sprite_anim_player_t *player)
{
    if (!player)
        return 0;

    return player->playing ? 1 : 0;
}

int gfx_sprite_anim_player_is_finished(const gfx_sprite_anim_player_t *player)
{
    if (!player)
        return 0;

    return player->finished ? 1 : 0;
}