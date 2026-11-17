#ifndef MIPSYNC_RENDER_H
#define MIPSYNC_RENDER_H

#include <stdint.h>
#define PS1_RENDER_OT_LEN 2048

/* Opt-in application-owned packet reuse. No hook calls are emitted by default.
 * Replay returns a negative value for a miss, leaving packet/OT state untouched,
 * or a nonnegative primitive count after appending a complete cached fragment.
 * Capture observes the exact packet range emitted by a normal entity draw.
 * Applications own cache keys, source lifetimes, packet bounds and OT ordering.
 */
#ifndef PS1_RENDER_ENTITY_PACKET_HOOKS
#define PS1_RENDER_ENTITY_PACKET_HOOKS 0
#endif
#if PS1_RENDER_ENTITY_PACKET_HOOKS
struct ps1_entity;
int ps1_render_entity_packet_replay(uint32_t* ot, char** nextpri,
    const char* packet_end, const struct ps1_entity* entity, int viewmodel);
void ps1_render_entity_packet_capture(uint32_t* ot, const char* begin,
    const char* nextpri, const char* packet_end,
    const struct ps1_entity* entity, int viewmodel, int primitives);
#endif

void ps1_render_init(int screen_w, int screen_h);
/* nextpri points into the caller's packet buffer; packet_end must be the first byte after it. */
void ps1_render_frame(uint32_t* ot, char** nextpri, const char* packet_end);
/* Project a Q16.16 world point using the current render camera. Returns zero
 * before initialization, beyond the depth range or outside signed XY limits.
 * Points outside the viewport are retained for native GPU clipping. Does not
 * change GTE state. depth may be NULL; screen_x and screen_y are required. */
int ps1_render_project_world(int32_t x_q16, int32_t y_q16, int32_t z_q16,
    int16_t* screen_x, int16_t* screen_y, int32_t* depth);
/* Submit world, apply authored grading, then submit ungraded overlay UI. */
void ps1_render_submit(uint32_t* ot, int framebuffer_x, int framebuffer_y, int width, int height, const uint16_t* lut);

#endif /* MIPSYNC_RENDER_H */
