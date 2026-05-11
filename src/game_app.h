#ifndef GAME_APP_H
#define GAME_APP_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct game_app game_app_t;

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

#ifdef __cplusplus
}
#endif

#endif /* GAME_APP_H */
