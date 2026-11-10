#include <stdio.h>
#include <stdlib.h>
#include "../templates/ps1/starter/runtime/world_projection.h"

static void check(int condition, const char* message) {
    if (!condition) { fprintf(stderr, "%s\n", message); exit(1); }
}

int main(void) {
    const int16_t rotation[3][3] = {{4096,0,0},{0,4096,0},{0,0,4096}};
    const int32_t translation[3] = {0,0,640};
    int16_t x = -1, y = -1;
    int32_t depth = -1;
    check(ps1_project_world_point(rotation, translation, 320,240,160,8,6400,
        0,0,0,&x,&y,&depth) && x == 160 && y == 120 && depth == 640,
        "Origin must use the current camera translation and screen center");
    check(ps1_project_world_point(rotation, translation, 320,240,160,8,6400,
        65536,-65536,0,&x,&y,0) && x == 176 && y == 104,
        "World axes must follow camera projection and accept optional depth");
    const int16_t yaw[3][3] = {{0,0,4096},{0,4096,0},{-4096,0,0}};
    check(ps1_project_world_point(yaw, translation, 320,240,160,8,6400,
        65536,0,0,&x,&y,&depth) && x == 160 && depth == 576,
        "Projection must honor camera rotation, not a fixed board formula");
    check(ps1_project_world_point(rotation, translation, 320,240,160,8,6400,
        65536*20,0,0,&x,&y,0) && x == 480,
        "Offscreen points must survive for partially visible overlays");
    x = 77; y = 88; depth = 99;
    check(!ps1_project_world_point(rotation, translation, 320,240,160,8,6400,
        0,0,-65536*10,&x,&y,&depth) && x == 77 && y == 88 && depth == 99,
        "Points behind the near plane must leave outputs unchanged");
    check(!ps1_project_world_point(rotation, translation, 320,240,160,8,6400,
        0,0,65536*100,&x,&y,&depth), "Far-plane points must be rejected");
    check(!ps1_project_world_point(rotation, translation, 320,240,160,8,6400,
        0,0,0,0,&y,&depth), "Required output pointers must be checked");
    const int32_t extreme[3] = {INT32_MAX,INT32_MAX,640};
    check(!ps1_project_world_point(rotation, extreme, 320,240,160,8,6400,
        INT32_MAX,INT32_MAX,0,&x,&y,&depth), "Extreme transforms must not overflow");
    puts("World projection checks passed");
    return 0;
}
