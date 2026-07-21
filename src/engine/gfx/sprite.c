#include "engine/gfx/sprite.h"

#include <string.h>

typedef struct gfx_sprite_slot {
    int used;
    int visible;
    gfx_texture_handle_t texture;
    unsigned int order;
    gfx_draw_params_t params;

    int use_src_rect;
    gfx_rect_t src_rect;
} gfx_sprite_slot_t;

static gfx_sprite_slot_t g_sprites[GFX_MAX_SPRITES];
static int g_draw_order[GFX_MAX_SPRITES];
static int g_draw_count = 0;
static unsigned int g_next_order = 0;
static int g_sprite_system_initialized = 0;

static int gfx_sprite_before(int lhs_id, int rhs_id)
{
    const gfx_sprite_slot_t *a = &g_sprites[lhs_id];
    const gfx_sprite_slot_t *b = &g_sprites[rhs_id];

    if (a->params.layer != b->params.layer)
        return a->params.layer < b->params.layer;

    return a->order < b->order;
}

static void gfx_sprite_insert_draw_order(int sprite_id)
{
    int i = g_draw_count;

    while (i > 0 && !gfx_sprite_before(g_draw_order[i - 1], sprite_id)) {
        g_draw_order[i] = g_draw_order[i - 1];
        --i;
    }

    g_draw_order[i] = sprite_id;
    ++g_draw_count;
}

static void gfx_sprite_remove_draw_order(int sprite_id)
{
    int i;

    for (i = 0; i < g_draw_count; ++i) {
        if (g_draw_order[i] == sprite_id) {
            for (; i < g_draw_count - 1; ++i)
                g_draw_order[i] = g_draw_order[i + 1];

            --g_draw_count;
            return;
        }
    }
}

static void gfx_sprite_resort(int sprite_id)
{
    gfx_sprite_remove_draw_order(sprite_id);
    gfx_sprite_insert_draw_order(sprite_id);
}

static int gfx_sprite_slot_valid(gfx_sprite_id_t sprite_id)
{
    if (!g_sprite_system_initialized)
        return 0;

    if (sprite_id < 0 || sprite_id >= GFX_MAX_SPRITES)
        return 0;

    if (!g_sprites[sprite_id].used)
        return 0;

    return 1;
}

static int gfx_sprite_alloc_slot(gfx_sprite_id_t *out_sprite_id)
{
    int i;

    if (!out_sprite_id)
        return -1;

    *out_sprite_id = -1;

    if (!g_sprite_system_initialized)
        return -1;

    for (i = 0; i < GFX_MAX_SPRITES; ++i) {
        if (!g_sprites[i].used) {
            memset(&g_sprites[i], 0, sizeof(g_sprites[i]));
            *out_sprite_id = i;
            return 0;
        }
    }

    return -1;
}

int gfx_sprite_system_init(void)
{
    memset(g_sprites, 0, sizeof(g_sprites));
    memset(g_draw_order, 0, sizeof(g_draw_order));
    g_draw_count = 0;
    g_next_order = 0;
    g_sprite_system_initialized = 1;
    return 0;
}

void gfx_sprite_system_shutdown(void)
{
    if (!g_sprite_system_initialized)
        return;

    gfx_sprite_clear_all();
    g_sprite_system_initialized = 0;
}

int gfx_sprite_create(gfx_texture_handle_t texture,
                      const gfx_draw_params_t *params,
                      gfx_sprite_id_t *out_sprite_id)
{
    gfx_sprite_id_t sprite_id;

    if (!params || !out_sprite_id)
        return -1;

    if (!gfx_texture_is_valid(texture))
        return -1;

    if (gfx_sprite_alloc_slot(&sprite_id) != 0)
        return -1;

    g_sprites[sprite_id].used = 1;
    g_sprites[sprite_id].visible = 1;
    g_sprites[sprite_id].texture = texture;
    g_sprites[sprite_id].order = g_next_order++;
    g_sprites[sprite_id].params = *params;
    g_sprites[sprite_id].use_src_rect = 0;

    gfx_sprite_insert_draw_order(sprite_id);
    *out_sprite_id = sprite_id;
    return 0;
}

int gfx_sprite_create_region(gfx_texture_handle_t texture,
                             const gfx_draw_params_t *params,
                             const gfx_rect_t *src_rect,
                             gfx_sprite_id_t *out_sprite_id)
{
    gfx_sprite_id_t sprite_id;

    if (!params || !src_rect || !out_sprite_id)
        return -1;

    if (src_rect->w <= 0.0f || src_rect->h <= 0.0f)
        return -1;

    if (gfx_sprite_create(texture, params, &sprite_id) != 0)
        return -1;

    g_sprites[sprite_id].use_src_rect = 1;
    g_sprites[sprite_id].src_rect = *src_rect;
    *out_sprite_id = sprite_id;
    return 0;
}

int gfx_sprite_update(gfx_sprite_id_t sprite_id,
                      const gfx_draw_params_t *params)
{
    int old_layer;

    if (!gfx_sprite_slot_valid(sprite_id) || !params)
        return -1;

    old_layer = g_sprites[sprite_id].params.layer;
    g_sprites[sprite_id].params = *params;

    if (g_sprites[sprite_id].params.layer != old_layer)
        gfx_sprite_resort(sprite_id);

    return 0;
}

int gfx_sprite_get_draw_params(gfx_sprite_id_t sprite_id,
                               gfx_draw_params_t *out_params)
{
    if (!gfx_sprite_slot_valid(sprite_id) || !out_params)
        return -1;

    *out_params = g_sprites[sprite_id].params;
    return 0;
}

int gfx_sprite_set_texture(gfx_sprite_id_t sprite_id,
                           gfx_texture_handle_t texture)
{
    if (!gfx_sprite_slot_valid(sprite_id))
        return -1;

    if (!gfx_texture_is_valid(texture))
        return -1;

    g_sprites[sprite_id].texture = texture;
    return 0;
}

int gfx_sprite_get_texture(gfx_sprite_id_t sprite_id,
                           gfx_texture_handle_t *out_texture)
{
    if (!gfx_sprite_slot_valid(sprite_id) || !out_texture)
        return -1;

    *out_texture = g_sprites[sprite_id].texture;
    return 0;
}

int gfx_sprite_set_region(gfx_sprite_id_t sprite_id,
                          const gfx_rect_t *src_rect)
{
    if (!gfx_sprite_slot_valid(sprite_id) || !src_rect)
        return -1;

    if (src_rect->w <= 0.0f || src_rect->h <= 0.0f)
        return -1;

    g_sprites[sprite_id].use_src_rect = 1;
    g_sprites[sprite_id].src_rect = *src_rect;
    return 0;
}

int gfx_sprite_clear_region(gfx_sprite_id_t sprite_id)
{
    if (!gfx_sprite_slot_valid(sprite_id))
        return -1;

    g_sprites[sprite_id].use_src_rect = 0;
    memset(&g_sprites[sprite_id].src_rect, 0, sizeof(g_sprites[sprite_id].src_rect));
    return 0;
}

int gfx_sprite_get_region(gfx_sprite_id_t sprite_id,
                          gfx_rect_t *out_rect,
                          int *out_has_region)
{
    if (!gfx_sprite_slot_valid(sprite_id))
        return -1;

    if (out_rect)
        *out_rect = g_sprites[sprite_id].src_rect;
    if (out_has_region)
        *out_has_region = g_sprites[sprite_id].use_src_rect;

    return 0;
}

int gfx_sprite_set_visible(gfx_sprite_id_t sprite_id,
                           int visible)
{
    if (!gfx_sprite_slot_valid(sprite_id))
        return -1;

    g_sprites[sprite_id].visible = visible ? 1 : 0;
    return 0;
}

int gfx_sprite_is_visible(gfx_sprite_id_t sprite_id)
{
    if (!gfx_sprite_slot_valid(sprite_id))
        return 0;

    return g_sprites[sprite_id].visible;
}

void gfx_sprite_remove(gfx_sprite_id_t sprite_id)
{
    if (!gfx_sprite_slot_valid(sprite_id))
        return;

    gfx_sprite_remove_draw_order(sprite_id);
    memset(&g_sprites[sprite_id], 0, sizeof(g_sprites[sprite_id]));
}

void gfx_sprite_clear_all(void)
{
    if (!g_sprite_system_initialized)
        return;

    memset(g_sprites, 0, sizeof(g_sprites));
    memset(g_draw_order, 0, sizeof(g_draw_order));
    g_draw_count = 0;
    g_next_order = 0;
}

void gfx_sprite_draw_all(void)
{
    int i;

    if (!g_sprite_system_initialized)
        return;

    for (i = 0; i < g_draw_count; ++i) {
        int sprite_id = g_draw_order[i];
        const gfx_sprite_slot_t *slot = &g_sprites[sprite_id];

        if (!slot->used || !slot->visible)
            continue;

        if (!gfx_texture_is_valid(slot->texture))
            continue;

        if (slot->use_src_rect) {
            gfx_draw_texture_region(slot->texture, &slot->params, &slot->src_rect);
        } else {
            gfx_draw_texture(slot->texture, &slot->params);
        }
    }
}