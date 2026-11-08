#include "test_framework.h"
#include "render/menu_car_load.h"
#include <atomic>
#include <chrono>
#include <stdexcept>
#include <thread>
using namespace gt3r::render;

namespace {
std::optional<MenuCarLoad> wait_result(MenuCarLoader& loader) {
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
    while(std::chrono::steady_clock::now()<deadline) {
        if(auto result=loader.take_ready()) return result;
        std::this_thread::yield();
    }
    CHECK(false); return {};
}
}
TEST(menu_car_loader_keeps_ui_poll_nonblocking_and_bounds_pending_work) {
    MenuCarLoader loader;
    std::promise<void> entered, release;
    auto gate=release.get_future().share();
    CHECK(loader.start([&] { entered.set_value(); gate.wait(); MenuCarLoad result; result.low[0]=42; return result; }));
    entered.get_future().wait();
    CHECK(loader.busy());
    CHECK(!loader.take_ready()); // job cannot finish until the menu thread releases it
    CHECK(!loader.start([] { return MenuCarLoad{}; }));
    release.set_value();
    auto result=wait_result(loader);
    CHECK(bool(result)); if(result) CHECK(result->low[0]==42);
    CHECK(!loader.busy());
    CHECK(loader.start([] { MenuCarLoad result; result.high[0]=7; return result; }));
    result=wait_result(loader);
    CHECK(bool(result)); if(result) CHECK(result->high[0]==7);
}
TEST(menu_car_loaders_join_before_disc_resources_can_be_released) {
    MenuCarLoader a,b;
    auto token=std::make_shared<int>(7);
    std::weak_ptr<int> lifetime=token;
    std::atomic<int> completed=0;
    CHECK(a.start([held=token,&completed] { ++completed; return MenuCarLoad{}; }));
    CHECK(b.start([held=token,&completed] { ++completed; return MenuCarLoad{}; }));
    token.reset();
    finish_menu_car_loads();
    CHECK(completed==2); CHECK(!a.busy()); CHECK(!b.busy());
    CHECK(lifetime.expired());
}
TEST(menu_car_loader_propagates_failure_once_and_can_restart) {
    MenuCarLoader loader;
    CHECK(loader.start([]() -> MenuCarLoad { throw std::runtime_error("bad model"); }));
    bool caught=false;
    try { wait_result(loader); } catch(const std::runtime_error&) { caught=true; }
    CHECK(caught); CHECK(!loader.busy());
    CHECK(loader.start([] { return MenuCarLoad{}; }));
    CHECK(bool(wait_result(loader)));
}
