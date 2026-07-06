#include "game/states/test/debug_menu_state.h"

#include "engine/logging/log.h"
#include "engine/input/input.h"
#include "engine/resources/resources.h"
#include "engine/streaming/streaming.h"
#include "engine/memory/memory_arena.h"

#include <string.h>

#ifndef TEST_RESOURCE_PATH
#define TEST_RESOURCE_PATH "assets/test/test.bin"
#endif

#ifndef RESOURCE_TEST_TIMEOUT_FRAMES
#define RESOURCE_TEST_TIMEOUT_FRAMES 300
#endif

#ifndef RESOURCE_TEST_INVALID_HANDLE_INDEX
#define RESOURCE_TEST_INVALID_HANDLE_INDEX 0xffffu
#endif

typedef enum resource_test_phase {
    RESOURCE_TEST_PHASE_IDLE = 0,
    RESOURCE_TEST_PHASE_RAW_QUEUE,
    RESOURCE_TEST_PHASE_RAW_WAIT,
    RESOURCE_TEST_PHASE_RAW_VERIFY,
    RESOURCE_TEST_PHASE_RAW_RELEASE,
    RESOURCE_TEST_PHASE_SPRITE_QUEUE,
    RESOURCE_TEST_PHASE_SPRITE_WAIT,
    RESOURCE_TEST_PHASE_SPRITE_VERIFY,
    RESOURCE_TEST_PHASE_SPRITE_RELEASE,
    RESOURCE_TEST_PHASE_DONE,
    RESOURCE_TEST_PHASE_FAILED
} resource_test_phase_t;

typedef struct resource_test_ctx {
    resource_test_phase_t phase;

    resource_handle_t handle;
    resource_type_t expected_type;
    const char *expected_path;
    const char *tag;

    int callback_count;
    int last_callback_fired;
    resource_status_t last_callback_status;
    u32 last_callback_size;
    void *last_callback_data;

    int phase_ticks;
    int total_ticks;
} resource_test_ctx_t;

static resource_handle_t resource_test_invalid_handle(void)
{
    resource_handle_t h;
    h.index = RESOURCE_TEST_INVALID_HANDLE_INDEX;
    h.generation = 0;
    return h;
}

static resource_test_ctx_t *resource_test_data(game_app_t *app)
{
    return GAME_APP_STATE_DATA_AS(app,  resource_test_ctx_t);
}

static const char *resource_test_phase_name(resource_test_phase_t phase)
{
    switch (phase) {
    case RESOURCE_TEST_PHASE_IDLE:           return "idle";
    case RESOURCE_TEST_PHASE_RAW_QUEUE:      return "raw_queue";
    case RESOURCE_TEST_PHASE_RAW_WAIT:       return "raw_wait";
    case RESOURCE_TEST_PHASE_RAW_VERIFY:     return "raw_verify";
    case RESOURCE_TEST_PHASE_RAW_RELEASE:    return "raw_release";
    case RESOURCE_TEST_PHASE_SPRITE_QUEUE:   return "sprite_queue";
    case RESOURCE_TEST_PHASE_SPRITE_WAIT:    return "sprite_wait";
    case RESOURCE_TEST_PHASE_SPRITE_VERIFY:  return "sprite_verify";
    case RESOURCE_TEST_PHASE_SPRITE_RELEASE: return "sprite_release";
    case RESOURCE_TEST_PHASE_DONE:           return "done";
    case RESOURCE_TEST_PHASE_FAILED:         return "failed";
    default:                                 return "unknown";
    }
}

static void resource_test_set_phase(resource_test_ctx_t *ctx,
                                    resource_test_phase_t phase)
{
    if (!ctx)
        return;

    if (ctx->phase != phase) {
        LOGLN("[state:resource_test] phase %s -> %s",
              resource_test_phase_name(ctx->phase),
              resource_test_phase_name(phase));
        ctx->phase = phase;
        ctx->phase_ticks = 0;
    }
}

static void resource_test_reset_callback_info(resource_test_ctx_t *ctx)
{
    if (!ctx)
        return;

    ctx->last_callback_fired = 0;
    ctx->last_callback_status = RESOURCE_STATUS_UNUSED;
    ctx->last_callback_size = 0;
    ctx->last_callback_data = NULL;
}

#ifndef RESOURCE_TEST_FAILF
#define RESOURCE_TEST_FAILF(ctx, ...) \
    do { \
        LOGLN("[state:resource_test] " __VA_ARGS__); \
        resource_test_set_phase(ctx, RESOURCE_TEST_PHASE_FAILED); \
    } while (0)
#endif

static void resource_test_on_loaded(resource_handle_t handle,
                                    resource_status_t status,
                                    void *data,
                                    u32 size,
                                    void *userdata)
{
    resource_test_ctx_t *ctx = (resource_test_ctx_t *)userdata;

    if (!ctx) {
        LOGLN("[state:resource_test] callback with null ctx handle=(%u,%u)",
              handle.index,
              handle.generation);
        return;
    }

    ctx->callback_count++;
    ctx->last_callback_fired = 1;
    ctx->last_callback_status = status;
    ctx->last_callback_size = size;
    ctx->last_callback_data = data;

    LOGLN("[state:resource_test] callback #%d tag=%s handle=(%u,%u) valid=%d status=%s type=%s size=%u data=%p path=%s",
          ctx->callback_count,
          ctx->tag ? ctx->tag : "null",
          handle.index,
          handle.generation,
          resource_is_valid(handle),
          resource_status_name(status),
          resource_type_name(resource_type(handle)),
          size,
          data,
          resource_path(handle));
}

static int resource_test_queue(resource_test_ctx_t *ctx,
                               resource_type_t type,
                               const char *path,
                               const char *tag)
{
    resource_load_desc_t desc;

    if (!ctx)
        return -1;

    memset(&desc, 0, sizeof(desc));
    desc.path = path;
    desc.type = type;
    desc.priority = STREAM_PRIORITY_HIGH;
    desc.callback = resource_test_on_loaded;
    desc.userdata = ctx;

    resource_test_reset_callback_info(ctx);

    ctx->handle = resource_load_file(&desc);
    if (!resource_is_valid(ctx->handle)) {
        LOGLN("[state:resource_test] queue failed tag=%s type=%s path=%s",
              tag,
              resource_type_name(type),
              path);
        return -1;
    }

    ctx->expected_type = type;
    ctx->expected_path = path;
    ctx->tag = tag;

    LOGLN("[state:resource_test] queued tag=%s handle=(%u,%u) type=%s path=%s",
          tag,
          ctx->handle.index,
          ctx->handle.generation,
          resource_type_name(type),
          path);

    return 0;
}

static int resource_test_wait_for_terminal(resource_test_ctx_t *ctx,
                                           resource_handle_t handle,
                                           int phase_ticks,
                                           int timeout_frames,
                                           const char *tag,
                                           resource_status_t *out_status)
{
    resource_status_t st;

    (void)ctx;

    if (!resource_is_valid(handle)) {
        LOGLN("[state:resource_test] %s wait invalid handle", tag);
        return -1;
    }

    st = resource_status(handle);
    if (out_status)
        *out_status = st;

    if (st == RESOURCE_STATUS_READY || st == RESOURCE_STATUS_FAILED)
        return 1;

    if (phase_ticks >= timeout_frames) {
        LOGLN("[state:resource_test] %s wait timeout frames=%d path=%s",
              tag,
              phase_ticks,
              resource_path(handle));
        return -2;
    }

    if ((phase_ticks % 30) == 0) {
        LOGLN("[state:resource_test] %s waiting frames=%d status=%s path=%s",
              tag,
              phase_ticks,
              resource_status_name(st),
              resource_path(handle));
    }

    return 0;
}

static int resource_test_verify_loaded(resource_test_ctx_t *ctx,
                                       const char *tag)
{
    resource_status_t status;
    resource_type_t type;
    const char *path;
    void *data;
    u32 size;

    if (!ctx)
        return -100;

    if (!resource_is_valid(ctx->handle)) {
        LOGLN("[state:resource_test] verify failed tag=%s reason=invalid_handle", tag);
        return -1;
    }

    status = resource_status(ctx->handle);
    type = resource_type(ctx->handle);
    path = resource_path(ctx->handle);
    data = resource_data(ctx->handle);
    size = resource_size(ctx->handle);

    LOGLN("[state:resource_test] verify tag=%s status=%s type=%s size=%u data=%p path=%s callback=%d cb_status=%s cb_size=%u cb_data=%p",
          tag,
          resource_status_name(status),
          resource_type_name(type),
          size,
          data,
          path,
          ctx->last_callback_fired,
          resource_status_name(ctx->last_callback_status),
          ctx->last_callback_size,
          ctx->last_callback_data);

    if (status != RESOURCE_STATUS_READY) {
        LOGLN("[state:resource_test] verify failed tag=%s reason=status_not_ready", tag);
        return -2;
    }

    if (type != ctx->expected_type) {
        LOGLN("[state:resource_test] verify failed tag=%s reason=type_mismatch expected=%s actual=%s",
              tag,
              resource_type_name(ctx->expected_type),
              resource_type_name(type));
        return -3;
    }

    if (strcmp(path, ctx->expected_path) != 0) {
        LOGLN("[state:resource_test] verify failed tag=%s reason=path_mismatch expected=%s actual=%s",
              tag,
              ctx->expected_path,
              path);
        return -4;
    }

    if (!ctx->last_callback_fired) {
        LOGLN("[state:resource_test] verify failed tag=%s reason=callback_not_fired", tag);
        return -5;
    }

    if (ctx->last_callback_status != RESOURCE_STATUS_READY) {
        LOGLN("[state:resource_test] verify failed tag=%s reason=callback_status_not_ready status=%s",
              tag,
              resource_status_name(ctx->last_callback_status));
        return -6;
    }

    if (ctx->last_callback_size != size) {
        LOGLN("[state:resource_test] verify failed tag=%s reason=callback_size_mismatch cb=%u actual=%u",
              tag,
              ctx->last_callback_size,
              size);
        return -7;
    }

    if (ctx->last_callback_data != data) {
        LOGLN("[state:resource_test] verify failed tag=%s reason=callback_data_mismatch cb=%p actual=%p",
              tag,
              ctx->last_callback_data,
              data);
        return -8;
    }

    if (size == 0) {
        if (data != NULL) {
            LOGLN("[state:resource_test] verify failed tag=%s reason=zero_size_nonnull_data data=%p",
                  tag,
                  data);
            return -9;
        }

        LOGLN("[state:resource_test] verify ok tag=%s zero-size resource", tag);
        return 0;
    }

    if (!data) {
        LOGLN("[state:resource_test] verify failed tag=%s reason=null_data_nonzero_size", tag);
        return -10;
    }

    LOGLN("[state:resource_test] verify ok tag=%s first_bytes=%02x %02x %02x %02x",
          tag,
          size > 0 ? ((unsigned char *)data)[0] : 0,
          size > 1 ? ((unsigned char *)data)[1] : 0,
          size > 2 ? ((unsigned char *)data)[2] : 0,
          size > 3 ? ((unsigned char *)data)[3] : 0);

    return 0;
}

static void resource_test_release_current(resource_test_ctx_t *ctx,
                                          const char *tag)
{
    resource_handle_t old_handle;

    if (!ctx)
        return;

    old_handle = ctx->handle;

    if (!resource_is_valid(old_handle)) {
        LOGLN("[state:resource_test] release skipped tag=%s reason=invalid_handle", tag);
        return;
    }

    LOGLN("[state:resource_test] releasing tag=%s handle=(%u,%u) type=%s path=%s",
          tag,
          old_handle.index,
          old_handle.generation,
          resource_type_name(resource_type(old_handle)),
          resource_path(old_handle));

    resource_release(old_handle);

    LOGLN("[state:resource_test] released tag=%s old_valid_now=%d",
          tag,
          resource_is_valid(old_handle));

    ctx->handle = resource_test_invalid_handle();
    ctx->expected_type = RESOURCE_TYPE_RAW;
    ctx->expected_path = NULL;
    ctx->tag = NULL;
    resource_test_reset_callback_info(ctx);
}

static int resource_test_enter(game_app_t *app, void *userdata)
{
    resource_test_ctx_t *ctx;

    (void)userdata;

    ctx = (resource_test_ctx_t *)mem_arena_calloc(
        game_app_state_arena(app),
        1,
        sizeof(*ctx),
        16
    );
    if (!ctx) {
        LOGLN("[state:resource_test] enter failed: no state arena memory");
        return -1;
    }

    ctx->handle = resource_test_invalid_handle();
    game_app_set_state_userdata(app, ctx);

    LOGLN("[state:resource_test] enter");
    LOGLN("[state:resource_test] automatic test begin path=%s timeout=%d frames",
          TEST_RESOURCE_PATH,
          RESOURCE_TEST_TIMEOUT_FRAMES);
    LOGLN("[state:resource_test] phases: raw -> release -> sprite_bank -> release");
    LOGLN("[state:resource_test] START pressed, quit");

    resource_test_set_phase(ctx, RESOURCE_TEST_PHASE_RAW_QUEUE);
    return 0;
}

static void resource_test_exit(game_app_t *app)
{
    resource_test_ctx_t *ctx = resource_test_data(app);

    if (ctx && resource_is_valid(ctx->handle))
        resource_release(ctx->handle);

    game_app_set_state_userdata(app, NULL);

    LOGLN("[state:resource_test] exit");
}

static void resource_test_update(game_app_t *app, float dt)
{
    resource_test_ctx_t *ctx = resource_test_data(app);
    resource_status_t st;
    int rc;

    (void)dt;

    if (!ctx)
        return;

    ctx->total_ticks++;
    ctx->phase_ticks++;

    switch (ctx->phase) {
    case RESOURCE_TEST_PHASE_RAW_QUEUE:
        if (resource_test_queue(ctx, RESOURCE_TYPE_RAW, TEST_RESOURCE_PATH, "raw") < 0) {
            resource_test_set_phase(ctx, RESOURCE_TEST_PHASE_FAILED);
        } else {
            resource_test_set_phase(ctx, RESOURCE_TEST_PHASE_RAW_WAIT);
        }
        break;

    case RESOURCE_TEST_PHASE_RAW_WAIT:
        rc = resource_test_wait_for_terminal(ctx,
                                             ctx->handle,
                                             ctx->phase_ticks,
                                             RESOURCE_TEST_TIMEOUT_FRAMES,
                                             "raw",
                                             &st);
        if (rc < 0) {
            RESOURCE_TEST_FAILF(ctx, "raw wait failed rc=%d", rc);
        } else if (rc > 0) {
            if (st == RESOURCE_STATUS_READY)
                resource_test_set_phase(ctx, RESOURCE_TEST_PHASE_RAW_VERIFY);
            else
                RESOURCE_TEST_FAILF(ctx, "raw completed with status=%s", resource_status_name(st));
        }
        break;

    case RESOURCE_TEST_PHASE_RAW_VERIFY:
        if (resource_test_verify_loaded(ctx, "raw") < 0)
            resource_test_set_phase(ctx, RESOURCE_TEST_PHASE_FAILED);
        else
            resource_test_set_phase(ctx, RESOURCE_TEST_PHASE_RAW_RELEASE);
        break;

    case RESOURCE_TEST_PHASE_RAW_RELEASE:
        resource_test_release_current(ctx, "raw");
        resource_test_set_phase(ctx, RESOURCE_TEST_PHASE_SPRITE_QUEUE);
        break;

    case RESOURCE_TEST_PHASE_SPRITE_QUEUE:
        if (resource_test_queue(ctx, RESOURCE_TYPE_SPRITE_BANK, TEST_RESOURCE_PATH, "sprite_bank") < 0) {
            resource_test_set_phase(ctx, RESOURCE_TEST_PHASE_FAILED);
        } else {
            resource_test_set_phase(ctx, RESOURCE_TEST_PHASE_SPRITE_WAIT);
        }
        break;

    case RESOURCE_TEST_PHASE_SPRITE_WAIT:
        rc = resource_test_wait_for_terminal(ctx,
                                             ctx->handle,
                                             ctx->phase_ticks,
                                             RESOURCE_TEST_TIMEOUT_FRAMES,
                                             "sprite_bank",
                                             &st);
        if (rc < 0) {
            RESOURCE_TEST_FAILF(ctx, "sprite_bank wait failed rc=%d", rc);
        } else if (rc > 0) {
            if (st == RESOURCE_STATUS_READY)
                resource_test_set_phase(ctx, RESOURCE_TEST_PHASE_SPRITE_VERIFY);
            else
                RESOURCE_TEST_FAILF(ctx, "sprite_bank completed with status=%s", resource_status_name(st));
        }
        break;

    case RESOURCE_TEST_PHASE_SPRITE_VERIFY:
        if (resource_test_verify_loaded(ctx, "sprite_bank") < 0)
            resource_test_set_phase(ctx, RESOURCE_TEST_PHASE_FAILED);
        else
            resource_test_set_phase(ctx, RESOURCE_TEST_PHASE_SPRITE_RELEASE);
        break;

    case RESOURCE_TEST_PHASE_SPRITE_RELEASE:
        resource_test_release_current(ctx, "sprite_bank");
        LOGLN("[state:resource_test] all tests passed total_ticks=%d callbacks=%d",
              ctx->total_ticks,
              ctx->callback_count);
        resource_test_set_phase(ctx, RESOURCE_TEST_PHASE_DONE);
        break;

    case RESOURCE_TEST_PHASE_DONE:
    case RESOURCE_TEST_PHASE_FAILED:
    case RESOURCE_TEST_PHASE_IDLE:
    default:
        break;
    }

    if (input_button_pressed(INPUT_BUTTON_START)) {
        LOGLN("[state:resource_test] START pressed, quit phase=%s",
              resource_test_phase_name(ctx->phase));
        game_app_request_state_change(debug_menu_state_desc(), NULL);
        return;
    }
}

static void resource_test_draw(game_app_t *app)
{
    (void)app;
}

static const game_state_desc_t g_resource_test_state = {
    "resource_test",
    resource_test_enter,
    resource_test_exit,
    resource_test_update,
    resource_test_draw
};

const game_state_desc_t *resource_test_state_desc(void)
{
    return &g_resource_test_state;
}