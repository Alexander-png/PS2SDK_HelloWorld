#include "engine/text/text_font_resource.h"
#include "engine/text/text_bmfont.h"
#include "engine/logging/log.h"

#include <string.h>

static resource_handle_t text_font_resource_invalid_resource(void)
{
    resource_handle_t h;
    h.index = 0xffffu;
    h.generation = 0;
    return h;
}

static texture_handle_t text_font_resource_invalid_texture(void)
{
    texture_handle_t h;
    h.index = 0xffffu;
    h.generation = 0;
    return h;
}

void text_font_resource_init(text_font_resource_t *res)
{
    if (!res)
        return;

    memset(res, 0, sizeof(*res));
    res->desc_res = text_font_resource_invalid_resource();
    res->atlas_tex = text_font_resource_invalid_texture();
    res->tex_id = -1;
    res->status = TEXT_FONT_RESOURCE_STATUS_IDLE;
    text_font_init(&res->font);
}

int text_font_resource_request(game_app_t *app,
                               text_font_resource_t *res,
                               const text_font_resource_desc_t *desc)
{
    resource_load_desc_t res_desc;

    (void)app;

    if (!res || !desc || !desc->fnt_path || !desc->atlas_path)
        return -1;

    text_font_resource_init(res);

    res_desc.path = desc->fnt_path;
    res_desc.type = RESOURCE_TYPE_RAW;
    res_desc.priority = STREAM_PRIORITY_NORMAL;
    res_desc.callback = NULL;
    res_desc.userdata = NULL;

    res->desc_res = resource_load_file(&res_desc);
    if (!resource_is_valid(res->desc_res)) {
        LOGLNC(LOGCAT_RESOURCES, "[text_font] failed to request font descriptor path=%s",
              desc->fnt_path);
        res->status = TEXT_FONT_RESOURCE_STATUS_FAILED;
        return -1;
    }

    res->atlas_tex = texture_load_png(desc->atlas_path, STREAM_PRIORITY_NORMAL);
    if (!texture_is_valid(res->atlas_tex)) {
        LOGLNC(LOGCAT_RESOURCES, "[text_font] failed to request font atlas path=%s",
              desc->atlas_path);
        resource_release(res->desc_res);
        res->desc_res = text_font_resource_invalid_resource();
        res->status = TEXT_FONT_RESOURCE_STATUS_FAILED;
        return -1;
    }

    res->status = TEXT_FONT_RESOURCE_STATUS_LOADING;

    LOGLNC(LOGCAT_RESOURCES, "[text_font] requested descriptor=%s atlas=%s",
          desc->fnt_path,
          desc->atlas_path);
    return 0;
}

void text_font_resource_update(game_app_t *app,
                               text_font_resource_t *res)
{
    resource_status_t desc_status;
    texture_status_t atlas_status;
    const void *desc_data;
    u32 desc_size;
    text_bmfont_load_desc_t load_desc;

    if (!app || !res)
        return;

    if (res->status == TEXT_FONT_RESOURCE_STATUS_READY ||
        res->status == TEXT_FONT_RESOURCE_STATUS_FAILED) {
        return;
    }

    if (!resource_is_valid(res->desc_res) || !texture_is_valid(res->atlas_tex)) {
        res->status = TEXT_FONT_RESOURCE_STATUS_FAILED;
        return;
    }

    desc_status = resource_status(res->desc_res);
    atlas_status = texture_status(res->atlas_tex);

    if (desc_status == RESOURCE_STATUS_FAILED) {
        if (!res->desc_failed_logged) {
            LOGLNC(LOGCAT_RESOURCES, "[text_font] font descriptor failed path=%s",
                  resource_path(res->desc_res));
            res->desc_failed_logged = 1;
        }
        res->status = TEXT_FONT_RESOURCE_STATUS_FAILED;
        return;
    }

    if (atlas_status == TEXTURE_STATUS_FAILED) {
        if (!res->atlas_failed_logged) {
            LOGLNC(LOGCAT_RESOURCES, "[text_font] font atlas failed path=%s",
                  texture_path(res->atlas_tex));
            res->atlas_failed_logged = 1;
        }
        res->status = TEXT_FONT_RESOURCE_STATUS_FAILED;
        return;
    }

    if (desc_status != RESOURCE_STATUS_READY ||
        atlas_status != TEXTURE_STATUS_READY) {
        res->status = TEXT_FONT_RESOURCE_STATUS_LOADING;
        return;
    }

    res->tex_id = texture_tex_id(res->atlas_tex);
    if (res->tex_id < 0) {
        res->status = TEXT_FONT_RESOURCE_STATUS_LOADING;
        return;
    }

    if (!res->prewarmed) {
        int warm = texture_prewarm(res->atlas_tex);
        LOGLNC(LOGCAT_RESOURCES, "[text_font] prewarm tex_id=%d result=%d", res->tex_id, warm);
        res->prewarmed = 1;
    }

    if (res->build_attempted) {
        return;
    }

    desc_data = resource_data(res->desc_res);
    desc_size = resource_size(res->desc_res);

    if (!desc_data || desc_size == 0) {
        LOGLNC(LOGCAT_RESOURCES, "[text_font] descriptor ready but empty");
        res->build_attempted = 1;
        res->status = TEXT_FONT_RESOURCE_STATUS_FAILED;
        return;
    }

    load_desc.fnt_text = (const char *)desc_data;
    load_desc.fnt_size = (unsigned int)desc_size;
    load_desc.debug_name = resource_path(res->desc_res);
    load_desc.tex_id = res->tex_id;

    res->build_attempted = 1;

    if (text_bmfont_load_from_memory(game_app_state_arena(app),
                                     &res->font,
                                     &load_desc) != 0) {
        LOGLNC(LOGCAT_RESOURCES, "[text_font] text_bmfont_load_from_memory failed");
        res->status = TEXT_FONT_RESOURCE_STATUS_FAILED;
        return;
    }

    res->status = TEXT_FONT_RESOURCE_STATUS_READY;

    LOGLNC(LOGCAT_RESOURCES, "[text_font] ready glyphs=%u kernings=%u",
          (unsigned int)res->font.glyph_count,
          (unsigned int)res->font.kerning_count);
}

text_font_resource_status_t
text_font_resource_get_status(const text_font_resource_t *res)
{
    if (!res)
        return TEXT_FONT_RESOURCE_STATUS_FAILED;

    return res->status;
}

int text_font_resource_is_ready(const text_font_resource_t *res)
{
    return res && res->status == TEXT_FONT_RESOURCE_STATUS_READY;
}

const text_font_t *
text_font_resource_get_font(const text_font_resource_t *res)
{
    if (!res || res->status != TEXT_FONT_RESOURCE_STATUS_READY)
        return NULL;

    return &res->font;
}

void text_font_resource_shutdown(game_app_t *app,
                                 text_font_resource_t *res)
{
    (void)app;

    if (!res)
        return;

    if (resource_is_valid(res->desc_res))
        resource_release(res->desc_res);

    if (texture_is_valid(res->atlas_tex))
        texture_release(res->atlas_tex);

    text_font_shutdown(&res->font);
    text_font_resource_init(res);
}