#ifndef MIPSYNC_TEST_PSXGPU_H
#define MIPSYNC_TEST_PSXGPU_H
#include <stdint.h>
typedef struct { int16_t x, y, w, h; } RECT;
void DrawOTag(const uint32_t* ot);
int DrawSync(int mode);
int StoreImage(const RECT* rect, uint32_t* pixels);
int LoadImage(const RECT* rect, const uint32_t* pixels);
#endif
