// GT3 Revisited experimental SDL3 render driver adapter.
// Compile against the exact private SDL headers verified by prepare_sdl.py.
#include "SDL_internal.h"
#include "render/SDL_sysrender.h"
#include "sdl_backend.h"

typedef struct {
    GT3R_DekoBackend gpu;
    SDL_Rect viewport, clip;
    bool clip_enabled;
} DekoRenderer;
typedef struct {
    void *gpu;
    Uint8 *pixels;
    SDL_Rect locked;
    bool is_locked;
} DekoTexture;

static bool QueueNoOp(SDL_Renderer *r, SDL_RenderCommand *cmd) { return true; }
static bool SupportsBlend(SDL_Renderer *r, SDL_BlendMode mode) {
    DekoRenderer *d = r->internal;
    return d->gpu.supports_blend(d->gpu.userdata, mode);
}
static bool CreateTexture(SDL_Renderer *r, SDL_Texture *t, SDL_PropertiesID props) {
    DekoRenderer *d = r->internal;
    DekoTexture *data = SDL_calloc(1, sizeof(*data));
    if (!data) return false;
    if (!d->gpu.create_texture(d->gpu.userdata, t->w, t->h, t->access, props, &data->gpu)) {
        SDL_free(data);
        return false;
    }
    if (t->access == SDL_TEXTUREACCESS_STREAMING) {
        data->pixels = SDL_calloc((size_t)t->w * t->h, 4);
        if (!data->pixels) {
            d->gpu.destroy_texture(d->gpu.userdata, data->gpu);
            SDL_free(data);
            return false;
        }
    }
    t->internal = data;
    return true;
}
static bool UpdateTexture(SDL_Renderer *r, SDL_Texture *t, const SDL_Rect *rect, const void *pixels, int pitch) {
    DekoRenderer *d = r->internal;
    DekoTexture *data = t->internal;
    if (!d->gpu.update_texture(d->gpu.userdata, data->gpu, rect, pixels, pitch)) return false;
    if (data->pixels) for (int y = 0; y < rect->h; ++y)
        SDL_memcpy(data->pixels + ((size_t)(rect->y + y) * t->w + rect->x) * 4,
                   (const Uint8 *)pixels + (size_t)y * pitch, (size_t)rect->w * 4);
    return true;
}
static bool LockTexture(SDL_Renderer *r, SDL_Texture *t, const SDL_Rect *rect, void **pixels, int *pitch) {
    DekoTexture *data = t->internal;
    if (!data->pixels || data->is_locked) return SDL_SetError("Deko SDL: texture cannot be locked");
    data->locked = *rect; data->is_locked = true;
    *pitch = t->w * 4;
    *pixels = data->pixels + ((size_t)rect->y * t->w + rect->x) * 4;
    return true;
}
static void UnlockTexture(SDL_Renderer *r, SDL_Texture *t) {
    DekoRenderer *d = r->internal;
    DekoTexture *data = t->internal;
    if (!data->is_locked) return;
    data->is_locked = false;
    const void *pixels = data->pixels + ((size_t)data->locked.y * t->w + data->locked.x) * 4;
    if (!d->gpu.update_texture(d->gpu.userdata, data->gpu, &data->locked, pixels, t->w * 4))
        SDL_LogError(SDL_LOG_CATEGORY_RENDER, "Deko texture unlock upload: %s", SDL_GetError());
}
static void DestroyTexture(SDL_Renderer *r, SDL_Texture *t) {
    DekoRenderer *d = r->internal;
    DekoTexture *data = t->internal;
    if (data) { d->gpu.destroy_texture(d->gpu.userdata, data->gpu); SDL_free(data->pixels); SDL_free(data); }
    t->internal = NULL;
}
static bool SetTarget(SDL_Renderer *r, SDL_Texture *t) {
    DekoRenderer *d = r->internal;
    return d->gpu.set_target(d->gpu.userdata, t ? ((DekoTexture *)t->internal)->gpu : NULL);
}
static bool QueueGeometry(SDL_Renderer *r, SDL_RenderCommand *cmd, SDL_Texture *t,
    const float *xy, int xy_stride, const SDL_FColor *color, int color_stride,
    const float *uv, int uv_stride, int num_vertices, const void *indices,
    int num_indices, int size_indices, float scale_x, float scale_y) {
    const int count = indices ? num_indices : num_vertices;
    if (count <= 0) { cmd->data.draw.count = 0; return true; }
    if (indices && size_indices != 1 && size_indices != 2 && size_indices != 4)
        return SDL_SetError("Deko: unsupported index size");
    GT3R_DekoVertex *v = SDL_AllocateRenderVertices(r, (size_t)count * sizeof(*v), _Alignof(GT3R_DekoVertex), &cmd->data.draw.first);
    if (!v) return false;
    for (int i = 0; i < count; ++i) {
        Uint32 k = (Uint32)i;
        if (indices) {
            k = 0;
            SDL_memcpy(&k, (const Uint8 *)indices + i * size_indices, (size_t)size_indices);
        }
        if (k >= (Uint32)num_vertices) return SDL_SetError("Deko: vertex index out of bounds");
        float pos[2];
        SDL_memcpy(pos, (const Uint8 *)xy + (size_t)k * xy_stride, sizeof(pos));
        v[i].x = pos[0] * scale_x; v[i].y = pos[1] * scale_y;
        SDL_memcpy(&v[i].color, (const Uint8 *)color + (size_t)k * color_stride, sizeof(SDL_FColor));
        v[i].color.r *= cmd->data.draw.color_scale;
        v[i].color.g *= cmd->data.draw.color_scale;
        v[i].color.b *= cmd->data.draw.color_scale;
        v[i].u = v[i].v = 0;
        if (t) {
            float tex[2];
            SDL_memcpy(tex, (const Uint8 *)uv + (size_t)k * uv_stride, sizeof(tex));
            v[i].u = tex[0]; v[i].v = tex[1];
        }
    }
    cmd->data.draw.count = (size_t)count;
    return true;
}
static bool QueuePoints(SDL_Renderer *r, SDL_RenderCommand *cmd, const SDL_FPoint *points, int count) {
    // SDL debug text and point-based primitives become ordinary solid triangles.
    GT3R_DekoVertex *v = SDL_AllocateRenderVertices(r, (size_t)count * 6 * sizeof(*v), _Alignof(GT3R_DekoVertex), &cmd->data.draw.first);
    if (!v && count) return false;
    SDL_FColor c = cmd->data.draw.color;
    c.r *= cmd->data.draw.color_scale; c.g *= cmd->data.draw.color_scale; c.b *= cmd->data.draw.color_scale;
    const float offsets[6][2] = {{0,0},{1,0},{0,1},{0,1},{1,0},{1,1}};
    for (int p = 0; p < count; ++p)
        for (int k = 0; k < 6; ++k) {
            GT3R_DekoVertex *out = &v[p * 6 + k];
            out->x = points[p].x + offsets[k][0]; out->y = points[p].y + offsets[k][1];
            out->u = out->v = 0; out->color = c;
        }
    cmd->data.draw.count = (size_t)count * 6;
    return true;
}
static bool RunCommands(SDL_Renderer *r, SDL_RenderCommand *cmd, void *vertices, size_t bytes) {
    DekoRenderer *d = r->internal;
    for (; cmd; cmd = cmd->next) {
        switch (cmd->command) {
        case SDL_RENDERCMD_SETVIEWPORT: d->viewport = cmd->data.viewport.rect; break;
        case SDL_RENDERCMD_SETCLIPRECT:
            d->clip_enabled = cmd->data.cliprect.enabled;
            d->clip = cmd->data.cliprect.rect;
            break;
        case SDL_RENDERCMD_CLEAR: {
            SDL_FColor c = cmd->data.color.color;
            c.r *= cmd->data.color.color_scale; c.g *= cmd->data.color.color_scale; c.b *= cmd->data.color.color_scale;
            if (!d->gpu.clear(d->gpu.userdata, &c)) return false;
            break;
        }
        case SDL_RENDERCMD_GEOMETRY:
        case SDL_RENDERCMD_DRAW_POINTS: {
            const size_t first = cmd->data.draw.first, count = cmd->data.draw.count;
            if (first > bytes || count > (bytes - first) / sizeof(GT3R_DekoVertex))
                return SDL_SetError("Deko: invalid queued geometry range");
            SDL_Texture *t = cmd->data.draw.texture;
            void *tex = t ? ((DekoTexture *)t->internal)->gpu : NULL;
            if (count && !d->gpu.draw(d->gpu.userdata, tex,
                    (const GT3R_DekoVertex *)((const Uint8 *)vertices + first), count,
                    &d->viewport, d->clip_enabled ? &d->clip : NULL,
                    cmd->data.draw.blend, cmd->data.draw.texture_scale_mode,
                    cmd->data.draw.texture_address_mode_u, cmd->data.draw.texture_address_mode_v)) return false;
            break;
        }
        case SDL_RENDERCMD_NO_OP:
        case SDL_RENDERCMD_SETDRAWCOLOR: break;
        default: return SDL_SetError("Deko: unimplemented render command %d", cmd->command);
        }
    }
    return true;
}
// SDL_FlushRenderer calls this unconditionally after draining its command queue.
// This adapter caches logical viewport/clip only, not native GPU bindings: each
// queued draw snapshots its state and the backend records native state later.
// Preserve viewport/clip so a flush does not change subsequent SDL drawing.
static void InvalidateCachedState(SDL_Renderer *r) { (void)r; }
static bool Present(SDL_Renderer *r) {
    DekoRenderer *d = r->internal;
    return d->gpu.present(d->gpu.userdata);
}
static void DestroyRenderer(SDL_Renderer *r) { SDL_free(r->internal); r->internal = NULL; }
static bool CreateRenderer(SDL_Renderer *r, SDL_Window *window, SDL_PropertiesID props) {
    GT3R_DekoBackend *gpu = SDL_GetPointerProperty(props, GT3R_DEKO_BACKEND_PROPERTY, NULL);
    if (!gpu || gpu->version != GT3R_DEKO_BACKEND_VERSION || !gpu->supports_blend || !gpu->create_texture ||
        !gpu->update_texture || !gpu->destroy_texture || !gpu->set_target || !gpu->clear || !gpu->draw || !gpu->present)
        return SDL_SetError("Deko: a complete versioned native backend is required");
    SDL_SetupRendererColorspace(r, props);
    if (r->output_colorspace != SDL_COLORSPACE_SRGB) return SDL_SetError("Deko: only sRGB output supported");
    DekoRenderer *d = SDL_calloc(1, sizeof(*d));
    if (!d) return false;
    d->gpu = *gpu;
    SDL_GetWindowSizeInPixels(window, &d->viewport.w, &d->viewport.h);
    r->internal = d; r->window = window; r->name = "gt3r-deko";
    r->SupportsBlendMode = SupportsBlend;
    r->InvalidateCachedState = InvalidateCachedState;
    r->CreateTexture = CreateTexture; r->UpdateTexture = UpdateTexture; r->DestroyTexture = DestroyTexture;
    r->SetRenderTarget = SetTarget; r->LockTexture = LockTexture; r->UnlockTexture = UnlockTexture;
    r->QueueSetViewport = QueueNoOp; r->QueueSetDrawColor = QueueNoOp;
    r->QueueGeometry = QueueGeometry; r->QueueDrawPoints = QueuePoints;
    r->RunCommandQueue = RunCommands; r->RenderPresent = Present; r->DestroyRenderer = DestroyRenderer;
    // SDL converts other incoming pixel formats into this driver's byte layout.
    return SDL_AddSupportedTextureFormat(r, SDL_PIXELFORMAT_RGBA32);
}
SDL_RenderDriver GT3R_Deko_RenderDriver = { CreateRenderer, "gt3r-deko" };
