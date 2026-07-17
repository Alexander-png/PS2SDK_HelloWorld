#ifndef PLATFORM_H
#define PLATFORM_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct platform_memory_info {
    unsigned int total_physical_bytes;
    unsigned int available_physical_bytes;
    int has_total_physical;
    int has_available_physical;
} platform_memory_info_t;

int  platform_init(void);
void platform_shutdown(void);

void platform_delay_us(int usec);
unsigned int platform_ticks_ms(void);

unsigned long long platform_time_now_us(void);
float platform_time_now_seconds(void);

/* Cooperative yield for outer loop when needed. */
void platform_yield(void);

int platform_get_memory_info(platform_memory_info_t *out);

int platform_load_module(const char *path, int argc, const char *argv);
int platform_exec_module_buffer(const void *buffer,
                                unsigned int size,
                                int argc,
                                const char *argv,
                                int *result);

#ifdef __cplusplus
}
#endif

#endif /* PLATFORM_H */