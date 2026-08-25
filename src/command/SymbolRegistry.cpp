#include "SymbolRegistry.h"

#include <algorithm>
#include <cctype>
#include <sstream>

namespace MipsyncEngine::Command {
namespace {

std::string Lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

std::vector<std::string> Words(const std::string& value) {
    std::vector<std::string> result;
    std::istringstream input(Lower(value));
    std::string word;
    while (input >> word) result.push_back(word);
    return result;
}

int Score(const SymbolDescriptor& symbol, const std::vector<std::string>& words) {
    const std::string id = Lower(symbol.id);
    const std::string name = Lower(symbol.name);
    const std::string body = Lower(symbol.summary + " " + symbol.description + " " + symbol.signature);
    int score = 0;
    int matched = 0;
    for (const auto& word : words) {
        if (id == word || name == word) { score += 100; ++matched; }
        else if (id.rfind(word, 0) == 0 || name.rfind(word, 0) == 0) { score += 55; ++matched; }
        else if (id.find(word) != std::string::npos || name.find(word) != std::string::npos) { score += 30; ++matched; }
        else if (body.find(word) != std::string::npos) { score += 10; ++matched; }
    }
    if (matched == 0) return 0;
    return score + matched * matched * 5;
}

} // namespace

Json SymbolDescriptor::ToJson(bool detailed) const {
    Json result{{"id", id}, {"kind", kind}, {"name", name}, {"summary", summary}};
    if (!detailed) return result;
    result["description"] = description;
    if (!signature.empty()) result["signature"] = signature;
    if (!returnType.empty()) result["returnType"] = returnType;
    result["parameters"] = Json::array();
    for (const auto& parameter : parameters) {
        result["parameters"].push_back({
            {"name", parameter.name}, {"type", ToString(parameter.type)},
            {"required", parameter.required}, {"summary", parameter.summary},
        });
    }
    result["examples"] = examples;
    result["relatedSymbols"] = relatedSymbols;
    result["sourceModule"] = sourceModule;
    result["version"] = version;
    return result;
}

bool SymbolRegistry::Register(SymbolDescriptor descriptor, std::string* outError) {
    if (descriptor.id.empty()) {
        if (outError) *outError = "symbol id is empty";
        return false;
    }
    const std::string id = descriptor.id;
    if (descriptor.name.empty()) descriptor.name = id;
    if (!m_Symbols.emplace(id, std::move(descriptor)).second) {
        if (outError) *outError = "duplicate symbol id: " + id;
        return false;
    }
    return true;
}

const SymbolDescriptor* SymbolRegistry::Find(const std::string& id) const {
    auto it = m_Symbols.find(id);
    if (it != m_Symbols.end()) return &it->second;
    const std::string wanted = Lower(id);
    for (const auto& [symbolId, symbol] : m_Symbols)
        if (Lower(symbolId) == wanted || Lower(symbol.name) == wanted) return &symbol;
    return nullptr;
}

std::vector<const SymbolDescriptor*> SymbolRegistry::Search(const std::string& query, size_t limit) const {
    const auto words = Words(query);
    std::vector<std::pair<int, const SymbolDescriptor*>> scored;
    for (const auto& [id, symbol] : m_Symbols) {
        const int score = words.empty() ? 1 : Score(symbol, words);
        if (score > 0) scored.push_back({score, &symbol});
    }
    std::sort(scored.begin(), scored.end(), [](const auto& a, const auto& b) {
        return a.first != b.first ? a.first > b.first : a.second->id < b.second->id;
    });
    std::vector<const SymbolDescriptor*> result;
    for (size_t i = 0; i < scored.size() && i < limit; ++i) result.push_back(scored[i].second);
    return result;
}

std::vector<const SymbolDescriptor*> SymbolRegistry::Children(const std::string& prefix) const {
    if (prefix.empty()) return All();
    std::vector<const SymbolDescriptor*> result;
    const std::string dotted = prefix + ".";
    for (const auto& [id, symbol] : m_Symbols)
        if (id == prefix || id.rfind(dotted, 0) == 0) result.push_back(&symbol);
    return result;
}

std::vector<const SymbolDescriptor*> SymbolRegistry::All() const {
    std::vector<const SymbolDescriptor*> result;
    result.reserve(m_Symbols.size());
    for (const auto& [id, symbol] : m_Symbols) result.push_back(&symbol);
    return result;
}

} // namespace MipsyncEngine::Command
