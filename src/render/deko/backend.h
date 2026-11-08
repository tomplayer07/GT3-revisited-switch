#pragma once
#include "render/native_renderer.h"

namespace gt3r::render::deko {
// Experimental scene-independent backend. SDL video/ROMFS must be initialized;
// window is borrowed and must outlive the returned backend and its resources.
// Returns null and sets SDL's error on initialization failure. Current model
// capabilities are the basic views documented in README.md.
std::shared_ptr<NativeRenderer> create_renderer(SDL_Window* window);
} // namespace gt3r::render::deko
