/* engine/gfx/gfx2d.h */
#ifndef GFX2D_H
#define GFX2D_H

#ifdef __cplusplus
extern "C" {
#endif

int  gfx2d_init(void);
void gfx2d_shutdown(void);

void gfx2d_begin_frame(void);
void gfx2d_end_frame(void);

int  gfx2d_load_texture(const char *path, int *out_tex_id);
void gfx2d_draw_sprite(int tex_id, float x, float y, float w, float h);

#ifdef __cplusplus
}
#endif

#endif