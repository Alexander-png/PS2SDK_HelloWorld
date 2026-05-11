#ifndef PLATFORM_H
#define PLATFORM_H

#ifdef __cplusplus
extern "C" {
#endif

int  platform_init(void);
void platform_shutdown(void);

void platform_delay_us(int usec);
unsigned int platform_ticks_ms(void);

#ifdef __cplusplus
}
#endif

#endif /* PLATFORM_H */