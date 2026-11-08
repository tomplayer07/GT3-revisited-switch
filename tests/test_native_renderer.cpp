#include "test_framework.h"
#include "render/view3d.h"
#include "render/menu_car_load.h"
#include "vfs/vfs.h"
using namespace gt3r;
using namespace gt3r::render;
namespace {
struct Counts { int uploads=0,models=0,views=0,draws=0,vertex_updates=0,width=0,height=0; bool fail=false; const NativeTexture* sphere=nullptr; bool gt3_lighting=false; float reflect=0; int texture_renders=0,cubes=0,fail_texture_at=-1,postprocesses=0; bool grade=false; float grade_black=0; const NativeTexture* image=nullptr; const NativeCubeTexture* cube=nullptr; };
class TestModel final:public NativeModel {
    Counts& counts;
public:
    explicit TestModel(Counts& c):counts(c) { ++counts.models; }
    ~TestModel() override { --counts.models; }
    bool valid() const override { return true; }
    bool update_vertices(std::span<const scene::Vertex>) override { ++counts.vertex_updates; return !counts.fail; }
    bool update_geometry(const std::vector<scene::Mesh>&) override { return !counts.fail; }
    bool update_textures(std::span<const ModelTextureUpdate>) override { return !counts.fail; }
};
class TestTexture final:public NativeTexture {
public:
    SDL_Texture* image;
    explicit TestTexture(SDL_Renderer* renderer):image(SDL_CreateTexture(renderer,SDL_PIXELFORMAT_RGBA32,SDL_TEXTUREACCESS_STATIC,1,1)) {}
    ~TestTexture() override { SDL_DestroyTexture(image); }
    SDL_Texture* renderer_texture() const override { return image; }
};
class TestCube final:public NativeCubeTexture {
public:
    std::array<std::shared_ptr<NativeTexture>,6> faces;
    explicit TestCube(const std::array<std::shared_ptr<NativeTexture>,6>& source):faces(source) {}
};
class TestView final:public NativeView {
    Counts& counts;
    SDL_Renderer* renderer;
    SDL_Texture* image=nullptr;
public:
    TestView(Counts& c,SDL_Renderer* r):counts(c),renderer(r) { ++counts.views; }
    ~TestView() override { SDL_DestroyTexture(image); --counts.views; }
    SDL_Texture* draw(int w,int h,const Mat4&,const Mat4&,const Vec3&,
        const std::vector<DrawItem>& items,const DrawOptions& options) override {
        counts.image=items.empty() ? nullptr : items[0].native_texture.get(); counts.cube=options.native_env_cube.get();
        counts.sphere=options.native_env_sphere.get(); counts.gt3_lighting=options.gt3_lighting; counts.reflect=options.reflection_strength;
        ++counts.draws; counts.width=w; counts.height=h;
        if(items.empty() || !items[0].model->native_model()) return nullptr;
        if(!image) {
            image=SDL_CreateTexture(renderer,SDL_PIXELFORMAT_RGBA32,SDL_TEXTUREACCESS_STATIC,1,2);
            const unsigned char red[]={255,0,0,255, 0,0,255,255};
            if(image) { SDL_SetTextureScaleMode(image,SDL_SCALEMODE_NEAREST); SDL_UpdateTexture(image,nullptr,red,4); }
        }
        return image;
    }
    std::shared_ptr<NativeTexture> render_texture(int w,int h,const Mat4&,const Mat4&,const Vec3&,
        const std::vector<DrawItem>&,const DrawOptions&) override {
        counts.width=w; counts.height=h;
        if(++counts.texture_renders==counts.fail_texture_at) { SDL_SetError("test target failed"); return {}; }
        return std::make_shared<TestTexture>(renderer);
    }
    std::shared_ptr<NativeTexture> process_texture(int w,int h,const std::shared_ptr<NativeTexture>& source,const DrawOptions& options) override {
        ++counts.postprocesses; counts.grade=options.post_grade.on; counts.grade_black=options.post_grade.black;
        counts.width=w; counts.height=h;
        return source ? std::make_shared<TestTexture>(renderer) : nullptr;
    }

};
class TestBackend final:public NativeRenderer {
public:
    Counts counts;
    SDL_Surface* surface=SDL_CreateSurface(32,32,SDL_PIXELFORMAT_RGBA32);
    SDL_Renderer* renderer=surface ? SDL_CreateSoftwareRenderer(surface) : nullptr;
    ~TestBackend() override { SDL_DestroyRenderer(renderer); SDL_DestroySurface(surface); }
    SDL_Renderer* renderer2d() const override { return renderer; }
    const char* name() const override { return "native-test"; }
    std::unique_ptr<NativeModel> upload_model(const scene::Model&) override {
        ++counts.uploads; return std::make_unique<TestModel>(counts);
    }
    std::unique_ptr<NativeModel> share_model_textures(const NativeModel&) override { return std::make_unique<TestModel>(counts); }
    std::unique_ptr<NativeView> create_view() override { return std::make_unique<TestView>(counts,renderer); }
    std::shared_ptr<NativeCubeTexture> create_cube(const std::array<std::shared_ptr<NativeTexture>,6>& faces) override {
        ++counts.cubes; return std::make_shared<TestCube>(faces);
    }
};
scene::Model source_model() {
    scene::Model source; scene::Mesh mesh;
    mesh.vertices.resize(3); mesh.indices={0,1,2};
    scene::Primitive p; p.index_count=3; mesh.primitives.push_back(p);
    source.meshes.push_back(mesh); return source;
}
}
TEST(native_renderer_models_updates_and_view_compose_without_gpu_device) {
    auto backend=std::make_shared<TestBackend>();
    CHECK(backend->renderer); if(!backend->renderer) return;
    auto context=GpuContext::adopt_native(backend);
    CHECK(bool(context)); if(!context) return;
    CHECK(context->device()==nullptr);
    CHECK(std::string(context->driver_name())=="native-test");
    context->set_msaa(4); context->set_anisotropy(16);
    CHECK(context->msaa()==1); CHECK(context->anisotropy()==1);
    auto source=source_model();
    {
        GpuModel model(*context,source,true);
        CHECK(model.valid()); CHECK(backend->counts.uploads==1);
        {
            auto other=std::make_shared<TestBackend>();
            auto otherContext=GpuContext::adopt_native(other);
            CHECK(bool(otherContext));
            if(otherContext) { GpuModel invalid(*otherContext,model); CHECK(!invalid.valid()); }
        }
        auto changed=source.meshes[0].vertices; changed[0].pos[0]=2;
        backend->counts.fail=true;
        CHECK(!model.update_vertices(changed));
        CHECK(model.geometry()->vertices[0].pos[0]==0);
        backend->counts.fail=false;
        CHECK(model.update_vertices(changed));
        CHECK(model.geometry()->vertices[0].pos[0]==2);
        CHECK(!model.update_vertices({}));
        {
            GpuModel dynamic(*context,model);
            CHECK(dynamic.update_geometry(source.meshes));
            CHECK(dynamic.valid()); CHECK(dynamic.vertex_count()==3);
            View3D view(*context);
            SDL_SetRenderDrawColor(backend->renderer,0,0,0,255);
            SDL_RenderClear(backend->renderer);
            DrawOptions options; options.fixed_width=8; options.fixed_height=12;
            DrawItem item{}; item.model=&model;
            view.draw(backend->renderer,{0,0,32,32},Mat4{},Mat4{},Vec3{},{item},options);
            CHECK(backend->counts.draws==1);
            CHECK(!view.render_env_cube({}, {}, options, 16));
            CHECK(!view.render_offscreen(0,16,16,Mat4{},Mat4{},{},{},options));
            CHECK(!view.draw_to(nullptr,16,16,Mat4{},Mat4{},{},{},options));
            CHECK(backend->counts.width==8); CHECK(backend->counts.height==12);
            CHECK(SDL_RenderPresent(backend->renderer));
            Uint8 r=0,g=0,b=0,a=0;
            CHECK(SDL_ReadSurfacePixel(backend->surface,8,8,&r,&g,&b,&a));
            CHECK(r==255); CHECK(g==0); CHECK(b==0);
            CHECK(SDL_ReadSurfacePixel(backend->surface,24,24,&r,&g,&b,&a));
            CHECK(r==0); CHECK(g==0); CHECK(b==255);
        }
        CHECK(backend->counts.views==0); CHECK(backend->counts.models==1);
    }
    CHECK(backend->counts.models==0);
    context.reset();
    CHECK(SDL_SetRenderDrawColor(backend->renderer,0,0,0,255));
    CHECK(SDL_RenderClear(backend->renderer)); // renderer still owned by backend
    CHECK(!GpuContext::adopt_native({}));
}
TEST(native_geometry_metadata_refresh_preserves_counts_bounds_and_optional_cpu_geometry) {
    auto backend=std::make_shared<TestBackend>();
    CHECK(backend->renderer); if(!backend->renderer) return;
    auto context=GpuContext::adopt_native(backend); CHECK(context); if(!context) return;
    for(bool retained:{false,true}) {
        auto source=source_model(); GpuModel model(*context,source,retained);
        source.meshes[0].vertices[1].pos[0]=12;
        source.meshes[0].transform[12]=5;
        CHECK(model.update_geometry(source.meshes)); CHECK(model.vertex_count()==3);
        CHECK(model.meshes().size()==1);
        CHECK(model.meshes()[0].transform.m[12]==5);
        CHECK(model.meshes()[0].radius>=6);
        CHECK(model.meshes()[0].primitives[0].index_count==3);
        if(retained) {
            CHECK(model.geometry());
            if(model.geometry()) CHECK(model.geometry()->vertices[1].pos[0]==12);
        } else CHECK(!model.geometry());
        backend->counts.fail=true;
        CHECK(!model.update_geometry({})); CHECK(model.vertex_count()==3);
        backend->counts.fail=false;
        CHECK(model.update_geometry({})); CHECK(model.vertex_count()==0); CHECK(model.meshes().empty());
    }
}

TEST(native_showroom_options_reach_view_without_legacy_gpu_texture) {
    class Reflection final:public NativeTexture {};
    auto backend=std::make_shared<TestBackend>();
    CHECK(backend->renderer); if(!backend->renderer) return;
    auto context=GpuContext::adopt_native(backend);
    CHECK(bool(context)); if(!context) return;
    GpuModel model(*context,source_model());
    View3D view(*context);
    DrawOptions options;
    options.native_env_sphere=std::make_shared<Reflection>();
    options.gt3_lighting=true;
    options.reflection_strength=0.75f;
    DrawItem item; item.model=&model;
    view.draw(backend->renderer,{0,0,32,32},Mat4{},Mat4{},{},{item},options);
    CHECK(backend->counts.draws==1);
    CHECK(backend->counts.sphere==options.native_env_sphere.get());
    CHECK(backend->counts.gt3_lighting);
    CHECK(backend->counts.reflect==0.75f);
    CHECK(options.env_sphere==nullptr);
    CHECK(SDL_RenderPresent(backend->renderer));
}

TEST(menu_car_background_upload_requires_backend_opt_in) {
    auto backend=std::make_shared<TestBackend>();
    CHECK(backend->renderer); if(!backend->renderer) return;
    auto context=GpuContext::adopt_native(backend);
    CHECK(bool(context)); if(!context) return;
    Vfs vfs(nullptr);
    bool rejected=false;
    try { load_menu_car(vfs,*context,MenuCarRequest{}); }
    catch(const std::runtime_error&) { rejected=true; }
    CHECK(rejected); CHECK(backend->counts.uploads==0);
}

TEST(native_offscreen_texture_survives_its_view_and_forwards_as_typed_input) {
    auto backend=std::make_shared<TestBackend>();
    CHECK(backend->renderer); if(!backend->renderer) return;
    auto context=GpuContext::adopt_native(backend);
    GpuModel model(*context,source_model()); DrawItem item; item.model=&model;
    std::shared_ptr<NativeTexture> image;
    {
        View3D producer(*context);
        CHECK(!producer.render_native_offscreen(4,16,16,Mat4{},Mat4{},{},{item},DrawOptions{}));
        CHECK(backend->counts.views==0);
        image=producer.render_native_offscreen(0,16,24,Mat4{},Mat4{},{},{item},DrawOptions{});
        CHECK(bool(image)); CHECK(backend->counts.texture_renders==1);
        CHECK(backend->counts.width==16); CHECK(backend->counts.height==24);
    }
    CHECK(backend->counts.views==0);
    CHECK(image && image->renderer_texture());
    View3D consumer(*context); item.native_texture=image;
    consumer.draw(backend->renderer,{0,0,32,32},Mat4{},Mat4{},{},{item},DrawOptions{});
    CHECK(backend->counts.image==image.get());
}
TEST(native_cube_initializes_all_faces_and_partial_update_keeps_old_snapshot) {
    auto backend=std::make_shared<TestBackend>();
    CHECK(backend->renderer); if(!backend->renderer) return;
    auto context=GpuContext::adopt_native(backend);
    GpuModel model(*context,source_model()); DrawItem item; item.model=&model;
    View3D view(*context); DrawOptions options;
    auto first=std::dynamic_pointer_cast<TestCube>(view.render_native_env_cube({}, {item},options,16,1));
    CHECK(bool(first)); if(!first) return;
    CHECK(backend->counts.texture_renders==6); CHECK(backend->counts.views==6);
    for(unsigned face=0;face<6;++face) CHECK(first->faces[face]==view.native_env_face(face));
    auto second=std::dynamic_pointer_cast<TestCube>(view.render_native_env_cube({}, {item},options,16,1u<<3));
    CHECK(bool(second)); if(!second) return;
    CHECK(backend->counts.texture_renders==7);
    for(unsigned face=0;face<6;++face) CHECK((first->faces[face]==second->faces[face])==(face!=3));
    CHECK(first->faces[3]!=view.native_env_face(3));
    CHECK(view.render_native_env_cube({}, {item},options,16,0)==second);
    CHECK(backend->counts.texture_renders==7);
    auto resized=view.render_native_env_cube({}, {item},options,32,1);
    CHECK(bool(resized)); CHECK(backend->counts.texture_renders==13);
    CHECK(backend->counts.width==32); CHECK(backend->counts.height==32);
    options.native_env_cube=resized;
    view.draw(backend->renderer,{0,0,32,32},Mat4{},Mat4{},{},{item},options);
    CHECK(backend->counts.cube==resized.get());
}
TEST(native_cube_failed_face_does_not_publish_a_partial_capture) {
    auto backend=std::make_shared<TestBackend>();
    CHECK(backend->renderer); if(!backend->renderer) return;
    auto context=GpuContext::adopt_native(backend);
    GpuModel model(*context,source_model()); DrawItem item; item.model=&model;
    View3D view(*context); DrawOptions options;
    auto original=view.render_native_env_cube({}, {item},options,16);
    CHECK(bool(original)); if(!original) return;
    auto face=view.native_env_face(0);
    backend->counts.fail_texture_at=backend->counts.texture_renders+2;
    CHECK(!view.render_native_env_cube({}, {item},options,16,3));
    CHECK(view.native_env_face(0)==face); CHECK(backend->counts.cubes==1);
    CHECK(view.render_native_env_cube({}, {item},options,16,0)==original);
    CHECK(!view.render_native_env_cube({}, {item},options,0));
    CHECK(view.native_env_face(0)==face);
}

TEST(native_race_grade_uses_typed_intermediate_and_fullscreen_pass) {
    auto backend=std::make_shared<TestBackend>();
    CHECK(backend->renderer); if(!backend->renderer) return;
    auto context=GpuContext::adopt_native(backend);
    GpuModel model(*context,source_model()); DrawItem item; item.model=&model;
    View3D view(*context); DrawOptions options; options.post_grade.on=true; options.post_grade.black=0.125f;
    view.draw(backend->renderer,{0,0,32,32},Mat4{},Mat4{},{},{item},options);
    CHECK(backend->counts.texture_renders==1); CHECK(backend->counts.postprocesses==1);
    CHECK(backend->counts.grade); CHECK(backend->counts.grade_black==0.125f);
    CHECK(backend->counts.width==32); CHECK(backend->counts.height==32);
    CHECK(backend->counts.draws==0);
    CHECK(SDL_RenderPresent(backend->renderer));
}
