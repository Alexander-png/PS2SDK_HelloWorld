#include "engine/gfx/texture.h"
#include "engine/gfx/internal/texture_internal.h"
#include "engine/gfx/internal/renderer_internal.h"
#include "engine/logging/log.h"
#include "engine/memory/memory.h"

#include <string.h>
#include <png.h>
#include <gsTexManager.h>

#define GS_VRAM_SIZE (4 * 1024 * 1024)

typedef struct gfx_texture_slot {
    int used;
    u16 generation;
    u32 flags;
    u32 last_used_frame;
    gfx_texture_cpu_image_t cpu;
    GSTEXTURE gs;
} gfx_texture_slot_t;

typedef struct gfx_png_buffer {
    const unsigned char *data;
    u32 size;
    u32 offset;
} gfx_png_buffer_t;

typedef struct gfx_rgba32_pixel {
    u8 r, g, b, a;
} gfx_rgba32_pixel_t;

static gfx_texture_slot_t g_textures[GFX_TEXTURE_MAX_ITEMS];

static u8 gfx_texture_modulate_to_gs(u8 alpha)
{
    return (u8)(((int)alpha * 128 + 127) / 255);
}

static void gfx_texture_png_read_fn(png_structp png_ptr, png_bytep out_bytes, png_size_t byte_count)
{
    gfx_png_buffer_t *buf = (gfx_png_buffer_t *)png_get_io_ptr(png_ptr);

    if (!buf || buf->offset + (u32)byte_count > buf->size)
        png_error(png_ptr, "gfx_texture_png_read_fn overflow");

    memcpy(out_bytes, buf->data + buf->offset, byte_count);
    buf->offset += (u32)byte_count;
}

static void gfx_texture_free_png_rows(png_bytep *rows, int height)
{
    int y;

    if (!rows)
        return;

    for (y = 0; y < height; ++y) {
        if (rows[y])
            mem_free(rows[y], MEMTAG_TEMP);
    }

    mem_free(rows, MEMTAG_TEMP);
}

static void gfx_texture_pack_rgba32_for_gs(void *dst_data, png_bytep *rows, int width, int height)
{
    gfx_rgba32_pixel_t *dst;
    int x, y, k;

    if (!dst_data || !rows || width <= 0 || height <= 0)
        return;

    dst = (gfx_rgba32_pixel_t *)dst_data;
    k = 0;

    for (y = 0; y < height; ++y) {
        png_bytep src = rows[y];
        for (x = 0; x < width; ++x) {
            dst[k].r = src[4 * x + 0];
            dst[k].g = src[4 * x + 1];
            dst[k].b = src[4 * x + 2];
            dst[k].a = gfx_texture_modulate_to_gs(src[4 * x + 3]);
            ++k;
        }
    }
}

static gfx_texture_handle_t gfx_texture_make_handle(int index, u16 generation)
{
    gfx_texture_handle_t h;
    h.index = (u16)index;
    h.generation = generation;
    return h;
}

gfx_texture_handle_t gfx_texture_invalid(void)
{
    gfx_texture_handle_t h;
    h.index = 0xffffu;
    h.generation = 0;
    return h;
}

static int gfx_texture_handle_to_index(gfx_texture_handle_t handle)
{
    gfx_texture_slot_t *t;

    if (handle.index >= GFX_TEXTURE_MAX_ITEMS)
        return -1;

    t = &g_textures[handle.index];
    if (!t->used)
        return -1;
    if (t->generation != handle.generation)
        return -1;

    return (int)handle.index;
}

int gfx_texture_is_valid(gfx_texture_handle_t handle)
{
    return gfx_texture_handle_to_index(handle) >= 0;
}

int gfx_texture_system_init(void)
{
    GSGLOBAL *gs = renderer_ps2_gs();

    memset(g_textures, 0, sizeof(g_textures));

    if (!gs)
        return -1;

    gsKit_TexManager_init(gs);
    LOGLNC(LOGCAT_GFX, "[texture] init max_items=%d", GFX_TEXTURE_MAX_ITEMS);
    return 0;
}

void gfx_texture_system_shutdown(void)
{
    int i;
    for (i = 0; i < GFX_TEXTURE_MAX_ITEMS; ++i) {
        if (g_textures[i].used)
            gfx_texture_release(gfx_texture_make_handle(i, g_textures[i].generation));
    }
}

int gfx_texture_create_from_png_data(const void *data,
                                     u32 size,
                                     const gfx_texture_desc_t *desc,
                                     gfx_texture_handle_t *out_handle)
{
    GSGLOBAL *gs = renderer_ps2_gs();
    png_structp png_ptr = NULL;
    png_infop info_ptr = NULL;
    png_bytep *rows = NULL;
    gfx_png_buffer_t buf;
    gfx_texture_slot_t *slot = NULL;
    void *image_data = NULL;
    int index = -1;
    png_uint_32 width = 0, height = 0;
    int bit_depth = 0, color_type = 0;
    u32 rowbytes = 0;
    u32 flags = desc ? desc->flags : 0;
    int y;
    int result = -1;

    if (out_handle)
        *out_handle = gfx_texture_invalid();

    if (!gs || !data || !size || !out_handle)
        return -1;

    for (index = 0; index < GFX_TEXTURE_MAX_ITEMS; ++index) {
        if (!g_textures[index].used)
            break;
    }

    if (index >= GFX_TEXTURE_MAX_ITEMS) {
        LOGLNC(LOGCAT_GFX, "[texture] no free slots");
        return -1;
    }

    slot = &g_textures[index];
    {
        u16 generation = slot->generation;
        memset(slot, 0, sizeof(*slot));
        slot->generation = generation + 1;
        if (slot->generation == 0)
            slot->generation = 1;
    }

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

    png_set_read_fn(png_ptr, &buf, gfx_texture_png_read_fn);
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
    if (color_type == PNG_COLOR_TYPE_GRAY || color_type == PNG_COLOR_TYPE_GRAY_ALPHA)
        png_set_gray_to_rgb(png_ptr);
    if (!(color_type & PNG_COLOR_MASK_ALPHA))
        png_set_filler(png_ptr, 0xFF, PNG_FILLER_AFTER);

    png_read_update_info(png_ptr, info_ptr);

    bit_depth = png_get_bit_depth(png_ptr, info_ptr);
    color_type = png_get_color_type(png_ptr, info_ptr);
    rowbytes = (u32)png_get_rowbytes(png_ptr, info_ptr);

    if (bit_depth != 8 || color_type != PNG_COLOR_TYPE_RGBA)
        goto cleanup;

    rows = (png_bytep *)mem_calloc(height, sizeof(png_bytep), 0, MEMTAG_TEMP);
    if (!rows)
        goto cleanup;

    for (y = 0; y < (int)height; ++y) {
        rows[y] = (png_bytep)mem_alloc(rowbytes, 0, MEMTAG_TEMP);
        if (!rows[y])
            goto cleanup;
    }

    png_read_image(png_ptr, rows);
    png_read_end(png_ptr, NULL);

    image_data = mem_alloc(gsKit_texture_size_ee((int)width, (int)height, GS_PSM_CT32),
                           128,
                           MEMTAG_GFX);
    if (!image_data)
        goto cleanup;

    gfx_texture_pack_rgba32_for_gs(image_data, rows, (int)width, (int)height);

    memset(&slot->gs, 0, sizeof(slot->gs));
    slot->gs.Width = (int)width;
    slot->gs.Height = (int)height;
    slot->gs.PSM = GS_PSM_CT32;
    slot->gs.Filter = GS_FILTER_LINEAR;
    slot->gs.Mem = image_data;
    slot->gs.Clut = NULL;
    slot->gs.Vram = 0;
    slot->gs.VramClut = 0;

    gsKit_setup_tbw(&slot->gs);

    slot->cpu.pixels = image_data;
    slot->cpu.size_bytes = gsKit_texture_size_ee((int)width, (int)height, GS_PSM_CT32);
    slot->cpu.width = (u16)width;
    slot->cpu.height = (u16)height;
    slot->cpu.psm = GS_PSM_CT32;

    slot->used = 1;
    slot->flags = flags;
    slot->last_used_frame = renderer_frame_index();

    *out_handle = gfx_texture_make_handle(index, slot->generation);
    image_data = NULL;
    result = 0;

cleanup:
    gfx_texture_free_png_rows(rows, (int)height);

    if (png_ptr || info_ptr)
        png_destroy_read_struct(&png_ptr, &info_ptr, NULL);

    if (result != 0) {
        if (slot)
            memset(slot, 0, sizeof(*slot));
        if (image_data)
            mem_free(image_data, MEMTAG_GFX);
    }

    return result;
}

void gfx_texture_release(gfx_texture_handle_t handle)
{
    GSGLOBAL *gs = renderer_ps2_gs();
    int index = gfx_texture_handle_to_index(handle);
    gfx_texture_slot_t *slot;

    if (index < 0)
        return;

    slot = &g_textures[index];

    if (gs)
        gsKit_TexManager_free(gs, &slot->gs);

    if (slot->gs.Mem) {
        mem_free(slot->gs.Mem, MEMTAG_GFX);
        slot->gs.Mem = NULL;
    }

    if (slot->gs.Clut) {
        mem_free(slot->gs.Clut, MEMTAG_GFX);
        slot->gs.Clut = NULL;
    }

    {
        u16 generation = slot->generation;
        memset(slot, 0, sizeof(*slot));
        slot->generation = generation;
    }
}

int gfx_texture_touch(gfx_texture_handle_t handle)
{
    GSGLOBAL *gs = renderer_ps2_gs();
    int index = gfx_texture_handle_to_index(handle);
    gfx_texture_slot_t *slot;
    unsigned int transfer_mask;

    if (!gs || index < 0)
        return -1;

    slot = &g_textures[index];

    if (!slot->gs.Mem)
        return -1;

    transfer_mask = gsKit_TexManager_bind(gs, &slot->gs);
    slot->last_used_frame = renderer_frame_index();
    return (int)transfer_mask;
}

int gfx_texture_get_size(gfx_texture_handle_t handle, int *out_w, int *out_h)
{
    int index = gfx_texture_handle_to_index(handle);

    if (!out_w || !out_h)
        return -1;

    *out_w = 0;
    *out_h = 0;

    if (index < 0)
        return -1;

    *out_w = g_textures[index].gs.Width;
    *out_h = g_textures[index].gs.Height;
    return 0;
}

int gfx_texture_get_stats(gfx_texture_stats_t *out)
{
    GSGLOBAL *gs = renderer_ps2_gs();
    u32 used = 0;
    int i;

    if (!out)
        return -1;

    memset(out, 0, sizeof(*out));
    out->total_bytes = GS_VRAM_SIZE;

    for (i = 0; i < GFX_TEXTURE_MAX_ITEMS; ++i) {
        if (g_textures[i].used)
            ++out->texture_count;
    }

    if (!gs) {
        out->free_bytes = out->total_bytes;
        return 0;
    }

    used = (u32)gs->CurrentPointer;
    if (used > GS_VRAM_SIZE)
        used = GS_VRAM_SIZE;

    out->used_bytes = used;
    out->free_bytes = GS_VRAM_SIZE - used;
    return 0;
}

int gfx_texture_get_gstexture_internal(gfx_texture_handle_t handle, GSTEXTURE **out_tex)
{
    int index;

    if (!out_tex)
        return -1;

    *out_tex = NULL;

    index = gfx_texture_handle_to_index(handle);
    if (index < 0)
        return -1;

    *out_tex = &g_textures[index].gs;
    return 0;
}