#include "game/text/text_block.h"
#include "engine/logging/log.h"

#include <string.h>

static void text_block_apply_horizontal_alignment(text_block_t *tb)
{
    unsigned short line_index;

    if (!tb)
        return;

    if (tb->box_w <= 0)
        return;

    for (line_index = 0; line_index < tb->layout.line_count; ++line_index) {
        text_layout_line_t *line = &tb->layout.lines[line_index];
        short offset_x = 0;
        unsigned short first;
        unsigned short end;
        unsigned short i;

        if (line->width >= tb->box_w)
            continue;

        if (tb->align_h == TEXT_ALIGN_CENTER)
            offset_x = (short)((tb->box_w - line->width) / 2);
        else if (tb->align_h == TEXT_ALIGN_RIGHT)
            offset_x = (short)(tb->box_w - line->width);
        else
            offset_x = 0;

        if (offset_x == 0)
            continue;

        first = line->first_item;
        end = (unsigned short)(first + line->item_count);

        if (end > tb->layout.item_count)
            end = tb->layout.item_count;

        for (i = first; i < end; ++i)
            tb->layout.items[i].x = (short)(tb->layout.items[i].x + offset_x);
    }
}

static void text_block_apply_vertical_alignment(text_block_t *tb)
{
    short layout_height;
    short offset_y;
    unsigned short i;

    if (!tb)
        return;

    if (tb->box_h <= 0)
        return;

    layout_height = tb->layout.height;
    if (layout_height >= tb->box_h)
        return;

    if (tb->align_v == TEXT_ALIGN_MIDDLE)
        offset_y = (short)((tb->box_h - layout_height) / 2);
    else if (tb->align_v == TEXT_ALIGN_BOTTOM)
        offset_y = (short)(tb->box_h - layout_height);
    else
        offset_y = 0;

    if (offset_y == 0)
        return;

    for (i = 0; i < tb->layout.item_count; ++i)
        tb->layout.items[i].y = (short)(tb->layout.items[i].y + offset_y);
}

void text_block_init(text_block_t *blk,
                     text_rich_run_t *runs,
                     unsigned short run_capacity,
                     text_layout_item_t *items,
                     unsigned short item_capacity,
                     text_layout_line_t *lines,
                     unsigned short line_capacity,
                     float reveal_seconds_per_unit)
{
    if (!blk)
        return;

    memset(blk, 0, sizeof(*blk));

    blk->font = NULL;

    blk->box_x = 0;
    blk->box_y = 0;
    blk->box_w = 0;
    blk->box_h = 0;

    blk->align_h = TEXT_ALIGN_LEFT;
    blk->align_v = TEXT_ALIGN_TOP;

    text_style_init(&blk->base_style);

    blk->wrap_mode = TEXT_WRAP_WORD;
    blk->reveal_mode = TEXT_REVEAL_GLYPH;

    blk->reveal_seconds_per_unit =
        (reveal_seconds_per_unit > 0.0f) ? reveal_seconds_per_unit : 0.03f;

    text_reveal_state_init(&blk->reveal, blk->reveal_seconds_per_unit);
    blk->time_seconds = 0.0f;

    blk->shake_amp_scale_x = 1.0f;
    blk->shake_amp_scale_y = 1.0f;
    blk->shake_speed_scale = 1.0f;

    blk->wave_amp_scale_x = 1.0f;
    blk->wave_amp_scale_y = 1.0f;
    blk->wave_speed_scale = 1.0f;

    blk->reveal_speed_scale = 1.0f;

    blk->text_utf8 = NULL;

    blk->runs = runs;
    blk->run_capacity = run_capacity;
    blk->run_count = 0;

    blk->items = items;
    blk->item_capacity = item_capacity;

    blk->lines = lines;
    blk->line_capacity = line_capacity;

    text_rich_layout_init(&blk->layout,
                          blk->items,
                          blk->item_capacity,
                          blk->lines,
                          blk->line_capacity);
}

void text_block_set_font(text_block_t *blk, const text_font_t *font)
{
    if (!blk)
        return;

    blk->font = font;
}

void text_block_set_text(text_block_t *blk, const char *text_utf8)
{
    if (!blk)
        return;

    blk->text_utf8 = text_utf8;
}

void text_block_set_reveal_mode(text_block_t *blk, text_reveal_mode_t reveal_mode)
{
    if (!blk)
        return;

    blk->reveal_mode = reveal_mode;
}

void text_block_set_origin(text_block_t *blk, short x, short y)
{
    if (!blk)
        return;

    blk->box_x = x;
    blk->box_y = y;
}

void text_block_set_box(text_block_t *blk, short x, short y, short w, short h)
{
    if (!blk)
        return;

    blk->box_x = x;
    blk->box_y = y;
    blk->box_w = w;
    blk->box_h = h;
}

void text_block_set_align_h(text_block_t *blk, text_align_h_t align_h)
{
    if (!blk)
        return;

    blk->align_h = align_h;
}

void text_block_set_align_v(text_block_t *blk, text_align_v_t align_v)
{
    if (!blk)
        return;

    blk->align_v = align_v;
}

void text_block_set_style(text_block_t *blk, const text_style_t *style)
{
    if (!blk || !style)
        return;

    blk->base_style = *style;
}

void text_block_set_text_scale(text_block_t *blk, float scale)
{
    if (!blk)
        return;

    if (scale <= 0.0f)
        scale = 1.0f;

    blk->base_style.scale = scale;
}

void text_block_set_wrap_mode(text_block_t *blk, text_wrap_mode_t wrap_mode)
{
    if (!blk)
        return;

    blk->wrap_mode = wrap_mode;
}

void text_block_reset_draw_params(text_block_t *blk)
{
    if (!blk)
        return;

    blk->shake_amp_scale_x = 1.0f;
    blk->shake_amp_scale_y = 1.0f;
    blk->shake_speed_scale = 1.0f;

    blk->wave_amp_scale_x = 1.0f;
    blk->wave_amp_scale_y = 1.0f;
    blk->wave_speed_scale = 1.0f;
}

void text_block_set_shake_scale(text_block_t *blk, float amp_scale)
{
    if (!blk)
        return;

    blk->shake_amp_scale_x = amp_scale;
    blk->shake_amp_scale_y = amp_scale;
}

void text_block_set_shake_scale_xy(text_block_t *blk, float amp_scale_x, float amp_scale_y)
{
    if (!blk)
        return;

    blk->shake_amp_scale_x = amp_scale_x;
    blk->shake_amp_scale_y = amp_scale_y;
}

void text_block_set_shake_speed_scale(text_block_t *blk, float speed_scale)
{
    if (!blk)
        return;

    blk->shake_speed_scale = speed_scale;
}

void text_block_set_wave_scale(text_block_t *blk, float amp_scale)
{
    if (!blk)
        return;

    blk->wave_amp_scale_x = amp_scale;
    blk->wave_amp_scale_y = amp_scale;
}

void text_block_set_wave_scale_xy(text_block_t *blk, float amp_scale_x, float amp_scale_y)
{
    if (!blk)
        return;

    blk->wave_amp_scale_x = amp_scale_x;
    blk->wave_amp_scale_y = amp_scale_y;
}

void text_block_set_wave_speed_scale(text_block_t *blk, float speed_scale)
{
    if (!blk)
        return;

    blk->wave_speed_scale = speed_scale;
}

void text_block_set_reveal_speed_scale(text_block_t *blk, float speed_scale)
{
    if (!blk)
        return;

    blk->reveal_speed_scale = speed_scale;
}

int text_block_refresh(text_block_t *blk)
{
    text_layout_params_t lp;

    if (!blk) {
        LOGLNC(LOGCAT_TEXT, "[text_block] refresh failed: blk=NULL");
        return -1;
    }

    if (!blk->font) {
        LOGLNC(LOGCAT_TEXT, "[text_block] refresh failed: font=NULL");
        return -1;
    }

    if (!blk->items || blk->item_capacity == 0) {
        LOGLNC(LOGCAT_TEXT, "[text_block] refresh failed: items buffer missing");
        return -1;
    }

    if (!blk->lines || blk->line_capacity == 0) {
        LOGLNC(LOGCAT_TEXT, "[text_block] refresh failed: lines buffer missing");
        return -1;
    }

    if (!blk->runs || blk->run_capacity == 0) {
        LOGLNC(LOGCAT_TEXT, "[text_block] refresh failed: runs buffer missing");
        return -1;
    }

    text_rich_style_t rich_base_style;
    text_rich_style_init(&rich_base_style);
    rich_base_style.color = blk->base_style.color;
    rich_base_style.scale = blk->base_style.scale;

    if (!blk->text_utf8) {
        LOGLNC(LOGCAT_TEXT, "[text_block] refresh failed: text_utf8=NULL");
        return -1;
    }

    if (text_rich_parse(blk->text_utf8,
        &rich_base_style,
        blk->runs,
        blk->run_capacity,
        &blk->run_count) != 0) {
            LOGLNC(LOGCAT_TEXT, "[text_block] refresh failed: text_rich_parse failed");
            return -1;
    }

    text_rich_layout_reset(&blk->layout);

    memset(&lp, 0, sizeof(lp));
    lp.origin_x = blk->box_x;
    lp.origin_y = blk->box_y;
    lp.max_width = blk->box_w;
    lp.wrap_mode = blk->wrap_mode;
    lp.style = blk->base_style;

    if (text_rich_layout_build(&blk->layout,
                               blk->font,
                               blk->runs,
                               blk->run_count,
                               &lp,
                               blk->reveal_mode) != 0) {
        LOGLNC(LOGCAT_TEXT,
              "[text_block] refresh failed: text_rich_layout_build failed runs=%u wrap=%d box=(%d,%d,%d,%d)",
              (unsigned int)blk->run_count,
              (int)blk->wrap_mode,
              (int)blk->box_x,
              (int)blk->box_y,
              (int)blk->box_w,
              (int)blk->box_h);
        return -1;
    }

    text_block_apply_horizontal_alignment(blk);
    text_block_apply_vertical_alignment(blk);

    text_reveal_state_init(&blk->reveal, blk->reveal_seconds_per_unit);

    // Commented due noisy
    // LOGLNC(LOGCAT_TEXT,
    //       "[text_block] refresh ok runs=%u items=%u lines=%u groups=%u size=%dx%d",
    //       (unsigned int)blk->run_count,
    //       (unsigned int)blk->layout.item_count,
    //       (unsigned int)blk->layout.line_count,
    //       (unsigned int)blk->layout.reveal_group_count,
    //       (int)blk->layout.width,
    //       (int)blk->layout.height);

    return 0;
}

short text_block_width(const text_block_t *blk)
{
    return blk ? blk->layout.width : 0;
}

short text_block_height(const text_block_t *blk)
{
    return blk ? blk->layout.height : 0;
}

void text_block_reveal_reset(text_block_t *blk)
{
    if (!blk)
        return;

    text_reveal_state_init(&blk->reveal, blk->reveal_seconds_per_unit);
}

void text_block_reveal_finish(text_block_t *blk)
{
    if (!blk)
        return;

    blk->reveal.visible_units = blk->layout.reveal_group_count;
}

void text_block_update(text_block_t *blk, float dt)
{
    if (!blk)
        return;

    blk->time_seconds += dt;

    text_reveal_state_update(&blk->reveal,
                             blk->layout.reveal_group_count,
                             dt * blk->reveal_speed_scale);
}

void text_block_draw(const text_block_t *blk)
{
    text_rich_draw_params_t dp;

    if (!blk || !blk->font)
        return;

    text_rich_draw_params_init(&dp);
    dp.shake_amp_scale_x = blk->shake_amp_scale_x;
    dp.shake_amp_scale_y = blk->shake_amp_scale_y;
    dp.shake_speed_scale = blk->shake_speed_scale;
    dp.wave_amp_scale_x = blk->wave_amp_scale_x;
    dp.wave_amp_scale_y = blk->wave_amp_scale_y;
    dp.wave_speed_scale = blk->wave_speed_scale;

    text_rich_draw_layout_ex(blk->font,
                             &blk->layout,
                             &blk->reveal,
                             blk->time_seconds,
                             &dp);
}