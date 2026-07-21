#ifndef TEXT_BMFONT_H
#define TEXT_BMFONT_H

#include "engine/text/text.h"
#include "engine/gfx/texture.h"
#include "engine/memory/memory_arena.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct text_bmfont_load_desc {
    const char *fnt_text;
    unsigned int fnt_size;
    const char *debug_name;
    gfx_texture_handle_t texture;
} text_bmfont_load_desc_t;

int text_bmfont_load_from_memory(mem_arena_t *arena,
                                 text_font_t *out_font,
                                 const text_bmfont_load_desc_t *desc);

#ifdef __cplusplus
}
#endif

#endif