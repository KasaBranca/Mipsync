#include <stdio.h>
#include <stdlib.h>
#include "../templates/ps1/starter/runtime/projection_safety.h"

static void check(int condition, const char* message) {
    if (!condition) {
        fprintf(stderr, "%s\n", message);
        exit(1);
    }
}

int main(void) {
    const int16_t x[] = {-80, 400, -80, 400};
    const int16_t y[] = {-40, -40, 280, 280};
    check(ps1_polygon_fits_gpu(x, y, 4),
          "Native polygons may cross all four viewport edges");
    const int16_t boundary_x[] = {-510, 510, -510, 510};
    const int16_t boundary_y[] = {-254, -254, 254, 254};
    check(ps1_polygon_fits_gpu(boundary_x, boundary_y, 4),
          "Polygons at the safe extent limit must remain eligible");
    const int16_t wide[] = {-510, 511, -510, 511};
    const int16_t tall[] = {-254, -254, 255, 255};
    check(!ps1_polygon_fits_gpu(wide, y, 4), "Oversized width requires CPU clipping");
    check(!ps1_polygon_fits_gpu(x, tall, 4), "Oversized height requires CPU clipping");
    /* Cached visibility can certify both component triangles independently.
     * Their union still needs the native-quad extent test in the packet path. */
    const int16_t left_x[] = {-600, -100, 100};
    const int16_t left_y[] = {0, 100, -100};
    const int16_t right_x[] = {100, -100, 600};
    const int16_t right_y[] = {-100, 100, 0};
    const int16_t union_x[] = {-600, -100, 100, 600};
    const int16_t union_y[] = {0, 100, -100, 0};
    check(ps1_polygon_fits_gpu(left_x, left_y, 3) &&
          ps1_polygon_fits_gpu(right_x, right_y, 3),
          "Both original triangles fit the native GPU limits");
    check(!ps1_polygon_fits_gpu(union_x, union_y, 4),
          "A certified pair can still require two triangles instead of a quad");
    const int16_t saturated[] = {-1024, 100, -1024, 100};
    check(!ps1_polygon_fits_gpu(saturated, y, 4),
          "Saturated coordinates require CPU clipping");
    check(!ps1_polygon_fits_gpu(x, y, 0), "Empty polygons are ineligible");
    /* Depth/GTE saturation must retain CPU clipping independently of XY. */
    check(ps1_projection_vertex_needs_clip(0, 1, 2, 5000, 160),
          "The near plane must still clip");
    check(ps1_projection_vertex_needs_clip(0, 6000, 2, 5000, 160),
          "The far plane must still clip");
    check(ps1_projection_vertex_needs_clip(0, 50, 2, 5000, 160),
          "GTE division saturation must still clip");
    check(!ps1_projection_vertex_needs_clip(0, 100, 2, 5000, 160),
          "Valid depth must remain eligible");
    const int32_t inside = (120u << 16) | 160u;
    check(ps1_projection_can_defer_camera(0, inside, 640, 8, 6400, 160, 320, 240),
          "An unsaturated interior point may defer camera-space work");
    for (unsigned bit = 0; bit < 32; ++bit)
        check(!ps1_projection_can_defer_camera(1u << bit, inside, 640, 8, 6400, 160, 320, 240),
              "Any GTE flag must conservatively retain scalar camera coordinates");
    check(!ps1_projection_can_defer_camera(0, inside, 80, 8, 6400, 160, 320, 240),
          "The H/Z saturation boundary cannot defer camera-space work");
    check(!ps1_projection_can_defer_camera(0, inside, 6401, 8, 6400, 160, 320, 240),
          "Beyond-far points retain signed camera-space clipping");
    check(!ps1_projection_can_defer_camera(0, inside, 65535, 8, 70000, 160, 320, 240),
          "A saturated depth FIFO cannot certify exact camera depth");
    check(!ps1_projection_can_defer_camera(0, (120u << 16) | 1u, 640, 8, 6400, 160, 320, 240),
          "Viewport borders keep their conservative outcodes");
    check(!ps1_projection_can_defer_camera(0, (120u << 16) | 318u, 640, 8, 6400, 160, 320, 240),
          "The right margin also keeps conservative outcodes");
    check(!ps1_projection_can_defer_camera(0, (238u << 16) | 160u, 640, 8, 6400, 160, 320, 240),
          "The bottom margin also keeps conservative outcodes");
    check(!ps1_projection_can_defer_camera(0, inside, 640, 8, 6400, 160, INT32_MIN, 240),
          "Invalid viewport dimensions must not overflow boundary arithmetic");
    check(!ps1_projection_can_defer_camera(0, inside, 640, 8, 6400, 0, 320, 240),
          "A non-perspective focal length cannot certify camera outcodes");
    check(!ps1_projection_can_defer_camera(0, (120u << 16) | 1020u, 640, 8, 6400, 160, 1024, 240),
          "Wide viewports must still reserve signed GPU coordinate headroom");
    check(!ps1_projection_can_defer_camera(0, (1020u << 16) | 160u, 640, 8, 6400, 160, 320, 1024),
          "Tall viewports must still reserve signed GPU coordinate headroom");
    check(!ps1_projection_can_defer_camera(0, inside, 40000, 8, 64000, 65536, 320, 240),
          "Focal lengths must fit the GTE's unsigned 16-bit H register");
    return 0;
}
