// Entry point using SDL3's callback model, which maps onto every platform's
// native application lifecycle (including Android and iOS).

#define SDL_MAIN_USE_CALLBACKS 1
#include "app/app.h"

#include <SDL3/SDL_main.h>

#if defined(__SWITCH__)
#include <switch.h>
#include <sys/stat.h>
#include <cstdio>
#include <unistd.h>
#endif

using gt3r::App;

#if defined(__SWITCH__)
static void switch_log_callback(void* userdata, int category, SDL_LogPriority priority, const char* message) {
    (void)userdata;
    printf("[SDL %d:%d] %s\n", category, priority, message);
    fflush(stdout);
}
#endif

SDL_AppResult SDL_AppInit(void** state, int argc, char** argv) {
#if defined(__SWITCH__)
    mkdir("sdmc:/switch", 0777);
    mkdir("sdmc:/switch/gt3r", 0777);
    FILE* logf = freopen("sdmc:/switch/gt3r/gt3r.log", "w", stdout);
    if (!logf) {
        freopen("gt3r.log", "w", stdout);
    }
    dup2(fileno(stdout), STDERR_FILENO);
    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);

    printf("=== GT3-Revisited starting on Nintendo Switch ===\n");
    fflush(stdout);

    SDL_SetLogOutputFunction(switch_log_callback, nullptr);
#endif

    try {
        printf("Instantiating App...\n");
        fflush(stdout);
        auto* app = new App();
        *state = app;
        printf("Calling app->init()...\n");
        fflush(stdout);
        bool ok = app->init(argc, argv);
        printf("app->init() returned %s\n", ok ? "TRUE" : "FALSE");
        fflush(stdout);
        if (!ok) {
            printf("SDL Error: %s\n", SDL_GetError());
            fflush(stdout);
            return SDL_APP_FAILURE;
        }
        return SDL_APP_CONTINUE;
    } catch (const std::exception& e) {
        printf("FATAL EXCEPTION in SDL_AppInit: %s\n", e.what());
        fflush(stdout);
        return SDL_APP_FAILURE;
    } catch (...) {
        printf("FATAL UNKNOWN EXCEPTION in SDL_AppInit!\n");
        fflush(stdout);
        return SDL_APP_FAILURE;
    }
}

SDL_AppResult SDL_AppIterate(void* state) {
    try {
        return static_cast<App*>(state)->iterate();
    } catch (const std::exception& e) {
        printf("FATAL EXCEPTION in SDL_AppIterate: %s\n", e.what());
        fflush(stdout);
        return SDL_APP_FAILURE;
    } catch (...) {
        printf("FATAL UNKNOWN EXCEPTION in SDL_AppIterate!\n");
        fflush(stdout);
        return SDL_APP_FAILURE;
    }
}

SDL_AppResult SDL_AppEvent(void* state, SDL_Event* event) {
    try {
        return static_cast<App*>(state)->event(*event);
    } catch (const std::exception& e) {
        printf("FATAL EXCEPTION in SDL_AppEvent: %s\n", e.what());
        fflush(stdout);
        return SDL_APP_FAILURE;
    } catch (...) {
        printf("FATAL UNKNOWN EXCEPTION in SDL_AppEvent!\n");
        fflush(stdout);
        return SDL_APP_FAILURE;
    }
}

void SDL_AppQuit(void* state, SDL_AppResult) {
    if (auto* app = static_cast<App*>(state)) {
        app->shutdown();
        delete app;
    }
}
