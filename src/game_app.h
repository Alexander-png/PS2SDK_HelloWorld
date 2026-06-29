#ifndef GAME_APP_H
#define GAME_APP_H

#ifdef __cplusplus
extern "C" {
#endif

#define GAME_APP_STATE_DATA_AS(app, type) \
    ((type *)game_app_state_userdata((app)))

#define GAME_APP_SET_STATE_DATA(app, ptr) \
    game_app_set_state_userdata((app), (void *)(ptr))

typedef struct game_app game_app_t;
typedef struct mem_arena mem_arena_t;

typedef struct game_state_desc {
    const char *name;

    int  (*enter)(game_app_t *app, void *userdata);
    void (*exit)(game_app_t *app);
    void (*update)(game_app_t *app, float dt);
    void (*draw)(game_app_t *app);
} game_state_desc_t;

int  game_app_init(void);
void game_app_shutdown(void);

void game_app_tick(void);
int  game_app_is_running(void);
void game_app_request_quit(void);

int  game_app_change_state(const game_state_desc_t *state, void *userdata);

const char *game_app_current_state_name(void);
unsigned int game_app_frame_index(void);
float game_app_delta_time(void);

mem_arena_t *game_app_temp_arena(game_app_t *app);
mem_arena_t *game_app_state_arena(game_app_t *app);

void *game_app_state_userdata(game_app_t *app);
void game_app_set_state_userdata(game_app_t *app, void *userdata);

#ifdef __cplusplus
}
#endif

#endif /* GAME_APP_H */