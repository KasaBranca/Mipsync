# Mipsync GTE compatibility layer: provenance and scope

This directory contains Mipsync Engine's original, minimal PlayStation GTE
compatibility layer. Its purpose is to prevent Mipsync-built programs from
linking PSn00bSDK's `libpsxgte` implementation.

## Implementation inputs

- Public PlayStation hardware documentation describing COP2/GTE registers,
  commands, fixed-point formats and behavior:
  <https://psx-spx.consoledev.net/geometrytransformationenginegte/>
- The MIPS/GCC inline-assembly interface needed to issue COP2 instructions.
- Mipsync Engine's own renderer call sites, used only to determine the minimum
  API surface required by this runtime.

No Sony SDK source, Sony library binary, PSn00bSDK `libpsxgte` source, or
PSn00bSDK GTE macro implementation was copied into this layer.

## Implemented scope

- Mipsync-owned matrix/vector types and fixed-point matrix operations.
- Rotation, translation, scaling, composition, application and normalization.
- Direct loads/stores of the documented COP2 data/control registers.
- Only the GTE commands currently required by the Mipsync renderer: RTPS, RTPT,
  NCS, NCLIP, AVSZ3 and AVSZ4.

The compatibility aliases in `mipsync_gte.h` are intentionally limited to the
existing Mipsync runtime call sites. They are not intended to reproduce the
complete Sony or PSn00bSDK API.

## Build boundary

`CMakeLists.txt` removes `psxgte` from PSn00bSDK's default executable link
libraries and builds `mipsync_gte.c` instead. Other PSn00bSDK components remain
in use for unrelated facilities such as GPU, CD-ROM, input and audio. This
document therefore records the provenance of the GTE path only; it is not a
legal certification of the complete toolchain.
