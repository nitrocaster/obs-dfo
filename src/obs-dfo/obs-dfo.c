#define _CRT_SECURE_NO_WARNINGS
#include <obs-internal.h>
#include <obs-module.h>
#include <obs.h>

typedef struct dfo
{
    obs_source_t *source;
    obs_source_t *overlay_src;
    float update_time_elapsed;
    pthread_t thread;
    volatile bool run_thread;
    int dropped_frames;
    const char *prefix_str;
    const char *suffix_str;
} dfo_t;

bool output_enum_cb(void *ctx, obs_output_t *output)
{
    int *df_total = (int *)ctx;
    *df_total += obs_output_get_frames_dropped(output);
    return true;
}

void *dfo_thread_proc(dfo_t *dfo)
{
    while (dfo->run_thread)
    {
        int df_total = 0;
        obs_enum_outputs(output_enum_cb, &df_total);
        dfo->dropped_frames = df_total;
        os_sleep_ms(100);
    }
    return NULL;
}

static void dfo_start_thread(dfo_t *dfo)
{
    dfo->run_thread = true;
    if (pthread_create(&dfo->thread, NULL, dfo_thread_proc, dfo))
        blog(LOG_ERROR, "Can't create updater thread");
}

static void dfo_stop_thread(dfo_t *dfo)
{
    dfo->run_thread = false;
    if (pthread_join(dfo->thread, NULL))
        blog(LOG_ERROR, "Can't join updater thread");
}

static const char *dfo_get_name(void *unused)
{
    UNUSED_PARAMETER(unused);
    return obs_module_text("visible_name");
}

static void *dfo_create(obs_data_t *settings, obs_source_t *source)
{
    dfo_t *dfo = bzalloc(sizeof(dfo_t));
    dfo->dropped_frames = 0;
    dfo->source = source;
    dfo->prefix_str = "";
    dfo->suffix_str = "";
    const char *text_src_id = "text_ft2_source";
    dfo->overlay_src = obs_source_create(
        text_src_id, text_src_id, settings, NULL);
    obs_source_add_active_child(dfo->source, dfo->overlay_src);
    return dfo;
}

static void dfo_destroy(void *data)
{
    dfo_t *dfo = data;
    dfo_stop_thread(dfo);
    dfo->dropped_frames = 0;
    obs_source_remove(dfo->overlay_src);
    obs_source_release(dfo->overlay_src);
    dfo->overlay_src = NULL;
    bfree(dfo);
}

static void dfo_update(void *data, obs_data_t *settings)
{
    dfo_t *dfo = data;
    dfo_stop_thread(dfo);
    obs_data_set_string(dfo->overlay_src->context.settings, "text", "0");
    dfo->prefix_str = obs_data_get_string(settings, "prefix_str");
    if (!dfo->prefix_str)
        dfo->prefix_str = "";
    dfo->suffix_str = obs_data_get_string(settings, "suffix_str");
    if (!dfo->suffix_str)
        dfo->suffix_str = "";
    dfo_start_thread(dfo);
}

static void dfo_defaults(obs_data_t *data)
{
    obs_data_set_default_string(data, "prefix_str", "");
    obs_data_set_default_string(data, "suffix_str", "");
}

static void dfo_show(void *data)
{
    dfo_t *dfo = data;
    dfo_start_thread(dfo);
}

static uint32_t dfo_get_width(void *data)
{
    dfo_t *dfo = data;
    return obs_source_get_width(dfo->overlay_src);
}

static uint32_t dfo_get_height(void *data)
{
    dfo_t *dfo = data;
    return obs_source_get_height(dfo->overlay_src);
}

static void dfo_render(void *data, gs_effect_t *effect)
{
    dfo_t *dfo = data;
    obs_source_video_render(dfo->overlay_src);
}

static void dfo_tick(void *data, float seconds)
{
    dfo_t *dfo = data;
    if (!obs_source_showing(dfo->source))
        return;
    dfo->update_time_elapsed += seconds;
    if (dfo->update_time_elapsed >= 0.1f)
    {
        dfo->update_time_elapsed = 0.0f;
        char buf[4096];
        snprintf(buf, sizeof(buf), "%s%d%s", dfo->prefix_str,
            dfo->dropped_frames, dfo->suffix_str);
        obs_data_set_string(dfo->overlay_src->context.settings,
            "text", dfo->run_thread ? buf : "0");
        obs_source_update(
            dfo->overlay_src, dfo->overlay_src->context.settings);
    }
}

static obs_properties_t *dfo_properties(void *data)
{
    dfo_t *dfo = data;
    obs_properties_t *props = obs_source_properties(dfo->overlay_src);
    obs_properties_add_text(
        props, "prefix_str", "Text before counter", OBS_TEXT_MULTILINE);
    obs_properties_add_text(
        props, "suffix_str", "Text after counter", OBS_TEXT_MULTILINE);
    return props;
}

void dfo_enum_active_sources(void *data, obs_source_enum_proc_t enum_callback,
    void *param)
{
    dfo_t *dfo = data;
    enum_callback(dfo->source, dfo->overlay_src, param);
}

static struct obs_source_info dfo_info =
{
    .id = "dropped-frames-overlay",
    .type = OBS_SOURCE_TYPE_INPUT,
    .output_flags = OBS_SOURCE_VIDEO,
    .get_name = dfo_get_name,
    .create = dfo_create,
    .destroy = dfo_destroy,
    .update = dfo_update,
    .get_defaults = dfo_defaults,
    .show = dfo_show,
    .get_width = dfo_get_width,
    .get_height = dfo_get_height,
    .video_render = dfo_render,
    .video_tick = dfo_tick,
    .get_properties = dfo_properties,
    .enum_active_sources = dfo_enum_active_sources
};

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("obs-dfo", "en-US")

bool obs_module_load(void)
{
    obs_register_source(&dfo_info);
    return true;
}
