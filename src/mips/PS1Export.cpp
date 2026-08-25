#include "PS1Export.h"
#include "../assets/AssetManager.h"
#include "../core/Log.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <unordered_set>

namespace MipsyncEngine::Mips {
namespace fs = std::filesystem;

namespace {

void Push16(std::vector<uint8_t>& buf, uint16_t v) {
    buf.push_back(static_cast<uint8_t>(v & 0xFF));
    buf.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
}

void Push32(std::vector<uint8_t>& buf, uint32_t v) {
    buf.push_back(static_cast<uint8_t>(v & 0xFF));
    buf.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
    buf.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
    buf.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
}

void PushI32(std::vector<uint8_t>& buf, int32_t v) {
    Push32(buf, static_cast<uint32_t>(v));
}

void PushString(std::vector<uint8_t>& buf, const std::string& s) {
    // Clamp; the runtime stores lengths as u16 so 64 KiB is the hard cap.
    const size_t len = std::min<size_t>(s.size(), 0xFFFFu);
    Push16(buf, static_cast<uint16_t>(len));
    buf.insert(buf.end(), s.begin(), s.begin() + static_cast<ptrdiff_t>(len));
}

std::string SanitizeSymbol(const std::string& className) {
    std::string out;
    out.reserve(className.size());
    for (char c : className) {
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') || c == '_') {
            out.push_back(c);
        } else {
            out.push_back('_');
        }
    }
    if (out.empty() || (out[0] >= '0' && out[0] <= '9'))
        out.insert(out.begin(), '_');
    return out;
}

void AddUniqueError(std::vector<std::string>& errors, const std::string& error) {
    if (std::find(errors.begin(), errors.end(), error) == errors.end())
        errors.push_back(error);
}

const char* UnsupportedHostReason(HostFunc func) {
    switch (func) {
    case HostFunc::Vector3_Create:
    case HostFunc::Vector3_Add:
    case HostFunc::Vector3_Sub:
    case HostFunc::Vector3_Scale:
    case HostFunc::Vector3_Length:
    case HostFunc::Vector3_Normalize:
    case HostFunc::Vector3_Up:
    case HostFunc::Vector3_Forward:
    case HostFunc::Vector3_Right:
        return "Vector3 value operations are not available on the PS1 runtime yet";
    case HostFunc::Physics_Raycast:
        return "Physics.Raycast is not available on the PS1 runtime yet (Physics.Move and Physics.IsGrounded are supported)";
    case HostFunc::Entity_GetName:
        return "Entity.name is not available on the PS1 runtime yet";
    case HostFunc::Animator_SetTrigger:
        return "Animator.SetTrigger is not available on the PS1 runtime yet";
    case HostFunc::Scene_Load:
    case HostFunc::Scene_LoadBuildIndex:
        return "Scene loading from Mips# is not available on the PS1 runtime yet";
    case HostFunc::Application_Quit:
        return "Application.Quit is not available on the PS1 runtime";
    case HostFunc::Save_GetInt:
    case HostFunc::Save_SetInt:
    case HostFunc::Save_GetFloat:
    case HostFunc::Save_SetFloat:
    case HostFunc::Save_GetString:
    case HostFunc::Save_SetString:
    case HostFunc::Save_GetBool:
    case HostFunc::Save_SetBool:
    case HostFunc::Save_Write:
    case HostFunc::Save_Read:
        return "Save API is not available on the PS1 runtime yet";
    case HostFunc::Physics_OtherEntityId:
        return "collision/trigger callback context is not available on the PS1 runtime yet";
    case HostFunc::AudioSource_SetClip:
        return "AudioSource.clip runtime assignment is not available on PS1; configure the clip in scene/build data";
    default:
        return nullptr;
    }
}

bool IsUnsupportedLifecycle(const std::string& name) {
    static const std::unordered_set<std::string> unsupported = {
        "LateUpdate", "OnDestroy", "OnCollisionEnter", "OnCollisionExit",
        "OnTriggerEnter", "OnTriggerExit"
    };
    return unsupported.find(name) != unsupported.end();
}

uint16_t ReadU16(const std::vector<uint8_t>& code, size_t offset) {
    return static_cast<uint16_t>(code[offset] |
                                 (static_cast<uint16_t>(code[offset + 1]) << 8));
}

void ValidateMethodBytecode(const CompiledModule& module,
                            const CompiledMethod& method,
                            std::vector<std::string>& errors) {
    const auto where = module.className + "." + method.name;
    const auto& code = method.code;
    size_t pc = 0;
    uint16_t previousConst = 0;
    bool previousWasConst = false;

    auto need = [&](size_t count) {
        if (count <= code.size() - pc) return true;
        AddUniqueError(errors, where + ": truncated bytecode operand at offset " +
                               std::to_string(pc));
        pc = code.size();
        return false;
    };
    auto readU16 = [&]() {
        const uint16_t value = ReadU16(code, pc);
        pc += 2;
        return value;
    };

    while (pc < code.size()) {
        const size_t opOffset = pc;
        const uint8_t raw = code[pc++];
        if (raw > static_cast<uint8_t>(OpCode::YieldBreak)) {
            AddUniqueError(errors, where + ": unknown opcode " + std::to_string(raw) +
                                   " at offset " + std::to_string(opOffset));
            break;
        }
        const OpCode op = static_cast<OpCode>(raw);
        bool currentWasConst = false;
        uint16_t currentConst = 0;

        switch (op) {
        case OpCode::PushConst:
            if (!need(2)) break;
            currentConst = readU16();
            currentWasConst = true;
            if (currentConst >= module.numberConstants.size())
                AddUniqueError(errors, where + ": number constant index is out of range");
            break;
        case OpCode::PushString: {
            if (!need(2)) break;
            const uint16_t index = readU16();
            if (index >= module.stringConstants.size())
                AddUniqueError(errors, where + ": string constant index is out of range");
            break;
        }
        case OpCode::PushField:
        case OpCode::SetField: {
            if (!need(2)) break;
            const uint16_t index = readU16();
            if (index >= module.fields.size() || index >= VM_FIELD_CAP)
                AddUniqueError(errors, where + ": field index exceeds the PS1 field capacity");
            break;
        }
        case OpCode::PushLocal:
        case OpCode::SetLocal: {
            if (!need(2)) break;
            const uint16_t index = readU16();
            if (index >= VM_LOCAL_CAP)
                AddUniqueError(errors, where + ": local index exceeds the PS1 local capacity");
            break;
        }
        case OpCode::GetGlobal:
        case OpCode::GetComponent:
        case OpCode::GetMember:
        case OpCode::StartCoroutine: {
            if (!need(2)) break;
            const uint16_t index = readU16();
            if (index >= module.nameConstants.size())
                AddUniqueError(errors, where + ": name constant index is out of range");
            break;
        }
        case OpCode::PushBool:
        case OpCode::GetVec3Axis:
        case OpCode::SetVec3Axis:
            if (need(1)) ++pc;
            break;
        case OpCode::Jump:
        case OpCode::JumpIfFalse:
            if (need(4)) pc += 4;
            break;
        case OpCode::CallHost: {
            if (!need(3)) break;
            const uint16_t id = readU16();
            ++pc; // argc
            if (id > static_cast<uint16_t>(HostFunc::Mathf_Sign)) {
                AddUniqueError(errors, where + ": unknown PS1 host function #" +
                                       std::to_string(id));
            } else if (const char* reason = UnsupportedHostReason(static_cast<HostFunc>(id))) {
                AddUniqueError(errors, where + ": " + reason);
            }
            break;
        }
        case OpCode::SetVec3FromValue:
            AddUniqueError(errors, where +
                                   ": whole Vector3 assignment is not available on the PS1 runtime yet; assign x/y/z separately");
            break;
        case OpCode::NewArray: {
            if (!need(2)) break;
            const uint16_t count = readU16();
            if (count > VM_ARRAY_LENGTH)
                AddUniqueError(errors, where + ": array literal has " + std::to_string(count) +
                                       " elements; the PS1 limit is " + std::to_string(VM_ARRAY_LENGTH));
            break;
        }
        case OpCode::NewArraySized:
            if (!previousWasConst || previousConst >= module.numberConstants.size()) {
                AddUniqueError(errors, where +
                                       ": dynamic array size cannot be verified for PS1; use a constant size up to " +
                                       std::to_string(VM_ARRAY_LENGTH));
            } else {
                const double count = module.numberConstants[previousConst];
                if (!std::isfinite(count) || count < 0.0 || count > VM_ARRAY_LENGTH ||
                    std::floor(count) != count) {
                    AddUniqueError(errors, where + ": array size must be an integer from 0 to " +
                                           std::to_string(VM_ARRAY_LENGTH) + " on PS1");
                }
            }
            break;
        default:
            break;
        }

        previousWasConst = currentWasConst;
        previousConst = currentConst;
    }
}

} // namespace

int32_t ToFixed16(double v) {
    if (std::isnan(v)) return 0;
    const double scaled = v * 65536.0;
    if (scaled >= 2147483647.0)  return 0x7FFFFFFF;
    if (scaled <= -2147483648.0) return static_cast<int32_t>(0x80000000);
    return static_cast<int32_t>(std::lrint(scaled));
}

double FromFixed16(int32_t v) {
    return static_cast<double>(v) / 65536.0;
}

bool ValidatePs1Target(const std::vector<CompiledModule>& modules,
                       uint32_t bindingCount,
                       std::string& outError) {
    std::vector<std::string> errors;
    if (modules.size() > MIPSYNC_PS1_MODULE_CAP) {
        AddUniqueError(errors, "project contains " + std::to_string(modules.size()) +
                               " script modules; the PS1 limit is " +
                               std::to_string(MIPSYNC_PS1_MODULE_CAP));
    }
    if (bindingCount > MIPSYNC_PS1_INSTANCE_CAP) {
        AddUniqueError(errors, "scene contains " + std::to_string(bindingCount) +
                               " script instances; the PS1 limit is " +
                               std::to_string(MIPSYNC_PS1_INSTANCE_CAP));
    }

    for (const auto& module : modules) {
        const std::string prefix = module.className.empty() ? "<unnamed script>" : module.className;
        for (const auto& error : module.ps1CompatibilityErrors)
            AddUniqueError(errors, error);
        if (module.className.size() >= VM_CLASS_NAME_LEN)
            AddUniqueError(errors, prefix + ": class name exceeds the PS1 limit of " +
                                   std::to_string(VM_CLASS_NAME_LEN - 1) + " bytes");
        if (module.numberConstants.size() > VM_NUMBER_CAP)
            AddUniqueError(errors, prefix + ": number constant count exceeds " + std::to_string(VM_NUMBER_CAP));
        if (module.stringConstants.size() > VM_STRING_CAP)
            AddUniqueError(errors, prefix + ": string constant count exceeds " + std::to_string(VM_STRING_CAP));
        if (module.nameConstants.size() > VM_NAME_CAP)
            AddUniqueError(errors, prefix + ": name constant count exceeds " + std::to_string(VM_NAME_CAP));
        if (module.fields.size() > VM_FIELD_CAP)
            AddUniqueError(errors, prefix + ": field count exceeds " + std::to_string(VM_FIELD_CAP));
        if (module.methods.size() > VM_METHOD_CAP)
            AddUniqueError(errors, prefix + ": method count exceeds " + std::to_string(VM_METHOD_CAP));

        for (const auto& value : module.stringConstants)
            if (value.size() >= VM_STRING_LEN)
                AddUniqueError(errors, prefix + ": a string constant exceeds the PS1 limit of " +
                                       std::to_string(VM_STRING_LEN - 1) + " bytes");
        for (const auto& value : module.nameConstants)
            if (value.size() >= VM_NAME_LEN)
                AddUniqueError(errors, prefix + ": name '" + value + "' exceeds the PS1 limit of " +
                                       std::to_string(VM_NAME_LEN - 1) + " bytes");
        for (const auto& field : module.fields) {
            if (field.name.size() >= VM_FIELD_NAME_LEN)
                AddUniqueError(errors, prefix + ": field '" + field.name + "' exceeds the PS1 name limit");
            if (field.defaultConstIndex >= module.numberConstants.size())
                AddUniqueError(errors, prefix + ": field '" + field.name + "' has an invalid default constant");
        }
        for (const auto& method : module.methods) {
            if (method.name.size() >= VM_METHOD_NAME_LEN)
                AddUniqueError(errors, prefix + ": method '" + method.name + "' exceeds the PS1 name limit");
            if (method.localCount > VM_LOCAL_CAP)
                AddUniqueError(errors, prefix + "." + method.name + ": local count exceeds " +
                                       std::to_string(VM_LOCAL_CAP));
            if (method.code.size() > VM_METHOD_CODE_CAP)
                AddUniqueError(errors, prefix + "." + method.name + ": bytecode exceeds " +
                                       std::to_string(VM_METHOD_CODE_CAP) + " bytes");
            if (IsUnsupportedLifecycle(method.name))
                AddUniqueError(errors, prefix + "." + method.name +
                                       ": this lifecycle callback is not dispatched by the PS1 runtime yet");
            ValidateMethodBytecode(module, method, errors);
        }
    }

    if (errors.empty()) {
        outError.clear();
        return true;
    }
    std::ostringstream message;
    message << "PS1 compatibility check failed:";
    for (const auto& error : errors)
        message << "\n - " << error;
    outError = message.str();
    return false;
}

std::vector<uint8_t> EncodeMbc(const CompiledModule& module) {
    std::vector<uint8_t> buf;
    buf.reserve(256 + module.className.size());

    buf.push_back('M'); buf.push_back('B'); buf.push_back('C'); buf.push_back('1');
    Push16(buf, kMbcCurrentVersion);
    Push16(buf, static_cast<uint16_t>(std::min<size_t>(module.className.size(), 0xFFFFu)));
    Push16(buf, static_cast<uint16_t>(std::min<size_t>(module.numberConstants.size(), 0xFFFFu)));
    Push16(buf, static_cast<uint16_t>(std::min<size_t>(module.stringConstants.size(), 0xFFFFu)));
    Push16(buf, static_cast<uint16_t>(std::min<size_t>(module.nameConstants.size(),   0xFFFFu)));
    Push16(buf, static_cast<uint16_t>(std::min<size_t>(module.fields.size(),          0xFFFFu)));
    Push16(buf, static_cast<uint16_t>(std::min<size_t>(module.methods.size(),         0xFFFFu)));
    Push16(buf, 0);

    buf.insert(buf.end(), module.className.begin(), module.className.end());

    for (double n : module.numberConstants)
        PushI32(buf, ToFixed16(n));

    for (const auto& s : module.stringConstants)
        PushString(buf, s);

    for (const auto& s : module.nameConstants)
        PushString(buf, s);

    for (const auto& f : module.fields) {
        PushString(buf, f.name);
        Push16(buf, f.defaultConstIndex);
        buf.push_back(static_cast<uint8_t>(f.valueKind));
        const uint8_t flags = static_cast<uint8_t>((f.hasGetter ? 0x1 : 0) |
                                                   (f.hasSetter ? 0x2 : 0));
        buf.push_back(flags);
    }

    for (const auto& m : module.methods) {
        PushString(buf, m.name);
        Push32(buf, static_cast<uint32_t>(m.code.size()));
        buf.insert(buf.end(), m.code.begin(), m.code.end());
        Push16(buf, static_cast<uint16_t>(std::min<uint32_t>(m.localCount, 0xFFFFu)));
        Push16(buf, 0);
    }

    return buf;
}

bool WriteMbcFile(const CompiledModule& module, const std::string& path,
                  std::string& outError) {
    try {
        const auto bytes = EncodeMbc(module);
        const fs::path target = PathUtf8::FromString(path);
        if (target.has_parent_path())
            fs::create_directories(target.parent_path());
        std::ofstream out(target, std::ios::binary | std::ios::trunc);
        if (!out.is_open()) {
            outError = "cannot open .mbc for write: " + path;
            return false;
        }
        out.write(reinterpret_cast<const char*>(bytes.data()),
                  static_cast<std::streamsize>(bytes.size()));
        return out.good();
    } catch (const std::exception& ex) {
        outError = ex.what();
        return false;
    }
}

bool EmitScriptsDataC(const std::vector<CompiledModule>& modules,
                      const std::string& outCFile,
                      ScriptsDataEmit& outStats,
                      std::string& outError) {
    try {
        outStats = {};
        const fs::path cPath = PathUtf8::FromString(outCFile);
        if (cPath.has_parent_path())
            fs::create_directories(cPath.parent_path());
        std::ofstream out(cPath, std::ios::trunc);
        if (!out.is_open()) {
            outError = "cannot open scripts_data.c for write: " + outCFile;
            return false;
        }

        out << "/* Auto-generated by Mipsync (src/mips/PS1Export.cpp).\n"
               " * Do not edit by hand — regenerated by `Build PS1` every time.\n"
               " * Holds .mbc bytecode for every Mipsync script in the project,\n"
               " * embedded as `static const unsigned char` arrays. The PS1 mini-VM\n"
               " * (templates/ps1/starter/runtime/vm.c) iterates `g_mipsync_scripts`\n"
               " * and decodes each blob.\n"
               " */\n"
               "#include <stddef.h>\n"
               "#include \"runtime/scripts_data.h\"\n\n";

        for (size_t i = 0; i < modules.size(); ++i) {
            const auto bytes = EncodeMbc(modules[i]);
            outStats.totalBytes += static_cast<uint32_t>(bytes.size());
            const std::string sym = "k_mbc_" + SanitizeSymbol(modules[i].className);
            out << "static const unsigned char " << sym << "[" << bytes.size() << "] = {\n  ";
            for (size_t j = 0; j < bytes.size(); ++j) {
                out << "0x";
                out.width(2);
                out.fill('0');
                out << std::hex << static_cast<int>(bytes[j]) << std::dec;
                if (j + 1 < bytes.size()) out << ",";
                if (((j + 1) % 16) == 0) out << "\n  ";
                else if (j + 1 < bytes.size()) out << " ";
            }
            out << "\n};\n\n";
        }

        out << "const mipsync_script_blob g_mipsync_scripts[] = {\n";
        for (size_t i = 0; i < modules.size(); ++i) {
            const auto bytes = EncodeMbc(modules[i]);
            const std::string sym = "k_mbc_" + SanitizeSymbol(modules[i].className);
            out << "    { \"" << modules[i].className << "\", "
                << sym << ", " << bytes.size() << " },\n";
        }
        out << "};\n";
        out << "const unsigned int g_mipsync_script_count = "
            << modules.size() << "u;\n";

        outStats.scriptCount = static_cast<uint32_t>(modules.size());
        return out.good();
    } catch (const std::exception& ex) {
        outError = ex.what();
        return false;
    }
}

} // namespace MipsyncEngine::Mips
