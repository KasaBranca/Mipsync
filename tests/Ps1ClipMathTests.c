#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include "../templates/ps1/starter/runtime/clip_math.h"
#include "../templates/ps1/starter/runtime/projection_safety.h"
#include "../templates/ps1/starter/runtime/affine_subdivision.h"
#include "../templates/ps1/starter/runtime/watertight_projection.h"

static int32_t xy(int x, int y) {
    return (int32_t)((uint16_t)x | (uint32_t)(uint16_t)y << 16);
}
static void check(int condition) { if (!condition) exit(1); }

static void verify(int32_t a, int32_t b, int32_t divisor) {
    int64_t expected = (int64_t)a * b / divisor;
    if (ps1_clip_mul_div(a, b, divisor) != expected) {
        fprintf(stderr, "Clip interpolation changed signed truncation: %d, %d, %d\n", a, b, divisor);
        exit(1);
    }
}

static double perspective_u(double x, double y) {
    const double b = x / 320.0, c = y / 240.0, a = 1 - b - c;
    return (b * 172 / 1600) / (a / 100 + b / 1600 + c / 100);
}
static void verify_affine_subdivision(void) {
    const ps1_screen_clip_vert face[3] = {
        {0,0,100,0,0,255,255,255}, {320,0,1600,172,0,255,255,255},
        {0,240,100,0,255,255,255} };
    struct { ps1_screen_clip_vert fragments[PS1_AFFINE_FRAGMENTS][3]; unsigned guard; } result;
    result.guard = 0xabcdef01u;
    const int count = ps1_affine_subdivide(face, result.fragments);
    check(count > 1 && count <= PS1_AFFINE_FRAGMENTS && result.guard == 0xabcdef01u);
    double largest_error = 0;
    int64_t total_area = 0;
    for (int t = 0; t < count; ++t) {
        const ps1_screen_clip_vert* v = result.fragments[t];
        const int64_t area = (int64_t)(v[1].x-v[0].x)*(v[2].y-v[0].y) -
                             (int64_t)(v[1].y-v[0].y)*(v[2].x-v[0].x);
        check(area > 0); total_area += area;
        const double x = (v[0].x + v[1].x + v[2].x) / 3.0;
        const double y = (v[0].y + v[1].y + v[2].y) / 3.0;
        const double error = fabs((v[0].u + v[1].u + v[2].u) / 3.0 - perspective_u(x,y));
        if (error > largest_error) largest_error = error;
    }
    check(llabs(total_area - 320*240) <= 320);
    const double original_error = fabs(172 / 3.0 - perspective_u(320 / 3.0,240 / 3.0));
    printf("Oblique wall UV error: %.2f -> %.2f texels, %d fragments\n",original_error,largest_error,count);
    check(largest_error < original_error / 4);
    ps1_screen_clip_vert flat[3] = {face[0],face[1],face[2]};
    flat[1].z = 100;
    check(ps1_affine_subdivide(flat,result.fragments) == 1);
    check(!memcmp(flat,result.fragments[0],sizeof(flat)));
}

int main(void) {
    uint32_t random = 0x3819acd2u;
    for (unsigned i = 0; i < 100000; ++i) {
        random = random * 1664525u + 1013904223u;
        const int za = 1 + random % 65535;
        random = random * 1664525u + 1013904223u;
        const int zb = 1 + random % 65535;
        random = random * 1664525u + 1013904223u;
        const int denominator = 1 + random % 2000001;
        const int numerator = (random >> 8) % (denominator + 1);
        const ps1_clip_weight weight = ps1_clip_make_weight(za,zb,numerator,denominator);
        const int values[4] = {0,255,-32768,32767};
        for (int v = 0; v < 4; ++v)
            check(ps1_clip_weight_attribute(values[v],values[3-v],&weight) ==
                ps1_clip_screen_attribute(values[v],values[3-v],za,zb,numerator,denominator));
    }
    /* Shared near/far and screen edges run in opposite directions. Verify
     * their actual integer intersections agree, rather than hiding cracks
     * by enlarging rasterized triangles. */
    for (int a = -9000; a <= 9000; a += 117)
        for (int b = -8000; b <= 8000; b += 179)
            for (int n = 0; n <= 301; n += 13)
                check(ps1_clip_lerp(a,b,n,301) == ps1_clip_lerp(b,a,n-301,-301));
    check(ps1_projection_factor(208,128) == 106496);
    check(ps1_projection_axis(-1,106496,160) == 158); // GTE arithmetic shift
    check(ps1_projection_axis(1,106496,160) == 161);
    check(ps1_projection_factor(208,6) > 0x1ffffu); // unclamped near-plane clip
    check(ps1_projection_axis(INT32_MAX,ps1_projection_factor(550,1),0) > INT32_MAX);
    for (int focal = 25; focal <= 550; focal += 17)
        for (int depth = focal/2+1; depth < 65536; depth += 113) {
            const double exact = (double)focal*65536/depth;
            check(fabs((double)ps1_projection_factor(focal,depth)-exact) < 2);
        }
    verify_affine_subdivision();
    /* Split both front and rear surfaces before sorting their local geometry.
       The nearby wall fragment must cover stairs behind it. */
    check(ps1_clipped_face_sort_depth(300, 350, 400, 6, 1600) <
          ps1_clipped_face_sort_depth(1000, 1000, 1000, 6, 1600));
    /* Source centroids outside a clipping plane must not discard the remaining
       visible polygon, and extreme signed coordinates must not overflow. */
    check(ps1_clipped_face_sort_depth(-100, -100, 100, 6, 1600) == 1);
    check(ps1_clipped_face_sort_depth(1400, 2000, 2000, 6, 1600) == 400);
    check(ps1_clipped_face_sort_depth(INT32_MAX, INT32_MAX, INT32_MAX, 6, 1600) == 400);
    check(ps1_clip_screen_attribute(0, 255, 256, 1024, 1, 2) == 51);
    check(ps1_clip_screen_attribute(256, 1024, 256, 1024, 1, 2) == 409);
    check(ps1_clip_screen_attribute(0, 255, 256, 1024, 0, 2) == 0);
    check(ps1_clip_screen_attribute(0, 255, 256, 1024, 2, 2) == 255);
    check(ps1_clip_screen_attribute(0, 255, 30000, 60000, 1000000, 2000000) == 85);
    check(ps1_clip_screen_attribute(0, 255, 256, 1024, -1, -2) == 51);
    check(!ps1_projection_vertex_needs_clip(xy(160, 120), 105, 6, 1600, 208));
    check(ps1_projection_vertex_needs_clip(xy(160, 120), 104, 6, 1600, 208));
    check(ps1_projection_vertex_needs_clip(xy(100, 100), -1, 6, 1600, 208));
    check(ps1_projection_vertex_needs_clip(xy(100, 100), 1601, 6, 1600, 208));
    check(ps1_projection_vertex_needs_clip(xy(1023, 100), 400, 6, 1600, 208));
    check(ps1_projection_vertex_needs_clip(xy(100, -1024), 400, 6, 1600, 208));
    check(!ps1_projection_vertex_needs_clip(xy(1022, -1023), 400, 6, 1600, 208));
    const int32_t values[] = {INT32_MIN, INT32_MIN + 1, -65536, -32768, -3, -1,
                             0, 1, 3, 32767, 65536, INT32_MAX - 1, INT32_MAX};
    const size_t count = sizeof(values) / sizeof(values[0]);
    for (size_t a = 0; a < count; ++a)
        for (size_t b = 0; b < count; ++b)
            for (size_t d = 0; d < count; ++d)
                if (values[d]) verify(values[a], values[b], values[d]);
    /* Near-plane and viewport intersections require truncation toward zero
       on both sides of the screen, including values around DIV overflow. */
    for (int32_t x = -4096; x <= 4096; x += 13)
        for (int32_t z = 1; z <= 1024; z += 17) {
            verify(x, 208, z);
            verify(x, -1024, z);
        }
    puts("PS1 clipping arithmetic matches wide signed division.");
    return 0;
}
