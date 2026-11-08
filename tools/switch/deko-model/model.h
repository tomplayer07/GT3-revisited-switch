#pragma once
#include <deko3d.h>
#include <memory>
#include <array>
#include <vector>
#include <cstdint>
#include <span>
#include "render/math3d.h"
namespace gt3r::render { struct DrawOptions; struct DrawItem; }
namespace gt3r::sim { struct VehicleState; struct VehicleSetup; }
namespace gt3r::scene { struct Model; struct Vertex; }
namespace gt3r::deko_probe {
struct WheelBinding { int index=-1; bool spin=true; render::Vec3 center{}; };
struct SubmissionTiming { double wait=0, record=0; uint64_t draws=0,triangles=0; double setup=0,cull=0,packets=0,submit=0; };
class Model;
class FrameArena;
struct SceneOptions {
    bool frontend=false;
    const Model* textureSource=nullptr; // immutable original images borrowed from a retained model
    FrameArena* frameArena=nullptr;
    DkQueue uploadQueue=nullptr; // optional isolated loader queue, completed before return
    bool freeCamera=false;
    bool captureEnvironment=false;
    std::array<float,4> camera{}; // XYZ and yaw
    std::vector<WheelBinding> wheels; // optional per-mesh wheel ownership
    std::vector<uint8_t> meshRoles; // optional: empty means ordinary meshes; 0 course, 1 base car, 2 backdrop, 3 brake car, 4 camera-centred sky (runtime only)
};
// Borrows device/queue/shaders; destroy before their owner.
class Model {
public:
    Model(DkDevice device, DkQueue queue, const DkShader* vertex, const DkShader* fragment, const DkShader* capture,
          const DkShader* common = nullptr, DkGpuAddr sharedSamplerDescriptors = 0);
    ~Model();
    void load(const char* path, std::span<const uint8_t> course = {}); // diagnostic packet adapter
    void load_scene(const scene::Model& model, const SceneOptions& options = {});
    void load_course_animations(std::span<const uint8_t> course);
    void prepare_dynamic_resources(); // loading phase only, before frame submission
    void configure_cars(unsigned count); // shared geometry, independent instance state
    void animate_course(float seconds);
    void reset_course_animations();
    void update_car_lights(const sim::VehicleState& state, int lampHint, unsigned car = 0);
    bool toggle_point_lighting();
    void set_brake_factor(float factor, bool now, unsigned car = 0);
    // Same vertex count/topology. CPU input is copied before return.
    bool update_vertices(size_t mesh, std::span<const scene::Vertex> vertices);
    // Frame state copied into fenced per-draw uniforms at submission.
    bool set_mesh_transform(size_t mesh, const render::Mat4& transform);
    bool set_camera(const render::Mat4& view, const render::Mat4& projection, const render::Vec3& eye);
    sim::VehicleState initial_vehicle_state() const;
    bool apply_vehicle_state(const sim::VehicleState& state, const sim::VehicleSetup& setup, unsigned car = 0);
    bool toggle_wheel_motion();
    void clear_external_camera();
    void demo_object_transform(float phase); // diagnostic only
    bool update_texture(size_t texture, std::span<const uint32_t> rgba);
    void demo_texture_color(float phase); // diagnostic only
    void demo_vertex_offset(float height); // diagnostic only
    bool toggle_materials();
    bool toggle_reflections();
    bool toggle_course_culling();
    bool toggle_course_fog();
    void inspect_course_textures(); // explicit diagnostic, synchronizes GPU
    unsigned cycle_course_mask_view();
    void capture_environment();
    void update_environment(unsigned slot, const render::Vec3& origin);
    bool segment_blocked(const render::Vec3& from, const render::Vec3& to) const;
    bool set_environment_origin(const render::Vec3& origin);
    void move_camera(float forward, float side, float turn, float tilt, float lift, float seconds);
    void submit(unsigned slot, float angle, unsigned width, unsigned height);
    // Explicit target for the game frontend adapter; same reversed-depth convention.
    void submit_to(unsigned slot, unsigned width, unsigned height, const DkImage* color,
                   const DkImage* depth, const std::array<float,4>& clear,
                   const render::DrawOptions* options = nullptr, const DkImageDescriptor* sphere = nullptr,
                   const render::DrawItem* item = nullptr, bool clear_target = true, bool draw_geometry = true,
                   const DkImageDescriptor* cube_faces = nullptr, const DkImageDescriptor* image = nullptr, unsigned phase = 0, bool post = false);
    SubmissionTiming submission_timing() const;
    DkFence submission_fence(unsigned slot) const;
    void wait_submissions();
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
