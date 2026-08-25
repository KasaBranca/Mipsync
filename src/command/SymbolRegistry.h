#pragma once

#include "CommandTypes.h"
#include <map>
#include <string>
#include <vector>

namespace MipsyncEngine::Command {

struct SymbolDescriptor {
    std::string id;
    std::string kind;
    std::string name;
    std::string summary;
    std::string description;
    std::string signature;
    std::vector<ParameterDescriptor> parameters;
    std::string returnType;
    std::vector<std::string> examples;
    std::vector<std::string> relatedSymbols;
    std::string sourceModule;
    std::string version;

    Json ToJson(bool detailed = true) const;
};

class SymbolRegistry {
public:
    bool Register(SymbolDescriptor descriptor, std::string* outError = nullptr);
    const SymbolDescriptor* Find(const std::string& id) const;
    std::vector<const SymbolDescriptor*> Search(const std::string& query, size_t limit = 20) const;
    std::vector<const SymbolDescriptor*> Children(const std::string& prefix) const;
    std::vector<const SymbolDescriptor*> All() const;

private:
    std::map<std::string, SymbolDescriptor> m_Symbols;
};

} // namespace MipsyncEngine::Command
