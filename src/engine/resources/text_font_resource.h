#ifndef TEXT_FONT_RESOURCE_H
#define TEXT_FONT_RESOURCE_H

#include "game_app.h"
#include "engine/resources/resources.h"
#include "engine/resources/texture_assets.h"
#include "engine/text/text.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum text_font_resource_status {
    TEXT_FONT_RESOURCE_STATUS_IDLE = 0,
    TEXT_FONT_RESOURCE_STATUS_LOADING,
    TEXT_FONT_RESOURCE_STATUS_READY,
    TEXT_FONT_RESOURCE_STATUS_FAILED
} text_font_resource_status_t;

typedef struct text_font_resource_desc {
    const char *fnt_path;
    const char *atlas_path;
} text_font_resource_desc_t;

typedef struct text_font_resource {
    resource_handle_t desc_res;
    texture_handle_t atlas_tex;

    text_font_t font;

    int tex_id;

    unsigned char prewarmed;
    unsigned char build_attempted;
    unsigned char desc_failed_logged;
    unsigned char atlas_failed_logged;

    text_font_resource_status_t status;
} text_font_resource_t;

void text_font_resource_init(text_font_resource_t *res);

int text_font_resource_request(game_app_t *app,
                               text_font_resource_t *res,
                               const text_font_resource_desc_t *desc);

void text_font_resource_update(game_app_t *app,
                               text_font_resource_t *res);

text_font_resource_status_t
text_font_resource_get_status(const text_font_resource_t *res);

int text_font_resource_is_ready(const text_font_resource_t *res);

const text_font_t *
text_font_resource_get_font(const text_font_resource_t *res);

void text_font_resource_shutdown(game_app_t *app,
                                 text_font_resource_t *res);

#ifdef __cplusplus
}
#endif

#endif /* TEXT_FONT_RESOURCE_H */