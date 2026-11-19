#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <psxgpu.h>
#include "../templates/ps1/starter/runtime/textures.h"
#include "../templates/ps1/starter/runtime/texture_unpack.h"

static const uint32_t owner_raw[] = {0x10u,0x8u,0x2cu,0x1ff0300u,0x10010u,0x12345678u,0x12345679u,0x1234567au,0x1234567bu,0x1234567cu,0x1234567du,0x1234567eu,0x1234567fu,0x14u,0x200210u,0x40001u,0x87654321u,0x65432101u};
static const uint32_t alias_raw[] = {0x10u,0x0u,0x14u,0x200210u,0x40001u,0x44332211u,0x88776655u};
static const uint32_t alias_packed[] = {0x861000u,0x100f8214u,0x1002002u,0x11000400u,0x55443322u,0x887766u};
static const uint32_t owner_packed[] = {0x821000u,0x2c008208u,0xff032783u,0x1001001u,0x34567800u,0x34567912u,0x34567a12u,0x34567b12u,0x34567c12u,0x34567d12u,0x34567e12u,0x34567f12u,0xf821412u,0x200210u,0x40001u,0x87654321u,0x65432101u};
static const uint32_t bad_header[] = {0x9bu};

static const uint32_t truncated[] = {127};
const ps1_texture_desc g_ps1_textures[] = {
    {owner_raw, 18, 0, 0, 1, 1, 0},
    {alias_packed, 7, 0, 1, 1, 1, 23},
    {owner_packed, 18, 0, 0, 3, 3, 68},
    {alias_packed, 7, 0, 1, 3, 3, 23},
    {truncated, 7, 0, 0, 1, 5, 1},
    {bad_header, 7, 0, 0, 1, 6, 1},
    {alias_packed, UINT_MAX, 0, 0, 1, 7, 23},
    {alias_packed, 7, 0, 0, 1, 257, 23}
};
const unsigned int g_ps1_texture_count = 8;
static unsigned uploads, commands, parses;
static struct { const uint32_t* data; unsigned bytes; unsigned char snapshot[64]; } pending[64];
static unsigned pending_count;
static void check(int condition, const char* message) {
    if (!condition) { fprintf(stderr, "%s\n", message); exit(1); }
}
int GetTimInfo(const uint32_t* data, TIM_IMAGE* out) {
    const uint32_t* image = data + 2;
    ++parses;
    memset(out, 0, sizeof(*out));
    out->mode = data[1];
    if (out->mode & 8) {
        out->crect = (RECT*)(image + 1);
        out->caddr = (uint32_t*)(image + 3);
        image += *image / 4;
    }
    out->prect = (RECT*)(image + 1);
    out->paddr = (uint32_t*)(image + 3);
    return 0;
}
void ps1_gpu_load_image(const RECT* rect, const uint32_t* data) {
    const unsigned bytes = (unsigned)rect->w * (unsigned)rect->h * 2u;
    check(bytes <= sizeof(pending[0].snapshot) && pending_count < 64, "fixture upload bounds");
    pending[pending_count].data = data;
    pending[pending_count].bytes = bytes;
    memcpy(pending[pending_count].snapshot, data, bytes);
    ++pending_count; ++uploads; ++commands;
}
int DrawSync(int mode) {
    unsigned i;
    (void)mode;
    for (i = 0; i < pending_count; ++i)
        check(memcmp(pending[i].data, pending[i].snapshot, pending[i].bytes) == 0,
              "Scratch must remain unchanged until queued DMA completes");
    pending_count = 0; ++commands; return 0;
}
void DrawPrim(const uint32_t* packet) { (void)packet; ++commands; }
static void test_decoder(void) {
    unsigned char result[32], overlap[32] = {128};
    const unsigned char zero[] = {255}, literal[] = {2, 10, 20, 30};
    const unsigned char short_literal[] = {2, 10}, short_output[] = {128};
    unsigned char large[128];
    check(ps1_texture_unpack_zero_runs(alias_packed, 23, result, 28), "decode fixture");
    check(memcmp(result, alias_raw, 28) == 0, "decoded TIM must match every source byte");
    check(ps1_texture_unpack_zero_runs(zero, 1, large, 128), "maximum zero run");
    for (unsigned i = 0; i < 128; ++i) check(large[i] == 0, "zero run bytes");
    check(ps1_texture_unpack_zero_runs(literal, 4, result, 3) &&
          result[0] == 10 && result[1] == 20 && result[2] == 30, "literal bytes");
    check(!ps1_texture_unpack_zero_runs(short_literal, 2, result, 3), "truncated literal");
    check(!ps1_texture_unpack_zero_runs(short_output, 1, result, 2), "short decoded output");
    check(!ps1_texture_unpack_zero_runs(zero, 1, result, 32), "output overflow");
    check(!ps1_texture_unpack_zero_runs(0, 1, result, 1), "null source");
    check(!ps1_texture_unpack_zero_runs(zero, 1, 0, 1), "null destination");
    check(!ps1_texture_unpack_zero_runs(zero, 0, result, 1), "empty stream");
    check(!ps1_texture_unpack_zero_runs(overlap, 1, overlap, 1), "same buffer");
    check(!ps1_texture_unpack_zero_runs(overlap, 4, overlap + 1, 1), "forward overlap");
    check(!ps1_texture_unpack_zero_runs(overlap + 1, 4, overlap, 4), "backward overlap");
}
int main(void) {
    uint32_t scratch[18];
    unsigned before;
    test_decoder();
    ps1_textures_set_unpack_buffer(0, 0);
    ps1_textures_init();
    check(ps1_texture_width(1) == 4 && ps1_texture_width(2) == 0, "raw TIMs work without scratch");
    ps1_textures_set_unpack_buffer(scratch, 6);
    before = commands;
    ps1_texture_load_to_vram(2);
    check(commands == before, "undersized buffer refuses before GPU commands");
    ps1_textures_set_unpack_buffer((uint32_t*)((unsigned char*)scratch + 1), 18);
    ps1_texture_load_to_vram(2);
    check(commands == before, "unaligned buffer refuses before GPU commands");
    ps1_textures_set_unpack_buffer(scratch, 18);
    ps1_textures_init();
    check(ps1_texture_width(2) == 4 && ps1_texture_height(2) == 4, "compressed dimensions");
    check(ps1_texture_clut(2) == ps1_texture_clut(1) &&
          ps1_texture_clut(4) == ps1_texture_clut(3), "raw and encoded palette owners");
    check(ps1_texture_tpage(2) == ps1_texture_tpage(1), "slot alias metadata");
    before = parses;
    ps1_texture_ensure_resident(2);
    check(parses == before + 1, "encoded pose reuses its initialized CLUT without unpacking the owner");
    ps1_texture_ensure_resident(1);
    before = uploads;
    ps1_texture_ensure_resident(2);
    check(uploads == before + 1, "alias uploads one decoded image");
    ps1_texture_ensure_resident(2);
    check(uploads == before + 1, "warm alias does not unpack or upload again");
    ps1_texture_ensure_resident(1);
    check(uploads == before + 3, "raw owner restores palette and image");
    ps1_texture_ensure_resident(4);
    check(ps1_texture_clut(4) == ps1_texture_clut(3), "palette lookup may reuse scratch after upload");
    before = commands;
    for (unsigned i = 5; i <= 8; ++i) ps1_texture_load_to_vram(i);
    check(commands == before, "bad streams, TIM headers, sizes and slots issue no GPU commands");
    before = parses;
    ps1_texture_ensure_resident(4);
    check(parses == before, "warm residency avoids reparsing and decoding");
    check(pending_count == 0, "all uploads synchronized");
    puts("PS1 encoded TIM, bounds, residency, palette and DMA lifetime checks passed.");
    return 0;
}
