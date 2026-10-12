#include "CoreCommands.h"

#include "CommandRegistry.h"
#include "IpcTransport.h"
#include "SymbolRegistry.h"
#include "AuthoringCatalog.h"
#include "AnimationSchemas.h"
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
    std::vector<Mips::CompiledModule> compiledModules;
    if (program && errors.empty()) {
        for (const auto& classDecl : program->classes) {
            std::vector<std::string> classErrors;
            auto module = Mips::CompileClass(*classDecl, program.get(), path.string(), classErrors);
            errors.insert(errors.end(), classErrors.begin(), classErrors.end());
            if (!module) continue;
            size_t bytecodeBytes = 0;
            for (const auto& method : module->methods) bytecodeBytes += method.code.size();
            std::string ps1Error;
            Mips::ValidatePs1Target({*module}, 0, ps1Error);
            Json ps1Diagnostics=Json::array();
            if(!ps1Error.empty()) {
                std::istringstream messages(ps1Error);std::string message;
                while(std::getline(messages,message))if(message.starts_with(" - ")) {
                    message.erase(0,3);auto location=classDecl->location;
                    for(const auto& method:classDecl->methods)if(message.starts_with(module->className+"."+method->name+":"))location=method->location;
                    auto diagnostic=ParseCompilerDiagnostic(message,ps1Diagnostics.size());
                    diagnostic.code="MIPS_PS1_LIMIT";
                    if(diagnostic.location.is_null())diagnostic.location={{"file",path.string()},{"line",location.line},{"column",location.column}};
                    ps1Diagnostics.push_back(diagnostic.ToJson());
                }
            }
            Json methods=Json::array();
            for(const auto& m:module->methods) methods.push_back({{"name",m.name},{"bytecodeBytes",m.code.size()},
                {"locals",m.localCount},{"parameters",m.parameterCount}});
            modules.push_back({
                {"class", module->className},
                {"fields", module->fields.size()},
                {"methods", module->methods.size()},
                {"bytecodeBytes", bytecodeBytes},
                {"numberConstants",module->numberConstants.size()}, {"methodDetails",methods},
                {"ps1Compatible", ps1Diagnostics.empty()},
                {"ps1Diagnostics", ps1Diagnostics},
            });
            compiledModules.push_back(*module);
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
    std::string ps1Error;
    const bool compatible=Mips::ValidatePs1Target(compiledModules,0,ps1Error);
    auto result=CommandResult::Ok({
        {"file", path.string()},
        {"languageVersion", Mips::kLanguageVersion},
        {"modules", std::move(modules)},
        {"ps1Compatible",compatible},{"ps1Diagnostics",ps1Error.empty()?Json::array():Json::array({ps1Error})},
        {"limits",Ps1Limits()},
    });
    const auto target=request.arguments.value("target",std::string("pc"));
    if(target!="pc"&&target!="ps1")return CommandResult::Fail("MIPSYNC_TARGET","target must be pc or ps1.");
    if(target=="ps1"&&!compatible){result.success=false;result.status="failed";result.diagnostics.push_back({"MIPS_PS1_COMPATIBILITY","error",ps1Error,{{"file",path.string()},{"line",1},{"column",1}}});}
    return result;
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
    add("Input.GetAxis", "method", "PS1 sticks: LeftStickX/Y and RightStickX/Y, -1 to 1; right/up are positive. Horizontal/Vertical alias the left stick.", "float Input.GetAxis(string axis)", {"Input.GetAxis(\"RightStickX\")"});
    add("Input.GetButton", "method", "PS1 button held: Cross, Circle, Square, Triangle, Start, Select, L1/R1/L2/R2/L3/R3, DPadUp/Down/Left/Right. Editor mappings are in Preferences > PS1 Controller.", "bool Input.GetButton(string button)", {"Input.GetButton(\"Square\")"});
    add("Input.GetButtonDown", "method", "PS1 button press edge, buffered between P-Sync ticks.", "bool Input.GetButtonDown(string button)", {"Input.GetButtonDown(\"Cross\")"});
    add("Input.GetButtonUp", "method", "PS1 button release edge.", "bool Input.GetButtonUp(string button)");
    add("Scene.Load", "method", "Loads a scene after the current frame. PS1 destinations must be in Scenes In Build.", "void Scene.Load(string projectRelativePath)");
    add("Scene.LoadBuildIndex", "method", "Loads a scene by its zero-based index in Scenes In Build after the current frame.", "void Scene.LoadBuildIndex(int buildIndex)");
    add("UI.SetTextVisible", "method", "Shows or hides the UIText component on an entity by ID. Available on PC and PS1.", "bool UI.SetTextVisible(int entityId, bool visible)");
    add("UI.SetImageVisible", "method", "Shows or hides the UIImage component on an entity by ID. Available on PC and PS1.", "bool UI.SetImageVisible(int entityId, bool visible)");
    add("UI.SetText", "method", "Updates text on PC/PS1. Maximum 95 UTF-8 bytes; PS1 supports exported glyphs and built-in ASCII digits/letters. Returns false for missing target or exhausted dynamic UI capacity.", "bool UI.SetText(int entityId, string text)",{"UI.SetText(12, \"READY\")"});
    add("UI.SetTextNumber", "method", "Displays an integer without stacking digit entities. Truncates toward zero, range -32768..32767 on PC/PS1.", "bool UI.SetTextNumber(int entityId, float number)",{"UI.SetTextNumber(12, score)"});
    add("UI.SetColor", "method", "Updates text/image RGBA on PC/PS1; channels are clamped to 0..1.", "bool UI.SetColor(int entityId, float r, float g, float b, float a)");
    add("Mathf.Sin", "method", "Returns the sine of a radian angle.", "float Mathf.Sin(float radians)");
    add("Mathf.Cos", "method", "Returns the cosine of a radian angle.", "float Mathf.Cos(float radians)");
    add("Mathf.Clamp", "method", "Clamps a number to an inclusive range.", "float Mathf.Clamp(float value, float min, float max)");
    add("Physics.Move", "method", "Moves the current character using desired velocity.", "void Physics.Move(float x, float y, float z)");
    add("Physics.IsGrounded", "method", "Returns whether the current character is grounded.", "bool Physics.IsGrounded()");
    add("Physics.IsGroundedWithin", "method", "Returns whether walkable collision is within a downward distance.", "bool Physics.IsGroundedWithin(float distance)");
    add("Physics.UseCharacterController", "method", "Configures the current entity for sliding character physics.", "void Physics.UseCharacterController()");
    add("Physics.UseKinematicController", "method", "Configures the current entity for script-driven kinematic movement.", "void Physics.UseKinematicController()");
    add("Physics.Raycast", "method", "Returns the hit entity ID or zero.", "int Physics.Raycast(float ox, float oy, float oz, float dx, float dy, float dz, float maxDistance)");
    add("Animator.GetNormalizedTime", "method", "Reads the visible animation clip phase; -1 for a missing or inactive state.", "float Animator.GetNormalizedTime(string stateName = \"\")");
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
                {"ps1Limits",Ps1Limits()},{"components",ComponentCatalog()},
                {"animation",{{"modelMetadata",true},{"controllerAuthoring",true},{"controllerValidation",true},{"runtimeInspection",true},{"boneRegionExtraction",true},{"layers",false},{"boneMasks",false},{"ps1Profile","idle/walk/aim plus one action; extraction supports independent animated regions"}}},
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
         Param("limit", ValueType::Integer, false, "Maximum results.", 20),
         Param("kind",ValueType::String,false,"Optional command/method/property/type/namespace filter."),
         Param("namespace",ValueType::String,false,"Optional symbol namespace prefix.")}, "symbol_search_results",
        ExecutionMode::Local, {SideEffect::ReadOnly}, {"mipsync search \"move object every frame\""},
        [](const CommandRequest& request, CommandContext& context) {
            const auto query = request.arguments.at("query").get<std::string>();
            const auto limit = static_cast<size_t>(request.arguments.value("limit", 20));
            Json matches = Json::array();
            const auto kind=request.arguments.value("kind",std::string{}),prefix=request.arguments.value("namespace",std::string{});
            if(limit<1||limit>1000)return CommandResult::Fail("MIPSYNC_SEARCH_RANGE","limit must be 1..1000.");
            for (const auto* symbol : context.symbols->Search(query, 10000)) {
                if(!kind.empty()&&symbol->kind!=kind)continue;
                if(!prefix.empty()&&symbol->id!=prefix&&!symbol->id.starts_with(prefix+"."))continue;
                matches.push_back(symbol->ToJson(false));if(matches.size()>=limit)break;
            }
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
                if (const auto* command = context.commands->Find(id)) {
                    Json descriptor=command->ToJson();
                    if(id=="component.add")descriptor["supportedComponents"]=ComponentCatalog();
                    if(id=="build.start"||id=="language.compile"||id=="language.validate")descriptor["ps1Limits"]=Ps1Limits();
                    return CommandResult::Ok({{"kind", "command"}, {"descriptor",descriptor}});
                }
                if (const auto* symbol = context.symbols->Find(id); symbol && symbol->kind != "namespace") {
                    Json properties=Json::array();
                    for(const auto* child:context.symbols->Children(id))properties.push_back(child->ToJson());
                    return CommandResult::Ok({{"kind", symbol->kind}, {"descriptor", symbol->ToJson()}, {"properties",properties}});
                }
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
        {Param("file", ValueType::Path, true, "Mips# source path."),Param("target",ValueType::String,false,"pc or ps1. PS1 incompatibility produces a failing exit status.","pc")}, "compile_result",
        ExecutionMode::Local, {SideEffect::ReadOnly}, {"mipsync language compile assets/scripts/Player.mips --json"},
        CompileLanguage});

    const std::vector<CommandDescriptor> editorCommands = {
        {"scene.inspect", "Inspect the current Editor scene.", "Returns scene path, state and entity count.", {}, "scene", ExecutionMode::Editor, {SideEffect::ReadOnly}, {"mipsync scene inspect"}, ForwardEditor},
        {"scene.open", "Open another scene in the current project.", "Loads a .nscene in Edit Mode. Unsaved changes block opening unless save-current is requested. Invalid destinations preserve the current scene.",
            {Param("path", ValueType::Path, true, "Project-relative or absolute .nscene path inside the current project."),
             Param("save-current", ValueType::Boolean, false, "Save unsaved changes before opening the destination.", false)},
            "scene_open_result", ExecutionMode::Editor, {SideEffect::EditorMutation, SideEffect::ProjectMutation, SideEffect::FilesystemWrite},
            {"mipsync scene open scenes/Title.nscene --json", "mipsync scene open scenes/Game.nscene --save-current true"}, ForwardEditor},
        {"scene.get-json", "Return the complete serialized scene.", "Exposes the same canonical JSON document used by scene files and Undo snapshots.", {}, "scene_json", ExecutionMode::Editor, {SideEffect::ReadOnly}, {"mipsync scene get-json --json"}, ForwardEditor},
        {"scene.patch", "Apply an RFC 6902 JSON Patch to the scene.", "Provides an escape hatch for every serialized component property while preserving validation and Editor Undo.",
            {Param("patch", ValueType::Json, true, "RFC 6902 JSON Patch array."),
             Param("confirm", ValueType::Boolean, false, "Must be true because a patch can alter the whole scene.", false)},
            "scene_patch_result", ExecutionMode::Editor, {SideEffect::EditorMutation, SideEffect::Dangerous},
            {"mipsync scene patch '[{\"op\":\"replace\",\"path\":\"/entities/0/active\",\"value\":false}]' --confirm true"}, ForwardEditor},
        {"scene.save", "Save the current Editor scene.", "Writes the active scene through the existing Editor save service.", {}, "scene_save_result", ExecutionMode::Editor, {SideEffect::ProjectMutation, SideEffect::FilesystemWrite}, {"mipsync scene save"}, ForwardEditor},
        {"editor.undo", "Undo the latest Editor mutation.", "Restores scene edits and project asset files in the same chronological history.", {}, "history_result", ExecutionMode::Editor, {SideEffect::EditorMutation}, {"mipsync editor undo"}, ForwardEditor},
        {"editor.redo", "Redo the latest undone Editor mutation.", "Uses the Editor's normal scene snapshot history.", {}, "history_result", ExecutionMode::Editor, {SideEffect::EditorMutation}, {"mipsync editor redo"}, ForwardEditor},
        {"editor.dialog.inspect", "Inspect pending save and build dialogs.", "Returns supported dialog IDs, pending actions, errors and permitted responses without waiting for a UI click.", {}, "editor_dialogs", ExecutionMode::Editor,
            {SideEffect::ReadOnly}, {"mipsync editor dialog inspect --json"}, ForwardEditor},
        {"editor.dialog.respond", "Respond to a pending save or build dialog.", "scene_save accepts save, discard or cancel. build_progress accepts cancel while building or close when finished. Save/discard resumes the original pending action on the next editor frame; cancel preserves the current scene.",
            {Param("dialog", ValueType::String, true, "scene_save or build_progress."), Param("action", ValueType::String, true, "save, discard, cancel or close, as returned by inspect.")},
            "editor_dialog_response", ExecutionMode::Editor, {SideEffect::ProjectMutation, SideEffect::FilesystemWrite, SideEffect::RuntimeMutation, SideEffect::ExternalProcess},
            {"mipsync editor dialog respond scene_save cancel --json", "mipsync editor dialog respond build_progress close --json"}, ForwardEditor},
        {"entity.list", "List entities in the current scene.", "Returns stable entity IDs, hierarchy and component names.", {}, "entity_list", ExecutionMode::Editor, {SideEffect::ReadOnly}, {"mipsync entity list --json"}, ForwardEditor},
        {"entity.inspect", "Inspect an entity by ID or name.", "Returns transform, hierarchy and optional serialized component information.", {Param("entity", ValueType::String, true, "Entity ID or exact name."),Param("include-components",ValueType::Boolean,false,"Include all serialized component properties.",false)}, "entity", ExecutionMode::Editor, {SideEffect::ReadOnly}, {"mipsync entity inspect Player"}, ForwardEditor},
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
        {"component.add", "Add a component to an entity.", "Use component.list for all supported types and component.describe for defaults/properties. UI components and Canvas automatically add RectTransform.",
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
        {"audio.set", "Configure an AudioSource.", "BGM streams WAV/MP3/VAG from CD; SFX plays MIDI notes on resident SPU tones; sample holds short recorded effects in SPU RAM. MIDI clips select SFX automatically unless a type is specified.",
            {Param("entity", ValueType::String, true, "AudioSource entity ID or exact name."),
             Param("type", ValueType::String, false, "bgm, sfx (MIDI), or sample (resident SPU WAV/MP3/VAG)."),
             Param("clip", ValueType::Path, false, "Project-relative audio asset, or none."),
             Param("play-on-awake", ValueType::Boolean, false, "Start automatically."),
             Param("loop", ValueType::Boolean, false, "Repeat the clip."),
             Param("mute", ValueType::Boolean, false, "Mute this source."),
             Param("volume", ValueType::Number, false, "Volume from 0 to 1.")},
            "audio_source", ExecutionMode::Editor, {SideEffect::EditorMutation},
            {"mipsync audio set MenuMove --type sfx --clip assets/audio/menu_move.mid --play-on-awake false"}, ForwardEditor},
        {"ui.button-group-sounds", "Assign optional button-group SFX sources.", "Navigation plays when selection changes; press plays before an interactable button action. Use none to clear either source.",
            {Param("entity", ValueType::String, true, "UIButtonGroup entity ID or exact name."),
             Param("navigation", ValueType::String, false, "SFX AudioSource ID/name, or none."),
             Param("press", ValueType::String, false, "SFX AudioSource ID/name, or none.")},
            "button_group_sounds", ExecutionMode::Editor, {SideEffect::EditorMutation},
            {"mipsync ui button-group-sounds Menu --navigation MenuMove --press MenuConfirm"}, ForwardEditor},
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
        {"runtime.play", "Enter Editor play mode.", "Returns preparing, playing or save_required explicitly. Poll runtime.inspect until state=playing. Use --save-current true to save before starting.",
            {Param("save-current",ValueType::Boolean,false,"Save unsaved scene changes before Play.",false)}, "runtime_state", ExecutionMode::Editor, {SideEffect::RuntimeMutation}, {"mipsync runtime play --save-current true"}, ForwardEditor},
        {"runtime.stop", "Exit Editor play mode.", "Stops play mode and restores the edit scene.", {}, "runtime_state", ExecutionMode::Editor, {SideEffect::RuntimeMutation}, {"mipsync runtime stop"}, ForwardEditor},
        {"runtime.inspect", "Inspect Editor runtime state.", "Reports playing, paused state, mapped PS1 input/cursor capture and P-Sync workload/timing diagnostics.", {}, "runtime_state", ExecutionMode::Editor, {SideEffect::ReadOnly}, {"mipsync runtime inspect"}, ForwardEditor},
        {"build.start", "Start an Editor build without dialogs.", "Starts the same background build used by Build Settings without progress/error dialogs or opening Explorer. Dirty scenes return save_required. Use language.validate for preflight and build.inspect to poll completion, errors and logs.",
            {Param("target", ValueType::String, true, "pc, ps1 or disc."), Param("save-current", ValueType::Boolean, false, "Save unsaved scene changes before building.", false)}, "build_state", ExecutionMode::Editor,
            {SideEffect::ProjectMutation, SideEffect::FilesystemWrite, SideEffect::ExternalProcess}, {"mipsync build start pc --json"}, ForwardEditor},
        {"build.inspect", "Inspect the current Editor build.", "Reports state, progress, completion, success, exit code, diagnostic message, log and output paths. Failed/cancelled builds return a failing exit status without a dialog.", {}, "build_state", ExecutionMode::Editor,
            {SideEffect::ReadOnly}, {"mipsync build inspect --json"}, ForwardEditor},
        {"build.cancel", "Cancel the active build.", "Terminates the isolated build process and its children, preserves captured logs and dismisses its progress dialog if present.", {}, "build_state", ExecutionMode::Editor,
            {SideEffect::ExternalProcess}, {"mipsync build cancel --json"}, ForwardEditor},
    };
    for (auto descriptor : editorCommands) commands.Register(std::move(descriptor));

    commands.Register({"component.list","List supported components.","Includes dependencies and multiplicity.",{},"component_list",
        ExecutionMode::Local,{SideEffect::ReadOnly},{"mipsync component list --json"},
        [](const CommandRequest&,CommandContext&){return CommandResult::Ok({{"components",ComponentCatalog()}});}});
    commands.Register({"language.limits","List compiler and PS1 VM capacities.","Values use the same ABI constants as the exporter and PS1 decoder. Instruction budgets are per invocation; loops make actual usage data-dependent.",{},"language_limits",
        ExecutionMode::Local,{SideEffect::ReadOnly},{"mipsync language limits --json"},
        [](const CommandRequest&,CommandContext&){return CommandResult::Ok(Ps1Limits());}});
    commands.Register({"language.reference","Read Mips# syntax and limitations.","Self-contained syntax and small examples without reading engine sources.",{},"language_reference",
        ExecutionMode::Local,{SideEffect::ReadOnly},{"mipsync language reference --json"},
        [](const CommandRequest&,CommandContext&){return CommandResult::Ok({
            {"version",Mips::kLanguageVersion},{"types",{"int","float","double","bool","Vector3","AudioClip","Entity","Camera","AudioSource","int[]","float[]","bool[]"}},
            {"fields","Declare public fields after class declaration. Fields require public; private and SerializeField are not supported. [Header], [Tooltip], [InspectorName], [Range], [Min], [Order], [Space], [HideInInspector], [ReadOnly] are supported. Methods may be public or omit an access modifier."},
            {"controlFlow","if/else, while, for, break, continue, return; arithmetic and comparison operators; typed locals and var."},
            {"arrays","new int[20], [1,2,3], indexing, Length, Count, Add, RemoveAt, Clear. PS1 arrays are bounded."},
            {"methods","Awake, Start, Update, LateUpdate; coroutines using yield return null or WaitForSeconds. Custom method calls are not currently supported; compiler errors identify the source location."},
            {"input","Use Input.GetButton/GetButtonDown/GetButtonUp with PS1 names and Input.GetAxis with LeftStickX/Y, RightStickX/Y. Editor mappings are in Preferences."},
            {"examples",{"class Counter : MipsBehaviour { [Header(\"Score\")] public int score = 0; void Update() { if (Input.GetButtonDown(\"Cross\")) score = score + 1; } }",
                "class Board : MipsBehaviour { public int[] rows; void Start() { rows = new int[20]; rows[0] = 1; } }"}},
            {"limits",Ps1Limits()}});}});
    commands.Register({"language.validate","Validate a set of scripts against the build's PS1 verifier.","Checks aggregate module/instance limits as well as all bytecode/data limits. Pass project-relative source paths as a JSON array; --bindings is the scene script-instance count. Does not validate textures/audio or replace a full build.",
        {Param("files",ValueType::Json,true,"JSON array of script paths."),Param("bindings",ValueType::Integer,false,"Script instance count in the target scene.",0)},"ps1_validation",
        ExecutionMode::Local,{SideEffect::ReadOnly},{"mipsync language validate '[\"assets/scripts/Tetris.mips\",\"assets/scripts/Display.mips\"]' --bindings 4 --json"},
        [](const CommandRequest& r,CommandContext& c){
            if(!r.arguments.at("files").is_array()||r.arguments.at("files").empty())return CommandResult::Fail("MIPSYNC_FILES","files must be a nonempty JSON array.");
            std::vector<Mips::CompiledModule> modules;std::set<std::string> paths;Json results=Json::array();
            for(const auto& f:r.arguments.at("files")){
                if(!f.is_string())return CommandResult::Fail("MIPSYNC_FILES","Each file must be a path string.");
                auto path=ResolvePath(r,c,f.get<std::string>());
                if(!paths.insert(path.lexically_normal().string()).second)continue;
                CommandRequest one=r;one.arguments={{"file",f}};auto result=CompileLanguage(one,c);
                if(!result.success)return result;
                std::ifstream input(path);std::ostringstream source;source<<input.rdbuf();Mips::Lexer l(source.str(),path.string());Mips::Parser p(l.Tokenize(),path.string());auto ast=p.ParseProgram();
                for(const auto& cls:ast->classes){std::vector<std::string> errors;auto m=Mips::CompileClass(*cls,ast.get(),path.string(),errors);if(m)modules.push_back(*m);}
                results.push_back(result.value);
            }
            const auto bindings=r.arguments.value("bindings",int64_t{0});if(bindings<0||bindings>UINT32_MAX)return CommandResult::Fail("MIPSYNC_BINDINGS","bindings must be 0..4294967295.");
            std::string error;bool valid=Mips::ValidatePs1Target(modules,static_cast<uint32_t>(bindings),error);
            auto result=CommandResult::Ok({{"ps1Compatible",valid},{"ps1Diagnostics",error.empty()?Json::array():Json::array({error})},{"limits",Ps1Limits()},{"moduleCount",modules.size()},{"files",results}});
            if(!valid){result.success=false;result.status="failed";result.diagnostics.push_back({"MIPS_PS1_COMPATIBILITY","error",error});}
            return result;
        }});
    auto editorCommand=[&](const char* id,const char* summary,std::vector<ParameterDescriptor> parameters, bool mutation=true){
        std::vector<SideEffect> effects{mutation?SideEffect::EditorMutation:SideEffect::ReadOnly};
        if(std::string(id).starts_with("runtime.")&&std::string(id)!="runtime.fields"&&std::string(id)!="runtime.animator")effects={std::string(id)=="runtime.capture"?SideEffect::FilesystemWrite:SideEffect::RuntimeMutation};
        commands.Register({id,summary,summary,std::move(parameters),"editor_result",ExecutionMode::Editor,
            effects,{std::string("mipsync ")+id+" --json"},ForwardEditor});
    };
    editorCommand("component.describe","Inspect default component properties, types and valid ranges.",{Param("component",ValueType::String,true,"Supported component name.")},false);
    editorCommand("component.get","Read serialized properties of one component.",{Param("entity",ValueType::String,true,"Entity ID/name."),Param("component",ValueType::String,true,"Component name."),Param("index",ValueType::Integer,false,"Zero-based component index.",0)},false);
    for(const char* id:{"ui.rect","ui.text","ui.image","canvas.set"})
        editorCommand(id,"Configure UI properties with validation and Undo; automatically adds required UI components.",{
            Param("entity",ValueType::String,true,"Entity ID/name."),Param("properties",ValueType::Json,true,"Property object; use component.describe for names, defaults and ranges.")});
    editorCommand("script.attach","Attach a script after compiling it successfully.",{Param("entity",ValueType::String,true,"Entity ID/name."),Param("file",ValueType::Path,true,"Project-relative .mips file."),Param("fields",ValueType::Json,false,"Named field overrides.",Json::object())});
    editorCommand("script.fields","Set named serialized script fields.",{Param("entity",ValueType::String,true,"Entity ID/name."),Param("fields",ValueType::Json,true,"Named numeric/bool/asset field overrides."),Param("index",ValueType::Integer,false,"Zero-based MipsScript index.",0)});
    editorCommand("script.reload","Force recompilation of scene scripts without relying on timestamps.",{Param("file",ValueType::Path,false,"Optional script path; omit to reload all attached scripts.")});
    editorCommand("entity.order","Set sibling order, which controls UI drawing order.",{Param("entity",ValueType::String,true,"Entity ID/name."),Param("index",ValueType::Integer,true,"Zero-based sibling order; zero is first.")});
    editorCommand("entity.grid","Duplicate an entity hierarchy into a grid in row-major drawing order.",{Param("entity",ValueType::String,true,"Template ID/name."),Param("rows",ValueType::Integer,true,"Rows, 1..4096."),Param("columns",ValueType::Integer,true,"Columns; total cells at most 4096."),Param("dx",ValueType::Number,false,"Column spacing.",100.0),Param("dy",ValueType::Number,false,"Row spacing.",-100.0),Param("name",ValueType::String,false,"Base name; each copy gets row/column suffix.")});
    editorCommand("batch.run","Execute authoring commands in one Undo transaction; failure rolls back the complete scene batch.",{Param("operations",ValueType::Json,false,"Array of {command,arguments}. Authoring commands only."),Param("file",ValueType::Path,false,"Project-relative JSON file containing the array.")});
    editorCommand("runtime.fields","Read actual script runtime variables, including arrays.",{Param("entity",ValueType::String,true,"Entity ID/name."),Param("index",ValueType::Integer,false,"Zero-based script index.",0)},false);
    editorCommand("runtime.pause","Set runtime pause state.",{Param("paused",ValueType::Boolean,false,"Pause or resume.",true)},false);
    editorCommand("runtime.step","Queue deterministic gameplay frames while paused.",{Param("frames",ValueType::Integer,false,"Frames, 1..600.",1),Param("delta",ValueType::Number,false,"Seconds per frame.",1.0/60.0),Param("buttons",ValueType::Json,false,"Optional PS1 button names to set atomically before stepping."),Param("axes",ValueType::Json,false,"Optional PS1 stick axes to set atomically before stepping.")},false);
    editorCommand("runtime.input","Inject virtual PS1 input for queued runtime.step frames.",{Param("buttons",ValueType::Json,false,"Held PS1 button names.",Json::array()),Param("axes",ValueType::Json,false,"Object containing LeftStickX/Y and RightStickX/Y, -1..1.",Json::object())},false);
    editorCommand("runtime.capture","Save the last rendered Game View as a PNG.",{Param("file",ValueType::Path,true,"Project-relative output PNG path.")},false);

    editorCommand("model.inspect","Inspect model mesh parts, bones, animation clip IDs/stacks/durations and target channels.",{Param("model",ValueType::Path,true,"Project model path.")},false);
    editorCommand("animation.list","List animation clips, stack indices, durations and animated bone targets.",{Param("model",ValueType::Path,true,"Project model path.")},false);
    for(const char* id:{"animator.set","skinned-mesh.set","component.set"}) {
        std::vector<ParameterDescriptor> parameters={Param("entity",ValueType::String,true,"Entity ID/name."),Param("properties",ValueType::Json,true,"Typed properties; discover with component.describe.")};
        if(std::string(id)=="component.set")parameters.push_back(Param("component",ValueType::String,true,"Component type."));
        editorCommand(id,"Set component properties with validation, serialized persistence and Undo.",parameters);
    }
    editorCommand("runtime.animator","Inspect current/next animation states, clip phase, parameters and transition probes.",{Param("entity",ValueType::String,true,"Entity ID/name.")},false);
    for(const char* id:{"animator.controller.inspect","animator.controller.validate"})
        editorCommand(id,"Inspect or validate states, transitions, parameters and clip ID/stack references.",{Param("path",ValueType::Path,true,"Project .ncontroller path.")},false);
    commands.Register({"animator.controller.schema","Read the public Animator Controller JSON schema.","Discover controller states, parameters and transitions without engine source.",{},"json_schema",ExecutionMode::Local,{SideEffect::ReadOnly},{"mipsync animator.controller.schema --json"},[](const CommandRequest&,CommandContext&){return CommandResult::Ok(AnimationControllerSchema());}});
    auto assetCommand=[&](const char* id,const char* summary,std::vector<ParameterDescriptor> params){commands.Register({id,summary,summary,std::move(params),"asset_result",ExecutionMode::Editor,{SideEffect::EditorMutation,SideEffect::ProjectMutation,SideEffect::FilesystemWrite},{std::string("mipsync ")+id+" --json"},ForwardEditor});};
    assetCommand("animator.controller.set","Create or replace a validated Animator Controller; preserves the original on failure.",{Param("path",ValueType::Path,true,"Project .ncontroller path."),Param("controller",ValueType::Json,true,"Full controller document; use animator.controller.schema.")});
    assetCommand("animator.controller.state.set","Add or replace an animation state by name.",{Param("path",ValueType::Path,true,"Controller path."),Param("state",ValueType::Json,true,"State object."),Param("default",ValueType::Boolean,false,"Make default state.",false)});
    assetCommand("animator.controller.parameter.set","Add or replace an animation parameter by name.",{Param("path",ValueType::Path,true,"Controller path."),Param("parameter",ValueType::Json,true,"Parameter object.")});
    assetCommand("animator.controller.transition.set","Append or replace an animation transition with validated conditions.",{Param("path",ValueType::Path,true,"Controller path."),Param("transition",ValueType::Json,true,"Transition object."),Param("index",ValueType::Integer,false,"-1 appends; otherwise existing zero-based index.",-1)});
    assetCommand("model.extract","Extract mesh parts or dominant bone regions for independent animations; original model is preserved. Use instead of unsupported animation layers/masks.",{Param("model",ValueType::Path,true,"Original model path."),Param("path",ValueType::Path,true,"Output .nmodel descriptor."),Param("bone-roots",ValueType::Json,false,"Bone names including descendants.",Json::array()),Param("mesh-parts",ValueType::Json,false,"Mesh part indices.",Json::array())});

    RegisterLanguageSymbols(symbols);
    for (const auto* command : commands.All()) {
        symbols.Register({command->id, "command", command->id, command->summary, command->description,
                          command->id, command->parameters, command->returnType, command->examples, {},
                          "Command Registry", MIPSYNC_ENGINE_VERSION});
    }
}

} // namespace MipsyncEngine::Command
