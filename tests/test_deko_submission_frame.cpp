#include "test_framework.h"
#include "render/deko/submission_frame.h"
#include <stdexcept>
#include <vector>

using gt3r::render::deko::SubmissionFrame;

TEST(deko_ui_batches_keep_one_swapchain_image_until_present) {
    SubmissionFrame<2> frame;
    unsigned acquisitions=0, submissions=0;
    bool image_owned=false;
    std::vector<int> images;
    std::vector<unsigned> slots;
    auto acquire=[&] {
        CHECK(!image_owned);
        image_owned=true;
        return int(++acquisitions % 2);
    };
    // More UI batches than either the image pool or the command-memory ring:
    // draining a showroom may submit repeatedly without presenting the frame.
    for (unsigned visible_frame=0;visible_frame<3;++visible_frame) {
        const int expected=int((visible_frame+1)%2);
        for (unsigned batch=0;batch<7;++batch) {
            images.push_back(frame.acquire(acquire));
            slots.push_back(frame.next_submission());
            CHECK(images.back()==expected);
            CHECK(slots.back()==submissions++%2);
        }
        CHECK(acquisitions==visible_frame+1);
        image_owned=false; // the GPU presents the acquired image
        frame.presented();
    }
    CHECK(acquisitions==3);
    CHECK(images.front()==1); CHECK(slots.front()==0); // independent indices
    CHECK(slots[7]==1); // presenting does not restart the command-memory ring
}

TEST(deko_ui_frame_retries_acquisition_after_failure) {
    SubmissionFrame<2> frame;
    bool caught=false;
    try {
        frame.acquire([]() -> int { throw std::runtime_error("acquire failed"); });
    } catch (const std::runtime_error&) { caught=true; }
    CHECK(caught);
    CHECK(frame.acquire([] { return 1; })==1);
    bool reacquired=false;
    CHECK(frame.acquire([&] { reacquired=true; return 0; })==1);
    CHECK(!reacquired);
    frame.presented();
    CHECK(frame.acquire([] { return 0; })==0);
}
