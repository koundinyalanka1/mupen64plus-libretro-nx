#pragma once

#ifdef __LIBRETRO__
#include <frame_skip.h>
#include "Config.h"
#include "FrameBuffer.h"
#include "FrameBufferInfo.h"
#include "RSP.h"
#endif

namespace FrameSkip {

inline bool skipPresentation()
{
#ifdef __LIBRETRO__
	return (libretro_get_frame_skip_flags() & LIBRETRO_SKIP_VIDEO) != 0;
#else
	return false;
#endif
}

#ifdef __LIBRETRO__
inline bool requiresFramebufferEffects()
{
	// These existing compatibility paths consume framebuffer contents, even
	// when the frontend does not display the current frame.
	const u32 framebufferHacks = hack_Ogre64 | hack_blurPauseScreen |
		hack_paper_mario_subscreen | hack_subscreen | hack_Snap |
		hack_rectDepthBufferCopyPD | hack_rectDepthBufferCopyCBFD;
	return (config.generalEmulation.hacks & framebufferHacks) != 0;
}
#endif

inline bool skipColorReadback(bool sync)
{
#ifdef __LIBRETRO__
	return !sync &&
		(libretro_get_frame_skip_flags() & LIBRETRO_SKIP_ASYNC_READBACK) != 0 &&
		!requiresFramebufferEffects();
#else
	return false;
#endif
}

inline bool skipDraw()
{
#ifdef __LIBRETRO__
	if ((libretro_get_frame_skip_flags() & LIBRETRO_SKIP_DRAW) == 0)
		return false;
	if (config.frameBufferEmulation.copyToRDRAM == Config::ctSync ||
		config.frameBufferEmulation.copyAuxToRDRAM != 0 ||
		config.frameBufferEmulation.copyDepthToRDRAM == Config::cdCopyFromVRam ||
		(RSP.LLE && config.frameBufferEmulation.copyDepthToRDRAM != Config::cdDisable) ||
		FBInfo::fbInfo.isSupported() || requiresFramebufferEffects())
		return false;
	const FrameBuffer* buffer = frameBufferList().getCurrent();
	return config.frameBufferEmulation.enable == 0 ||
		(buffer != nullptr && !buffer->isAuxiliary() && !buffer->m_isDepthBuffer && !buffer->m_cfb);
#else
	return false;
#endif
}

}
