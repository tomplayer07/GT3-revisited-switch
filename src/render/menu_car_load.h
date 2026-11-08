#pragma once
#include "formats/model/car_model.h"
#include "render/gpu_model.h"
#include <array>
#include <functional>
#include <future>
#include <optional>

namespace gt3r { class Vfs; }
namespace gt3r::render {
// Owned inputs: no database/session pointers or borrowed geometry spans escape
// the menu thread. The VFS/context outlive all registered jobs (close_disc joins).
struct MenuCarRequest {
    std::string model, wheel;
    int paint=0;
    bool menu_wheel=false;
    std::optional<WheelSpec> wheels;
    CarBodyMod body;
    float ride_height=0;
};
struct MenuCarLoad {
    std::unique_ptr<GpuModel> model;
    std::array<float,3> low{}, high{};
    double parse_ms=0, upload_ms=0, total_ms=0;
};
MenuCarLoad load_menu_car(Vfs&,GpuContext&,const MenuCarRequest&);

// One outstanding job per picker. A busy picker never queues more work, so
// rapid selection changes cannot build an unbounded list of obsolete uploads.
class MenuCarLoader {
    std::future<MenuCarLoad> future_;
public:
    MenuCarLoader();
    ~MenuCarLoader();
    MenuCarLoader(const MenuCarLoader&)=delete;
    MenuCarLoader& operator=(const MenuCarLoader&)=delete;
    bool busy() const { return future_.valid(); }
    bool start(std::function<MenuCarLoad()> task);
    std::optional<MenuCarLoad> take_ready();
    void finish(); // join and retire result on the caller/menu thread
};
// Called before disc/VFS/context teardown. Registry access is menu-thread only.
void finish_menu_car_loads();
} // namespace gt3r::render
