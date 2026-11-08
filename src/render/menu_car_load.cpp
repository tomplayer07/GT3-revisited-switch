#include "render/menu_car_load.h"
#include "vfs/vfs.h"
#include "base/log.h"
#include <algorithm>
#include <chrono>
#include <stdexcept>
#include <vector>

namespace gt3r::render {
namespace {
std::vector<MenuCarLoader*>& loaders() { static std::vector<MenuCarLoader*> value; return value; }
}
MenuCarLoader::MenuCarLoader() { loaders().push_back(this); }
MenuCarLoader::~MenuCarLoader() {
    finish();
    auto& active=loaders();
    active.erase(std::remove(active.begin(),active.end(),this),active.end());
}
bool MenuCarLoader::start(std::function<MenuCarLoad()> task) {
    if (busy()) return false;
    future_=std::async(std::launch::async,std::move(task));
    return true;
}
std::optional<MenuCarLoad> MenuCarLoader::take_ready() {
    if (!busy() || future_.wait_for(std::chrono::seconds(0))!=std::future_status::ready) return {};
    return future_.get();
}
void MenuCarLoader::finish() {
    if (!busy()) return;
    try { auto completed=future_.get(); }
    catch (const std::exception& e) { LOG_WARN("Menu car loader stopped: %s",e.what()); }
    catch (...) { LOG_WARN("Menu car loader stopped with an unknown error"); }
}
void finish_menu_car_loads() { for (auto* loader:loaders()) loader->finish(); }

MenuCarLoad load_menu_car(Vfs& vfs,GpuContext& context,const MenuCarRequest& request) {
    if (!context.native_renderer() || !context.native_renderer()->background_model_upload())
        throw std::runtime_error("Backend does not support background model uploads");
    const u64 begin=SDL_GetTicksNS();
    auto file=request.model.empty() ? std::optional<Bytes>{} : vfs.read("menu/cars/"+request.model);
    if (!file && !request.model.empty()) file=vfs.read("cars/day/"+request.model);
    if (!file) throw std::runtime_error("Menu car file missing: "+request.model);
    CarModelOptions options;
    options.lod=0; options.menu_tyres=true; options.paint=request.paint;
    options.wheel_spec=request.wheels; options.body_mod=request.body;
    std::optional<Bytes> wheel;
    if (!request.wheel.empty()) {
        if (request.menu_wheel) wheel=vfs.read("menu/wheel/"+request.wheel);
        if (!wheel) wheel=vfs.read("wheel/"+request.wheel);
    } else if (!request.body.wheel.empty()) wheel=vfs.read("wheel/"+request.body.wheel);
    if (wheel) options.wheel_override=*wheel;
    auto source=load_car_model(*file,options,request.model);
    if (request.ride_height!=0) {
        for(auto& mesh:source.meshes) if(mesh.name.rfind("wheel_",0)!=0) mesh.transform[13]+=request.ride_height;
        source.bounds_max[1]+=request.ride_height;
    }
    MenuCarLoad result;
    std::copy_n(source.bounds_min,3,result.low.begin());
    std::copy_n(source.bounds_max,3,result.high.begin());
    const u64 parsed=SDL_GetTicksNS();
    result.model=std::make_unique<GpuModel>(context,source);
    if (!result.model->valid()) throw std::runtime_error(std::string("Native menu model upload failed: ")+SDL_GetError());
    const u64 end=SDL_GetTicksNS();
    result.parse_ms=double(parsed-begin)/1e6; result.upload_ms=double(end-parsed)/1e6; result.total_ms=double(end-begin)/1e6;
    return result;
}
} // namespace gt3r::render
