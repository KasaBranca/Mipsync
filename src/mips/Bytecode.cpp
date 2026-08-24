#include "Bytecode.h"

namespace MipsyncEngine::Mips {

const CompiledMethod* CompiledModule::FindMethod(const std::string& name) const {
    for (const auto& method : methods) {
        if (method.name == name)
            return &method;
    }
    return nullptr;
}

int CompiledModule::FindFieldIndex(const std::string& name) const {
    for (size_t i = 0; i < fields.size(); ++i) {
        if (fields[i].name == name)
            return static_cast<int>(i);
    }
    return -1;
}

void BytecodeWriter::EmitOp(OpCode op) {
    m_Code.push_back(static_cast<uint8_t>(op));
}

void BytecodeWriter::EmitU8(uint8_t value) {
    m_Code.push_back(value);
}

void BytecodeWriter::EmitU16(uint16_t value) {
    m_Code.push_back(static_cast<uint8_t>(value & 0xFF));
    m_Code.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
}

void BytecodeWriter::EmitI32(int32_t value) {
    m_Code.push_back(static_cast<uint8_t>(value & 0xFF));
    m_Code.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
    m_Code.push_back(static_cast<uint8_t>((value >> 16) & 0xFF));
    m_Code.push_back(static_cast<uint8_t>((value >> 24) & 0xFF));
}

size_t BytecodeWriter::EmitJumpPlaceholder() {
    const size_t offset = m_Code.size();
    EmitOp(OpCode::Jump);
    EmitI32(0);
    return offset;
}

size_t BytecodeWriter::EmitJumpIfFalsePlaceholder() {
    const size_t offset = m_Code.size();
    EmitOp(OpCode::JumpIfFalse);
    EmitI32(0);
    return offset;
}

void BytecodeWriter::PatchJump(size_t jumpOffset, size_t targetOffset) {
    const int32_t rel = static_cast<int32_t>(targetOffset) - static_cast<int32_t>(jumpOffset + 5);
    m_Code[jumpOffset + 1] = static_cast<uint8_t>(rel & 0xFF);
    m_Code[jumpOffset + 2] = static_cast<uint8_t>((rel >> 8) & 0xFF);
    m_Code[jumpOffset + 3] = static_cast<uint8_t>((rel >> 16) & 0xFF);
    m_Code[jumpOffset + 4] = static_cast<uint8_t>((rel >> 24) & 0xFF);
}

} // namespace MipsyncEngine::Mips
