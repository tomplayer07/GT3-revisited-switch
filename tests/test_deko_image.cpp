#include "test_framework.h"
#include "render/deko/borrowed_image.h"
using gt3r::render::deko::BorrowedImageLease;
namespace {
struct Owner { int retained=0, released=0; };
GT3R_DekoImage image(Owner& owner) {
    return {GT3R_DEKO_IMAGE_VERSION,&owner,&owner,320,180,
        [](void* p) { ++static_cast<Owner*>(p)->retained; },
        [](void* p) { ++static_cast<Owner*>(p)->released; },
        [](void*,unsigned slot,Uint32* descriptor) { *descriptor=slot+3; return true; }};
}
}
TEST(deko_image_survives_texture_and_queued_command_destruction) {
    Owner owner;
    auto contract=image(owner);
    auto texture=BorrowedImageLease::acquire(contract,&owner,320,180);
    CHECK(bool(texture)); CHECK(owner.retained==1);
    auto queued=texture;
    auto frame=queued;
    contract.release=nullptr; // SDL property storage need not outlive creation.
    texture.reset(); queued.reset();
    CHECK(owner.released==0);
    Uint32 descriptor=0;
    CHECK(frame->source.descriptor(frame->source.userdata,1,&descriptor));
    CHECK(descriptor==4);
    frame.reset(); CHECK(owner.released==1);
}
TEST(deko_image_rejects_wrong_owner_dimensions_and_incomplete_contract) {
    Owner owner, other;
    auto contract=image(owner);
    CHECK(!BorrowedImageLease::acquire(contract,&other,320,180));
    CHECK(!BorrowedImageLease::acquire(contract,&owner,1280,720));
    contract.version=0;
    CHECK(!BorrowedImageLease::acquire(contract,&owner,320,180));
    contract=image(owner); contract.release=nullptr;
    CHECK(!BorrowedImageLease::acquire(contract,&owner,320,180));
    CHECK(owner.retained==0); CHECK(owner.released==0);
}
