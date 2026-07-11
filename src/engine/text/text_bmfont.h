#ifndef TEXT_BMFONT_H
#define TEXT_BMFONT_H


#include "engine/memory/memory_arena.h"
#include "engine/text/text.h"


#ifdef __cplusplus
extern "C" {
#endif


typedef struct text_bmfont_load_desc {
    const char *fnt_text;
    unsigned int fnt_size;
    const char *debug_name;
    int tex_id;
} text_bmfont_load_desc_t;


int text_bmfont_load_from_memory(mem_arena_t *arena,
                                 text_font_t *font,
                                 const text_bmfont_load_desc_t *desc);


#ifdef __cplusplus
}
#endif


#endif /* TEXT_BMFONT_H */