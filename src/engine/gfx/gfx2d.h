#ifndef GFX2D_H
#define GFX2D_H

#ifndef GFX2D_MAX_TEXTURES
#define GFX2D_MAX_TEXTURES 16
#endif

#ifdef __cplusplus
extern "C" {
#endif

int  gfx2d_init(void);
void gfx2d_shutdown(void);

void gfx2d_begin_frame(void);
void gfx2d_end_frame(void);

int  gfx2d_load_texture(const char *path, int *out_tex_id);
void gfx2d_free_texture(int tex_id);
void gfx2d_draw_sprite(int tex_id, float x, float y, float w, float h);

void gfx2d_draw_sprite_ex(
    int tex_id,
    float x, float y,
    float w, float h,
    float origin_x, float origin_y,
    float scale_x, float scale_y,
    float rotation_rad,
    int flip_x, int flip_y
);

void gfx2d_draw_quad(
    int tex_id,
    float x0, float y0,
    float x1, float y1,
    float x2, float y2,
    float x3, float y3
);

#ifdef __cplusplus
}
#endif

#endif