#ifndef INTRO_TEST_STATE_H
#define INTRO_TEST_STATE_H

#include "game_app.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum state_option {
    EN = 0,
    RU,
    EXIT
} state_option_t;

typedef struct intro_state_menu_option {
    const char *name;
    const char *text_utf8;
    state_option_t option_tag;
} intro_state_menu_option_t;

static const intro_state_menu_option_t menu_options[] = {
    { "en",   "English", EN },
    { "ru",   "Русский", RU },
    { "exit", "Exit",    EXIT }
};

typedef enum subslide_visual_mode {
    INTRO_VISUAL_STRETCH = 0,
    INTRO_VISUAL_NATIVE,
    INTRO_VISUAL_FIT
} subslide_visual_mode_t;

typedef struct subslide_desc {
    const char *path;
    subslide_visual_mode_t mode;
} intro_subslide_desc_t;

static const char snd_confirm[] = "assets/undertale_yellow/sounds/snd_confirm.wav";
static const char snd_mainmenu_select[] = "assets/undertale_yellow/sounds/snd_mainmenu_select.wav";
static const char sndfnt_default2[] = "assets/undertale_yellow/sounds/sndfnt_default2.wav";
static const char mus_intro[] = "assets/undertale_yellow/mus/intro.wav";
static const char intronoise[] = "assets/undertale_yellow/mus/intronoise.wav";

static const char logo[] = "assets/undertale_yellow/sprites/undertale_logo.png";
static const char logo_yellow[] = "assets/undertale_yellow/sprites/undertale_yellow_logo.png";

/*---ENGLIGH START---*/
static const intro_subslide_desc_t intro1_slides[] = {
    { "assets/undertale_yellow/sprites/spr_intro_slide_1.png", INTRO_VISUAL_STRETCH },
    { "assets/undertale_yellow/sprites/spr_intro_slide_1_5.png", INTRO_VISUAL_STRETCH },
    { "assets/undertale_yellow/sprites/spr_intro_slide_1_5_eyes.png", INTRO_VISUAL_STRETCH }
};

static const char *intro1_text[] = {
    "For years, monsters have\nbeen sealed away by\na powerful spell."
};

static const intro_subslide_desc_t intro2_slides[] = {
    { "assets/undertale_yellow/sprites/spr_intro_coffins_slide_2.png", INTRO_VISUAL_STRETCH }
};

static const char *intro2_text[] = {
    "A spell that could only be\nbroken with seven human SOULs."
};

static const intro_subslide_desc_t intro3_slides[] = {
    { "assets/undertale_yellow/sprites/spr_intro_slide_3.png", INTRO_VISUAL_STRETCH }
};

static const char *intro3_text[] = {
    "Their king was peaceful\nand wished to avoid any\nmore conflict..."
};

static const intro_subslide_desc_t intro4_slides[] = {
    { "assets/undertale_yellow/sprites/spr_intro_background_slide.png", INTRO_VISUAL_STRETCH },
    { "assets/undertale_yellow/sprites/spr_intro_dood_slide_4.png", INTRO_VISUAL_NATIVE },
    { "assets/undertale_yellow/sprites/spr_intro_heart_slide_4.png", INTRO_VISUAL_NATIVE },
    { "assets/undertale_yellow/sprites/spr_intro_canister_slide_4.png", INTRO_VISUAL_NATIVE }
};

static const char *intro4_text[] = {
    "But eventually declared that\nany human who fell...",
    "Would die..."
};

static const intro_subslide_desc_t intro5_slides[] = {
    { "assets/undertale_yellow/sprites/spr_intro_mountain_slide_5.png", INTRO_VISUAL_STRETCH }
};

static const char *intro5_text[] = {
    "Mt. Ebott...",
    "Few humans have braved\nthis mountain."
};

static const intro_subslide_desc_t intro6_slides[] = {
    { "assets/undertale_yellow/sprites/spr_intro_slide_6.png", INTRO_VISUAL_STRETCH }
};

static const char *intro6_text[] = {
    "Those who did... were\nnever seen again." 
};

static const intro_subslide_desc_t intro7_slides[] = {
    { "assets/undertale_yellow/sprites/spr_intro_slide_7.png", INTRO_VISUAL_STRETCH }
};

static const intro_subslide_desc_t intro8_slides[] = {
    { "assets/undertale_yellow/sprites/spr_intro_slide_8.png", INTRO_VISUAL_STRETCH }
};

static const intro_subslide_desc_t intro9_slides[] = {
    { "assets/undertale_yellow/sprites/spr_intro_clover_slide_9.png", INTRO_VISUAL_STRETCH }
};

static const intro_subslide_desc_t intro10_slides[] = {
    { "assets/undertale_yellow/sprites/spr_intro_cave_slide_10.png", INTRO_VISUAL_STRETCH },
    { "assets/undertale_yellow/sprites/spr_intro_clover_slide_10.png", INTRO_VISUAL_NATIVE }
};

/*---ENGLIGH END---*/

/*---RUSSIAN START---*/

static const intro_subslide_desc_t intro1_slides_ru[] = {
    { "assets/undertale_yellow/sprites/spr_intro_slide_1.png", INTRO_VISUAL_STRETCH },
    { "assets/undertale_yellow/sprites/spr_intro_slide_1_5.png", INTRO_VISUAL_STRETCH },
    { "assets/undertale_yellow/sprites/spr_intro_slide_1_5_eyes.png", INTRO_VISUAL_STRETCH }
};

static const char *intro1_text_ru[] = {
    "Долгие годы монстры были\nзапечатаны мощным заклинанием."
};

static const intro_subslide_desc_t intro2_slides_ru[] = {
    { "assets/undertale_yellow/sprites/spr_intro_coffins_slide_2.png", INTRO_VISUAL_STRETCH }
};

static const char *intro2_text_ru[] = {
    "Заклинанием, которое может быть\nразрушено лишь с помощью\nсеми человеческих ДУШ."
};

static const intro_subslide_desc_t intro3_slides_ru[] = {
    { "assets/undertale_yellow/sprites/spr_intro_slide_3.png", INTRO_VISUAL_STRETCH }
};

static const char *intro3_text_ru[] = {
    "Их король был миролюбив\nи хотел избежать новых\nконфликтов..."
};

static const intro_subslide_desc_t intro4_slides_ru[] = {
    { "assets/undertale_yellow/sprites/spr_intro_background_slide.png", INTRO_VISUAL_STRETCH },
    { "assets/undertale_yellow/sprites/spr_intro_dood_slide_4.png", INTRO_VISUAL_NATIVE },
    { "assets/undertale_yellow/sprites/spr_intro_heart_slide_4.png", INTRO_VISUAL_NATIVE },
    { "assets/undertale_yellow/sprites/spr_intro_canister_slide_4.png", INTRO_VISUAL_NATIVE }
};

static const char *intro4_text_ru[] = {
    "Но в конце концов объявил,\nчто каждый упавший человек...",
    "Должен умереть..."
};

static const intro_subslide_desc_t intro5_slides_ru[] = {
    { "assets/undertale_yellow/sprites/spr_intro_mountain_slide_5.png", INTRO_VISUAL_STRETCH }
};

static const char *intro5_text_ru[] = {
    "Гора Эботт...",
    "Немногие люди отважились\nвзойти на эту гору."
};

static const intro_subslide_desc_t intro6_slides_ru[] = {
    { "assets/undertale_yellow/sprites/spr_intro_slide_6_ru.png", INTRO_VISUAL_STRETCH }
};

static const char *intro6_text_ru[] = {
    "Тех, кто осмелился...\nБольше никогда не видели."
};

static const intro_subslide_desc_t intro7_slides_ru[] = {
    { "assets/undertale_yellow/sprites/spr_intro_slide_7.png", INTRO_VISUAL_STRETCH }
};

static const intro_subslide_desc_t intro8_slides_ru[] = {
    { "assets/undertale_yellow/sprites/spr_intro_slide_8.png", INTRO_VISUAL_STRETCH }
};

static const intro_subslide_desc_t intro9_slides_ru[] = {
    { "assets/undertale_yellow/sprites/spr_intro_clover_slide_9.png", INTRO_VISUAL_STRETCH }
};

static const intro_subslide_desc_t intro10_slides_ru[] = {
    { "assets/undertale_yellow/sprites/spr_intro_cave_slide_10.png", INTRO_VISUAL_STRETCH },
    { "assets/undertale_yellow/sprites/spr_intro_clover_slide_10.png", INTRO_VISUAL_NATIVE }
};

/*---RUSSIAN END---*/

const game_state_desc_t *intro_test_state_desc(void);

#ifdef __cplusplus
}
#endif

#endif /* INTRO_TEST_STATE_H */