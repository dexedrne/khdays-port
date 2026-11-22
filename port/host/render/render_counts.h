/* The graphics card's allocations and read-backs, counted for the window's summaries (gpu_present.c's "pace:"
 * line every 10 s on the phone): a picture should need neither once the window is up. Included after the SDL
 * headers by the files that draw the game's pictures (gpu_present.c, gfx_render.c, gpu_raster3d.c, gpu_hd.c,
 * kh2_sequence_gpu.c, host/app/window.c); each call below is counted, then made as SDL has it. Window thread
 * only. */
#ifndef KHDAYS_RENDER_COUNTS_H
#define KHDAYS_RENDER_COUNTS_H
#include <SDL3/SDL.h>

#ifdef __cplusplus
extern "C" {
#endif
extern unsigned khdays_render_allocations, khdays_render_readbacks;
/* The shaders and pipelines made, each timed (SDL_GPU on Metal compiles a shader's MSL text when the shader or
 * the compute pipeline is made, and the graphics pipeline's state when that is made: the stalls a first scene
 * would show). Kept by where it was made (file and line); khdays_render_pipelines_report prints them once
 * ("shaders: N pipelines in X ms ..."), after the window's renderer is up; each one made after that is said as
 * it is made ("shaders: made during play ..."), the first 40. gpu_hd.c. */
void khdays_render_pipeline_made(const char *file, int line, char kind, Uint64 ns, int ok);
void khdays_render_pipelines_report(void);
#ifdef __cplusplus
}
#endif

static inline SDL_GPUComputePipeline *khdays_timed_compute_pipeline(SDL_GPUDevice *d,
                                                                    const SDL_GPUComputePipelineCreateInfo *i,
                                                                    const char *file, int line)
{
    const Uint64 t = SDL_GetTicksNS();
    SDL_GPUComputePipeline *p = SDL_CreateGPUComputePipeline(d, i);
    khdays_render_pipeline_made(file, line, 'c', SDL_GetTicksNS() - t, p != NULL);
    return p;
}
static inline SDL_GPUGraphicsPipeline *khdays_timed_graphics_pipeline(SDL_GPUDevice *d,
                                                                      const SDL_GPUGraphicsPipelineCreateInfo *i,
                                                                      const char *file, int line)
{
    const Uint64 t = SDL_GetTicksNS();
    SDL_GPUGraphicsPipeline *p = SDL_CreateGPUGraphicsPipeline(d, i);
    khdays_render_pipeline_made(file, line, 'g', SDL_GetTicksNS() - t, p != NULL);
    return p;
}
static inline SDL_GPUShader *khdays_timed_shader(SDL_GPUDevice *d, const SDL_GPUShaderCreateInfo *i, const char *file,
                                                 int line)
{
    const Uint64 t = SDL_GetTicksNS();
    SDL_GPUShader *p = SDL_CreateGPUShader(d, i);
    khdays_render_pipeline_made(file, line, 's', SDL_GetTicksNS() - t, p != NULL);
    return p;
}
#define SDL_CreateGPUComputePipeline(d, i) khdays_timed_compute_pipeline((d), (i), __FILE__, __LINE__)
#define SDL_CreateGPUGraphicsPipeline(d, i) khdays_timed_graphics_pipeline((d), (i), __FILE__, __LINE__)
#define SDL_CreateGPUShader(d, i) khdays_timed_shader((d), (i), __FILE__, __LINE__)

#define SDL_CreateGPUBuffer(d, i) (++khdays_render_allocations, SDL_CreateGPUBuffer((d), (i)))
#define SDL_CreateGPUTexture(d, i) (++khdays_render_allocations, SDL_CreateGPUTexture((d), (i)))
#define SDL_CreateGPUTransferBuffer(d, i) (++khdays_render_allocations, SDL_CreateGPUTransferBuffer((d), (i)))
#define SDL_CreateTexture(r, f, a, w, h) (++khdays_render_allocations, SDL_CreateTexture((r), (f), (a), (w), (h)))
#define SDL_CreateTextureWithProperties(r, p) (++khdays_render_allocations, SDL_CreateTextureWithProperties((r), (p)))
#define SDL_DownloadFromGPUTexture(c, s, d) (++khdays_render_readbacks, SDL_DownloadFromGPUTexture((c), (s), (d)))
#define SDL_DownloadFromGPUBuffer(c, s, d) (++khdays_render_readbacks, SDL_DownloadFromGPUBuffer((c), (s), (d)))
#define SDL_RenderReadPixels(r, rect) (++khdays_render_readbacks, SDL_RenderReadPixels((r), (rect)))

#endif
