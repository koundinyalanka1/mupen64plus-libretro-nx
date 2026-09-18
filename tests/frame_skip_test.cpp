// Release-mode policy tests using production configuration and buffer types.
// GPU allocation and FBInfo callbacks are mocked; the skip policy is not.
#include <FrameSkip.h>
#include <Graphics/ColorBufferReader.h>
#include <Graphics/Context.h>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <thread>

#define REQUIRE(expr) do { if (!(expr)) { \
    std::fprintf(stderr, "line %d: %s\n", __LINE__, #expr); std::abort(); \
} } while (0)

Config config{};
RSPInfo RSP{};
static bool auxiliary;
FrameBuffer::FrameBuffer() = default;
FrameBuffer::~FrameBuffer() = default;
bool FrameBuffer::isAuxiliary() const { return auxiliary; }
FrameBufferList& FrameBufferList::get() { static FrameBufferList list; return list; }
namespace FBInfo {
FBInfo fbInfo;
FBInfo::FBInfo() : m_supported(false) {}
void FBInfo::reset() { m_supported = false; }
void FBInfo::GetInfo(void*) { m_supported = true; }
}

int main()
{
    FrameBuffer buffer;
    frameBufferList().setCurrent(&buffer);
    config.frameBufferEmulation.enable = 1;
    config.frameBufferEmulation.copyToRDRAM = Config::ctDoubleBuffer;
    config.frameBufferEmulation.copyDepthToRDRAM = Config::cdSoftwareRender;

    for (unsigned mode = LIBRETRO_SKIP_PRESENTATION; mode <= LIBRETRO_SKIP_RENDERING; ++mode) {
        libretro_set_frame_skip(false, mode);
        REQUIRE(!FrameSkip::skipPresentation());
        REQUIRE(!FrameSkip::skipColorReadback(false));
        REQUIRE(!FrameSkip::skipDraw());
        libretro_set_frame_skip(true, mode);
        REQUIRE(FrameSkip::skipPresentation());
        REQUIRE(FrameSkip::skipColorReadback(false) == (mode != LIBRETRO_SKIP_PRESENTATION));
        REQUIRE(!FrameSkip::skipColorReadback(true));
        REQUIRE(FrameSkip::skipDraw() == (mode == LIBRETRO_SKIP_RENDERING));
    }

    // Drawing continues for explicit framebuffer consumers in Rendering mode.
    config.frameBufferEmulation.copyToRDRAM = Config::ctSync;
    REQUIRE(!FrameSkip::skipDraw());
    config.frameBufferEmulation.copyToRDRAM = Config::ctDoubleBuffer;
    config.frameBufferEmulation.copyAuxToRDRAM = 1;
    REQUIRE(!FrameSkip::skipDraw());
    config.frameBufferEmulation.copyAuxToRDRAM = 0;
    config.frameBufferEmulation.copyDepthToRDRAM = Config::cdCopyFromVRam;
    REQUIRE(!FrameSkip::skipDraw());
    config.frameBufferEmulation.copyDepthToRDRAM = Config::cdSoftwareRender;
    RSP.LLE = true;
    REQUIRE(!FrameSkip::skipDraw());
    RSP.LLE = false;
    FBInfo::fbInfo.GetInfo(nullptr);
    REQUIRE(!FrameSkip::skipDraw());
    FBInfo::fbInfo.reset();
    auxiliary = true;
    REQUIRE(!FrameSkip::skipDraw());
    auxiliary = false;
    buffer.m_isDepthBuffer = true;
    REQUIRE(!FrameSkip::skipDraw());
    buffer.m_isDepthBuffer = false;
    buffer.m_cfb = true;
    REQUIRE(!FrameSkip::skipDraw());
    buffer.m_cfb = false;
    frameBufferList().setCurrent(nullptr);
    REQUIRE(!FrameSkip::skipDraw());
    frameBufferList().setCurrent(&buffer);

    const unsigned hacks[] = {hack_Ogre64, hack_blurPauseScreen,
        hack_paper_mario_subscreen, hack_subscreen, hack_Snap,
        hack_rectDepthBufferCopyPD, hack_rectDepthBufferCopyCBFD};
    for (unsigned hack : hacks) {
        config.generalEmulation.hacks = hack;
        REQUIRE(!FrameSkip::skipDraw());
        REQUIRE(!FrameSkip::skipColorReadback(false));
    }
    config.generalEmulation.hacks = 0;
    REQUIRE(FrameSkip::skipDraw());

    // Copies and shader helper rectangles are ineligible unless explicitly marked.
    graphics::Context::DrawRectParameters copy;
    REQUIRE(!copy.allowFrameSkip);

    // Visible frames resume immediately, including after several consecutive skips.
    for (unsigned i = 0; i < 10; ++i) {
        libretro_set_frame_skip(true, LIBRETRO_SKIP_RENDERING);
        REQUIRE(FrameSkip::skipDraw());
    }
    libretro_set_frame_skip(false, LIBRETRO_SKIP_RENDERING);
    REQUIRE(!FrameSkip::skipDraw());
    REQUIRE(!FrameSkip::skipColorReadback(false));

    // A frame carries the decision it was produced under. The threaded
    // renderer presents frames the producer emitted up to MAX_SWAP frames
    // earlier, so presentation must not consult the live flags: doing so
    // sends a frame whose work was skipped to the frontend as a real frame.
    libretro_set_frame_skip(true, LIBRETRO_SKIP_RENDERING);
    const unsigned producedFlags = libretro_get_frame_skip_flags();
    libretro_set_presented_frame_skip(producedFlags);   // producer enqueues swap
    libretro_set_frame_skip(false, LIBRETRO_SKIP_RENDERING); // later retro_run
    REQUIRE(libretro_get_frame_skip_flags() == 0);
    REQUIRE((libretro_get_presented_frame_skip() & LIBRETRO_SKIP_VIDEO) != 0);

    // ... and the converse: a fully rendered frame must still be presented
    // even if the frontend has since asked for skipping.
    libretro_set_presented_frame_skip(0);
    libretro_set_frame_skip(true, LIBRETRO_SKIP_RENDERING);
    REQUIRE((libretro_get_presented_frame_skip() & LIBRETRO_SKIP_VIDEO) == 0);
    libretro_set_frame_skip(false, LIBRETRO_SKIP_READBACK);
    libretro_set_presented_frame_skip(0);

    std::atomic<bool> done{false};
    std::thread producer([&]() {
        for (unsigned i = 0; i < 100000; ++i)
            libretro_set_frame_skip((i & 1) != 0, i % 3);
        done.store(true);
    });
    do {
        const unsigned flags = libretro_get_frame_skip_flags();
        REQUIRE(flags == 0 || flags == 1 || flags == 3 || flags == 7);
    } while (!done.load());
    producer.join();
    libretro_set_frame_skip(false, LIBRETRO_SKIP_READBACK);
    REQUIRE(libretro_get_frame_skip_flags() == 0);
}
