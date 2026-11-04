#include "test_framework.h"
#include "render/view3d.h"
using namespace gt3r;
using namespace gt3r::render;
namespace {
struct Counts { int uploads=0,models=0,views=0,draws=0,vertex_updates=0,width=0,height=0; bool fail=false; };
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
class TestView final:public NativeView {
    Counts& counts;
    SDL_Renderer* renderer;
    SDL_Texture* image=nullptr;
public:
    TestView(Counts& c,SDL_Renderer* r):counts(c),renderer(r) { ++counts.views; }
    ~TestView() override { SDL_DestroyTexture(image); --counts.views; }
    SDL_Texture* draw(int w,int h,const Mat4&,const Mat4&,const Vec3&,
        const std::vector<DrawItem>& items,const DrawOptions&) override {
        ++counts.draws; counts.width=w; counts.height=h;
        if(items.empty() || !items[0].model->native_model()) return nullptr;
        if(!image) {
            image=SDL_CreateTexture(renderer,SDL_PIXELFORMAT_RGBA32,SDL_TEXTUREACCESS_STATIC,1,2);
            const unsigned char red[]={255,0,0,255, 0,0,255,255};
            if(image) { SDL_SetTextureScaleMode(image,SDL_SCALEMODE_NEAREST); SDL_UpdateTexture(image,nullptr,red,4); }
        }
        return image;
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
