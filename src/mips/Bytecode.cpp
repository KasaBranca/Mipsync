#include "Bytecode.h"
#include <nlohmann/json.hpp>
#include <cstring>
#include <stdexcept>
#include <limits>

namespace MipsyncEngine::Mips {

std::vector<uint8_t> SerializePlayerModule(const CompiledModule& m) {
    using Json=nlohmann::json;
    Json j={{"class",m.className},{"numbers",m.numberConstants},{"strings",m.stringConstants},
            {"names",m.nameConstants},{"ps1Errors",m.ps1CompatibilityErrors},
            {"fields",Json::array()},{"methods",Json::array()}};
    for(const auto& f:m.fields) {
        const auto& a=f.inspector;
        j["fields"].push_back({f.name,f.typeName,static_cast<int>(f.valueKind),f.defaultConstIndex,f.hasGetter,f.hasSetter,
            {{"header",a.header},{"tooltip",a.tooltip},{"name",a.displayName},{"space",a.space},{"order",a.order},
             {"readOnly",a.readOnly},{"hidden",a.hidden},{"hasRange",a.hasRange},{"rangeMin",a.rangeMin},
             {"rangeMax",a.rangeMax},{"hasMin",a.hasMin},{"minimum",a.minimum}}});
    }
    for(const auto& f:m.methods)j["methods"].push_back({f.name,f.returnType,Json::binary(f.code),f.localCount,f.parameterCount,f.isPublic});
    std::vector<uint8_t> out={'M','I','P','B','C','0','0','1'};auto body=Json::to_cbor(j);
    out.insert(out.end(),body.begin(),body.end());return out;
}

std::shared_ptr<CompiledModule> DeserializePlayerModule(const std::vector<uint8_t>& b) {
    if(b.size()<9 || std::memcmp(b.data(),"MIPBC001",8))throw std::runtime_error("Invalid compiled player script");
    auto j=nlohmann::json::from_cbor(b.begin()+8,b.end());
    auto m=std::make_shared<CompiledModule>();
    m->className=j.at("class").get<std::string>();
    m->numberConstants=j.at("numbers").get<std::vector<double>>();
    m->stringConstants=j.at("strings").get<std::vector<std::string>>();
    m->nameConstants=j.at("names").get<std::vector<std::string>>();
    m->ps1CompatibilityErrors=j.at("ps1Errors").get<std::vector<std::string>>();
    if(j.at("fields").size()>65535 || j.at("methods").size()>65535 || m->numberConstants.size()>65535 || m->stringConstants.size()>65535 || m->nameConstants.size()>65535)throw std::runtime_error("Compiled script exceeds limits");
    for(const auto& v:j.at("fields")) {
        if(!v.is_array() || (v.size()!=6 && v.size()!=7) || v.at(2).get<unsigned>()>static_cast<unsigned>(FieldValueKind::EntityReference))throw std::runtime_error("Invalid compiled field");
        CompiledField f;f.name=v.at(0);f.typeName=v.at(1);f.valueKind=static_cast<FieldValueKind>(v.at(2).get<unsigned>());f.defaultConstIndex=v.at(3);f.hasGetter=v.at(4);f.hasSetter=v.at(5);
        if(v.size()==7) {
            const auto& j=v.at(6); if(!j.is_object()) throw std::runtime_error("Invalid inspector metadata");
            auto& a=f.inspector;
            a.header=j.value("header",std::string{});a.tooltip=j.value("tooltip",std::string{});a.displayName=j.value("name",std::string{});
            a.space=j.value("space",0.0);a.order=j.value("order",0);a.readOnly=j.value("readOnly",false);a.hidden=j.value("hidden",false);
            a.hasRange=j.value("hasRange",false);a.rangeMin=j.value("rangeMin",0.0);a.rangeMax=j.value("rangeMax",0.0);
            a.hasMin=j.value("hasMin",false);a.minimum=j.value("minimum",0.0);
            if(!std::isfinite(a.space) || a.space<0 || a.space>1000 || !std::isfinite(a.minimum) ||
               !std::isfinite(a.rangeMin) || !std::isfinite(a.rangeMax) ||
               (a.hasRange && (a.rangeMin>a.rangeMax || (a.hasMin && a.minimum>a.rangeMax))))
                throw std::runtime_error("Invalid inspector bounds");
            if ((a.hasRange || a.hasMin) && f.typeName != "int" && f.typeName != "float")
                throw std::runtime_error("Invalid inspector numeric field");
            if (a.hasRange && (std::abs(a.rangeMin) > std::numeric_limits<float>::max() ||
                              std::abs(a.rangeMax) > std::numeric_limits<float>::max()))
                throw std::runtime_error("Inspector range exceeds float limits");
            if (a.hasMin && std::abs(a.minimum) > std::numeric_limits<float>::max())
                throw std::runtime_error("Inspector minimum exceeds float limits");
            if (f.typeName == "int" &&
                ((a.hasRange && (a.rangeMin < std::numeric_limits<int>::min() ||
                                a.rangeMax > std::numeric_limits<int>::max() ||
                                std::ceil(a.hasMin ? std::max(a.rangeMin, a.minimum) : a.rangeMin) > std::floor(a.rangeMax))) ||
                 (a.hasMin && (a.minimum < std::numeric_limits<int>::min() || a.minimum > std::numeric_limits<int>::max()))))
                throw std::runtime_error("Inspector bounds do not contain a representable int");
        }
        const auto count=m->numberConstants.size();
        if(f.defaultConstIndex>=count)throw std::runtime_error("Invalid compiled field default");
        m->fields.push_back(std::move(f));
    }
    for(const auto& v:j.at("methods")) {
        if(!v.is_array() || v.size()!=6 || !v.at(2).is_binary())throw std::runtime_error("Invalid compiled method");
        CompiledMethod f;f.name=v.at(0);f.returnType=v.at(1);f.code=v.at(2).get_binary();f.localCount=v.at(3);f.parameterCount=v.at(4);f.isPublic=v.at(5);
        if(f.localCount>65535 || f.parameterCount>f.localCount)throw std::runtime_error("Invalid compiled method locals");
        m->methods.push_back(std::move(f));
    }
    return m;
}

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

std::vector<size_t> InspectorFieldIndices(const std::vector<CompiledField>& fields) {
    std::vector<size_t> indices;
    for(size_t i=0;i<fields.size();++i) {
        if(fields[i].typeName=="Vector3.y" || fields[i].typeName=="Vector3.z") continue;
        if(!fields[i].inspector.hidden) indices.push_back(i);
    }
    std::stable_sort(indices.begin(),indices.end(),[&](size_t a,size_t b) {
        return fields[a].inspector.order<fields[b].inspector.order;
    });
    return indices;
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
