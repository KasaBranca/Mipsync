#pragma once

#include "Bytecode.h"
#include <cstdint>
#include <string>
#include <vector>

namespace MipsyncEngine::Mips {

/// PS1 native script exporter: shared compiler IR -> C -> PSn00bSDK GCC/MIPS.
/// Production output contains native functions and method descriptors; no VM.
/// The MBC encoding helpers below remain available for interpreter-oracle tests.
///
/// Binary layout (.mbc, version 1):
///
///     u8  magic[4]            "MBC1"
///     u16 version             1
///     u16 classNameLen
///     u16 numberCount
///     u16 stringCount
///     u16 nameCount
///     u16 fieldCount
///     u16 methodCount
///     u16 reserved            0 (pad to 4-byte alignment after header)
///     u8  className[classNameLen]
///     i32 numbers[numberCount]              Q16.16 fixed-point
///     repeated stringCount: u16 len; u8 bytes[len]
///     repeated nameCount:   u16 len; u8 bytes[len]
///     repeated fieldCount:
///         u16 nameLen; u8 name[nameLen]
///         u16 defaultConstIdx
///         u8  valueKind     (0 = Number, 1 = Bool, 2 = AudioClip, 3 = Array)
///         u8  flags         bit0=hasGetter, bit1=hasSetter
///     repeated methodCount:
///         u16 nameLen; u8 name[nameLen]
///         u32 codeLen
///         u8  code[codeLen]
///         u16 localCount
///         u16 pad
///
/// All multi-byte integers are little-endian (matches MIPS R3000A native).
/// Strings are NOT null-terminated; consumers store their length explicitly.

constexpr uint16_t kMbcCurrentVersion = 1;
constexpr int32_t  kQ16_16_One = 0x00010000;

/// Convert an IEEE 754 double from the PC compiler into Q16.16 fixed-point.
/// Saturates to INT32_MIN/MAX on overflow.
int32_t ToFixed16(double v);

/// Inverse of ToFixed16 (mainly for debug/round-trip tests on the PC side).
double FromFixed16(int32_t v);

/// Serialise a single CompiledModule into the .mbc byte buffer.
std::vector<uint8_t> EncodeMbc(const CompiledModule& module);

/// Write a .mbc file. Returns true on success; on failure populates outError.
bool WriteMbcFile(const CompiledModule& module, const std::string& path,
                  std::string& outError);

/// Check compiler IR and native target runtime capacities before export.
bool ValidatePs1Target(const std::vector<CompiledModule>& modules,
                       uint32_t bindingCount,
                       std::string& outError);

/// Result of generating the engine-side data table that gets compiled into
/// the PS1 starter (templates/ps1/starter/generated/scripts_data.c).
struct ScriptsDataEmit {
    uint32_t scriptCount = 0;
    uint32_t totalBytes  = 0;
};

/// Query host API usage in shared compiler IR (for runtime asset catalogs).
bool ModuleUsesHostFunction(const CompiledModule& module, HostFunc function);

/// Emit native C functions and metadata to generated/scripts_data.c.
bool EmitScriptsDataC(const std::vector<CompiledModule>& modules,
                      const std::string& outCFile,
                      ScriptsDataEmit& outStats,
                      std::string& outError);

} // namespace MipsyncEngine::Mips
