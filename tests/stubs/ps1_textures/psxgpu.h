#ifndef MIPSYNC_TEST_TEXTURE_GPU_H
#define MIPSYNC_TEST_TEXTURE_GPU_H
#include <stdint.h>
typedef struct { int16_t x, y, w, h; } RECT;
typedef struct { uint32_t mode; RECT* crect; uint32_t* caddr; RECT* prect; uint32_t* paddr; } TIM_IMAGE;
typedef struct { uint32_t tag; uint32_t code[1]; } DR_TPAGE;
#define setlen(p, n) ((p)->tag = (uint32_t)(n) << 24)
#define getTPage(tp, abr, x, y) ((((x) & 0x3c0) >> 6) | (((y) & 0x100) >> 4) | (((y) & 0x200) << 2) | (((abr) & 3) << 5) | (((tp) & 3) << 7))
#define getClut(x, y) (((y) << 6) | (((x) >> 4) & 0x3f))
int GetTimInfo(const uint32_t* tim, TIM_IMAGE* info);
int DrawSync(int mode);
void DrawPrim(const uint32_t* packet);
#endif
