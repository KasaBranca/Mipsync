#include "Material.h"
#include "AssetManager.h"
#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>

namespace MipsyncEngine {

namespace fs = std::filesystem;
using json = nlohmann::json;

bool Material::Save(const std::string& absPath, const Material& mat, std::string& outError) {
    try {
        json j;
        j["version"] = 1;
        j["color"] = json::array({ mat.color.r, mat.color.g, mat.color.b, mat.color.a });
        j["texture"] = mat.texturePath;
        j["tiling"] = json::array({ mat.mainTextureTiling.x, mat.mainTextureTiling.y });
        j["offset"] = json::array({ mat.mainTextureOffset.x, mat.mainTextureOffset.y });

        fs::path p = PathUtf8::FromString(absPath);
        if (p.has_parent_path())
            fs::create_directories(p.parent_path());

        std::ofstream out(p);
        if (!out.is_open()) {
            outError = "failed to open: " + absPath;
            return false;
        }
        out << j.dump(2);
        return true;
    } catch (const std::exception& ex) {
        outError = ex.what();
        return false;
    }
}

bool Material::Load(const std::string& absPath, Material& out, std::string& outError) {
    try {
        std::ifstream in(PathUtf8::FromString(absPath));
        if (!in.is_open()) {
            outError = "failed to open: " + absPath;
            return false;
        }
        json j;
        in >> j;
        if (j.contains("color") && j["color"].is_array() && j["color"].size() >= 4) {
            out.color.r = j["color"][0].get<float>();
            out.color.g = j["color"][1].get<float>();
            out.color.b = j["color"][2].get<float>();
            out.color.a = j["color"][3].get<float>();
        }
        out.texturePath = j.value("texture", std::string{});
        if (j.contains("tiling") && j["tiling"].is_array() && j["tiling"].size() >= 2) {
            out.mainTextureTiling.x = j["tiling"][0].get<float>();
            out.mainTextureTiling.y = j["tiling"][1].get<float>();
        } else {
            out.mainTextureTiling = { 1.0f, 1.0f };
        }
        if (j.contains("offset") && j["offset"].is_array() && j["offset"].size() >= 2) {
            out.mainTextureOffset.x = j["offset"][0].get<float>();
            out.mainTextureOffset.y = j["offset"][1].get<float>();
        } else {
            out.mainTextureOffset = { 0.0f, 0.0f };
        }
        return true;
    } catch (const std::exception& ex) {
        outError = ex.what();
        return false;
    }
}

} // namespace MipsyncEngine
