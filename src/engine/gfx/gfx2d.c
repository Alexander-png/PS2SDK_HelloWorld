#include "engine/logging/log.h"
#include "gfx2d.h"

#include <gsKit.h>
#include <dmaKit.h>
#include <gsToolkit.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <png.h>
#include <malloc.h>
#include <gsTexManager.h>

typedef struct texture_slot {
    int used;
    GSTEXTURE tex;
} texture_slot_t;

typedef struct gfx2d_sprite_slot {
    int used;
    int tex_id;
    unsigned int order;
    gfx2d_draw_params_t params;
} gfx2d_sprite_slot_t;

typedef struct gfx2d_png_buffer {
    const unsigned char *data;
    u32 size;
    u32 offset;
} gfx2d_png_buffer_t;


typedef struct gfx2d_rgba32_pixel {
    u8 r;
    u8 g;
    u8 b;
    u8 a;
} gfx2d_rgba32_pixel_t;

static GSGLOBAL *g_gs;
static texture_slot_t g_textures[GFX2D_MAX_TEXTURES];
static gfx2d_sprite_slot_t g_sprites[GFX2D_MAX_SPRITES];
static int g_draw_order[GFX2D_MAX_SPRITES];
static int g_draw_count = 0;
static unsigned int g_next_order = 0;

static u64 g_clear_color = GS_SETREG_RGBAQ(0x00, 0x00, 0x00, 0x80, 0x00);

static u8 gfx2d_png_alpha_to_gs(u8 alpha)
{
    return (u8)(((int)alpha * 128 + 127) / 255);
}

static void gfx2d_pack_rgba32_for_gs(void *dst_data,
                                     png_bytep *rows,
                                     int width,
                                     int height)
{
    gfx2d_rgba32_pixel_t *dst;
    int x;
    int y;
    int k;

    if (!dst_data || !rows || width <= 0 || height <= 0)
        return;

    dst = (gfx2d_rgba32_pixel_t *)dst_data;
    k = 0;

    for (y = 0; y < height; ++y) {
        png_bytep src = rows[y];

        for (x = 0; x < width; ++x) {
            dst[k].r = src[4 * x + 0];
            dst[k].g = src[4 * x + 1];
            dst[k].b = src[4 * x + 2];
            dst[k].a = gfx2d_png_alpha_to_gs(src[4 * x + 3]);
            ++k;
        }
    }
}

static void gfx2d_free_png_rows(png_bytep *rows, int height)
{
    int y;

    if (!rows)
        return;

    for (y = 0; y < height; ++y) {
        if (rows[y])
            free(rows[y]);
    }

    free(rows);
}

static void gfx2d_png_read_fn(png_structp png_ptr, png_bytep out_bytes, png_size_t byte_count)
{
    gfx2d_png_buffer_t *buf = (gfx2d_png_buffer_t *)png_get_io_ptr(png_ptr);

    if (!buf || buf->offset + (u32)byte_count > buf->size)
        png_error(png_ptr, "gfx2d_png_read_fn overflow");

    memcpy(out_bytes, buf->data + buf->offset, byte_count);
    buf->offset += (u32)byte_count;
}

static int gfx2d_alloc_texture_slot(int *out_tex_id)
{
    int i;

    if (!out_tex_id)
        return -1;

    *out_tex_id = -1;

    for (i = 0; i < GFX2D_MAX_TEXTURES; ++i) {
        if (!g_textures[i].used) {
            memset(&g_textures[i].tex, 0, sizeof(g_textures[i].tex));
            *out_tex_id = i;
            return 0;
        }
    }

    return -1;
}

static u64 gfx2d_make_rgbaq(gfx2d_color_t color)
{
    return GS_SETREG_RGBAQ(color.r, color.g, color.b, color.a, 0x00);
}

static float gfx2d_resolve_halign(gfx2d_halign_t align, float w)
{
    switch (align) {
        case GFX2D_HALIGN_LEFT:   return 0.0f;
        case GFX2D_HALIGN_CENTER: return w * 0.5f;
        case GFX2D_HALIGN_RIGHT:  return w;
        default:                  return 0.0f;
    }
}

static float gfx2d_resolve_valign(gfx2d_valign_t align, float h)
{
    switch (align) {
        case GFX2D_VALIGN_TOP:    return 0.0f;
        case GFX2D_VALIGN_CENTER: return h * 0.5f;
        case GFX2D_VALIGN_BOTTOM: return h;
        default:                  return 0.0f;
    }
}

static void gfx2d_transform_corner(const gfx2d_draw_params_t *params,
                                   float pivot_x,
                                   float pivot_y,
                                   float skew_pivot_x,
                                   float skew_pivot_y,
                                   float local_x,
                                   float local_y,
                                   gfx2d_corner_t *out)
{
    float sx = tanf(params->skew_x_rad);
    float sy = tanf(params->skew_y_rad);
    float x, y;
    float c, s;
    float rx, ry;

    x = local_x;
    y = local_y;

    /* skew around skew pivot */
    x -= skew_pivot_x;
    y -= skew_pivot_y;
    {
        float skewed_x = x + sx * y;
        float skewed_y = y + sy * x;
        x = skewed_x;
        y = skewed_y;
    }
    x += skew_pivot_x;
    y += skew_pivot_y;

    /* scale + rotate around regular pivot */
    x -= pivot_x;
    y -= pivot_y;

    x *= params->scale_x;
    y *= params->scale_y;

    c = cosf(params->rotation_rad);
    s = sinf(params->rotation_rad);

    rx = x * c - y * s;
    ry = x * s + y * c;

    out->x = params->x + rx;
    out->y = params->y + ry;
    out->z = (float)params->layer;
}

static void gfx2d_draw_vertices(int tex_id,
                                const gfx2d_corner_t *top_left,
                                const gfx2d_corner_t *top_right,
                                const gfx2d_corner_t *bottom_left,
                                const gfx2d_corner_t *bottom_right,
                                u64 color)
{
    GSTEXTURE *tex;

    if (!g_gs)
        return;

    if (tex_id < 0 || tex_id >= GFX2D_MAX_TEXTURES || !g_textures[tex_id].used)
        return;

    tex = &g_textures[tex_id].tex;

    gsKit_prim_quad_texture_3d(
        g_gs,
        tex,
        top_left->x,     top_left->y,     top_left->z,     top_left->u,     top_left->v,
        top_right->x,    top_right->y,    top_right->z,    top_right->u,    top_right->v,
        bottom_left->x,  bottom_left->y,  bottom_left->z,  bottom_left->u,  bottom_left->v,
        bottom_right->x, bottom_right->y, bottom_right->z, bottom_right->u, bottom_right->v,
        color
    );
}

static void gfx2d_draw_sprite_internal(int tex_id, const gfx2d_draw_params_t *params)
{
    GSTEXTURE *tex;
    gfx2d_corner_t top_left, top_right, bottom_left, bottom_right;
    float u_left, u_right, v_top, v_bottom;
    float anchor_x, anchor_y;
    float pivot_x, pivot_y;
    float skew_pivot_x, skew_pivot_y;
    float local_left, local_top, local_right, local_bottom;

    if (!g_gs || !params)
        return;

    if (tex_id < 0 || tex_id >= GFX2D_MAX_TEXTURES || !g_textures[tex_id].used)
        return;

    if (params->w == 0.0f || params->h == 0.0f)
        return;

    tex = &g_textures[tex_id].tex;

    anchor_x = gfx2d_resolve_halign(params->anchor_h, params->w);
    anchor_y = gfx2d_resolve_valign(params->anchor_v, params->h);

    local_left   = -anchor_x;
    local_top    = -anchor_y;
    local_right  = local_left + params->w;
    local_bottom = local_top + params->h;

    pivot_x = gfx2d_resolve_halign(params->origin_h, params->w) - anchor_x;
    pivot_y = gfx2d_resolve_valign(params->origin_v, params->h) - anchor_y;

    skew_pivot_x = gfx2d_resolve_halign(params->skew_origin_h, params->w) - anchor_x;
    skew_pivot_y = gfx2d_resolve_valign(params->skew_origin_v, params->h) - anchor_y;

    gfx2d_transform_corner(params, pivot_x, pivot_y, skew_pivot_x, skew_pivot_y,
                           local_left,  local_top,    &top_left);
    gfx2d_transform_corner(params, pivot_x, pivot_y, skew_pivot_x, skew_pivot_y,
                           local_right, local_top,    &top_right);
    gfx2d_transform_corner(params, pivot_x, pivot_y, skew_pivot_x, skew_pivot_y,
                           local_left,  local_bottom, &bottom_left);
    gfx2d_transform_corner(params, pivot_x, pivot_y, skew_pivot_x, skew_pivot_y,
                           local_right, local_bottom, &bottom_right);

    u_left   = params->flip_x ? (float)tex->Width  : 0.0f;
    u_right  = params->flip_x ? 0.0f               : (float)tex->Width;
    v_top    = params->flip_y ? (float)tex->Height : 0.0f;
    v_bottom = params->flip_y ? 0.0f               : (float)tex->Height;

    top_left.u = u_left;
    top_left.v = v_top;

    top_right.u = u_right;
    top_right.v = v_top;

    bottom_left.u = u_left;
    bottom_left.v = v_bottom;

    bottom_right.u = u_right;
    bottom_right.v = v_bottom;

    gfx2d_draw_vertices(tex_id,
                        &top_left,
                        &top_right,
                        &bottom_left,
                        &bottom_right,
                        gfx2d_make_rgbaq(params->color));
}

static void gfx2d_draw_slot(const gfx2d_sprite_slot_t *slot)
{
    GSTEXTURE *tex;

    if (!slot || !slot->used)
        return;

    if (slot->tex_id < 0 || slot->tex_id >= GFX2D_MAX_TEXTURES)
        return;

    if (!g_textures[slot->tex_id].used)
        return;

    tex = &g_textures[slot->tex_id].tex;

    if (!tex->Mem)
        return;

    gsKit_TexManager_bind(g_gs, tex);
    gfx2d_draw_sprite_internal(slot->tex_id, &slot->params);
}

static int gfx2d_sprite_before(int lhs_id, int rhs_id)
{
    const gfx2d_sprite_slot_t *a = &g_sprites[lhs_id];
    const gfx2d_sprite_slot_t *b = &g_sprites[rhs_id];

    if (a->params.layer != b->params.layer)
        return a->params.layer < b->params.layer;

    return a->order < b->order;
}

static void gfx2d_insert_draw_order(int sprite_id)
{
    int i = g_draw_count;

    while (i > 0 && !gfx2d_sprite_before(g_draw_order[i - 1], sprite_id)) {
        g_draw_order[i] = g_draw_order[i - 1];
        --i;
    }

    g_draw_order[i] = sprite_id;
    ++g_draw_count;
}

static void gfx2d_remove_draw_order(int sprite_id)
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

static void gfx2d_resort_sprite(int sprite_id)
{
    gfx2d_remove_draw_order(sprite_id);
    gfx2d_insert_draw_order(sprite_id);
}

int gfx2d_init(void)
{
    g_gs = gsKit_init_global();
    if (!g_gs)
        return -1;

    g_gs->PSM  = GS_PSM_CT32;
    g_gs->Mode = gsKit_check_rom();
    g_gs->Height = (g_gs->Mode == GS_MODE_PAL) ? 512 : 448;

    dmaKit_init(D_CTRL_RELE_OFF,
                D_CTRL_MFD_OFF,
                D_CTRL_STS_UNSPEC,
                D_CTRL_STD_OFF,
                D_CTRL_RCYC_8,
                1 << DMA_CHANNEL_GIF);
    dmaKit_chan_init(DMA_CHANNEL_GIF);

    gsKit_init_screen(g_gs);

    g_gs->PrimAlphaEnable = GS_SETTING_ON;
    gsKit_set_primalpha(g_gs, GS_SETREG_ALPHA(0, 1, 0, 1, 0), 0);
    gsKit_set_test(g_gs, GS_ZTEST_OFF);
    gsKit_mode_switch(g_gs, GS_ONESHOT);

    gsKit_TexManager_init(g_gs);

    memset(g_textures, 0, sizeof(g_textures));
    gfx2d_clear_sprites();

    return 0;
}

void gfx2d_shutdown(void)
{
    int i;

    if (!g_gs)
        return;

    gfx2d_clear_sprites();

    for (i = 0; i < GFX2D_MAX_TEXTURES; ++i) {
        if (g_textures[i].used)
            gfx2d_free_texture(i);
    }

    g_gs = NULL;
}

void gfx2d_begin_frame(void)
{
    int oldAlpha;

    if (!g_gs)
        return;

    oldAlpha = g_gs->PrimAlphaEnable;
    g_gs->PrimAlphaEnable = GS_SETTING_OFF;
    gsKit_clear(g_gs, g_clear_color);
    g_gs->PrimAlphaEnable = oldAlpha;
}

void gfx2d_end_frame(void)
{
    if (!g_gs)
        return;

    gsKit_queue_exec(g_gs);
    gsKit_sync_flip(g_gs);
    gsKit_TexManager_nextFrame(g_gs);
}

int gfx2d_create_texture_from_png_data(const void *data, u32 size, int *out_tex_id)
{
    png_structp png_ptr = NULL;
    png_infop info_ptr = NULL;
    png_bytep *rows = NULL;
    gfx2d_png_buffer_t buf;
    GSTEXTURE *tex = NULL;
    void *image_data = NULL;
    int tex_id = -1;
    png_uint_32 width = 0;
    png_uint_32 height = 0;
    int bit_depth = 0;
    int color_type = 0;
    u32 rowbytes = 0;
    int y;
    int result = -1;

    if (!g_gs || !data || size == 0 || !out_tex_id)
        return -1;

    *out_tex_id = -1;

    if (gfx2d_alloc_texture_slot(&tex_id) < 0)
        return -1;

    png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if (!png_ptr)
        goto cleanup;

    info_ptr = png_create_info_struct(png_ptr);
    if (!info_ptr)
        goto cleanup;

    if (setjmp(png_jmpbuf(png_ptr)))
        goto cleanup;

    buf.data = (const unsigned char *)data;
    buf.size = size;
    buf.offset = 0;

    png_set_read_fn(png_ptr, &buf, gfx2d_png_read_fn);
    png_read_info(png_ptr, info_ptr);

    width = png_get_image_width(png_ptr, info_ptr);
    height = png_get_image_height(png_ptr, info_ptr);
    bit_depth = png_get_bit_depth(png_ptr, info_ptr);
    color_type = png_get_color_type(png_ptr, info_ptr);

    if (width == 0 || height == 0)
        goto cleanup;

    if (bit_depth == 16)
        png_set_strip_16(png_ptr);

    if (color_type == PNG_COLOR_TYPE_PALETTE)
        png_set_palette_to_rgb(png_ptr);

    if (color_type == PNG_COLOR_TYPE_GRAY && bit_depth < 8)
        png_set_expand_gray_1_2_4_to_8(png_ptr);

    if (png_get_valid(png_ptr, info_ptr, PNG_INFO_tRNS))
        png_set_tRNS_to_alpha(png_ptr);

    if (color_type == PNG_COLOR_TYPE_GRAY ||
        color_type == PNG_COLOR_TYPE_GRAY_ALPHA)
        png_set_gray_to_rgb(png_ptr);

    if (!(color_type & PNG_COLOR_MASK_ALPHA))
        png_set_filler(png_ptr, 0xFF, PNG_FILLER_AFTER);

    png_read_update_info(png_ptr, info_ptr);

    bit_depth = png_get_bit_depth(png_ptr, info_ptr);
    color_type = png_get_color_type(png_ptr, info_ptr);
    rowbytes = (u32)png_get_rowbytes(png_ptr, info_ptr);

    if (bit_depth != 8 || color_type != PNG_COLOR_TYPE_RGBA)
        goto cleanup;

    rows = (png_bytep *)calloc(height, sizeof(png_bytep));
    if (!rows)
        goto cleanup;

    for (y = 0; y < (int)height; ++y) {
        rows[y] = (png_bytep)malloc(rowbytes);
        if (!rows[y])
            goto cleanup;
    }

    png_read_image(png_ptr, rows);
    png_read_end(png_ptr, NULL);

    image_data = memalign(128, gsKit_texture_size_ee((int)width, (int)height, GS_PSM_CT32));
    if (!image_data)
        goto cleanup;

    gfx2d_pack_rgba32_for_gs(image_data, rows, (int)width, (int)height);

    tex = &g_textures[tex_id].tex;
    memset(tex, 0, sizeof(*tex));

    tex->Width = (int)width;
    tex->Height = (int)height;
    tex->PSM = GS_PSM_CT32;
    tex->Filter = GS_FILTER_LINEAR;
    tex->Mem = image_data;
    tex->Clut = NULL;
    tex->Vram = 0;
    tex->VramClut = 0;

    gsKit_setup_tbw(tex);

    g_textures[tex_id].used = 1;
    *out_tex_id = tex_id;

    image_data = NULL;
    result = 0;

cleanup:
    gfx2d_free_png_rows(rows, (int)height);

    if (result != 0) {
        if (tex)
            memset(tex, 0, sizeof(*tex));

        if (image_data)
            free(image_data);
    }

    if (png_ptr || info_ptr)
        png_destroy_read_struct(&png_ptr, &info_ptr, NULL);

    return result;
}

int gfx2d_touch_texture(int tex_id)
{
    GSTEXTURE *tex;
    unsigned int transfer_mask;

    if (!g_gs)
        return -1;

    if (tex_id < 0 || tex_id >= GFX2D_MAX_TEXTURES || !g_textures[tex_id].used)
        return -1;

    tex = &g_textures[tex_id].tex;

    if (!tex->Mem)
        return -1;

    transfer_mask = gsKit_TexManager_bind(g_gs, tex);
    return (int)transfer_mask;
}
void gfx2d_free_texture(int tex_id)
{
    GSTEXTURE *tex;
    int i;

    if (!g_gs
        || tex_id < 0
        || tex_id >= GFX2D_MAX_TEXTURES
        || !g_textures[tex_id].used)
        return;

    for (i = 0; i < GFX2D_MAX_SPRITES; ++i) {
        if (g_sprites[i].used && g_sprites[i].tex_id == tex_id)
            gfx2d_remove_sprite(i);
    }

    tex = &g_textures[tex_id].tex;

    gsKit_TexManager_free(g_gs, tex);

    if (tex->Mem) {
        free(tex->Mem);
        tex->Mem = NULL;
    }

    if (tex->Clut) {
        free(tex->Clut);
        tex->Clut = NULL;
    }

    tex->Vram = 0;
    tex->VramClut = 0;
    memset(tex, 0, sizeof(*tex));
    g_textures[tex_id].used = 0;
}

int gfx2d_add_sprite(int tex_id, const gfx2d_draw_params_t *params, int *out_sprite_id)
{
    int i;

    if (!g_gs || !params || !out_sprite_id)
        return -1;

    if (tex_id < 0 || tex_id >= GFX2D_MAX_TEXTURES || !g_textures[tex_id].used)
        return -1;

    *out_sprite_id = -1;

    for (i = 0; i < GFX2D_MAX_SPRITES; ++i) {
        if (!g_sprites[i].used) {
            g_sprites[i].used = 1;
            g_sprites[i].tex_id = tex_id;
            g_sprites[i].order = g_next_order++;
            g_sprites[i].params = *params;

            gfx2d_insert_draw_order(i);
            *out_sprite_id = i;
            return 0;
        }
    }

    return -1;
}

int gfx2d_update_sprite(int sprite_id, const gfx2d_draw_params_t *params)
{
    int old_layer;

    if (!g_gs || !params)
        return -1;

    if (sprite_id < 0 || sprite_id >= GFX2D_MAX_SPRITES || !g_sprites[sprite_id].used)
        return -1;

    old_layer = g_sprites[sprite_id].params.layer;
    g_sprites[sprite_id].params = *params;

    if (g_sprites[sprite_id].params.layer != old_layer)
        gfx2d_resort_sprite(sprite_id);

    return 0;
}

int gfx2d_set_sprite_texture(int sprite_id, int tex_id)
{
    if (!g_gs)
        return -1;

    if (sprite_id < 0 || sprite_id >= GFX2D_MAX_SPRITES || !g_sprites[sprite_id].used)
        return -1;

    if (tex_id < 0 || tex_id >= GFX2D_MAX_TEXTURES || !g_textures[tex_id].used)
        return -1;

    g_sprites[sprite_id].tex_id = tex_id;
    return 0;
}

void gfx2d_remove_sprite(int sprite_id)
{
    if (!g_gs)
        return;

    if (sprite_id < 0 || sprite_id >= GFX2D_MAX_SPRITES || !g_sprites[sprite_id].used)
        return;

    gfx2d_remove_draw_order(sprite_id);
    memset(&g_sprites[sprite_id], 0, sizeof(g_sprites[sprite_id]));
}

void gfx2d_clear_sprites(void)
{
    memset(g_sprites, 0, sizeof(g_sprites));
    memset(g_draw_order, 0, sizeof(g_draw_order));
    g_draw_count = 0;
    g_next_order = 0;
}

gfx2d_draw_params_t gfx2d_sprite_params(float x, float y, float w, float h)
{
    gfx2d_draw_params_t p;

    memset(&p, 0, sizeof(p));

    p.x = x;
    p.y = y;
    p.layer = 0;
    p.w = w;
    p.h = h;

    p.origin_h = GFX2D_HALIGN_CENTER;
    p.origin_v = GFX2D_VALIGN_CENTER;

    p.anchor_h = GFX2D_HALIGN_CENTER;
    p.anchor_v = GFX2D_VALIGN_CENTER;

    p.skew_origin_h = GFX2D_HALIGN_CENTER;
    p.skew_origin_v = GFX2D_VALIGN_CENTER;

    p.origin_x = 0.0f;
    p.origin_y = 0.0f;

    p.scale_x = 1.0f;
    p.scale_y = 1.0f;

    p.rotation_rad = 0.0f;
    p.skew_x_rad = 0.0f;
    p.skew_y_rad = 0.0f;

    p.flip_x = 0;
    p.flip_y = 0;

    p.color.r = 0x80;
    p.color.g = 0x80;
    p.color.b = 0x80;
    p.color.a = 0x80; 

    return p;
}

void gfx2d_draw(void)
{
    int i;

    if (!g_gs)
        return;

    for (i = 0; i < g_draw_count; ++i) {
        int sprite_id = g_draw_order[i];
        const gfx2d_sprite_slot_t *slot = &g_sprites[sprite_id];

        if (slot->used)
            gfx2d_draw_slot(slot);
    }
}