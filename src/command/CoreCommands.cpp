#include "CoreCommands.h"

#include "CommandRegistry.h"
#include "IpcTransport.h"
#include "SymbolRegistry.h"
#include "../mips/Bytecode.h"
#include "../mips/Compiler.h"
#include "../mips/Lexer.h"
#include "../mips/MipsSpec.h"
#include "../mips/Parser.h"
#include "../project/Project.h"
#include "EngineVersion.h"

#include <filesystem>
#include <fstream>
#include <regex>
#include <set>
#include <sstream>

namespace MipsyncEngine::Command {
namespace {

ParameterDescriptor Param(std::string name, ValueType type, bool required,
                          std::string summary, Json defaultValue = nullptr) {
    return {std::move(name), type, required, std::move(summary), std::move(defaultValue)};
}

Diagnostic ParseCompilerDiagnostic(const std::string& raw, size_t index) {
    static const std::regex located(R"(^(.*)\((\d+),(\d+)\):\s*(.*)$)");
    std::smatch match;
    Diagnostic diagnostic;
    diagnostic.code = "MIPS" + std::to_string(1000 + index);
    diagnostic.severity = "error";
    diagnostic.message = raw;
    if (std::regex_match(raw, match, located)) {
        diagnostic.location = {
            {"file", match[1].str()},
            {"line", std::stoi(match[2].str())},
            {"column", std::stoi(match[3].str())},
        };
        diagnostic.message = match[4].str();
    }
    return diagnostic;
}

std::filesystem::path ResolvePath(const CommandRequest& request, const CommandContext& context,
                                  const std::string& input) {
    std::filesystem::path path(input);
    if (path.is_absolute()) return path;
    const std::string project = !request.projectPath.empty() ? request.projectPath : context.projectPath;
    return project.empty() ? std::filesystem::absolute(path) : std::filesystem::path(project) / path;
}

CommandResult CompileLanguage(const CommandRequest& request, CommandContext& context) {
    const auto path = ResolvePath(request, context, request.arguments.at("file").get<std::string>());
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) return CommandResult::Fail("MIPS1001", "Unable to open Mips# source: " + path.string());
    std::ostringstream source;
    source << file.rdbuf();

    std::vector<std::string> errors;
    Mips::Lexer lexer(source.str(), path.string());
    const auto& tokens = lexer.Tokenize();
    errors.insert(errors.end(), lexer.GetErrors().begin(), lexer.GetErrors().end());
    std::unique_ptr<Mips::Ast::Program> program;
    if (errors.empty()) {
        Mips::Parser parser(tokens, path.string());
        program = parser.ParseProgram();
        errors.insert(errors.end(), parser.GetErrors().begin(), parser.GetErrors().end());
    }

    Json modules = Json::array();
    if (program && errors.empty()) {
        for (const auto& classDecl : program->classes) {
            std::vector<std::string> classErrors;
            auto module = Mips::CompileClass(*classDecl, program.get(), path.string(), classErrors);
            errors.insert(errors.end(), classErrors.begin(), classErrors.end());
            if (!module) continue;
            size_t bytecodeBytes = 0;
            for (const auto& method : module->methods) bytecodeBytes += method.code.size();
            modules.push_back({
                {"class", module->className},
                {"fields", module->fields.size()},
                {"methods", module->methods.size()},
                {"bytecodeBytes", bytecodeBytes},
                {"ps1Compatible", module->ps1CompatibilityErrors.empty()},
                {"ps1Diagnostics", module->ps1CompatibilityErrors},
            });
        }
    }
    if (!errors.empty()) {
        CommandResult result = CommandResult::Fail("MIPS1000", "Mips# compilation failed.");
        result.diagnostics.clear();
        for (size_t i = 0; i < errors.size(); ++i)
            result.diagnostics.push_back(ParseCompilerDiagnostic(errors[i], i));
        return result;
    }
    if (!program || program->classes.empty())
        return CommandResult::Fail("MIPS1002", "Source contains no Mips# class.");
    return CommandResult::Ok({
        {"file", path.string()},
        {"languageVersion", Mips::kLanguageVersion},
        {"modules", std::move(modules)},
    });
}

CommandResult ForwardEditor(const CommandRequest& request, CommandContext& context) {
    if (!context.editor)
        return CommandResult::Fail("MIPSYNC_EDITOR_REQUIRED", request.command + " requires a running Mipsync Editor.");
    return context.editor->ExecuteEditorCommand(request);
}

void RegisterLanguageSymbols(SymbolRegistry& symbols) {
    const std::string version = Mips::kLanguageVersion;
    auto add = [&](std::string id, std::string kind, std::string summary, std::string signature,
                   std::vector<std::string> examples = {}, std::vector<std::string> related = {}) {
        symbols.Register({id, kind, id.substr(id.find_last_of('.') + 1), summary, summary,
                          signature, {}, {}, std::move(examples), std::move(related),
                          "Mips# binding definitions", version});
    };
    add("language", "namespace", "Mips# language and engine bindings.", "");
    add("language.lifecycle", "namespace", "Lifecycle callbacks invoked by the engine.", "");
    add("language.lifecycle.Awake", "method", "Runs when a script instance is initialized.", "void Awake()");
    add("language.lifecycle.Start", "method", "Runs before the first gameplay update.", "void Start()");
    add("language.lifecycle.Update", "method", "Runs once per gameplay frame.", "void Update()", {"void Update() { transform.position.x = transform.position.x + Time.deltaTime; }"}, {"Time.deltaTime", "Transform.position"});
    add("language.lifecycle.LateUpdate", "method", "Runs after Update callbacks.", "void LateUpdate()");
    add("Transform", "type", "Entity transform component.", "Transform");
    add("Transform.position", "property", "Local position as a Vector3.", "Vector3 Transform.position", {}, {"Vector3"});
    add("Transform.rotation", "property", "Local Euler rotation in degrees.", "Vector3 Transform.rotation", {}, {"Mathf.Sin", "Mathf.Cos"});
    add("Transform.scale", "property", "Local scale as a Vector3.", "Vector3 Transform.scale");
    add("Vector3", "type", "Three-component vector value.", "Vector3(x, y, z)");
    add("Vector3.Normalize", "method", "Returns a unit-length vector.", "Vector3 Vector3.Normalize(Vector3 value)");
    add("Time.deltaTime", "property", "Scaled duration of the current frame in seconds.", "float Time.deltaTime");
    add("Input.GetKey", "method", "Returns whether a named key is held.", "bool Input.GetKey(string key)", {"Input.GetKey(\"W\")"});
    add("Input.GetKeyDown", "method", "Returns true on the frame a named key is pressed.", "bool Input.GetKeyDown(string key)");
    add("Input.GetAxis", "method", "Reads a named input axis.", "float Input.GetAxis(string axis)");
    add("Mathf.Sin", "method", "Returns the sine of a radian angle.", "float Mathf.Sin(float radians)");
    add("Mathf.Cos", "method", "Returns the cosine of a radian angle.", "float Mathf.Cos(float radians)");
    add("Mathf.Clamp", "method", "Clamps a number to an inclusive range.", "float Mathf.Clamp(float value, float min, float max)");
    add("Physics.Move", "method", "Moves the current character using desired velocity.", "void Physics.Move(float x, float y, float z)");
    add("Physics.IsGrounded", "method", "Returns whether the current character is grounded.", "bool Physics.IsGrounded()");
    add("Physics.IsGroundedWithin", "method", "Returns whether walkable collision is within a downward distance.", "bool Physics.IsGroundedWithin(float distance)");
    add("Physics.UseCharacterController", "method", "Configures the current entity for sliding character physics.", "void Physics.UseCharacterController()");
    add("Physics.UseKinematicController", "method", "Configures the current entity for script-driven kinematic movement.", "void Physics.UseKinematicController()");
    add("Physics.Raycast", "method", "Returns the hit entity ID or zero.", "int Physics.Raycast(float ox, float oy, float oz, float dx, float dy, float dz, float maxDistance)");
    add("Animator.SetFloat", "method", "Sets an Animator float parameter.", "void Animator.SetFloat(string name, float value)");
    add("Animator.SetTriggerHeld", "method", "Starts a trigger and holds its Exit Time pose until released.", "void Animator.SetTriggerHeld(string name)");
    add("Animator.ReleaseTrigger", "method", "Releases a held Animator trigger into its authored transition.", "void Animator.ReleaseTrigger(string name)");
    add("AudioSource.Play", "method", "Starts playback on the entity AudioSource.", "void AudioSource.Play()");
    add("Log.Info", "method", "Writes values to the engine console.", "void Log.Info(...values)");
}

} // namespace

void RegisterCoreCommands(CommandRegistry& commands, SymbolRegistry& symbols) {
    commands.Register({
        "help", "List commands or inspect a command namespace.",
        "Returns command metadata from the shared Command Registry.",
        {Param("path", ValueType::String, false, "Optional command namespace.")}, "command_list",
        ExecutionMode::Local, {SideEffect::ReadOnly}, {"mipsync help", "mipsync help entity"},
        [](const CommandRequest& request, CommandContext& context) {
            const std::string path = request.arguments.value("path", std::string{});
            Json items = Json::array();
            for (const auto* descriptor : context.commands->Under(path))
                items.push_back(descriptor->ToJson(false));
            if (items.empty()) return CommandResult::Fail("MIPSYNC_HELP_NOT_FOUND", "No commands under: " + path);
            return CommandResult::Ok({{"commands", std::move(items)}});
        }});
    commands.Register({
        "capabilities", "Describe this command endpoint.",
        "Reports protocol, engine version and supported platform features.", {}, "capabilities",
        ExecutionMode::Local, {SideEffect::ReadOnly}, {"mipsync capabilities --json"},
        [](const CommandRequest&, CommandContext& context) {
            return CommandResult::Ok({
                {"protocolVersion", kProtocolVersion}, {"engineVersion", context.engineVersion},
                {"features", {"commands", "symbols", "structured_diagnostics", "editor_ipc", "human_output", "json_output"}},
            });
        }});
    commands.Register({
        "instances", "List running Mipsync Editor instances.",
        "Reads the local instance registry used for project-aware IPC routing.", {}, "editor_instance_list",
        ExecutionMode::Local, {SideEffect::ReadOnly}, {"mipsync instances", "mipsync instances --json"},
        [](const CommandRequest&, CommandContext&) {
            Json result = Json::array();
            for (const auto& instance : InstanceRegistry::List()) {
                if (IpcClient::IsAvailable(instance)) result.push_back(instance.ToJson());
            }
            return CommandResult::Ok({{"instances", std::move(result)}});
        }});
    commands.Register({
        "search", "Search commands and Mips# symbols.",
        "Use this when you know the goal but not the API name.",
        {Param("query", ValueType::String, true, "Natural language or symbol query."),
         Param("limit", ValueType::Integer, false, "Maximum results.", 20)}, "symbol_search_results",
        ExecutionMode::Local, {SideEffect::ReadOnly}, {"mipsync search \"move object every frame\""},
        [](const CommandRequest& request, CommandContext& context) {
            const auto query = request.arguments.at("query").get<std::string>();
            const auto limit = static_cast<size_t>(request.arguments.value("limit", 20));
            Json matches = Json::array();
            for (const auto* symbol : context.symbols->Search(query, limit)) matches.push_back(symbol->ToJson(false));
            return CommandResult::Ok({{"query", query}, {"matches", std::move(matches)}});
        }});
    commands.Register({
        "describe", "Describe a command, namespace or Mips# symbol.",
        "Supports recursive discovery from root to a specific symbol.",
        {Param("symbol", ValueType::String, false, "Symbol or namespace to describe.")}, "symbol_description",
        ExecutionMode::Local, {SideEffect::ReadOnly}, {"mipsync describe", "mipsync describe Transform.position"},
        [](const CommandRequest& request, CommandContext& context) {
            const std::string id = request.arguments.value("symbol", std::string{});
            if (!id.empty()) {
                if (const auto* command = context.commands->Find(id))
                    return CommandResult::Ok({{"kind", "command"}, {"descriptor", command->ToJson()}});
                if (const auto* symbol = context.symbols->Find(id); symbol && symbol->kind != "namespace")
                    return CommandResult::Ok({{"kind", symbol->kind}, {"descriptor", symbol->ToJson()}});
            }
            Json children = Json::array();
            std::set<std::string> emitted;
            const std::string prefix = id.empty() ? std::string{} : id + ".";
            auto emit = [&](const std::string& fullId, const std::string& kind,
                            const std::string& name, const std::string& summary) {
                if (!id.empty() && fullId == id) return;
                if (!prefix.empty() && fullId.rfind(prefix, 0) != 0) return;
                const std::string remainder = prefix.empty() ? fullId : fullId.substr(prefix.size());
                const size_t dot = remainder.find('.');
                if (dot != std::string::npos) {
                    const std::string childId = prefix + remainder.substr(0, dot);
                    if (emitted.insert(childId).second)
                        children.push_back({{"id", childId}, {"kind", "namespace"},
                                            {"name", remainder.substr(0, dot)}, {"summary", "Explore " + childId + "."}});
                } else if (emitted.insert(fullId).second) {
                    children.push_back({{"id", fullId}, {"kind", kind}, {"name", name}, {"summary", summary}});
                }
            };
            for (const auto* symbol : context.symbols->All())
                emit(symbol->id, symbol->kind, symbol->name, symbol->summary);
            for (const auto* command : context.commands->All())
                emit(command->id, "command", command->id, command->summary);
            if (children.empty()) return CommandResult::Fail("MIPSYNC_SYMBOL_NOT_FOUND", "Unknown symbol: " + id);
            return CommandResult::Ok({{"namespace", id.empty() ? "root" : id}, {"children", std::move(children)}});
        }});
    commands.Register({
        "project.inspect", "Inspect a Mipsync project.",
        "Loads project metadata through the existing Project service.",
        {Param("path", ValueType::Path, false, "Project directory; defaults to --project or current project.")}, "project",
        ExecutionMode::Local, {SideEffect::ReadOnly}, {"mipsync project inspect", "mipsync --project ./Game project inspect --json"},
        [](const CommandRequest& request, CommandContext& context) {
            std::string path = request.arguments.value("path", std::string{});
            if (path.empty()) path = !request.projectPath.empty() ? request.projectPath : context.projectPath;
            if (path.empty()) return CommandResult::Fail("MIPSYNC_PROJECT_REQUIRED", "No Mipsync project was found. Use --project <path>.");
            ProjectInfo info;
            std::string error;
            if (!Project::LoadFromDir(path, info, error)) return CommandResult::Fail("MIPSYNC_PROJECT_LOAD", error);
            return CommandResult::Ok({
                {"name", info.name}, {"path", info.path}, {"engineVersion", info.engineVersion},
                {"defaultScene", info.defaultScene}, {"editorLastScene", info.editorLastScene},
                {"playerSettings", {{"productName", info.player.productName}, {"companyName", info.player.companyName},
                    {"startupSceneIndex", info.player.startupSceneIndex}, {"scenesInBuild", info.player.scenesInBuild}}},
            });
        }});
    commands.Register({
        "language.compile", "Compile and validate a Mips# source file.",
        "Runs the real Mips# lexer, parser and bytecode compiler and returns structured diagnostics.",
        {Param("file", ValueType::Path, true, "Mips# source path.")}, "compile_result",
        ExecutionMode::Local, {SideEffect::ReadOnly}, {"mipsync language compile assets/scripts/Player.mips --json"},
        CompileLanguage});

    const std::vector<CommandDescriptor> editorCommands = {
        {"scene.inspect", "Inspect the current Editor scene.", "Returns scene path, state and entity count.", {}, "scene", ExecutionMode::Editor, {SideEffect::ReadOnly}, {"mipsync scene inspect"}, ForwardEditor},
        {"scene.get-json", "Return the complete serialized scene.", "Exposes the same canonical JSON document used by scene files and Undo snapshots.", {}, "scene_json", ExecutionMode::Editor, {SideEffect::ReadOnly}, {"mipsync scene get-json --json"}, ForwardEditor},
        {"scene.patch", "Apply an RFC 6902 JSON Patch to the scene.", "Provides an escape hatch for every serialized component property while preserving validation and Editor Undo.",
            {Param("patch", ValueType::Json, true, "RFC 6902 JSON Patch array."),
             Param("confirm", ValueType::Boolean, false, "Must be true because a patch can alter the whole scene.", false)},
            "scene_patch_result", ExecutionMode::Editor, {SideEffect::EditorMutation, SideEffect::Dangerous},
            {"mipsync scene patch '[{\"op\":\"replace\",\"path\":\"/entities/0/active\",\"value\":false}]' --confirm true"}, ForwardEditor},
        {"scene.save", "Save the current Editor scene.", "Writes the active scene through the existing Editor save service.", {}, "scene_save_result", ExecutionMode::Editor, {SideEffect::ProjectMutation, SideEffect::FilesystemWrite}, {"mipsync scene save"}, ForwardEditor},
        {"editor.undo", "Undo the latest Editor mutation.", "Uses the Editor's normal scene snapshot history.", {}, "history_result", ExecutionMode::Editor, {SideEffect::EditorMutation}, {"mipsync editor undo"}, ForwardEditor},
        {"editor.redo", "Redo the latest undone Editor mutation.", "Uses the Editor's normal scene snapshot history.", {}, "history_result", ExecutionMode::Editor, {SideEffect::EditorMutation}, {"mipsync editor redo"}, ForwardEditor},
        {"entity.list", "List entities in the current scene.", "Returns stable entity IDs, hierarchy and component names.", {}, "entity_list", ExecutionMode::Editor, {SideEffect::ReadOnly}, {"mipsync entity list --json"}, ForwardEditor},
        {"entity.inspect", "Inspect an entity by ID or name.", "Returns transform, hierarchy and component information.", {Param("entity", ValueType::String, true, "Entity ID or exact name.")}, "entity", ExecutionMode::Editor, {SideEffect::ReadOnly}, {"mipsync entity inspect Player"}, ForwardEditor},
        {"entity.select", "Select and reveal an entity in the Editor.", "Selects an entity in Hierarchy and Inspector, switches to Scene View and frames it.", {Param("entity", ValueType::String, true, "Entity ID or exact name.")}, "entity", ExecutionMode::Editor, {SideEffect::EditorMutation}, {"mipsync entity select Player"}, ForwardEditor},
        {"entity.set", "Edit common entity properties.", "Renames an entity or changes active, static, Tag and Layer metadata.",
            {Param("entity", ValueType::String, true, "Entity ID or exact name."),
             Param("name", ValueType::String, false, "New entity name."),
             Param("active", ValueType::Boolean, false, "Whether the entity is active."),
             Param("static", ValueType::Boolean, false, "Whether the entity is static."),
             Param("tag", ValueType::String, false, "Editor Tag."),
             Param("layer", ValueType::String, false, "Editor Layer.")},
            "entity", ExecutionMode::Editor, {SideEffect::EditorMutation},
            {"mipsync entity set Player --name Hero --tag Player --static true"}, ForwardEditor},
        {"entity.duplicate", "Duplicate an entity hierarchy.", "Deep-clones the selected entity and all children with new IDs.",
            {Param("entity", ValueType::String, true, "Entity ID or exact name."),
             Param("name", ValueType::String, false, "Optional name for the duplicate root.")},
            "entity", ExecutionMode::Editor, {SideEffect::EditorMutation},
            {"mipsync entity duplicate Crate --name CrateCopy"}, ForwardEditor},
        {"entity.create", "Create an entity in the current scene.", "Creates an empty entity or primitive and optionally parents and positions it.",
            {Param("name", ValueType::String, true, "Entity name."),
             Param("primitive", ValueType::String, false, "empty, cube, sphere or plane.", "empty"),
             Param("x", ValueType::Number, false, "Local position X.", 0.0),
             Param("y", ValueType::Number, false, "Local position Y.", 0.0),
             Param("z", ValueType::Number, false, "Local position Z.", 0.0),
             Param("rx", ValueType::Number, false, "Local rotation X in degrees.", 0.0),
             Param("ry", ValueType::Number, false, "Local rotation Y in degrees.", 0.0),
             Param("rz", ValueType::Number, false, "Local rotation Z in degrees.", 0.0),
             Param("sx", ValueType::Number, false, "Local scale X.", 1.0),
             Param("sy", ValueType::Number, false, "Local scale Y.", 1.0),
             Param("sz", ValueType::Number, false, "Local scale Z.", 1.0),
             Param("material", ValueType::Path, false, "Optional project-relative .nmat material."),
             Param("parent", ValueType::String, false, "Optional parent entity ID or name.")},
            "entity", ExecutionMode::Editor, {SideEffect::EditorMutation},
            {"mipsync entity create Crate --primitive cube --x 2 --y 1 --z -3 --sx 2 --material assets/materials/Crate.nmat"}, ForwardEditor},
        {"entity.transform", "Set an entity transform.", "Updates any supplied local position, rotation or scale axes.",
            {Param("entity", ValueType::String, true, "Entity ID or exact name."),
             Param("x", ValueType::Number, false, "Local position X."), Param("y", ValueType::Number, false, "Local position Y."), Param("z", ValueType::Number, false, "Local position Z."),
             Param("rx", ValueType::Number, false, "Local rotation X in degrees."), Param("ry", ValueType::Number, false, "Local rotation Y in degrees."), Param("rz", ValueType::Number, false, "Local rotation Z in degrees."),
             Param("sx", ValueType::Number, false, "Local scale X."), Param("sy", ValueType::Number, false, "Local scale Y."), Param("sz", ValueType::Number, false, "Local scale Z.")},
            "entity", ExecutionMode::Editor, {SideEffect::EditorMutation},
            {"mipsync entity transform Crate --x 4 --y 0.5 --ry 45"}, ForwardEditor},
        {"entity.set-parent", "Set or clear an entity parent.", "Reparents an entity; use none or 0 to move it to the scene root.",
            {Param("entity", ValueType::String, true, "Entity ID or exact name."),
             Param("parent", ValueType::String, true, "Parent ID/name, or none/0.")},
            "entity", ExecutionMode::Editor, {SideEffect::EditorMutation},
            {"mipsync entity set-parent Crate Environment", "mipsync entity set-parent Crate none"}, ForwardEditor},
        {"entity.delete", "Delete an entity and its children.", "Deletes an entity hierarchy. Explicit confirmation is required and the operation is undoable.",
            {Param("entity", ValueType::String, true, "Entity ID or exact name."),
             Param("confirm", ValueType::Boolean, false, "Must be true to delete.", false)},
            "delete_result", ExecutionMode::Editor, {SideEffect::EditorMutation, SideEffect::Dangerous},
            {"mipsync entity delete Crate --confirm true"}, ForwardEditor},
        {"component.add", "Add a component to an entity.", "Adds a supported Editor component using its normal defaults.",
            {Param("entity", ValueType::String, true, "Entity ID or exact name."),
             Param("component", ValueType::String, true, "Component type name.")},
            "entity", ExecutionMode::Editor, {SideEffect::EditorMutation},
            {"mipsync component add Player Rigidbody"}, ForwardEditor},
        {"component.remove", "Remove a component from an entity.", "Removes a supported component. Transform and Tag cannot be removed.",
            {Param("entity", ValueType::String, true, "Entity ID or exact name."),
             Param("component", ValueType::String, true, "Component type name."),
             Param("confirm", ValueType::Boolean, false, "Must be true to remove.", false)},
            "entity", ExecutionMode::Editor, {SideEffect::EditorMutation, SideEffect::Dangerous},
            {"mipsync component remove Player Rigidbody --confirm true"}, ForwardEditor},
        {"component.set-enabled", "Enable or disable a component.", "Changes the common enabled flag on a supported component.",
            {Param("entity", ValueType::String, true, "Entity ID or exact name."),
             Param("component", ValueType::String, true, "Component type name."),
             Param("enabled", ValueType::Boolean, true, "New enabled state.")},
            "entity", ExecutionMode::Editor, {SideEffect::EditorMutation},
            {"mipsync component set-enabled Player MeshRenderer false"}, ForwardEditor},
        {"mesh.set", "Configure an entity MeshRenderer.", "Sets primitive geometry, size, material and the PS1 render type preset.",
            {Param("entity", ValueType::String, true, "Entity ID or exact name."),
             Param("primitive", ValueType::String, false, "cube, sphere or plane."),
             Param("size", ValueType::Number, false, "Primitive source mesh size."),
             Param("material", ValueType::Path, false, "Project-relative .nmat material, or none."),
             Param("enabled", ValueType::Boolean, false, "Renderer enabled state."),
             Param("editor-only", ValueType::Boolean, false, "Only draw in Scene View."),
             Param("type-preset", ValueType::String, false,
                   "prop, corridor, character, viewmodel or floor."),
             Param("view-model", ValueType::Boolean, false,
                   "Legacy alias: true selects viewmodel, false selects prop.")},
            "entity", ExecutionMode::Editor, {SideEffect::EditorMutation},
            {"mipsync mesh set Wall --material assets/materials/Wall.nmat"}, ForwardEditor},
        {"material.create", "Create or update a color material.", "Writes a project .nmat asset using the Editor material format and refreshes scene users.",
            {Param("path", ValueType::Path, true, "Project-relative assets/*.nmat path."),
             Param("r", ValueType::Number, true, "Red channel, 0..1."),
             Param("g", ValueType::Number, true, "Green channel, 0..1."),
             Param("b", ValueType::Number, true, "Blue channel, 0..1."),
             Param("a", ValueType::Number, false, "Alpha channel, 0..1.", 1.0),
             Param("texture", ValueType::Path, false, "Optional project-relative texture path.")},
            "material", ExecutionMode::Editor, {SideEffect::ProjectMutation, SideEffect::FilesystemWrite},
            {"mipsync material create assets/materials/Brick.nmat 0.65 0.18 0.12"}, ForwardEditor},
        {"material.apply", "Apply a material to an entity.", "Loads a project .nmat asset and assigns it through AssetManager.",
            {Param("entity", ValueType::String, true, "Entity ID or exact name."),
             Param("material", ValueType::Path, true, "Project-relative .nmat path, or none.")},
            "entity", ExecutionMode::Editor, {SideEffect::EditorMutation},
            {"mipsync material apply Wall assets/materials/Brick.nmat"}, ForwardEditor},
        {"ide.open", "Open a Mips# script in the configured IDE.", "Uses the same IDE integration as the Editor button and never falls back to the .mips file association.",
            {Param("file", ValueType::Path, true, "Project-relative or absolute Mips# script path."),
             Param("line", ValueType::Integer, false, "One-based line number.", 1),
             Param("column", ValueType::Integer, false, "One-based column number.", 1)},
            "ide_open_result", ExecutionMode::Editor, {SideEffect::ExternalProcess},
            {"mipsync ide open assets/scripts/Player.mips --line 12"}, ForwardEditor},
        {"runtime.play", "Enter Editor play mode.", "Starts the current scene through the Editor application service.", {}, "runtime_state", ExecutionMode::Editor, {SideEffect::RuntimeMutation}, {"mipsync runtime play"}, ForwardEditor},
        {"runtime.stop", "Exit Editor play mode.", "Stops play mode and restores the edit scene.", {}, "runtime_state", ExecutionMode::Editor, {SideEffect::RuntimeMutation}, {"mipsync runtime stop"}, ForwardEditor},
        {"runtime.inspect", "Inspect Editor runtime state.", "Reports playing and paused state.", {}, "runtime_state", ExecutionMode::Editor, {SideEffect::ReadOnly}, {"mipsync runtime inspect"}, ForwardEditor},
    };
    for (auto descriptor : editorCommands) commands.Register(std::move(descriptor));

    RegisterLanguageSymbols(symbols);
    for (const auto* command : commands.All()) {
        symbols.Register({command->id, "command", command->id, command->summary, command->description,
                          command->id, command->parameters, command->returnType, command->examples, {},
                          "Command Registry", MIPSYNC_ENGINE_VERSION});
    }
}

} // namespace MipsyncEngine::Command
