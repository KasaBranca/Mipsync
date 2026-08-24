#ifndef MIPSYNC_GTE_H
#define MIPSYNC_GTE_H

/*
 * Mipsync GTE compatibility layer
 *
 * This file is an original implementation for Mipsync Engine. It only exposes
 * the subset of PlayStation COP2/GTE functionality used by the runtime. The
 * register layout and command words come from public hardware documentation;
 * no Sony SDK or libpsxgte source is included or required.
 */

#include <stdint.h>

#define ONE (1 << 12)

typedef struct MipsyncMatrix {
    int16_t m[3][3];
    int32_t t[3];
} MATRIX;

typedef struct MipsyncVector {
    int32_t vx, vy, vz;
} VECTOR;

typedef struct MipsyncShortVector {
    int16_t vx, vy, vz, pad;
} SVECTOR;

typedef struct MipsyncColorVector {
    uint8_t r, g, b, cd;
} CVECTOR;

typedef struct MipsyncDrawVector {
    int16_t vx, vy;
} DVECTOR;

#ifdef __cplusplus
extern "C" {
#endif

void mipsync_gte_init(void);
MATRIX* mipsync_gte_rot_matrix(const SVECTOR* rotation, MATRIX* out);
MATRIX* mipsync_gte_trans_matrix(MATRIX* matrix, const VECTOR* translation);
MATRIX* mipsync_gte_scale_matrix(MATRIX* matrix, const VECTOR* scale);
MATRIX* mipsync_gte_mul_matrix(const MATRIX* left, const MATRIX* right, MATRIX* out);
MATRIX* mipsync_gte_comp_matrix(const MATRIX* parent, const MATRIX* local, MATRIX* out);
VECTOR* mipsync_gte_apply_matrix(const MATRIX* matrix, const VECTOR* input, VECTOR* out);
void mipsync_gte_normalize(const VECTOR* input, SVECTOR* out);

#ifdef __cplusplus
}
#endif

/* Source-compatible aliases for the small API surface used by Mipsync. */
#define InitGeom()                    mipsync_gte_init()
#define RotMatrix(r, m)               mipsync_gte_rot_matrix((r), (m))
#define TransMatrix(m, v)             mipsync_gte_trans_matrix((m), (v))
#define ScaleMatrix(m, v)             mipsync_gte_scale_matrix((m), (v))
#define MulMatrix0(a, b, out)         mipsync_gte_mul_matrix((a), (b), (out))
#define CompMatrixLV(a, b, out)       mipsync_gte_comp_matrix((a), (b), (out))
#define ApplyMatrixLV(m, v, out)      mipsync_gte_apply_matrix((m), (v), (out))
#define VectorNormalS(v, out)         mipsync_gte_normalize((v), (out))

/*
 * GCC inline assembly below directly targets documented COP2 registers. Each
 * wrapper is intentionally tiny and named for Mipsync rather than importing a
 * third-party macro header.
 */
static inline void mipsync_gte_load_v0(const SVECTOR* value) {
    __asm__ volatile(
        "lwc2 $0, 0(%0)\n"
        "lwc2 $1, 4(%0)\n"
        : : "r"(value) : "memory");
}

static inline void mipsync_gte_load_v3(const SVECTOR* a, const SVECTOR* b,
                                       const SVECTOR* c) {
    __asm__ volatile(
        "lwc2 $0, 0(%0)\n" "lwc2 $1, 4(%0)\n"
        "lwc2 $2, 0(%1)\n" "lwc2 $3, 4(%1)\n"
        "lwc2 $4, 0(%2)\n" "lwc2 $5, 4(%2)\n"
        : : "r"(a), "r"(b), "r"(c) : "memory");
}

static inline void mipsync_gte_load_rgb(const void* rgba) {
    __asm__ volatile("lwc2 $6, 0(%0)\n" : : "r"(rgba) : "memory");
}

static inline void mipsync_gte_set_geom_offset(int x, int y) {
    const int32_t fx = x << 16;
    const int32_t fy = y << 16;
    __asm__ volatile("ctc2 %0, $24\nctc2 %1, $25\n" : : "r"(fx), "r"(fy));
}

static inline void mipsync_gte_set_geom_screen(int distance) {
    __asm__ volatile("ctc2 %0, $26\n" : : "r"(distance));
}

static inline void mipsync_gte_set_back_color(int r, int g, int b) {
    const int32_t fr = r << 4, fg = g << 4, fb = b << 4;
    __asm__ volatile("ctc2 %0, $13\nctc2 %1, $14\nctc2 %2, $15\n"
                     : : "r"(fr), "r"(fg), "r"(fb));
}

/* Keep every COP2 control-register write explicit and easy to audit. */
static inline void mipsync_gte_set_rot_matrix(const MATRIX* matrix) {
    const uint32_t* w = (const uint32_t*)matrix->m;
    const uint16_t r33 = (uint16_t)matrix->m[2][2];
    __asm__ volatile("ctc2 %0,$0\nctc2 %1,$1\nctc2 %2,$2\nctc2 %3,$3\nctc2 %4,$4\n"
                     : : "r"(w[0]), "r"(w[1]), "r"(w[2]), "r"(w[3]), "r"(r33));
}

static inline void mipsync_gte_set_light_matrix(const MATRIX* matrix) {
    const uint32_t* w = (const uint32_t*)matrix->m;
    const uint16_t l33 = (uint16_t)matrix->m[2][2];
    __asm__ volatile("ctc2 %0,$8\nctc2 %1,$9\nctc2 %2,$10\nctc2 %3,$11\nctc2 %4,$12\n"
                     : : "r"(w[0]), "r"(w[1]), "r"(w[2]), "r"(w[3]), "r"(l33));
}

static inline void mipsync_gte_set_color_matrix(const MATRIX* matrix) {
    const uint32_t* w = (const uint32_t*)matrix->m;
    const uint16_t c33 = (uint16_t)matrix->m[2][2];
    __asm__ volatile("ctc2 %0,$16\nctc2 %1,$17\nctc2 %2,$18\nctc2 %3,$19\nctc2 %4,$20\n"
                     : : "r"(w[0]), "r"(w[1]), "r"(w[2]), "r"(w[3]), "r"(c33));
}

static inline void mipsync_gte_set_trans_matrix(const MATRIX* matrix) {
    __asm__ volatile("ctc2 %0,$5\nctc2 %1,$6\nctc2 %2,$7\n"
                     : : "r"(matrix->t[0]), "r"(matrix->t[1]), "r"(matrix->t[2]));
}

static inline void mipsync_gte_cmd_rtps(void) { __asm__ volatile("nop\nnop\ncop2 0x0180001\n"); }
static inline void mipsync_gte_cmd_rtpt(void) { __asm__ volatile("nop\nnop\ncop2 0x0280030\n"); }
static inline void mipsync_gte_cmd_ncs(void)  { __asm__ volatile("nop\nnop\ncop2 0x0c8041e\n"); }
static inline void mipsync_gte_cmd_nclip(void){ __asm__ volatile("nop\nnop\ncop2 0x1400006\n"); }
static inline void mipsync_gte_cmd_avsz3(void){ __asm__ volatile("nop\nnop\ncop2 0x158002d\n"); }
static inline void mipsync_gte_cmd_avsz4(void){ __asm__ volatile("nop\nnop\ncop2 0x168002e\n"); }

static inline void mipsync_gte_store_sxy(void* out)  { __asm__ volatile("swc2 $14,0(%0)\n" : : "r"(out) : "memory"); }
static inline void mipsync_gte_store_sxy0(void* out) { __asm__ volatile("swc2 $12,0(%0)\n" : : "r"(out) : "memory"); }
static inline void mipsync_gte_store_sxy1(void* out) { __asm__ volatile("swc2 $13,0(%0)\n" : : "r"(out) : "memory"); }
static inline void mipsync_gte_store_sxy2(void* out) { __asm__ volatile("swc2 $14,0(%0)\n" : : "r"(out) : "memory"); }
static inline void mipsync_gte_store_sz(void* out)   { __asm__ volatile("swc2 $19,0(%0)\n" : : "r"(out) : "memory"); }
static inline void mipsync_gte_store_sz3(void* a, void* b, void* c) {
    __asm__ volatile("swc2 $17,0(%0)\nswc2 $18,0(%1)\nswc2 $19,0(%2)\n"
                     : : "r"(a), "r"(b), "r"(c) : "memory");
}
static inline void mipsync_gte_store_otz(void* out)  { __asm__ volatile("swc2 $7,0(%0)\n" : : "r"(out) : "memory"); }
static inline void mipsync_gte_store_opz(void* out)  { __asm__ volatile("swc2 $24,0(%0)\n" : : "r"(out) : "memory"); }
static inline void mipsync_gte_store_rgb(void* out)  { __asm__ volatile("swc2 $22,0(%0)\n" : : "r"(out) : "memory"); }

#define gte_ldv0(v)             mipsync_gte_load_v0((v))
#define gte_ldv3(a,b,c)         mipsync_gte_load_v3((a),(b),(c))
#define gte_ldrgb(c)            mipsync_gte_load_rgb((c))
#define gte_SetGeomOffset(x,y)  mipsync_gte_set_geom_offset((x),(y))
#define gte_SetGeomScreen(h)    mipsync_gte_set_geom_screen((h))
#define gte_SetBackColor(r,g,b) mipsync_gte_set_back_color((r),(g),(b))
#define gte_SetRotMatrix(m)     mipsync_gte_set_rot_matrix((m))
#define gte_SetTransMatrix(m)   mipsync_gte_set_trans_matrix((m))
#define gte_SetLightMatrix(m)   mipsync_gte_set_light_matrix((m))
#define gte_SetColorMatrix(m)   mipsync_gte_set_color_matrix((m))
#define gte_rtps()              mipsync_gte_cmd_rtps()
#define gte_rtpt()              mipsync_gte_cmd_rtpt()
#define gte_ncs()               mipsync_gte_cmd_ncs()
#define gte_nclip()             mipsync_gte_cmd_nclip()
#define gte_avsz3()             mipsync_gte_cmd_avsz3()
#define gte_avsz4()             mipsync_gte_cmd_avsz4()
#define gte_stsxy(p)            mipsync_gte_store_sxy((p))
#define gte_stsxy0(p)           mipsync_gte_store_sxy0((p))
#define gte_stsxy1(p)           mipsync_gte_store_sxy1((p))
#define gte_stsxy2(p)           mipsync_gte_store_sxy2((p))
#define gte_stsz(p)             mipsync_gte_store_sz((p))
#define gte_stsz3(a,b,c)        mipsync_gte_store_sz3((a),(b),(c))
#define gte_stotz(p)            mipsync_gte_store_otz((p))
#define gte_stopz(p)            mipsync_gte_store_opz((p))
#define gte_strgb(p)            mipsync_gte_store_rgb((p))

#endif
