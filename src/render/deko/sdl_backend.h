#pragma once
#include <SDL3/SDL.h>

// Private application/SDL driver contract. No Deko handles cross this boundary.
// Renderer borrows the backend. Backend outlives renderer and all its textures.
#define GT3R_DEKO_BACKEND_PROPERTY "gt3r.deko.backend"
#define GT3R_DEKO_BACKEND_VERSION 1u
// Borrowed native image; the contract is copied at texture creation. Its owner
// retains the image until the SDL texture, queued draws and GPU frames release it.
#define GT3R_DEKO_BORROW_IMAGE_PROPERTY "gt3r.deko.borrow_image"
#define GT3R_DEKO_IMAGE_VERSION 1u
typedef struct GT3R_DekoImage {
    Uint32 version;
    void *backend_userdata; // must match the creating backend
    void *userdata;
    int width, height;
    void (*retain)(void *);
    void (*release)(void *);
    // Descriptor belongs to the backend's shared image table, resolved per slot.
    bool (*descriptor)(void *, unsigned, Uint32 *);
} GT3R_DekoImage;

// Legacy diagnostic property, retained for older standalone callers.
#define GT3R_DEKO_BORROW_SCENE_PROPERTY "gt3r.deko.borrow_scene"

typedef struct GT3R_DekoVertex {
    float x, y, u, v;
    SDL_FColor color;
} GT3R_DekoVertex;

typedef struct GT3R_DekoBackend {
    Uint32 version;
    void *userdata;
    bool (*supports_blend)(void *, SDL_BlendMode);
    bool (*create_texture)(void *, int, int, int, SDL_PropertiesID, void **);
    bool (*update_texture)(void *, void *, const SDL_Rect *, const void *, int);
    void (*destroy_texture)(void *, void *);
    bool (*set_target)(void *, void *);
    bool (*clear)(void *, const SDL_FColor *);
    bool (*draw)(void *, void *, const GT3R_DekoVertex *, size_t,
                 const SDL_Rect *, const SDL_Rect *, SDL_BlendMode,
                 SDL_ScaleMode, SDL_TextureAddressMode, SDL_TextureAddressMode);
    bool (*present)(void *);
} GT3R_DekoBackend;
