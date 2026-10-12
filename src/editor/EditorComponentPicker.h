#pragma once

#include <functional>
#include <string>
#include <vector>

namespace MipsyncEngine {

class EditorComponentPicker {
public:
    void Open(const char* popupId);
    bool Begin(const char* popupId);
    bool BeginCategory(const char* label);
    void EndCategory();
    void Add(const char* label, std::function<void()> action, bool enabled = true,
             const std::string& detail = {});
    void AddCreateScript();
    void Draw(const std::function<void(const char*)>& drawCreateScript);
    void End();

private:
    struct Entry {
        std::string category, label, detail;
        std::function<void()> action;
        bool enabled = true;
        bool createScript = false;
    };
    char m_Search[192]{};
    std::string m_Path;
    std::vector<std::string> m_CategoryStack;
    std::vector<Entry> m_Entries;
    bool m_FocusSearch = false;
    bool m_CreateScript = false;
    bool m_InitializeScript = false;
    bool m_AdjustPosition = false;
    float m_PopupX = 0, m_PopupY = 0;
    float m_AnchorY = 0, m_PopupHeight = 0;
    int m_OpenFrame = -1;
};

} // namespace MipsyncEngine
