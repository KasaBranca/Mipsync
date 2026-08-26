#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include "../../templates/ps1/starter/runtime/bytecode_abi.h"

namespace MipsyncEngine::Mips {

enum class OpCode : uint8_t {
#define MIPSYNC_CPP_OPCODE(cName, cppName, value) cppName = value,
    MIPSYNC_OPCODE_LIST(MIPSYNC_CPP_OPCODE)
#undef MIPSYNC_CPP_OPCODE
};

enum class HostFunc : uint16_t {
#define MIPSYNC_CPP_HOST_FUNC(cName, cppName, value) cppName = value,
    MIPSYNC_HOST_FUNC_LIST(MIPSYNC_CPP_HOST_FUNC)
#undef MIPSYNC_CPP_HOST_FUNC
};

struct CompiledMethod {
    std::string name;
    std::string returnType = "void";
    std::vector<uint8_t> code;
    uint32_t localCount = 0;
    uint16_t parameterCount = 0;
    bool isPublic = false;
};

enum class FieldValueKind : uint8_t { Number = 0, Bool, AudioClip, Array, EntityReference };

struct CompiledField {
    std::string name;
    std::string typeName;
    FieldValueKind valueKind = FieldValueKind::Number;
    uint16_t defaultConstIndex = 0;
    bool hasGetter = true;
    bool hasSetter = true;
};

struct CompiledModule {
    std::string className;
    std::vector<CompiledField> fields;
    std::vector<CompiledMethod> methods;
    std::vector<double> numberConstants;
    std::vector<std::string> stringConstants;
    std::vector<std::string> nameConstants; // globals / members
    // Desktop-valid constructs that intentionally cannot run with matching
    // semantics on the current PS1 runtime. The PS1 exporter reports these.
    std::vector<std::string> ps1CompatibilityErrors;

    const CompiledMethod* FindMethod(const std::string& name) const;
    int FindFieldIndex(const std::string& name) const;
};

class BytecodeWriter {
public:
    std::vector<uint8_t>& Code() { return m_Code; }
    const std::vector<uint8_t>& Code() const { return m_Code; }

    void EmitOp(OpCode op);
    void EmitU8(uint8_t value);
    void EmitU16(uint16_t value);
    void EmitI32(int32_t value);

    size_t EmitJumpPlaceholder();
    size_t EmitJumpIfFalsePlaceholder();
    void PatchJump(size_t jumpOffset, size_t targetOffset);

    size_t CurrentOffset() const { return m_Code.size(); }

private:
    std::vector<uint8_t> m_Code;
};

} // namespace MipsyncEngine::Mips
