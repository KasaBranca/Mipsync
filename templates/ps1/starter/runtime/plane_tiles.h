#ifndef MIPSYNC_PLANE_TILES_H
#define MIPSYNC_PLANE_TILES_H
#include <stdint.h>
#define PS1_PLANE_AXIS_CAP 32
typedef struct {
    int32_t start_q16, end_q16;
    int uv_start_q8, uv_end_q8;
} ps1_plane_span;
static inline int ps1_positive_fraction(int value) {
    int fraction=value%256;
    return fraction<0 ? fraction+256 : fraction;
}
/* Split exactly at material repeat boundaries. Uniform cells interpolate
 * across a UV wrap, stretching the last texel over an entire floor tile. */
static inline int ps1_plane_axis(int16_t tiling, int16_t offset,
                                uint8_t levels, ps1_plane_span out[PS1_PLANE_AXIS_CAP]) {
    const int direction=tiling<0 ? -1 : 1;
    const int magnitude=tiling<0 ? -(int)tiling : (int)tiling;
    int divisions=1 << (levels<2 ? 2 : levels>4 ? 4 : levels);
    int cursor=0, count=0;
    if (!magnitude) {
        int i;
        for (i=0;i<divisions;++i) {
            out[i].start_q16=i*65536/divisions;
            out[i].end_q16=(i+1)*65536/divisions;
            out[i].uv_start_q8=out[i].uv_end_q8=ps1_positive_fraction(offset);
        }
        return divisions;
    }
    while (cursor<magnitude && count<PS1_PLANE_AXIS_CAP) {
        const int coordinate=offset+direction*cursor;
        const int fraction=ps1_positive_fraction(coordinate);
        int length=direction>0 ? 256-fraction : (fraction ? fraction : 256);
        int parts, i;
        if (length>magnitude-cursor) length=magnitude-cursor;
        parts=(length*divisions+magnitude-1)/magnitude;
        if (parts<1) parts=1;
        for (i=0;i<parts && count<PS1_PLANE_AXIS_CAP;++i) {
            const int begin=cursor*parts+length*i;
            const int end=cursor*parts+length*(i+1);
            const int period=256*parts;
            int uv_begin=(offset*parts+direction*begin)%period;
            int uv_end=(offset*parts+direction*end)%period;
            ps1_plane_span* span=&out[count++];
            span->start_q16=(int32_t)((int64_t)begin*65536/(magnitude*parts));
            span->end_q16=(int32_t)((int64_t)end*65536/(magnitude*parts));
            if (uv_begin<0) uv_begin+=period;
            if (uv_end<0) uv_end+=period;
            span->uv_start_q8=(uv_begin+parts/2)/parts;
            span->uv_end_q8=(uv_end+parts/2)/parts;
            if (direction<0 && !uv_begin) span->uv_start_q8=256;
            if (direction>0 && !uv_end) span->uv_end_q8=256;
        }
        cursor+=length;
    }
    return count;
}
#endif
