#include "renderer/Mesh.h"
#include "core/Log.h"
#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <cstdio>
#include <chrono>
#include <iomanip>

using namespace MipsyncEngine;
using json = nlohmann::json;
static void Require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
static void Check(const std::filesystem::path& path) {
    Mesh whole = Mesh::LoadFromFileCpu(path.string());
    Require(whole.GetParts().size() == 2, "import merged distinct mesh instances");
    Mesh lower = Mesh::ExtractPart(whole, 0, true);
    Mesh upper = Mesh::ExtractPart(whole, 1, true);
    Require(lower.GetIndexCount() == 3 && upper.GetIndexCount() == 3 && whole.GetIndexCount() == 6,
            "child mesh includes another mesh's triangles");
    Require(lower.GetBoundsMax().y + whole.GetParts()[0].origin.y <
            upper.GetBoundsMin().y + whole.GetParts()[1].origin.y, "child origins or instance transforms were collapsed");
    for (int part = 0; part < 2; ++part) {
        Mesh child = Mesh::ExtractPart(whole, part, true);
        const auto& range = whole.GetParts()[part];
        for (size_t i = 0; i < child.GetIndices().size(); ++i)
            Require(glm::length(child.GetVertices()[child.GetIndices()[i]].position + range.origin -
                    whole.GetVertices()[whole.GetIndices()[range.indexOffset + i]].position) < 1e-6f,
                    "extracting a part changes shared model scale or pivot");
    }
    Mesh moved = std::move(whole);
    Require(moved.GetParts().size() == 2 && upper.IsModelPart(), "move lost model-part metadata");
    Require(Mesh::ExtractPart(moved, 9, true).GetVertexCount() == 0, "invalid mesh part rendered the whole model");
}
int main(int argc, char** argv) try {
    Log::Init();
    if (argc == 2) {
        json request; std::ifstream(argv[1]) >> request;
        Mesh model = Mesh::LoadFromFileCpu(request.at("model").get<std::string>());
        const auto output = std::filesystem::path(request.at("output").get<std::string>());
        std::filesystem::create_directories(output);
        json report = {{"parts",json::array()}};
        for (size_t i=0;i<model.GetParts().size();++i) {
            Mesh part=Mesh::ExtractPart(model,static_cast<int>(i),true);
            const auto min=part.GetBoundsMin(),max=part.GetBoundsMax();
            const auto dimensions=max-min;
            const float fit=glm::max(dimensions.x,glm::max(dimensions.y,dimensions.z));
            auto file=output/("ResidentialBlock"+std::to_string(i)+".obj");
            std::ofstream obj(file); obj<<std::setprecision(9)<<"o ResidentialBlock"<<i<<'\n';
            for(const auto& v:part.GetVertices())obj<<"v "<<v.position.x<<' '<<v.position.y<<' '<<v.position.z<<'\n';
            for(const auto& v:part.GetVertices())obj<<"vt "<<v.uv.x<<' '<<v.uv.y<<'\n';
            for(const auto& v:part.GetVertices())obj<<"vn "<<v.normal.x<<' '<<v.normal.y<<' '<<v.normal.z<<'\n';
            const auto& indices=part.GetIndices();
            for(size_t t=0;t+2<indices.size();t+=3) {
                obj<<"f"; for(int k=0;k<3;++k){const auto v=indices[t+k]+1;obj<<' '<<v<<'/'<<v<<'/'<<v;}obj<<'\n';
            }
            obj.close();
            Mesh imported=Mesh::LoadOBJ(file.string(),true);
            Require(imported.GetIndexCount()==part.GetIndexCount(),"OBJ export changed triangle count");
            const auto recovered=(imported.GetBoundsMax()-imported.GetBoundsMin())*fit;
            Require(glm::length(recovered-dimensions)<1e-5f,"OBJ export changed building proportions");
            report["parts"].push_back({{"index",i},{"name",model.GetParts()[i].name},
                {"triangles",indices.size()/3},{"fit",fit},{"dimensions",{dimensions.x,dimensions.y,dimensions.z}},
                {"file",file.generic_string()}});
        }
        std::ofstream(output/"parts.json")<<report.dump(2); std::puts(report.dump(2).c_str()); return 0;
    }
    const auto directory = std::filesystem::temp_directory_path() /
        ("mipsync-mesh-parts-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directory(directory);
    struct Cleanup { std::filesystem::path path; ~Cleanup() { std::error_code ec; std::filesystem::remove_all(path, ec); } } cleanup{directory};
    std::ofstream(directory / "parts.obj") <<
        "o Lower\nv 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n"
        "o Upper\nv 0 4 0\nv 1 4 0\nv 0 5 0\nf 4 5 6\n";
    Check(directory / "parts.obj");
    std::ofstream(directory / "parts.fbx") << R"FBX(; FBX 7.4.0 project file
FBXHeaderExtension: { FBXVersion: 7400 }
Objects: {
 Geometry: 1, "Geometry::Triangle", "Mesh" {
  Vertices: *9 { a: 0,0,0,1,0,0,0,1,0 }
  PolygonVertexIndex: *3 { a: 0,1,-3 }
 }
 Model: 2, "Model::Lower", "Mesh" {
  Properties70: { P: "Lcl Translation", "Lcl Translation", "", "A",0,0,0 }
 }
 Model: 3, "Model::Upper", "Mesh" {
  Properties70: { P: "Lcl Translation", "Lcl Translation", "", "A",0,4,0 }
 }
}
Connections: {
 C: "OO",1,2
 C: "OO",1,3
 C: "OO",2,0
 C: "OO",3,0
}
)FBX";
    Check(directory / "parts.fbx");
    json model = {
        {"asset", {{"version", "2.0"}}}, {"scene", 0},
        {"scenes", json::array({{{"nodes", json::array({0, 1})}}})},
        {"nodes", json::array({{{"name", "Lower"}, {"mesh", 0}},
                               {{"name", "Upper"}, {"mesh", 0}, {"translation", json::array({0, 4, 0})}}})},
        {"meshes", json::array({{{"name", "Triangle"}, {"primitives", json::array({{{"attributes", {{"POSITION", 0}}}}})}}})},
        {"buffers", json::array({{{"byteLength", 36}}})},
        {"bufferViews", json::array({{{"buffer", 0}, {"byteLength", 36}}})},
        {"accessors", json::array({{{"bufferView", 0}, {"componentType", 5126}, {"count", 3}, {"type", "VEC3"}}})}
    };
    std::string text = model.dump(); while (text.size() % 4) text += ' ';
    const float positions[9] = {0,0,0,1,0,0,0,1,0};
    std::ofstream glb(directory / "parts.glb", std::ios::binary);
    auto word = [&](uint32_t value) { glb.write(reinterpret_cast<const char*>(&value), 4); };
    word(0x46546c67); word(2); word(static_cast<uint32_t>(28 + text.size() + sizeof(positions)));
    word(static_cast<uint32_t>(text.size())); word(0x4e4f534a); glb.write(text.data(), text.size());
    word(sizeof(positions)); word(0x004e4942); glb.write(reinterpret_cast<const char*>(positions), sizeof(positions));
    glb.close(); Check(directory / "parts.glb");
    std::puts("OBJ, FBX shared instances and GLB nodes: mesh ranges, names, shared fit/pivot and child transforms passed.");
    return 0;
} catch (const std::exception& error) { std::fprintf(stderr, "%s\n", error.what()); return 1; }
