#include "engine/text/text_bmfont.h"
#include "engine/logging/log.h"

#include <string.h>

typedef struct text_bmfont_parse_ctx {
    const char *text;
    unsigned int size;
    const char *debug_name;

    unsigned short line_height;
    short base;
    unsigned short scale_w;
    unsigned short scale_h;
    int pages;

    int saw_common;
    int saw_page;

    unsigned int chars_declared;
    unsigned int chars_found;

    unsigned int kernings_declared;
    unsigned int kernings_found;
} text_bmfont_parse_ctx_t;

static int text_bmfont_is_space(char c)
{
    return (c == ' ' || c == '\t' || c == '\r' || c == '\n');
}

static int text_bmfont_starts_with(const char *s, const char *prefix)
{
    if (!s || !prefix)
        return 0;

    while (*prefix) {
        if (*s != *prefix)
            return 0;
        ++s;
        ++prefix;
    }

    return 1;
}

static const char *text_bmfont_find_key(const char *line, const char *key)
{
    size_t key_len;
    const char *p;

    if (!line || !key)
        return NULL;

    key_len = strlen(key);
    p = line;

    while (*p) {
        if ((p == line || text_bmfont_is_space(*(p - 1))) &&
            strncmp(p, key, key_len) == 0 &&
            p[key_len] == '=') {
            return p + key_len + 1;
        }
        ++p;
    }

    return NULL;
}

static int text_bmfont_parse_int_field(const char *line, const char *key, int *out_value)
{
    const char *p;
    int sign;
    int value;

    if (!out_value)
        return -1;

    p = text_bmfont_find_key(line, key);
    if (!p)
        return -1;

    sign = 1;
    if (*p == '-') {
        sign = -1;
        ++p;
    }

    if (*p < '0' || *p > '9')
        return -1;

    value = 0;
    while (*p >= '0' && *p <= '9') {
        value = value * 10 + (*p - '0');
        ++p;
    }

    *out_value = value * sign;
    return 0;
}

static int text_bmfont_parse_quoted_field(const char *line,
                                          const char *key,
                                          char *dst,
                                          unsigned int dst_size)
{
    const char *p;
    unsigned int n;

    if (!dst || dst_size == 0)
        return -1;

    p = text_bmfont_find_key(line, key);
    if (!p || *p != '"')
        return -1;

    ++p;
    n = 0;

    while (*p && *p != '"') {
        if (n + 1 >= dst_size)
            return -1;
        dst[n++] = *p++;
    }

    if (*p != '"')
        return -1;

    dst[n] = '\0';
    return 0;
}

static unsigned int text_bmfont_copy_line(const char *src,
                                          unsigned int remaining,
                                          char *dst,
                                          unsigned int dst_size)
{
    unsigned int i;

    if (!src || !dst || dst_size == 0)
        return 0;

    i = 0;
    while (i < remaining && src[i] != '\n' && i + 1 < dst_size) {
        dst[i] = src[i];
        ++i;
    }

    dst[i] = '\0';

    if (i < remaining && src[i] == '\n')
        return i + 1;

    return i;
}

static int text_bmfont_parse_common(text_bmfont_parse_ctx_t *ctx, const char *line)
{
    int line_height;
    int base;
    int scale_w;
    int scale_h;
    int pages;

    if (!ctx || !line)
        return -1;

    if (text_bmfont_parse_int_field(line, "lineHeight", &line_height) != 0) return -1;
    if (text_bmfont_parse_int_field(line, "base", &base) != 0) return -1;
    if (text_bmfont_parse_int_field(line, "scaleW", &scale_w) != 0) return -1;
    if (text_bmfont_parse_int_field(line, "scaleH", &scale_h) != 0) return -1;
    if (text_bmfont_parse_int_field(line, "pages", &pages) != 0) return -1;

    if (line_height < 0 || base < -32768 || base > 32767 ||
        scale_w < 0 || scale_w > 65535 ||
        scale_h < 0 || scale_h > 65535) {
        return -1;
    }

    ctx->line_height = (unsigned short)line_height;
    ctx->base = (short)base;
    ctx->scale_w = (unsigned short)scale_w;
    ctx->scale_h = (unsigned short)scale_h;
    ctx->pages = pages;
    ctx->saw_common = 1;

    return 0;
}

static int text_bmfont_parse_page(text_bmfont_parse_ctx_t *ctx, const char *line)
{
    int id;
    char file_buf[256];

    if (!ctx || !line)
        return -1;

    if (text_bmfont_parse_int_field(line, "id", &id) != 0)
        return -1;

    if (text_bmfont_parse_quoted_field(line, "file", file_buf, sizeof(file_buf)) != 0)
        return -1;

    if (id != 0) {
        LOGLNC(LOGCAT_TEXT, "[bmfont] %s: only page id=0 is supported",
              ctx->debug_name ? ctx->debug_name : "<unknown>");
        return -1;
    }

    ctx->saw_page = 1;
    return 0;
}

static int text_bmfont_parse_char_line(const char *line, text_glyph_t *glyph)
{
    int id;
    int x, y, width, height;
    int xoffset, yoffset, xadvance;
    int page;

    if (!line || !glyph)
        return -1;

    if (text_bmfont_parse_int_field(line, "id", &id) != 0) return -1;
    if (text_bmfont_parse_int_field(line, "x", &x) != 0) return -1;
    if (text_bmfont_parse_int_field(line, "y", &y) != 0) return -1;
    if (text_bmfont_parse_int_field(line, "width", &width) != 0) return -1;
    if (text_bmfont_parse_int_field(line, "height", &height) != 0) return -1;
    if (text_bmfont_parse_int_field(line, "xoffset", &xoffset) != 0) return -1;
    if (text_bmfont_parse_int_field(line, "yoffset", &yoffset) != 0) return -1;
    if (text_bmfont_parse_int_field(line, "xadvance", &xadvance) != 0) return -1;
    if (text_bmfont_parse_int_field(line, "page", &page) != 0) return -1;

    if (id < 0 || x < 0 || y < 0 || width < 0 || height < 0 || page < 0)
        return -1;

    if (id > 0x7fffffff || x > 65535 || y > 65535 ||
        width > 65535 || height > 65535 ||
        xoffset < -32768 || xoffset > 32767 ||
        yoffset < -32768 || yoffset > 32767 ||
        xadvance < -32768 || xadvance > 32767 ||
        page > 255) {
        return -1;
    }

    glyph->codepoint = (text_codepoint_t)id;
    glyph->atlas_x = (unsigned short)x;
    glyph->atlas_y = (unsigned short)y;
    glyph->atlas_w = (unsigned short)width;
    glyph->atlas_h = (unsigned short)height;
    glyph->xoffset = (short)xoffset;
    glyph->yoffset = (short)yoffset;
    glyph->xadvance = (short)xadvance;
    glyph->page = (unsigned char)page;
    glyph->valid = 1;

    return 0;
}

static int text_bmfont_parse_kerning_line(const char *line, text_kerning_pair_t *kerning)
{
    int first;
    int second;
    int amount;

    if (!line || !kerning)
        return -1;

    if (text_bmfont_parse_int_field(line, "first", &first) != 0) return -1;
    if (text_bmfont_parse_int_field(line, "second", &second) != 0) return -1;
    if (text_bmfont_parse_int_field(line, "amount", &amount) != 0) return -1;

    if (first < 0 || second < 0 || amount < -32768 || amount > 32767)
        return -1;

    kerning->first = (text_codepoint_t)first;
    kerning->second = (text_codepoint_t)second;
    kerning->amount = (short)amount;

    return 0;
}

static int text_bmfont_first_pass(text_bmfont_parse_ctx_t *ctx)
{
    const char *p;
    unsigned int remaining;
    char line[1024];

    if (!ctx || !ctx->text)
        return -1;

    p = ctx->text;
    remaining = ctx->size;

    while (remaining > 0) {
        unsigned int consumed = text_bmfont_copy_line(p, remaining, line, sizeof(line));

        if (consumed == 0)
            break;

        if (text_bmfont_starts_with(line, "common ")) {
            if (text_bmfont_parse_common(ctx, line) != 0) {
                LOGLNC(LOGCAT_TEXT, "[bmfont] %s: failed to parse common line",
                      ctx->debug_name ? ctx->debug_name : "<unknown>");
                return -1;
            }
        } else if (text_bmfont_starts_with(line, "page ")) {
            if (text_bmfont_parse_page(ctx, line) != 0) {
                LOGLNC(LOGCAT_TEXT, "[bmfont] %s: failed to parse page line",
                      ctx->debug_name ? ctx->debug_name : "<unknown>");
                return -1;
            }
        } else if (text_bmfont_starts_with(line, "chars ")) {
            int count = 0;
            if (text_bmfont_parse_int_field(line, "count", &count) != 0 || count < 0) {
                LOGLNC(LOGCAT_TEXT, "[bmfont] %s: failed to parse chars count",
                      ctx->debug_name ? ctx->debug_name : "<unknown>");
                return -1;
            }
            ctx->chars_declared = (unsigned int)count;
        } else if (text_bmfont_starts_with(line, "char ")) {
            ctx->chars_found++;
        } else if (text_bmfont_starts_with(line, "kernings ")) {
            int count = 0;
            if (text_bmfont_parse_int_field(line, "count", &count) != 0 || count < 0) {
                LOGLNC(LOGCAT_TEXT, "[bmfont] %s: failed to parse kernings count",
                      ctx->debug_name ? ctx->debug_name : "<unknown>");
                return -1;
            }
            ctx->kernings_declared = (unsigned int)count;
        } else if (text_bmfont_starts_with(line, "kerning ")) {
            ctx->kernings_found++;
        }

        p += consumed;
        remaining -= consumed;
    }

    if (!ctx->saw_common) {
        LOGLNC(LOGCAT_TEXT, "[bmfont] %s: missing common line",
              ctx->debug_name ? ctx->debug_name : "<unknown>");
        return -1;
    }

    if (ctx->pages != 1) {
        LOGLNC(LOGCAT_TEXT, "[bmfont] %s: only pages=1 is supported, got %d",
              ctx->debug_name ? ctx->debug_name : "<unknown>",
              ctx->pages);
        return -1;
    }

    if (!ctx->saw_page) {
        LOGLNC(LOGCAT_TEXT, "[bmfont] %s: missing page line",
              ctx->debug_name ? ctx->debug_name : "<unknown>");
        return -1;
    }

    if (ctx->chars_declared != 0 && ctx->chars_declared != ctx->chars_found) {
        LOGLNC(LOGCAT_TEXT, "[bmfont] %s: chars count mismatch declared=%u found=%u",
              ctx->debug_name ? ctx->debug_name : "<unknown>",
              ctx->chars_declared,
              ctx->chars_found);
    }

    if (ctx->kernings_declared != 0 && ctx->kernings_declared != ctx->kernings_found) {
        LOGLNC(LOGCAT_TEXT, "[bmfont] %s: kernings count mismatch declared=%u found=%u",
              ctx->debug_name ? ctx->debug_name : "<unknown>",
              ctx->kernings_declared,
              ctx->kernings_found);
    }

    return 0;
}

static int text_bmfont_second_pass(const text_bmfont_parse_ctx_t *ctx, text_font_t *font)
{
    const char *p;
    unsigned int remaining;
    unsigned int glyph_index;
    unsigned int kerning_index;
    char line[1024];

    if (!ctx || !font)
        return -1;

    p = ctx->text;
    remaining = ctx->size;
    glyph_index = 0;
    kerning_index = 0;

    while (remaining > 0) {
        unsigned int consumed = text_bmfont_copy_line(p, remaining, line, sizeof(line));

        if (consumed == 0)
            break;

        if (text_bmfont_starts_with(line, "char ")) {
            if (glyph_index >= font->glyph_count) {
                LOGLNC(LOGCAT_TEXT, "[bmfont] %s: too many char lines",
                      ctx->debug_name ? ctx->debug_name : "<unknown>");
                return -1;
            }

            if (text_bmfont_parse_char_line(line, &font->glyphs[glyph_index]) != 0) {
                LOGLNC(LOGCAT_TEXT, "[bmfont] %s: failed to parse char line index=%u",
                      ctx->debug_name ? ctx->debug_name : "<unknown>",
                      glyph_index);
                return -1;
            }

            if (font->glyphs[glyph_index].page != 0) {
                LOGLNC(LOGCAT_TEXT, "[bmfont] %s: glyph page %u is unsupported",
                      ctx->debug_name ? ctx->debug_name : "<unknown>",
                      (unsigned int)font->glyphs[glyph_index].page);
                return -1;
            }

            glyph_index++;
        } else if (text_bmfont_starts_with(line, "kerning ")) {
            if (kerning_index >= font->kerning_count) {
                LOGLNC(LOGCAT_TEXT, "[bmfont] %s: too many kerning lines",
                      ctx->debug_name ? ctx->debug_name : "<unknown>");
                return -1;
            }

            if (text_bmfont_parse_kerning_line(line, &font->kernings[kerning_index]) != 0) {
                LOGLNC(LOGCAT_TEXT, "[bmfont] %s: failed to parse kerning line index=%u",
                      ctx->debug_name ? ctx->debug_name : "<unknown>",
                      kerning_index);
                return -1;
            }

            kerning_index++;
        }

        p += consumed;
        remaining -= consumed;
    }

    if (glyph_index != font->glyph_count) {
        LOGLNC(LOGCAT_TEXT, "[bmfont] %s: glyph parse mismatch expected=%u parsed=%u",
              ctx->debug_name ? ctx->debug_name : "<unknown>",
              font->glyph_count,
              glyph_index);
        return -1;
    }

    if (kerning_index != font->kerning_count) {
        LOGLNC(LOGCAT_TEXT, "[bmfont] %s: kerning parse mismatch expected=%u parsed=%u",
              ctx->debug_name ? ctx->debug_name : "<unknown>",
              font->kerning_count,
              kerning_index);
        return -1;
    }

    return 0;
}

int text_bmfont_load_from_memory(mem_arena_t *arena,
                                 text_font_t *font,
                                 const text_bmfont_load_desc_t *desc)
{
    text_bmfont_parse_ctx_t ctx;

    if (!arena || !font || !desc || !desc->fnt_text || desc->fnt_size == 0) {
        LOGLNC(LOGCAT_TEXT, "[bmfont] invalid load arguments");
        return -1;
    }

    if (!gfx_texture_is_valid(desc->texture)) {
        LOGLNC(LOGCAT_TEXT, "[bmfont] %s: invalid atlas texture handle",
              desc->debug_name ? desc->debug_name : "<unknown>");
        return -1;
    }

    memset(&ctx, 0, sizeof(ctx));
    ctx.text = desc->fnt_text;
    ctx.size = desc->fnt_size;
    ctx.debug_name = desc->debug_name;

    if (text_bmfont_first_pass(&ctx) != 0)
        return -1;

    text_font_init(font);
    font->name = desc->debug_name;
    font->texture = desc->texture;
    font->atlas_width = ctx.scale_w;
    font->atlas_height = ctx.scale_h;
    font->line_height = ctx.line_height;
    font->base = ctx.base;
    font->fallback_codepoint = '?';
    font->glyph_count = ctx.chars_found;
    font->kerning_count = ctx.kernings_found;

    if (font->glyph_count > 0) {
        font->glyphs = (text_glyph_t *)mem_arena_calloc(
            arena,
            font->glyph_count,
            sizeof(text_glyph_t),
            16
        );
        if (!font->glyphs) {
            LOGLNC(LOGCAT_TEXT, "[bmfont] %s: failed to allocate glyph table",
                  desc->debug_name ? desc->debug_name : "<unknown>");
            return -1;
        }
    }

    if (font->kerning_count > 0) {
        font->kernings = (text_kerning_pair_t *)mem_arena_calloc(
            arena,
            font->kerning_count,
            sizeof(text_kerning_pair_t),
            16
        );
        if (!font->kernings) {
            LOGLNC(LOGCAT_TEXT, "[bmfont] %s: failed to allocate kerning table",
                  desc->debug_name ? desc->debug_name : "<unknown>");
            return -1;
        }
    }

    if (text_bmfont_second_pass(&ctx, font) != 0)
        return -1;

    LOGLNC(LOGCAT_TEXT, "[bmfont] %s: loaded glyphs=%u kernings=%u line_height=%u base=%d atlas=%ux%u",
          desc->debug_name ? desc->debug_name : "<unknown>",
          (unsigned int)font->glyph_count,
          (unsigned int)font->kerning_count,
          (unsigned int)font->line_height,
          (int)font->base,
          (unsigned int)font->atlas_width,
          (unsigned int)font->atlas_height);

    return 0;
}