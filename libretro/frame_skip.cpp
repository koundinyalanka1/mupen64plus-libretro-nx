#include "frame_skip.h"
#include <atomic>

static std::atomic<unsigned> frame_skip_flags{0};

void libretro_set_frame_skip(bool skip, unsigned mode)
{
   unsigned flags = 0;
   if (skip) {
      flags = LIBRETRO_SKIP_VIDEO;
      if (mode == LIBRETRO_SKIP_READBACK || mode == LIBRETRO_SKIP_RENDERING)
         flags |= LIBRETRO_SKIP_ASYNC_READBACK;
      if (mode == LIBRETRO_SKIP_RENDERING)
         flags |= LIBRETRO_SKIP_DRAW;
   }
   frame_skip_flags.store(flags, std::memory_order_relaxed);
}

unsigned libretro_get_frame_skip_flags(void)
{
   return frame_skip_flags.load(std::memory_order_relaxed);
}

/* Written by the consumer as it executes a swap, read by presentation in the
 * same retro_run, so relaxed ordering on a single word is sufficient. */
static std::atomic<unsigned> presented_frame_skip_flags{0};

void libretro_set_presented_frame_skip(unsigned flags)
{
   presented_frame_skip_flags.store(flags, std::memory_order_relaxed);
}

unsigned libretro_get_presented_frame_skip(void)
{
   return presented_frame_skip_flags.load(std::memory_order_relaxed);
}
