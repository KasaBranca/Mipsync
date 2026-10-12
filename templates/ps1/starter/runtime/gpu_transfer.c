#include "gpu_transfer.h"

void ps1_gpu_load_image(const RECT* rect, const uint32_t* data) {
    unsigned int width = (unsigned int)rect->w;
    unsigned int height = (unsigned int)rect->h;
    unsigned int row_alignment = 32, common = width;
    unsigned int bulk_rows, y, wide;
    RECT part = *rect;
    if (!width || !height) return;
    /* SDK 0.24 rounds transfers >= 16 words up to a 16-word DMA block.
     * Extra words become GPU commands once the image rectangle is full. */
    if (width * height < 32 || (width * height) % 32 == 0) {
        LoadImage(rect, data);
        return;
    }
    while (common % 2 == 0 && row_alignment > 1) {
        common /= 2;
        row_alignment /= 2;
    }
    bulk_rows = height - height % row_alignment;
    if (bulk_rows) {
        part.h = (short)bulk_rows;
        LoadImage(&part, data);
        DrawSync(0);
    }
    wide = width - width % 32;
    for (y = bulk_rows; y < height; ++y) {
        part.y = rect->y + (short)y;
        part.h = 1;
        part.x = rect->x;
        if (wide) {
            part.w = (short)wide;
            LoadImage(&part, data + y * width / 2);
            DrawSync(0);
        }
        if (width != wide) {
            part.x = rect->x + (short)wide;
            part.w = (short)(width - wide);
            LoadImage(&part, data + (y * width + wide) / 2);
            DrawSync(0);
        }
    }
}
