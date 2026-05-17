#include "texture_resource.h"
#include "engine/gfx/gfx2d.h"
#include "engine/logging/log.h"
#include <string.h>

int texture_resource_load(texture_resource_t *tr, const char *path)
{
    resource_load_desc_t desc;

    if (!tr || !path)
        return -1;

    memset(tr, 0, sizeof(*tr));
    tr->tex_id = -1;

    memset(&desc, 0, sizeof(desc));
    desc.path = path;
    desc.type = RESOURCE_TYPE_TEXTURE_PAGE;
    desc.priority = STREAM_PRIORITY_HIGH;
    desc.userdata = tr;

    tr->resource = resource_load_file(&desc);
    if (!resource_is_valid(tr->resource)) {
        LOGLN("[texture_resource] resource queue failed path=%s", path);
        return -1;
    }

    LOGLN("[texture_resource] queued path=%s handle=(%u,%u)",
          path,
          tr->resource.index,
          tr->resource.generation);
    return 0;
}

void texture_resource_release(texture_resource_t *tr)
{
    if (!tr)
        return;

    if (tr->loaded && tr->tex_id >= 0) {
        gfx2d_free_texture(tr->tex_id);
        tr->tex_id = -1;
        tr->loaded = 0;
    }

    if (resource_is_valid(tr->resource))
        resource_release(tr->resource);

    tr->resource.index = 0xffffu;
    tr->resource.generation = 0;
}

int texture_resource_is_ready(const texture_resource_t *tr)
{
    return tr && tr->loaded;
}

int texture_resource_tex_id(const texture_resource_t *tr)
{
    if (!tr || !tr->loaded)
        return -1;
    return tr->tex_id;
}