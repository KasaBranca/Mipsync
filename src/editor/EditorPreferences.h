#pragma once
#include <nlohmann/json.hpp>
#include <imgui.h>
#include <string>
#include <vector>
#include "../core/PS1PadInput.h"

namespace MipsyncEngine {
enum class PreferenceKind { Toggle, Number, Integer, Choice, Binding };
struct PreferenceDefinition {
    const char* key;
    const char* category;
    const char* label;
    PreferenceKind kind;
    double initial, minimum, maximum;
    const char* choices = nullptr;
    const char* help = nullptr;
};
const std::vector<PreferenceDefinition>& EditorPreferenceDefinitions();
const std::vector<const char*>& EditorPreferenceCategories();
class EditorPreferences {
public:
    void Load(const nlohmann::json& document, float defaultFontSize);
    nlohmann::json Save() const;
    double Get(const char* key) const;
    PS1PadMapping GetPS1PadMapping() const;
    bool Enabled(const char* key) const { return Get(key) != 0; }
    void Set(const char* key, double value);
    void ResetCategory(const char* category);
    bool Matches(const PreferenceDefinition& definition, const char* search) const;
private:
    nlohmann::json m_Document = nlohmann::json::object();
    float m_DefaultFontSize = 13;
};
class EditorPreferencesWindow {
public:
    void Open(const char* category);
    void DrawMenu();
    bool Draw(EditorPreferences& preferences);
    bool IsOpen() const { return m_Open; }
    const std::string& Category() const { return m_Category; }
private:
    bool m_Open = false, m_Focus = false;
    std::string m_Category = "General";
    char m_Search[128]{};
};
} // namespace MipsyncEngine
