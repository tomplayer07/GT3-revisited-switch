// Developer viewer for the game's real PRM presentations; also useful when
// validating prize materials without completing a race or changing a save.
#include "app/app.h"
#include "app/tool_buttons.h"
#include "app/screen_registry.h"
#include "base/log.h"
#include "render/prize_view.h"
#include <algorithm>

namespace gt3r {
namespace {
class PrizeViewer final:public Screen {
    std::vector<std::string> paths_;
    std::unique_ptr<render::PrizeView> prize_;
    size_t selected_=0;
    bool initialized_=false,paused_=false,white_=false;
    std::string error_;
    void load(App& app) {
        prize_.reset(); error_.clear(); paused_=false;
        if(paths_.empty() || !app.gpu() || !app.vfs()) return;
        try {
            auto bytes=app.vfs()->read(paths_[selected_]);
            if(!bytes) { error_="File non disponibile"; return; }
            auto next=std::make_unique<render::PrizeView>(*app.gpu());
            if(!next->load(*bytes,paths_[selected_])) { error_="Impossibile caricare il premio"; return; }
            prize_=std::move(next);
            LOG_INFO("Prize viewer loaded: %s",paths_[selected_].c_str());
        } catch(const std::exception& e) { error_=e.what(); LOG_WARN("Prize viewer: %s",e.what()); }
    }
public:
    void update(App& app,double dt) override {
        if(!initialized_) {
            initialized_=true;
            if(app.vfs()) {
                auto append=[&](const std::string& path) {
                    if(app.vfs()->exists(path) && std::find(paths_.begin(),paths_.end(),path)==paths_.end()) paths_.push_back(path);
                };
                for(const char* path:{"race/gold.prm","race/silver.prm","race/bronze.prm","menu/lic_b.prm","menu/lic_a.prm","menu/lic_s.prm"}) append(path);
                for(const char* dir:{"race/","menu/"})
                    for(const auto& file:app.vfs()->list_dir(dir)) if(file.ends_with(".prm")) append(std::string(dir)+file);
            }
            load(app);
        }
        const auto& input=app.menu();
        if(input.pressed(PadButton::Start)) { app.quit(); return; }
        if(input.pressed(tool_back)) { app.pop(); return; }
        if(input.pressed(tool_confirm)) paused_=!paused_;
        if(input.pressed(PadButton::L1)) white_=!white_;
        const int step=input.horizontal();
        if(step && !paths_.empty()) {
            selected_=size_t((int(selected_)+int(paths_.size())+step)%int(paths_.size()));
            load(app);
        }
        if(prize_ && !paused_) prize_->tick(std::clamp(dt,0.0,0.1));
    }
    void render(App&,Ui& ui) override {
        ui.header("PREMI",paths_.empty() ? "Nessuna presentazione disponibile" : paths_[selected_]);
        const float bg=white_ ? 1.0f : 0.06f;
        const float clear[4]={bg,bg,bg,1};
        if(prize_) prize_->draw(ui.renderer(),{0,32,ui.width(),ui.height()-62},clear);
        if(!error_.empty()) ui.text(10,50,Ui::clip(error_,ui.width()-20),colors::kBad);
        ui.text(10,ui.height()-20,"Sinistra/Destra: premio   A: pausa   L: sfondo   +: esci",colors::kDim);
    }
};
GT3R_REGISTER_SCREEN(prize_viewer,"prizes","Prize presentations","Animated trophies and license cards from the disc.",91,[] { return std::make_unique<PrizeViewer>(); });
} // namespace
} // namespace gt3r
