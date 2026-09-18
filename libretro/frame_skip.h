#ifndef LIBRETRO_FRAME_SKIP_H
#define LIBRETRO_FRAME_SKIP_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

enum libretro_frame_skip_mode {
   LIBRETRO_SKIP_PRESENTATION,
   LIBRETRO_SKIP_READBACK,
   LIBRETRO_SKIP_RENDERING
};

enum libretro_frame_skip_flags {
   LIBRETRO_SKIP_VIDEO = 1,
   LIBRETRO_SKIP_ASYNC_READBACK = 2,
   LIBRETRO_SKIP_DRAW = 4
};

/* Publish one coherent decision to the emulation/renderer producer thread.
 * Test it before enqueueing GL work, never when consuming an older GL command. */
void libretro_set_frame_skip(bool skip, unsigned mode);
unsigned libretro_get_frame_skip_flags(void);

/* The threaded renderer lets the producer run ahead of presentation, so the
 * live flags above describe a frame that has not been presented yet. The swap
 * command therefore carries the flags that were in effect when its frame was
 * produced, and publishes them here as it is consumed. Presentation must use
 * these, never the live flags, or a frame whose work was skipped can be sent
 * to the frontend as a real frame. */
void libretro_set_presented_frame_skip(unsigned flags);
unsigned libretro_get_presented_frame_skip(void);

#ifdef __cplusplus
}
#endif

#endif
