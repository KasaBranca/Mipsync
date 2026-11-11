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
#include <set>
#include <iomanip>
#include <cctype>
#include <stdexcept>

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

bool IsKnownHostFunction(uint16_t id) {
    switch (static_cast<HostFunc>(id)) {
#define MIPSYNC_KNOWN_HOST(cName, cppName, value) case HostFunc::cppName: return true;
        MIPSYNC_HOST_FUNC_LIST(MIPSYNC_KNOWN_HOST)
#undef MIPSYNC_KNOWN_HOST
    default: return false;
    }
}

bool IsUnsupportedLifecycle(const std::string& name) {
    static const std::unordered_set<std::string> unsupported = {
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
        if (raw > static_cast<uint8_t>(OpCode::GetComponentOn)) {
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
        case OpCode::GetComponentOn:
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
            if (!IsKnownHostFunction(id)) {
                AddUniqueError(errors, where + ": unknown PS1 host function #" +
                                       std::to_string(id));

            }
            break;
        }
        case OpCode::SetVec3FromValue:
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
            if (previousWasConst && previousConst < module.numberConstants.size()) {
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
        if (module.fields.size() > VM_FIELD_CAP)
            AddUniqueError(errors, prefix + ": field count exceeds " + std::to_string(VM_FIELD_CAP));
        for (const auto& field : module.fields) {
            if (field.defaultConstIndex >= module.numberConstants.size())
                AddUniqueError(errors, prefix + ": field '" + field.name + "' has an invalid default constant");
        }
        for (const auto& method : module.methods) {
            if (method.localCount > VM_LOCAL_CAP)
                AddUniqueError(errors, prefix + "." + method.name + ": local count exceeds " +
                                       std::to_string(VM_LOCAL_CAP));
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

namespace {
std::string CText(const std::string& text) {
    std::ostringstream out;
    out << '"';
    for (unsigned char c : text) {
        if (c == '"' || c == '\\') out << '\\' << static_cast<char>(c);
        else if (c >= 32 && c < 127) out << static_cast<char>(c);
        else out << '\\' << std::oct << std::setfill('0') << std::setw(3)
                 << static_cast<unsigned>(c) << std::dec;
    }
    return out.str() + '"';
}
struct NativeInstruction {
    size_t offset, next;
    OpCode op;
    uint16_t operand = 0;
    uint8_t argc = 0;
    size_t target = 0;
};
std::vector<NativeInstruction> NativeInstructions(const CompiledModule& module,
                                                 const CompiledMethod& method) {
    std::vector<NativeInstruction> result;
    std::set<size_t> boundaries;
    const auto& code = method.code;
    size_t pc = 0;
    auto read = [&](unsigned bytes) {
        if (pc + bytes > code.size()) throw std::runtime_error("truncated native IR operand");
        uint32_t value = 0;
        for (unsigned i=0;i<bytes;++i) value |= static_cast<uint32_t>(code[pc++]) << (i*8);
        return value;
    };
    while (pc < code.size()) {
        NativeInstruction i{}; i.offset = pc; boundaries.insert(pc);
        const auto raw = read(1);
        if (raw > static_cast<unsigned>(OpCode::GetComponentOn)) throw std::runtime_error("unknown native IR operation");
        i.op = static_cast<OpCode>(raw);
        switch (i.op) {
        case OpCode::PushConst: case OpCode::PushString:
        case OpCode::PushField: case OpCode::SetField: case OpCode::PushLocal: case OpCode::SetLocal:
        case OpCode::GetGlobal: case OpCode::GetComponent: case OpCode::GetComponentOn:
        case OpCode::GetMember: case OpCode::NewArray: case OpCode::StartCoroutine:
            i.operand = static_cast<uint16_t>(read(2)); break;
        case OpCode::PushBool: case OpCode::GetVec3Axis: case OpCode::SetVec3Axis:
            i.operand = static_cast<uint16_t>(read(1)); break;
        case OpCode::CallHost:
            i.operand = static_cast<uint16_t>(read(2)); i.argc = static_cast<uint8_t>(read(1));
            if (!IsKnownHostFunction(i.operand)) throw std::runtime_error("unknown native host function");
            break;
        case OpCode::Jump: case OpCode::JumpIfFalse: {
            const int32_t delta = static_cast<int32_t>(read(4));
            const int64_t target = static_cast<int64_t>(pc) + delta;
            if (target < 0 || target > static_cast<int64_t>(code.size())) throw std::runtime_error("native branch outside method");
            i.target = static_cast<size_t>(target); break;
        }
        default: break;
        }
        i.next = pc; result.push_back(i);
    }
    boundaries.insert(code.size());
    for (const auto& i : result) {
        if ((i.op == OpCode::Jump || i.op == OpCode::JumpIfFalse) && !boundaries.contains(i.target))
            throw std::runtime_error("native branch is not on an instruction boundary");
        if (i.op == OpCode::PushConst && i.operand >= module.numberConstants.size()) throw std::runtime_error("native number index out of range");
        if (i.op == OpCode::PushString && i.operand >= module.stringConstants.size()) throw std::runtime_error("native string index out of range");
    }
    return result;
}
std::string NativeOperationName(OpCode op) {
    switch(op) {
#define NATIVE_OP_NAME(c, cpp, id) case OpCode::cpp: { std::string s = #c; std::transform(s.begin(),s.end(),s.begin(),[](unsigned char ch){return static_cast<char>(std::tolower(ch));}); return s; }
        MIPSYNC_OPCODE_LIST(NATIVE_OP_NAME)
#undef NATIVE_OP_NAME
    }
    throw std::runtime_error("unknown native operation");
}
void EmitNativeMethod(std::ostream& out, const CompiledModule& module,
                      const CompiledMethod& method, const std::string& symbol) {
    const auto instructions = NativeInstructions(module, method);
    auto name = [&](uint16_t index) {
        if (index >= module.nameConstants.size()) throw std::runtime_error("native name index out of range");
        return CText(module.nameConstants[index]);
    };
    out << "static int " << symbol << "(vm_state* vm, script_instance* inst, vm_coroutine* co) {\n"
           "    (void)inst;\n    if(co) { switch(co->pc) {\n    case 0: goto L0;\n";
    std::set<size_t> resumes;
    for (const auto& i : instructions)
        if (i.op == OpCode::YieldNext || i.op == OpCode::YieldSeconds) resumes.insert(i.next);
    for (auto pc : resumes) if (pc != 0) out << "    case " << pc << ": goto L" << pc << ";\n";
    out << "    default: vm->error=1; return 0; } }\n";
    for (const auto& i : instructions) {
        out << "L" << i.offset << ":\n    ";
        switch(i.op) {
        case OpCode::PushConst:
            out << "vm_push(vm,make_num((fix16_t)" << ToFixed16(module.numberConstants[i.operand]) << "));"; break;
        case OpCode::PushBool:
            out << "vm_push(vm,make_bool(" << (i.operand ? 1 : 0) << "));"; break;
        case OpCode::PushString:
            out << "{ host_value v=make_nil(); v.tag=HOST_VAL_NATIVE_STRING; v.text=" << CText(module.stringConstants[i.operand]) << "; vm_push(vm,v); }"; break;
        case OpCode::PushField: case OpCode::SetField: case OpCode::PushLocal: case OpCode::SetLocal:
        case OpCode::GetVec3Axis: case OpCode::SetVec3Axis: case OpCode::NewArray:
            out << "native_" << NativeOperationName(i.op) << "(vm," << i.operand << ");"; break;
        case OpCode::GetGlobal: case OpCode::GetMember: case OpCode::StartCoroutine:
            out << "native_" << NativeOperationName(i.op) << "(vm," << name(i.operand) << ");"; break;
        case OpCode::GetComponent: case OpCode::GetComponentOn:
            out << "native_get_component_on(vm," << name(i.operand) << ',' << (i.op == OpCode::GetComponentOn ? 1 : 0) << ");"; break;
        case OpCode::CallHost:
            out << "if(!host_dispatch(vm," << i.operand << ',' << static_cast<unsigned>(i.argc) << ")) return 0;"; break;
        case OpCode::Jump:
            out << "goto L" << i.target << ';'; break;
        case OpCode::JumpIfFalse:
            out << "if(!val_as_bool(vm_pop(vm))) goto L" << i.target << ';'; break;
        case OpCode::Return: case OpCode::YieldBreak:
            out << "if(co) co->active=0; return !vm->error;"; break;
        case OpCode::YieldNext: case OpCode::YieldSeconds:
            out << "return native_yield(vm,co," << i.next << ',' << (i.op == OpCode::YieldSeconds ? 1 : 0) << ");"; break;
        default:
            out << "native_" << NativeOperationName(i.op) << "(vm);"; break;
        }
        out << "\n    if(vm->error) return 0;\n";
    }
    out << "L" << method.code.size() << ":\n    if(co) co->active=0; return !vm->error;\n}\n\n";
}
} // namespace

bool ModuleUsesHostFunction(const CompiledModule& module, HostFunc function) {
    for(const auto& method:module.methods)for(const auto& i:NativeInstructions(module,method))
        if(i.op==OpCode::CallHost && i.operand==static_cast<uint16_t>(function))return true;
    return false;
}

bool EmitScriptsDataC(const std::vector<CompiledModule>& modules,
                      const std::string& outCFile,
                      ScriptsDataEmit& outStats,
                      std::string& outError) {
    try {
        outStats = {};
        std::ostringstream out;
        out << "/* Generated Mips# native C. Compile with PSn00bSDK GCC for MIPS.\n"
               " * Shared compiler IR is lowered to direct branches and functions here.\n"
               " * No bytecode, instruction fetching, or interpreter is shipped. */\n"
               "#include \"runtime/scripts_data.h\"\n"
               "#include \"runtime/native_ops.h\"\n\n";
        for (size_t m=0;m<modules.size();++m) {
            const auto& module=modules[m];
            const std::string symbol="mipsync_native_"+std::to_string(m);
            for(size_t f=0;f<module.methods.size();++f)
                EmitNativeMethod(out,module,module.methods[f],symbol+"_"+std::to_string(f));
            out << "static const mbc_field " << symbol << "_fields[] = {\n";
            if(module.fields.empty()) out << "    {0,0},\n";
            for(const auto& field:module.fields) out << "    {" << CText(field.name) << ',' << static_cast<unsigned>(field.valueKind) << "},\n";
            out << "};\nstatic const mbc_method " << symbol << "_methods[] = {\n";
            if(module.methods.empty()) out << "    {0,0,0},\n";
            for(size_t f=0;f<module.methods.size();++f)
                out << "    {" << CText(module.methods[f].name) << ',' << symbol << '_' << f << ',' << module.methods[f].localCount << "},\n";
            out << "};\n\n";
        }
        out << "mbc_module g_mipsync_scripts[] = {\n";
        if(modules.empty()) out << "    {0},\n";
        for(size_t m=0;m<modules.size();++m) {
            const std::string symbol="mipsync_native_"+std::to_string(m);
            out << "    {" << CText(modules[m].className) << ',' << modules[m].fields.size() << ',' << symbol << "_fields,"
                << modules[m].methods.size() << ',' << symbol << "_methods},\n";
        }
        out << "};\nconst unsigned int g_mipsync_script_count=" << modules.size() << ";\n";
        const fs::path path=PathUtf8::FromString(outCFile);
        if(path.has_parent_path()) fs::create_directories(path.parent_path());
        std::ofstream file(path,std::ios::binary|std::ios::trunc);
        const auto text=out.str(); file.write(text.data(),static_cast<std::streamsize>(text.size()));
        if(!file.good()) throw std::runtime_error("cannot write native scripts: "+outCFile);
        outStats.scriptCount=static_cast<uint32_t>(modules.size());
        outStats.totalBytes=static_cast<uint32_t>(text.size());
        return true;
    } catch(const std::exception& e) {outError=e.what();return false;}
}

} // namespace MipsyncEngine::Mips
