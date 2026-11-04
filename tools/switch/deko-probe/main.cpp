#include "graphics.h"
#include <switch.h>
#include <cstdio>

using namespace gt3r::deko_probe;

int main() {
    open_log();
    trace("Deko3D offscreen composition probe (A toggles transparent overlay)");
    const Result mounted = romfsInit();
    if (R_FAILED(mounted)) {
        trace("romfsInit failed");
        close_log();
        return 1;
    }
    int status = 0;
    try {
        Graphics graphics;
        graphics.init();
        padConfigureInput(1, HidNpadStyleSet_NpadStandard);
        PadState pad;
        padInitializeDefault(&pad);
        bool first = true;
        while (appletMainLoop()) {
            padUpdate(&pad);
            if (padGetButtonsDown(&pad) & HidNpadButton_Plus) break;
            if (padGetButtonsDown(&pad) & HidNpadButton_A) {
                trace(graphics.toggle_overlay() ? "overlay on" : "overlay off");
            }
            graphics.present();
            if (first) { trace("first frame submitted and presented"); first = false; }
        }
        trace("leaving render loop");
    } catch (const char* error) {
        trace(error);
        FILE* log = std::fopen("sdmc:/switch/gt3r-deko-probe-error.log", "w");
        if (log) { std::fprintf(log, "%s\n", error); std::fclose(log); }
        status = 1;
    }
    romfsExit();
    trace("shutdown complete");
    close_log();
    return status;
}
