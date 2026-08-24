#pragma once
// Graphics backend selection (CMake: -DMIPSYNC_GFX_BACKEND=D3D12|OPENGL)

#if defined(MIPSYNC_GFX_D3D12)
#define MIPSYNC_GFX_USE_D3D12 1
#define MIPSYNC_GFX_USE_OPENGL 0
#else
#define MIPSYNC_GFX_USE_D3D12 0
#define MIPSYNC_GFX_USE_OPENGL 1
#endif
