#include "base/log.h"
#include "base/deferred_log.h"

#include <SDL3/SDL_log.h>

#include <cstdio>
#include <cstring>
#include <deque>
#include <mutex>
#include <string>

namespace gt3r {

static LogLevel g_level = LogLevel::Info;

namespace {
struct Diagnostic { SDL_LogPriority priority; std::string text; };
std::mutex diagnostic_mutex;
std::deque<Diagnostic> diagnostics;
bool defer_diagnostics = false;
size_t diagnostic_bytes = 0, dropped_diagnostics = 0;
constexpr size_t kDiagnosticBudget = 1024 * 1024;
}

void log_flush_diagnostics() {
    std::deque<Diagnostic> pending;
    size_t dropped = 0;
    {
        std::lock_guard lock(diagnostic_mutex);
        pending.swap(diagnostics);
        diagnostic_bytes = 0;
        dropped = dropped_diagnostics;
        dropped_diagnostics = 0;
    }
    if (pending.empty() && !dropped) return;
    char header[160];
    std::snprintf(header, sizeof(header), "Deferred render diagnostics: %zu messages, %zu older messages discarded\n",
                  pending.size(), dropped);
    std::string block = header;
    for (const auto& entry : pending) {
        block += entry.text;
        block += '\n';
    }
    // One callback/flush for the diagnostic block, rather than one SD flush per line.
    SDL_LogMessage(SDL_LOG_CATEGORY_APPLICATION, SDL_LOG_PRIORITY_INFO, "%s", block.c_str());
}

void log_defer_diagnostics(bool enabled) {
    {
        std::lock_guard lock(diagnostic_mutex);
        defer_diagnostics = enabled;
    }
    if (!enabled) log_flush_diagnostics();
}

void log_set_level(LogLevel level) { g_level = level; }

void log_message(LogLevel level, const char* fmt, ...) {
    if (level < g_level) return;
    char buf[2048];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    // SDL_Log routes to logcat on Android and stderr/console elsewhere.
    SDL_LogPriority prio = SDL_LOG_PRIORITY_INFO;
    switch (level) {
    case LogLevel::Debug: prio = SDL_LOG_PRIORITY_DEBUG; break;
    case LogLevel::Info: prio = SDL_LOG_PRIORITY_INFO; break;
    case LogLevel::Warn: prio = SDL_LOG_PRIORITY_WARN; break;
    case LogLevel::Error: prio = SDL_LOG_PRIORITY_ERROR; break;
    }
    const bool diagnostic = std::strncmp(buf, "SWPERF:", 7) == 0 || std::strncmp(buf, "SWDETAIL:", 9) == 0 ||
                            std::strncmp(buf, "SWCULL:", 7) == 0 || std::strncmp(buf, "Race perf [", 11) == 0;
    if (diagnostic && level < LogLevel::Warn) {
        std::lock_guard lock(diagnostic_mutex);
        if (defer_diagnostics) {
            const size_t bytes = std::strlen(buf) + 1;
            while (!diagnostics.empty() && diagnostic_bytes + bytes > kDiagnosticBudget) {
                diagnostic_bytes -= diagnostics.front().text.size() + 1;
                diagnostics.pop_front();
                ++dropped_diagnostics;
            }
            diagnostics.push_back({prio, buf});
            diagnostic_bytes += bytes;
            return;
        }
    }
    SDL_LogMessage(SDL_LOG_CATEGORY_APPLICATION, prio, "%s", buf);
}

} // namespace gt3r
