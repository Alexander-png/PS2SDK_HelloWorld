#ifndef TEXTURE_RESOURCE_H
#define TEXTURE_RESOURCE_H

#include "engine/resources/resources.h"

typedef struct texture_resource {
    resource_handle_t resource;
    int tex_id;
    int loaded;
} texture_resource_t;

int  texture_resource_load(texture_resource_t *tr, const char *path);
void texture_resource_release(texture_resource_t *tr);

int  texture_resource_is_ready(const texture_resource_t *tr);
int  texture_resource_tex_id(const texture_resource_t *tr);

#endif