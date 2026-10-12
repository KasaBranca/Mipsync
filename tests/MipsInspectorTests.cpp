#include "mips/Lexer.h"
#include "mips/Parser.h"
#include "mips/Compiler.h"
#include "mips/PS1Export.h"
#include "editor/EditorMipsInspector.h"
#include <imgui_internal.h>
#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <regex>
#include <filesystem>

using namespace MipsyncEngine;
using namespace MipsyncEngine::Mips;
static void Require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
static std::unique_ptr<CompiledModule> Compile(const std::string& source) {
    Lexer lexer(source); auto tokens = lexer.Tokenize();
    Require(lexer.GetErrors().empty(), "lexer failed");
    Parser parser(tokens); auto ast = parser.ParseProgram();
    if (!parser.GetErrors().empty()) throw std::runtime_error(parser.GetErrors().front());
    Require(ast->classes.size() == 1, "expected one class");
    Compiler compiler(*ast->classes[0], ast.get()); auto module = compiler.Compile();
    if (!compiler.GetErrors().empty()) throw std::runtime_error(compiler.GetErrors().front());
    Require(module != nullptr, "no compiled module"); return module;
}
static void SameRuntime(const CompiledModule& a, const CompiledModule& b) {
    Require(EncodeMbc(a) == EncodeMbc(b), "annotations changed PS1 bytecode");
    Require(a.numberConstants == b.numberConstants && a.stringConstants == b.stringConstants &&
        a.nameConstants == b.nameConstants && a.fields.size() == b.fields.size(), "annotations changed storage");
    Require(a.methods.size() == b.methods.size(), "method count changed");
    for (size_t i = 0; i < a.methods.size(); ++i)
        Require(a.methods[i].code == b.methods[i].code, "annotations changed desktop instructions");
}
static void CheckCompiler() {
    auto controls = Compile(R"(class Controls : MipsBehaviour {
        public float angle = 0;
        void Update() { angle = angle + Input.GetAxis("RightStickX") * Time.deltaTime;
            if (Input.GetButtonDown("Cross")) angle = angle + 1;
            if (Input.GetButtonUp("Circle")) angle = angle - 1;
            if (Input.GetButton("Square")) angle = angle + 2; }
    })");
    Require(controls->ps1CompatibilityErrors.empty(),"numeric pad input mistaken for string concatenation");
    auto concatenation=Compile(R"(class Invalid : MipsBehaviour {
        void Update() { Log.Info("hello" + Input.GetAxis("RightStickX")); }
    })");
    Require(!concatenation->ps1CompatibilityErrors.empty(),"real string concatenation lost diagnostics");
    const std::string source = R"(class Demo : MipsBehaviour {
        [Header("Movement"), Tooltip("Speed in m/s"), Range(-2, 10), Min(0), InspectorName("Move Speed"), Order(-2)]
        public float speed = 3;
        [Space] [ReadOnly, Order(4)] public int counter = 1;
        [HideInInspector] public bool internalFlag = false;
        [HeaderAttribute("Position"), Space(12), Order(-2)] public Vector3 position;
        [Tooltip("Camera"), InspectorName("Follow Camera")] public Camera camera;
        public void Move() { counter = counter + 1; speed = -4; internalFlag = true; }
    })";
    auto module = Compile(source);
    auto plain = Compile(std::regex_replace(source, std::regex(R"(\[[^\]]*\])"), ""));
    SameRuntime(*module, *plain);
    Require(module->fields[0].inspector.header == "Movement" && module->fields[0].inspector.displayName == "Move Speed", "text metadata lost");
    Require(module->fields[1].inspector.readOnly && module->fields[1].inspector.space == 8, "flag/default space lost");
    Require(module->fields[3].inspector.header == "Position" && module->fields[4].inspector.header.empty(), "vector section duplicated");
    Require(InspectorFieldIndices(module->fields) == std::vector<size_t>({0, 3, 6, 1}), "hidden/order/vector grouping failed");
    Require(module->fields[0].inspector.Constrain(-10) == 0 && module->fields[0].inspector.Constrain(20) == 10, "bounds failed");
    InspectorAttributes integer; integer.hasRange = true; integer.rangeMin = .5; integer.rangeMax = 3.5;
    Require(integer.Constrain(0, true) == 1 && integer.Constrain(4, true) == 3, "integer bounds failed");
    const auto bytes = SerializePlayerModule(*module); auto restored = DeserializePlayerModule(bytes);
    SameRuntime(*module, *restored);
    Require(restored->fields[0].inspector.tooltip == "Speed in m/s" && restored->fields[2].inspector.hidden, "metadata round trip failed");
    auto legacy = nlohmann::json::from_cbor(bytes.begin() + 8, bytes.end());
    for (auto& field : legacy["fields"]) field.erase(field.begin() + 6);
    auto legacyBytes = nlohmann::json::to_cbor(legacy);
    legacyBytes.insert(legacyBytes.begin(), bytes.begin(), bytes.begin() + 8);
    auto old = DeserializePlayerModule(legacyBytes); SameRuntime(*plain, *old);
    Require(old->fields[0].inspector.header.empty(), "old player metadata default changed");
    for (const std::string& declaration : {
        "[Header(3)] public float a;", "[Range(4, 1)] public float a;",
        "[Range(0, 1)] public bool a;", "[Min(0)] public Camera a;",
        "[Unknown] public float a;", "[ReadOnly(2)] public int a;",
        "[Order(1.5)] public int a;", "[Space(-1)] public int a;",
        "[Header(\"A\"), Header(\"B\")] public float a;",
        "[Range(0, 0.9), Min(0.5)] public int a;", "[Tooltip(\"A\")] void Update() {}",
        "[Range(0,)] public int a;", "[Header(] public int a;" }) {
        Lexer lexer("class Bad : MipsBehaviour { " + declaration + " }");
        Parser parser(lexer.Tokenize()); parser.ParseProgram();
        Require(!parser.GetErrors().empty(), ("accepted bad declaration: " + declaration).c_str());
    }
    Compile("class Valid : MipsBehaviour { [Range(0, 1), Min(0.5)] public int a; }");
}
static void CheckNativeUI(float scale) {
    ImGui::CreateContext(); auto& io = ImGui::GetIO(); io.IniFilename = nullptr;
    io.DisplaySize = {700, 700}; io.DeltaTime = 1.f / 60; io.FontGlobalScale = scale;
    io.Fonts->AddFontDefault(); unsigned char* pixels; int width, height;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    InspectorAttributes metadata; metadata.header = "Movement"; metadata.space = 12; metadata.readOnly = true;
    bool value = false;
    ImGui::NewFrame(); ImGui::Begin("Inspector");
    const float before = ImGui::GetCursorPosY();
    {
        MipsInspectorFieldScope scope(0, metadata);
        Require((ImGui::GetCurrentContext()->CurrentItemFlags & ImGuiItemFlags_Disabled) != 0, "read-only widget enabled");
        Require(ImGui::GetCursorPosY() > before + 12 * scale, "heading/spacing missing");
        ImGui::Checkbox("Runtime value", &value);
    }
    Require((ImGui::GetCurrentContext()->CurrentItemFlags & ImGuiItemFlags_Disabled) == 0, "disabled state leaked");
    ImGui::BeginDisabled();
    { MipsInspectorFieldScope scope(1, metadata); ImGui::TextUnformatted("Runtime value"); }
    Require((ImGui::GetCurrentContext()->CurrentItemFlags & ImGuiItemFlags_Disabled) != 0, "parent disabled state lost");
    ImGui::EndDisabled(); ImGui::End(); ImGui::Render(); ImGui::DestroyContext();
}
static void CheckReadOnlyInput() {
    ImGui::CreateContext(); auto& io = ImGui::GetIO(); io.IniFilename = nullptr;
    io.DisplaySize = {700, 700}; io.DeltaTime = 1.f / 60;
    io.Fonts->AddFontDefault(); unsigned char* pixels; int width, height;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    InspectorAttributes attr; attr.header = "Runtime"; attr.readOnly = true; attr.tooltip = "Current script state";
    bool value = false; ImVec2 point;
    auto frame = [&] {
        ImGui::NewFrame(); ImGui::SetNextWindowPos({20, 20}); ImGui::SetNextWindowSize({400, 450});
        ImGui::Begin("Inspector");
        { MipsInspectorFieldScope scope(0, attr); ImGui::Checkbox("Runtime value", &value);
          const auto rect = ImGui::GetCurrentContext()->LastItemData.Rect;
          point = {rect.Min.x + 7, rect.GetCenter().y}; }
        ImGui::End(); ImGui::Render();
    };
    frame(); frame();
    auto click = [&] {
        io.AddMousePosEvent(point.x, point.y); frame();
        io.AddMouseButtonEvent(0, true); frame();
        io.AddMouseButtonEvent(0, false); frame();
    };
    click(); Require(!value, "ReadOnly accepted mouse input");
    bool tooltipVisible = false;
    for (auto* window : ImGui::GetCurrentContext()->Windows)
        if (window->Active && (window->Flags & ImGuiWindowFlags_Tooltip)) tooltipVisible = true;
    Require(tooltipVisible, "ReadOnly tooltip missing");
    attr.readOnly = false; frame(); click(); Require(value, "editable field did not accept mouse input");
    ImGui::DestroyContext();
}
int main(int argc, char** argv) try {
    CheckCompiler(); CheckNativeUI(1); CheckNativeUI(1.5f); CheckReadOnlyInput();
    // Optional staged project sources: verify attributes have no effect on either runtime.
    for (int i = 1; i < argc; ++i) {
        std::ifstream stream(argv[i]); std::string source((std::istreambuf_iterator<char>(stream)), {});
        Require(!source.empty(), "could not read staged script");
        auto annotated = Compile(source);
        // Field annotations occupy their own lines; leave array literals intact.
        auto stripped = std::regex_replace(source, std::regex(R"((^|\n)[ \t]*\[[^\r\n]*\][ \t]*(?=\r?\n))"), "$1");
        auto plain = Compile(stripped); SameRuntime(*annotated, *plain);
        auto originalPath = std::filesystem::path(argv[i]).parent_path() / "original" / std::filesystem::path(argv[i]).filename();
        std::ifstream originalStream(originalPath);
        if (originalStream) {
            std::string original((std::istreambuf_iterator<char>(originalStream)), {});
            auto before = Compile(original); SameRuntime(*annotated, *before);
        }
    }
    std::cout << "Inspector attributes: syntax, diagnostics, metadata, legacy players, stable field indices, native scopes and unchanged PC/PS1 bytecode passed.\n";
    return 0;
} catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
