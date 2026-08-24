#include "GameSave.h"
#include "../assets/AssetManager.h"
#include <nlohmann/json.hpp>
#include <fstream>

namespace MipsyncEngine {

struct GameSave::Data {
    nlohmann::json root = nlohmann::json::object();
};

GameSave& GameSave::Get() {
    static GameSave instance;
    return instance;
}

void GameSave::Clear() {
    if (!m_Data)
        m_Data = new Data();
    m_Data->root = nlohmann::json::object();
}

void GameSave::SetInt(const std::string& key, int value) {
    if (!m_Data)
        m_Data = new Data();
    m_Data->root["values"][key] = value;
}

int GameSave::GetInt(const std::string& key, int defaultValue) const {
    if (!m_Data || !m_Data->root.contains("values"))
        return defaultValue;
    const auto& values = m_Data->root["values"];
    if (!values.contains(key) || !values[key].is_number_integer())
        return defaultValue;
    return values[key].get<int>();
}

void GameSave::SetFloat(const std::string& key, float value) {
    if (!m_Data)
        m_Data = new Data();
    m_Data->root["values"][key] = value;
}

float GameSave::GetFloat(const std::string& key, float defaultValue) const {
    if (!m_Data || !m_Data->root.contains("values"))
        return defaultValue;
    const auto& values = m_Data->root["values"];
    if (!values.contains(key) || !values[key].is_number())
        return defaultValue;
    return values[key].get<float>();
}

void GameSave::SetBool(const std::string& key, bool value) {
    if (!m_Data)
        m_Data = new Data();
    m_Data->root["values"][key] = value;
}

bool GameSave::GetBool(const std::string& key, bool defaultValue) const {
    if (!m_Data || !m_Data->root.contains("values"))
        return defaultValue;
    const auto& values = m_Data->root["values"];
    if (!values.contains(key) || !values[key].is_boolean())
        return defaultValue;
    return values[key].get<bool>();
}

void GameSave::SetString(const std::string& key, const std::string& value) {
    if (!m_Data)
        m_Data = new Data();
    m_Data->root["values"][key] = value;
}

std::string GameSave::GetString(const std::string& key, const std::string& defaultValue) const {
    if (!m_Data || !m_Data->root.contains("values"))
        return defaultValue;
    const auto& values = m_Data->root["values"];
    if (!values.contains(key) || !values[key].is_string())
        return defaultValue;
    return values[key].get<std::string>();
}

bool GameSave::LoadFromFile(const std::string& absolutePath, std::string& outError) {
    if (!m_Data)
        m_Data = new Data();

    std::ifstream in(PathUtf8::FromString(absolutePath), std::ios::binary);
    if (!in) {
        outError = "Could not open save file: " + absolutePath;
        return false;
    }

    try {
        nlohmann::json loaded;
        in >> loaded;
        if (!loaded.is_object())
            loaded = nlohmann::json::object();
        m_Data->root = std::move(loaded);
        if (!m_Data->root.contains("values"))
            m_Data->root["values"] = nlohmann::json::object();
        return true;
    } catch (const std::exception& ex) {
        outError = ex.what();
        return false;
    }
}

bool GameSave::SaveToFile(const std::string& absolutePath, std::string& outError) {
    if (!m_Data)
        m_Data = new Data();
    if (!m_Data->root.contains("values"))
        m_Data->root["values"] = nlohmann::json::object();

    m_Data->root["version"] = 1;

    std::ofstream out(PathUtf8::FromString(absolutePath), std::ios::binary | std::ios::trunc);
    if (!out) {
        outError = "Could not write save file: " + absolutePath;
        return false;
    }

    try {
        out << m_Data->root.dump(2);
        return true;
    } catch (const std::exception& ex) {
        outError = ex.what();
        return false;
    }
}

} // namespace MipsyncEngine
