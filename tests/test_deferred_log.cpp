#include "base/deferred_log.h"
#include "base/log.h"
#include "test_framework.h"
#include <SDL3/SDL_log.h>
#include <string>
#include <vector>

namespace {
struct Capture {
    SDL_LogOutputFunction previous = nullptr;
    void* userdata = nullptr;
    std::vector<std::string> messages;
    Capture() {
        gt3r::log_defer_diagnostics(false);
        SDL_GetLogOutputFunction(&previous, &userdata);
        SDL_SetLogOutputFunction([](void* data, int, SDL_LogPriority, const char* message) {
            static_cast<Capture*>(data)->messages.emplace_back(message);
        }, this);
    }
    ~Capture() {
        gt3r::log_defer_diagnostics(false);
        SDL_SetLogOutputFunction(previous, userdata);
    }
};
}

TEST(log_deferred_diagnostics_flush_in_order) {
    Capture capture;
    gt3r::log_defer_diagnostics(true);
    LOG_INFO("SWPERF: first");
    LOG_INFO("SWDETAIL: second");
    LOG_INFO("Race perf [test]: third");
    CHECK(capture.messages.empty());
    LOG_INFO("ordinary message");
    LOG_WARN("SWDETAIL: warning remains immediate");
    CHECK(capture.messages.size() == 2);
    gt3r::log_flush_diagnostics();
    CHECK(capture.messages.size() == 3);
    if (capture.messages.size() != 3) return;
    const auto& block = capture.messages.back();
    const auto first = block.find("SWPERF: first");
    const auto second = block.find("SWDETAIL: second");
    const auto third = block.find("Race perf [test]: third");
    CHECK(first != std::string::npos && second != std::string::npos && third != std::string::npos);
    CHECK(first < second && second < third);
    LOG_INFO("SWPERF: fourth");
    CHECK(capture.messages.size() == 3);
    gt3r::log_defer_diagnostics(false);
    CHECK(capture.messages.back().find("SWPERF: fourth") != std::string::npos);
    LOG_INFO("SWPERF: fifth");
    CHECK(capture.messages.back() == "SWPERF: fifth");
}

TEST(log_deferred_diagnostics_memory_is_bounded) {
    Capture capture;
    gt3r::log_defer_diagnostics(true);
    const std::string padding(1800, 'x');
    for (int i = 0; i < 800; ++i) LOG_INFO("SWDETAIL: %d %s", i, padding.c_str());
    CHECK(capture.messages.empty());
    gt3r::log_flush_diagnostics();
    CHECK(capture.messages.size() == 1);
    CHECK(capture.messages.front().find("0 older messages discarded") == std::string::npos);
    CHECK(capture.messages.back().find("SWDETAIL: 799 ") != std::string::npos);
}
