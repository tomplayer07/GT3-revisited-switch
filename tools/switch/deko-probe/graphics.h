#pragma once
#include <memory>
#include "frame_trace.h"
#ifdef GT3R_DEKO_SDL
#include "../deko-sdl/backend.h"
#endif

namespace gt3r::render { class NativeRenderer; }
namespace gt3r::race { class CarLampGlows; class LampGlows; }
namespace gt3r::deko_probe {
// Owns all native graphics resources. No libnx/Deko types escape this boundary.
class Graphics {
public:
    Graphics();
    ~Graphics();
    Graphics(const Graphics&) = delete;
    Graphics& operator=(const Graphics&) = delete;
    void init();
    void present();
    FramePhases frame_phases() const;
    bool toggle_overlay();
#ifdef GT3R_DEKO_SDL
    GT3R_DekoBackend sdl_backend();
    GT3R_DekoImage scene_image();
    std::shared_ptr<render::NativeRenderer> create_native_renderer(SDL_Window* window);
    bool toggle_materials();
    bool toggle_reflections();
    bool toggle_vertex_demo();
    bool toggle_texture_demo();
    bool toggle_object_demo();
    bool toggle_race_camera();
    bool toggle_course_animations();
    bool toggle_point_lighting();
    bool toggle_wheel_motion();
    void prepare_frame();
    void init_street_glows(race::LampGlows& glows, SDL_Renderer* renderer);
    void draw_street_glows(race::LampGlows& glows, SDL_Renderer* renderer);
    bool toggle_street_glows();
    bool toggle_course_culling();
    bool toggle_course_fog();
    void inspect_course_textures();
    unsigned cycle_course_mask_view();
    void init_car_glows(race::CarLampGlows& glows, SDL_Renderer* renderer);
    void draw_car_glows(race::CarLampGlows& glows, SDL_Renderer* renderer);
    void look_back(bool on);
    void driving_input(float throttle, float brake, float steer, bool reset);
    float speed_kmh() const;
    void capture_environment();
    void move_camera(float forward, float side, float turn, float tilt, float lift, float seconds);
#endif
private:
    struct Impl;
    std::shared_ptr<Impl> impl_;
};
bool open_log();
void trace(const char* stage);
void close_log();
}
