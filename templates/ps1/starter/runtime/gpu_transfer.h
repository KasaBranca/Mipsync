#ifndef MIPSYNC_GPU_TRANSFER_H
#define MIPSYNC_GPU_TRANSFER_H
#include <psxgpu.h>
/* Exported 16-bpp TIMs have even widths. Upload without SDK DMA rounding
 * reading beyond their pixel data, including narrow UI images. */
void ps1_gpu_load_image(const RECT* rect, const uint32_t* data);
/* Synchronous readback, waiting for GPU send-ready rather than receive-ready. */
void ps1_gpu_store_image(const RECT* rect, uint32_t* data);
#endif
