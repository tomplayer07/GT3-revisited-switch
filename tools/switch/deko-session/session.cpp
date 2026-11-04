#include "session.h"
#include "disc/core_image.h"
#include "game/data/car_database.h"
#include "race/exact_car.h"
#include "race/exact_setup.h"
#include "race/road_shade.h"
#include "sim/exact/race.h"
#include <map>
#include <cmath>
#include <stdexcept>
namespace gt3r::deko_probe {
namespace ex=sim::exact;
struct Session::Impl {
    std::map<std::string,Bytes> files;
    data::CarDatabase database;
    std::unique_ptr<ex::ProgramData> program;
    std::shared_ptr<const ex::RaceCourse> course;
    std::unique_ptr<ex::ExactRace> race;
    Bytes courseBytes, carBytes, flareBytes;
    std::vector<sim::VehicleState> states;
    std::vector<sim::VehicleSetup> setups;
    uint64_t ticks=0;
    std::vector<int> lampHints;
    std::vector<std::array<uint8_t,4>> brakeHistories;
    explicit Impl(std::span<const uint8_t> bytes,unsigned carCount) {
        if (!carCount || carCount>6) throw FormatError("Invalid prototype field size");
        size_t offset=0;
        auto word=[&]() { const auto value=rd32(bytes,offset); offset+=4; return value; };
        if (bytes.size()>128*1024*1024 || word()!=0x53524447 || word()!=1) throw FormatError("Invalid session bundle");
        const auto count=word(); if (!count || count>64) throw FormatError("Invalid session file count");
        for (unsigned i=0;i<count;++i) {
            const auto nameSize=word(), size=word();
            if (!nameSize || nameSize>1024 || offset>bytes.size() || nameSize>bytes.size()-offset) throw FormatError("Invalid session file name");
            std::string name(reinterpret_cast<const char*>(bytes.data()+offset),nameSize); offset+=nameSize;
            if (offset>bytes.size() || size>bytes.size()-offset || files.contains(name)) throw FormatError("Invalid session file size/name");
            files.emplace(std::move(name),Bytes(bytes.begin()+offset,bytes.begin()+offset+size)); offset+=size;
        }
        if (offset!=bytes.size()) throw FormatError("Trailing session bytes");
        const auto& suffix=files.at("probe/db_suffix");
        if (!database.load([&](const std::string& path)->std::optional<Bytes> {
            auto it=files.find(path); if (it==files.end()) return std::nullopt; return it->second;
        },std::string(suffix.begin(),suffix.end()))) throw FormatError(database.error());
        program=std::make_unique<ex::ProgramData>(ex::ProgramData::load(CoreImage::decode(files.at("probe/program"))));
        if (!program->issues().empty()) throw FormatError("Session program tables not fully recognized");
        course=ex::RaceCourse::load(files.at("probe/course"),*program);
        const auto equipment=race::exact_equipment(database,{"_787b_91"});
        if (!equipment) throw FormatError("Mazda 787B physics equipment unavailable");
        ex::RaceConfig rules{};
        ex::RaceSetup options;
        options.data=program.get(); options.db=&database; options.course=course;
        options.kind=ex::Conductor::SinglePlayer; options.rules=&rules; options.laps=0;
        options.course_category=race::exact_course_category(database,"route5_dry");
        options.start_type=-1; options.countdown=0;
        if (course->road.grid.size()<carCount) throw FormatError("Not enough starting grid slots");
        ex::StartEntry entry; entry.eq=*equipment; entry.human=1; entry.player=true; entry.grid_slot=int(carCount)-1; entry.best_lap=ex::kNoTime;
        options.entries.push_back(entry);
        for (unsigned i=1;i<carCount;++i) {
            ex::StartEntry opponent; opponent.eq=*equipment; opponent.grid_slot=int(i)-1; opponent.best_lap=0x157529FF;
            options.entries.push_back(opponent);
        }
        race=std::make_unique<ex::ExactRace>(options);
        race->set_start_count(0);
        states.resize(carCount); setups.resize(carCount); lampHints.resize(carCount); brakeHistories.resize(carCount);
        for (unsigned i=0;i<carCount;++i) {
            setups[i]=gt3r::race::exact_setup(race->cars()[i]);
            gt3r::race::exact_state(race->cars()[i],setups[i],states[i]);
        }
        // All source data used by the initialized session is owned by the engine objects.
        courseBytes=std::move(files.at("probe/course"));
        carBytes=std::move(files.at("probe/car")); flareBytes=std::move(files.at("probe/flare"));
        files.clear();
    }
    void tick(const sim::ControlInputs& input) {
        ex::PadFrame pad; pad.flags=0x1f; pad.kind=2;
        auto axis=[](float x) { return uint16_t(std::lround(std::clamp(x,0.0f,1.0f)*4096)); };
        pad.steer=int16_t(std::lround(std::clamp(-input.steer,-1.0f,1.0f)*4096));
        pad.throttle=axis(input.throttle); pad.brake=axis(input.brake); pad.third=input.handbrake ? 4096 : 0;
        pad.shift=input.shift_up ? 1 : input.shift_down ? -1 : 0;
        race->step(std::span<const ex::PadFrame>(&pad,1)); ++ticks;
        for (size_t i=0;i<states.size();++i) {
        auto& brakeHistory=brakeHistories[i];
        brakeHistory[0]=brakeHistory[1]; brakeHistory[1]=brakeHistory[2]; brakeHistory[2]=brakeHistory[3];
        brakeHistory[3]=race->cars()[i].brake_pedal.host()>0;
        auto& state=states[i];
        gt3r::race::exact_state(race->cars()[i],setups[i],state);
        const auto& wheel=state.wheels[0];
        const auto shade=gt3r::race::road_shade(course->road,wheel.contact.x,wheel.contact.z,wheel.contact.y+1);
        if (shade.hit) lampHints[i]=int(int16_t(shade.raw_a));
        if (!std::isfinite(state.position.x)||!std::isfinite(state.position.y)||!std::isfinite(state.position.z)||!std::isfinite(state.speed_kmh)) throw FormatError("Nonfinite physics state");
        }
    }
};
Session::Session(std::span<const uint8_t> b,unsigned n):impl_(std::make_unique<Impl>(b,n)){}
Session::~Session()=default;
void Session::tick(const sim::ControlInputs& i){impl_->tick(i);}
unsigned Session::car_count() const{return unsigned(impl_->states.size());}
const sim::VehicleState& Session::state(unsigned i) const{return impl_->states.at(i);}
const sim::VehicleSetup& Session::setup(unsigned i) const{return impl_->setups.at(i);}
std::span<const uint8_t> Session::car_bytes() const{return impl_->carBytes;}
std::span<const uint8_t> Session::flare_bytes() const{return impl_->flareBytes;}
std::span<const uint8_t> Session::course_bytes() const{return impl_->courseBytes;}
bool Session::brake_now(unsigned i) const{return impl_->brakeHistories.at(i)[3]!=0;}
float Session::brake_factor(unsigned i) const { const auto& h=impl_->brakeHistories.at(i); return float(h[0]+h[1]+h[2]+h[3])/4; }
int Session::lamp_hint(unsigned i) const{return impl_->lampHints.at(i);}
int Session::lap_counter() const{return impl_->race->cars()[0].laps;}
bool Session::finished() const{return impl_->race->cars()[0].finished!=0;}
uint64_t Session::ticks() const{return impl_->ticks;}
}
