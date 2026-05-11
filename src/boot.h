#ifndef BOOT_H
#define BOOT_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct boot_context {
    int initialized;
    int audio_available;
    int screen_available;
} boot_context_t;

int  boot_init(boot_context_t *boot);
void boot_shutdown(boot_context_t *boot);

#ifdef __cplusplus
}
#endif

#endif /* BOOT_H */