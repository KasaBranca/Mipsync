#include "gpu_transfer.h"
#include <psxetc.h>
#include <hwregs_c.h>
#include <stdint.h>

static void readback_dma(const RECT* rect, uint32_t* data, uint32_t unused) {
    unsigned int words = ((unsigned int)rect->w * rect->h + 1) / 2;
    unsigned int block = 16;
    (void)unused;
    while (words % block) block /= 2;
    while (!(GPU_GP1 & (1u << 26))) __asm__ volatile("");
    SetDrawOpType(DRAWOP_TYPE_DMA);
    GPU_GP1 = 0x04000000u;
    GPU_GP0 = 0x01000000u;
    GPU_GP0 = 0xc0000000u;
    GPU_GP0 = (uint16_t)rect->x | ((uint32_t)(uint16_t)rect->y << 16);
    GPU_GP0 = (uint16_t)rect->w | ((uint32_t)(uint16_t)rect->h << 16);
    GPU_GP1 = 0x04000003u;
    /* SDK 0.24 StoreImage waits for bit 28 (receive ready). During a VRAM
     * read DuckStation/hardware expose bit 27 (send ready) instead. */
    while ((DMA_CHCR(DMA_GPU) & (1u << 24)) || !(GPU_GP1 & (1u << 27)))
        __asm__ volatile("");
    DMA_MADR(DMA_GPU) = (uint32_t)data;
    DMA_BCR(DMA_GPU) = block | ((words / block) << 16);
    DMA_CHCR(DMA_GPU) = 0x01000200u;
}

void ps1_gpu_store_image(const RECT* rect, uint32_t* data) {
    if (rect->w <= 0 || rect->h <= 0) return;
    /* Drain the queue first and finish before returning, so the caller's
     * stack RECT and staging buffer remain valid throughout the DMA. */
    DrawSync(0);
    EnqueueDrawOp((void (*)())readback_dma, (uint32_t)rect, (uint32_t)data, 0);
    DrawSync(0);
}
