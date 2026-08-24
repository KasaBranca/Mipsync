#pragma once

#include <string>

namespace MipsyncEngine {

/// In-memory game save data (JSON file on disk). Keys are global to the active session.
class GameSave {
public:
    static GameSave& Get();

    void Clear();

    void SetInt(const std::string& key, int value);
    int GetInt(const std::string& key, int defaultValue) const;

    void SetFloat(const std::string& key, float value);
    float GetFloat(const std::string& key, float defaultValue) const;

    void SetBool(const std::string& key, bool value);
    bool GetBool(const std::string& key, bool defaultValue) const;

    void SetString(const std::string& key, const std::string& value);
    std::string GetString(const std::string& key, const std::string& defaultValue) const;

    bool LoadFromFile(const std::string& absolutePath, std::string& outError);
    bool SaveToFile(const std::string& absolutePath, std::string& outError);

private:
    GameSave() = default;

    struct Data;
    Data* m_Data = nullptr;
};

} // namespace MipsyncEngine
