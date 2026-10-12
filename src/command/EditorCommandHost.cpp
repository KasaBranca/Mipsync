#include "EditorCommandHost.h"

#include "CommandLine.h"
#include "ResultRenderer.h"
#include "AuthoringCatalog.h"
#include "AnimationAuthoring.h"
#include <stb_image_write.h>
#include <fstream>
#include <sstream>
#include "../assets/AssetManager.h"
#include "../assets/Material.h"
#include "../audio/MidiFile.h"
#include "../core/Engine.h"
#include "../core/Input.h"
#include "../core/Log.h"
#include "../core/RuntimePaths.h"
#include "../editor/EditorApp.h"
#include "../editor/MipsEditorIntegration.h"
#include "../scene/Scene.h"
#include "../scene/SceneIO.h"
#include "../mips/MipsRuntime.h"
#include <set>
#include "EngineVersion.h"

#include <charconv>
#include <chrono>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>

namespace MipsyncEngine::Command {
namespace {

std::string EntityName(Entity& entity) {
    if (auto* tag = entity.GetComponent<TagComponent>()) return tag->tag;
    return "Entity";
}

Json Vec3(const glm::vec3& value) { return Json::array({value.x, value.y, value.z}); }

std::vector<std::string> ComponentNames(Entity& entity) {
    std::vector<std::string> names{"Transform"};
    auto add = [&](auto* value, const char* name) { if (value) names.emplace_back(name); };
    add(entity.GetComponent<MeshRendererComponent>(), "MeshRenderer");
    add(entity.GetComponent<SkinnedMeshRendererComponent>(), "SkinnedMeshRenderer");
    add(entity.GetComponent<AnimatorComponent>(), "Animator");
    add(entity.GetComponent<CameraComponent>(), "Camera");
    add(entity.GetComponent<PostProcessVolumeComponent>(), "PostProcessVolume");
    add(entity.GetComponent<ColliderComponent>(), "Collider");
    add(entity.GetComponent<RigidbodyComponent>(), "Rigidbody");
    add(entity.GetComponent<LightComponent>(), "Light");
    add(entity.GetComponent<AudioSourceComponent>(), "AudioSource");
    add(entity.GetComponent<TerrainComponent>(), "Terrain");
    add(entity.GetComponent<ProModelerComponent>(), "ProModeler");
    add(entity.GetComponent<CanvasComponent>(), "Canvas");
    add(entity.GetComponent<RectTransformComponent>(), "RectTransform");
    add(entity.GetComponent<UIImageComponent>(), "UIImage");
    add(entity.GetComponent<UITextComponent>(), "UIText");
    add(entity.GetComponent<UIButtonComponent>(), "UIButton");
    add(entity.GetComponent<UIButtonGroupComponent>(), "UIButtonGroup");
    add(entity.GetComponent<UIAudioSpectrumComponent>(), "UIAudioSpectrum");
    const auto scripts = entity.GetComponents<MipsScriptComponent>();
    for (size_t i = 0; i < scripts.size(); ++i) names.emplace_back("MipsScript");
    return names;
}

Entity* FindEntity(Scene& scene, const std::string& query, std::string* outError = nullptr) {
    uint32_t id = 0;
    const auto parsed = std::from_chars(query.data(), query.data() + query.size(), id);
    if (parsed.ec == std::errc{} && parsed.ptr == query.data() + query.size()) {
        if (Entity* entity = scene.FindEntity(id)) return entity;
        if (outError) *outError = "Entity ID not found: " + query;
        return nullptr;
    }
    Entity* match = nullptr;
    for (const auto& entity : scene.GetEntities()) {
        if (EntityName(*entity) != query) continue;
        if (match) {
            if (outError) *outError = "Entity name is ambiguous; use a numeric ID: " + query;
            return nullptr;
        }
        match = entity.get();
    }
    if (match) return match;
    if (outError) *outError = "Entity not found: " + query;
    return nullptr;
}

std::string Lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

bool RequireEditMode(EditorApp& editor, CommandResult& outResult) {
    if (!editor.IsPlaying()) return true;
    outResult = CommandResult::Fail("MIPSYNC_EDIT_MODE_REQUIRED",
        "Scene authoring commands are disabled during Play Mode. Run 'runtime stop' first.");
    return false;
}

bool ResolveMaterialPath(const std::string& input, std::string& outRelative, std::string& outError) {
    const auto path = PathUtf8::FromString(input).lexically_normal();
    if (path.empty() || path.is_absolute()) {
        outError = "Material path must be project-relative.";
        return false;
    }
    for (const auto& part : path) {
        if (part == "..") {
            outError = "Material path cannot leave the project.";
            return false;
        }
    }
    auto it = path.begin();
    if (it == path.end() || Lower(PathUtf8::ToString(*it)) != "assets") {
        outError = "Material path must be inside assets/.";
        return false;
    }
    if (Lower(PathUtf8::ToString(path.extension())) != ".nmat") {
        outError = "Material path must end in .nmat.";
        return false;
    }
    outRelative = PathUtf8::ToString(path);
    return true;
}

bool ApplyMaterial(Entity& entity, const std::string& input, std::string& outError) {
    auto* renderer = entity.GetComponent<MeshRendererComponent>();
    if (!renderer) {
        outError = "Entity has no MeshRenderer component.";
        return false;
    }
    if (Lower(input) == "none" || Lower(input) == "null") {
        AssetManager::Get().ClearMeshRendererMaterial(*renderer);
        return true;
    }
    std::string relative;
    if (!ResolveMaterialPath(input, relative, outError)) return false;
    Material material;
    if (!Material::Load(AssetManager::Get().ToAbsolute(relative), material, outError)) return false;
    AssetManager::Get().ApplyMaterialToMeshRenderer(*renderer, material, relative);
    return true;
}

template<typename T>
bool AddUniqueComponent(Entity& entity, const char* name, std::string& outError) {
    if (entity.GetComponent<T>()) {
        outError = std::string{name} + " already exists on the entity.";
        return false;
    }
    entity.AddComponent<T>();
    return true;
}

bool AddNamedComponent(Entity& entity, const std::string& input, std::string& outError) {
    const std::string name = Lower(input);
    if (name=="uiimage"||name=="uitext"||name=="uibutton"||name=="uibuttongroup"||name=="uiaudiospectrum"||name=="canvas") {
        for(const auto& existing:ComponentNames(entity))if(Lower(existing)==name){outError=input+" already exists on the entity.";return false;}
        if(!entity.GetComponent<RectTransformComponent>())entity.AddComponent<RectTransformComponent>();
    }
    if (name == "meshrenderer") {
        if (!AddUniqueComponent<MeshRendererComponent>(entity, "MeshRenderer", outError)) return false;
        entity.GetComponent<MeshRendererComponent>()->SetPrimitive("Cube", 1.0f);
        return true;
    }
    if (name == "skinnedmeshrenderer") return AddUniqueComponent<SkinnedMeshRendererComponent>(entity, "SkinnedMeshRenderer", outError);
    if (name == "animator") return AddUniqueComponent<AnimatorComponent>(entity, "Animator", outError);
    if (name == "camera") return AddUniqueComponent<CameraComponent>(entity, "Camera", outError);
    if (name == "postprocessvolume") return AddUniqueComponent<PostProcessVolumeComponent>(entity, "PostProcessVolume", outError);
    if (name == "collider") return AddUniqueComponent<ColliderComponent>(entity, "Collider", outError);
    if (name == "rigidbody") return AddUniqueComponent<RigidbodyComponent>(entity, "Rigidbody", outError);
    if (name == "light") return AddUniqueComponent<LightComponent>(entity, "Light", outError);
    if (name == "audiosource") return AddUniqueComponent<AudioSourceComponent>(entity, "AudioSource", outError);
    if (name == "terrain") return AddUniqueComponent<TerrainComponent>(entity, "Terrain", outError);
    if (name == "promodeler") return AddUniqueComponent<ProModelerComponent>(entity, "ProModeler", outError);
    if (name == "recttransform") return AddUniqueComponent<RectTransformComponent>(entity, "RectTransform", outError);
    if (name == "canvas") return AddUniqueComponent<CanvasComponent>(entity, "Canvas", outError);
    if (name == "uiimage") return AddUniqueComponent<UIImageComponent>(entity, "UIImage", outError);
    if (name == "uitext") return AddUniqueComponent<UITextComponent>(entity, "UIText", outError);
    if (name == "uibutton") return AddUniqueComponent<UIButtonComponent>(entity, "UIButton", outError);
    if (name == "uibuttongroup") return AddUniqueComponent<UIButtonGroupComponent>(entity, "UIButtonGroup", outError);
    if (name == "uiaudiospectrum") return AddUniqueComponent<UIAudioSpectrumComponent>(entity, "UIAudioSpectrum", outError);
    if (name == "mipsscript") { entity.AddComponent<MipsScriptComponent>(); return true; }
    outError = "Unsupported component type: " + input;
    return false;
}

template<typename T>
bool RemoveOne(Entity& entity) {
    if (!entity.GetComponent<T>()) return false;
    entity.RemoveComponent<T>();
    return true;
}

bool RemoveNamedComponent(Entity& entity, const std::string& input) {
    const std::string name = Lower(input);
    if (name == "meshrenderer") return RemoveOne<MeshRendererComponent>(entity);
    if (name == "skinnedmeshrenderer") return RemoveOne<SkinnedMeshRendererComponent>(entity);
    if (name == "animator") return RemoveOne<AnimatorComponent>(entity);
    if (name == "camera") return RemoveOne<CameraComponent>(entity);
    if (name == "postprocessvolume") return RemoveOne<PostProcessVolumeComponent>(entity);
    if (name == "collider") return RemoveOne<ColliderComponent>(entity);
    if (name == "rigidbody") return RemoveOne<RigidbodyComponent>(entity);
    if (name == "light") return RemoveOne<LightComponent>(entity);
    if (name == "audiosource") return RemoveOne<AudioSourceComponent>(entity);
    if (name == "terrain") return RemoveOne<TerrainComponent>(entity);
    if (name == "promodeler") return RemoveOne<ProModelerComponent>(entity);
    if (name == "recttransform") return RemoveOne<RectTransformComponent>(entity);
    if (name == "canvas") return RemoveOne<CanvasComponent>(entity);
    if (name == "uiimage") return RemoveOne<UIImageComponent>(entity);
    if (name == "uitext") return RemoveOne<UITextComponent>(entity);
    if (name == "uibutton") return RemoveOne<UIButtonComponent>(entity);
    if (name == "uibuttongroup") return RemoveOne<UIButtonGroupComponent>(entity);
    if (name == "uiaudiospectrum") return RemoveOne<UIAudioSpectrumComponent>(entity);
    if (name == "mipsscript") return RemoveOne<MipsScriptComponent>(entity);
    return false;
}

template<typename T>
bool SetOneEnabled(Entity& entity, bool enabled) {
    if (auto* component = entity.GetComponent<T>()) { component->enabled = enabled; return true; }
    return false;
}

bool SetNamedComponentEnabled(Entity& entity, const std::string& input, bool enabled) {
    const std::string name = Lower(input);
    if (name == "meshrenderer") return SetOneEnabled<MeshRendererComponent>(entity, enabled);
    if (name == "skinnedmeshrenderer") return SetOneEnabled<SkinnedMeshRendererComponent>(entity, enabled);
    if (name == "animator") return SetOneEnabled<AnimatorComponent>(entity, enabled);
    if (name == "camera") return SetOneEnabled<CameraComponent>(entity, enabled);
    if (name == "postprocessvolume") return SetOneEnabled<PostProcessVolumeComponent>(entity, enabled);
    if (name == "collider") return SetOneEnabled<ColliderComponent>(entity, enabled);
    if (name == "rigidbody") return SetOneEnabled<RigidbodyComponent>(entity, enabled);
    if (name == "light") return SetOneEnabled<LightComponent>(entity, enabled);
    if (name == "audiosource") return SetOneEnabled<AudioSourceComponent>(entity, enabled);
    if (name == "terrain") return SetOneEnabled<TerrainComponent>(entity, enabled);
    if (name == "promodeler") return SetOneEnabled<ProModelerComponent>(entity, enabled);
    if (name == "recttransform") return SetOneEnabled<RectTransformComponent>(entity, enabled);
    if (name == "canvas") return SetOneEnabled<CanvasComponent>(entity, enabled);
    if (name == "uiimage") return SetOneEnabled<UIImageComponent>(entity, enabled);
    if (name == "uitext") return SetOneEnabled<UITextComponent>(entity, enabled);
    if (name == "uibutton") return SetOneEnabled<UIButtonComponent>(entity, enabled);
    if (name == "uibuttongroup") return SetOneEnabled<UIButtonGroupComponent>(entity, enabled);
    if (name == "uiaudiospectrum") return SetOneEnabled<UIAudioSpectrumComponent>(entity, enabled);
    if (name == "mipsscript") return SetOneEnabled<MipsScriptComponent>(entity, enabled);
    return false;
}

} // namespace

EditorCommandHost::EditorCommandHost(Engine& engine)
    : m_Engine(engine), m_Executor(m_Commands) {
    RegisterCoreCommands(m_Commands, m_Symbols);
    m_Context.projectPath = engine.GetProjectPath();
    m_Context.engineVersion = MIPSYNC_ENGINE_VERSION;
    m_Context.commands = &m_Commands;
    m_Context.symbols = &m_Symbols;
    m_Context.editor = this;
}

EditorCommandHost::~EditorCommandHost() { Stop(); }

bool EditorCommandHost::Start(std::string& outError) {
    if (m_Engine.IsPlayerMode()) return true;
    const uint64_t pid = CurrentProcessId();
    m_Instance.instanceId = std::to_string(pid);
    m_Instance.processId = pid;
    m_Instance.projectPath = m_Engine.GetProjectPath();
    m_Instance.projectId = PathUtf8::ToString(PathUtf8::FromString(m_Instance.projectPath).filename());
    m_Instance.engineVersion = MIPSYNC_ENGINE_VERSION;
    m_Instance.executablePath = PathUtf8::ToString(GetExecutablePath());
    m_Instance.endpoint = MakeEditorEndpoint(pid);
    if (!m_Server.Start(m_Instance.endpoint,
            [this](const std::string& payload) { return HandleIpcPayload(payload); }, outError))
        return false;
    if (!InstanceRegistry::Register(m_Instance, outError)) {
        m_Server.Stop();
        return false;
    }
    return true;
}

void EditorCommandHost::Stop() {
    if (!m_Instance.instanceId.empty()) InstanceRegistry::Unregister(m_Instance.instanceId);
    m_Server.Stop();
    std::lock_guard lock(m_QueueMutex);
    while (!m_Pending.empty()) {
        auto pending = m_Pending.front();
        m_Pending.pop();
        pending->response.set_value(CommandResult::Fail("MIPSYNC_EDITOR_STOPPED", "Editor stopped before command execution.")
            .ToJson(pending->request.requestId).dump());
    }
}

std::string EditorCommandHost::HandleIpcPayload(const std::string& payload) {
    try {
        auto pending = std::make_shared<PendingRequest>();
        pending->request = CommandRequest::FromJson(Json::parse(payload));
        auto future = pending->response.get_future();
        {
            std::lock_guard lock(m_QueueMutex);
            m_Pending.push(pending);
        }
        if (future.wait_for(std::chrono::seconds(30)) != std::future_status::ready)
            return CommandResult::Fail("MIPSYNC_EDITOR_TIMEOUT", "Editor did not process the command within 30 seconds.")
                .ToJson(pending->request.requestId).dump();
        return future.get();
    } catch (const std::exception& ex) {
        return CommandResult::Fail("MIPSYNC_PROTOCOL_REQUEST", ex.what()).ToJson().dump();
    }
}

void EditorCommandHost::Pump(bool preparationOnly) {
    std::queue<std::shared_ptr<PendingRequest>> pending;
    {
        std::lock_guard lock(m_QueueMutex);
        if (!preparationOnly) std::swap(pending, m_Pending);
        else {
            const size_t count=m_Pending.size();
            for(size_t i=0;i<count;++i){auto request=m_Pending.front();m_Pending.pop();
                if(request->request.command=="runtime.inspect"||request->request.command=="runtime.stop")pending.push(request);
                else m_Pending.push(request);
            }
        }
    }
    while (!pending.empty()) {
        auto request = pending.front();
        pending.pop();
        if (!request->request.projectPath.empty() &&
            std::filesystem::weakly_canonical(request->request.projectPath) !=
            std::filesystem::weakly_canonical(m_Engine.GetProjectPath())) {
            request->response.set_value(CommandResult::Fail("MIPSYNC_PROJECT_MISMATCH",
                "Request targets a different project than this Editor instance.")
                .ToJson(request->request.requestId).dump());
            continue;
        }
        MIPSYNC_INFO("[CLI] {} {}", request->request.command,
                     request->request.arguments.empty() ? std::string{} : request->request.arguments.dump());
        const auto* descriptor = m_Commands.Find(request->request.command);
        const bool mutation = descriptor && std::find(descriptor->effects.begin(),descriptor->effects.end(),
            SideEffect::EditorMutation) != descriptor->effects.end();
        if (mutation) m_Engine.GetEditor().CommitUndoTransaction();
        const auto result = m_Executor.Execute(request->request, m_Context);
        if (mutation) m_Engine.GetEditor().CommitUndoTransaction();
        if (!result.success && !result.diagnostics.empty())
            MIPSYNC_WARN("[CLI] {} failed: {}", request->request.command,
                         result.diagnostics.front().message);
        request->response.set_value(result.ToJson(request->request.requestId).dump());
    }
}

std::string EditorCommandHost::ExecuteConsoleLine(const std::string& line) {
    std::string tokenError;
    const auto tokens = TokenizeCommandLine(line, tokenError);
    if (!tokenError.empty()) return "error: " + tokenError;
    auto parsed = ParseCommandLine(tokens, m_Commands);
    if (!parsed.valid) return "error: " + parsed.error;
    parsed.request.projectPath = m_Engine.GetProjectPath();
    const auto* descriptor = m_Commands.Find(parsed.request.command);
    const bool mutation = descriptor && std::find(descriptor->effects.begin(),descriptor->effects.end(),
        SideEffect::EditorMutation) != descriptor->effects.end();
    if (mutation) m_Engine.GetEditor().CommitUndoTransaction();
    const auto result = m_Executor.Execute(parsed.request,m_Context);
    if (mutation) m_Engine.GetEditor().CommitUndoTransaction();
    return RenderHuman(parsed.request.command,result);
}

Json EditorCommandHost::DescribeEntity(Entity& entity) const {
    Json result{
        {"id", entity.GetID()}, {"name", EntityName(entity)}, {"active", entity.IsActive()},
        {"static", entity.IsStatic()}, {"tag", entity.GetEditorTag()}, {"layer", entity.GetEditorLayer()},
        {"parentId", entity.GetParentID()}, {"childIds", entity.GetChildIDs()},
        {"components", ComponentNames(entity)},
    };
    if (auto* transform = entity.GetComponent<TransformComponent>()) {
        result["transform"] = {
            {"position", Vec3(transform->position)}, {"rotation", Vec3(transform->rotation)},
            {"scale", Vec3(transform->scale)},
        };
    }
    Json scripts = Json::array();
    for (const auto* script : entity.GetComponents<MipsScriptComponent>())
        scripts.push_back({{"path", script->scriptPath}, {"enabled", script->enabled}});
    if (!scripts.empty()) result["scripts"] = std::move(scripts);
    return result;
}

CommandResult EditorCommandHost::ExecuteEditorCommand(const CommandRequest& request) {
    Scene& scene = m_Engine.GetScene();
    EditorApp& editor = m_Engine.GetEditor();
    if (request.command == "editor.dialog.inspect" || request.command == "editor.dialog.respond") {
        auto& builds = editor.GetBuildSettings();
        if (request.command == "editor.dialog.respond") {
            const auto id = request.arguments.at("dialog").get<std::string>();
            const auto action = request.arguments.at("action").get<std::string>();
            std::string error;
            bool ok = false;
            if (id == "scene_save") ok = editor.CommandRespondSceneSaveConfirmation(action, error);
            else if (id == "build_progress" && builds.HasBuildDialog()) {
                if (action == "cancel" && builds.IsBuildInProgress()) {
                    builds.CommandCancelBuild();
                    ok = builds.CommandCloseBuildDialog(error);
                } else if (action == "close") ok = builds.CommandCloseBuildDialog(error);
                else error = "Build dialog accepts cancel while running or close after completion.";
            } else error = "The requested dialog is not awaiting a response.";
            if (!ok) {
                auto result = CommandResult::Fail("MIPSYNC_DIALOG_RESPONSE", error);
                if (id == "scene_save" && editor.HasSceneSaveConfirmation())
                    result.value = editor.InspectSceneSaveConfirmation();
                return result;
            }
            return CommandResult::Ok({{"dialog", id}, {"action", action}, {"accepted", true},
                {"actionPending", id == "scene_save" && editor.HasSceneSaveConfirmation()}});
        }
        Json dialogs = Json::array();
        if (editor.HasSceneSaveConfirmation()) dialogs.push_back(editor.InspectSceneSaveConfirmation());
        if (builds.HasBuildDialog()) dialogs.push_back({{"id", "build_progress"},
            {"state", builds.IsBuildInProgress() ? "running" : builds.WasBuildSuccessful() ? "complete" : "failed"},
            {"message", builds.GetBuildMessage()}, {"respondable", true},
            {"actions", Json::array({builds.IsBuildInProgress() ? "cancel" : "close"})}});
        return CommandResult::Ok({{"dialogs", dialogs}, {"required", !dialogs.empty()}});
    }
    // Do not let a queued save choice act on a scene modified by a later CLI
    // request. Queries and explicit save/stop remain available without a wait.
    if (editor.HasSceneSaveConfirmation() && request.command != "scene.save" && request.command != "runtime.stop" && request.command != "build.cancel") {
        const auto* descriptor = m_Commands.Find(request.command);
        const bool readOnly = descriptor && std::all_of(descriptor->effects.begin(), descriptor->effects.end(),
            [](SideEffect effect) { return effect == SideEffect::ReadOnly; });
        if (!readOnly) {
            auto result = CommandResult::Fail("MIPSYNC_DIALOG_REQUIRED",
                "A scene save confirmation is pending. Use editor.dialog.inspect and editor.dialog.respond.");
            result.status = "dialog_required";
            result.value = editor.InspectSceneSaveConfirmation();
            return result;
        }
    }
#include "EditorAuthoringCommands.inl"
    if (request.command == "build.start" || request.command == "build.inspect" || request.command == "build.cancel") {
        auto& builds = editor.GetBuildSettings();
        builds.UpdateBuildProcess();
        if (request.command == "build.start") {
            if (editor.IsPlaying()) return CommandResult::Fail("MIPSYNC_BUILD_PLAY_MODE", "Stop Play Mode before building.");
            const auto target = Lower(request.arguments.at("target").get<std::string>());
            if (target != "pc" && target != "ps1" && target != "disc")
                return CommandResult::Fail("MIPSYNC_BUILD_TARGET", "Build target must be pc, ps1 or disc.");
            if (builds.IsBuildInProgress()) {
                auto result = CommandResult::Fail("MIPSYNC_BUILD_BUSY", "A build is already running. Use build.inspect or build.cancel.");
                result.status = "busy";
                result.value = {{"state", "running"}, {"running", true}};
                return result;
            }
            editor.CommandRefreshSceneDirtyState();
            if (editor.IsSceneDirty()) {
                if (!request.arguments.value("save-current", false)) {
                    auto result = CommandResult::Fail("MIPSYNC_SAVE_REQUIRED", "Save the scene or use build.start --save-current true.");
                    result.status = "save_required";
                    result.value = {{"state", "save_required"}, {"path", editor.GetSceneFilePath()}};
                    return result;
                }
                std::string error;
                if (!editor.CommandSaveScene(error)) return CommandResult::Fail("MIPSYNC_SCENE_SAVE", error);
            }
            std::string message;
            bool started = false;
            if (target == "pc") started = builds.RunPcNativeBuild(message, false, false);
            else if (target == "ps1") started = builds.RunBuild(message, false, false);
            else if (target == "disc") started = builds.RunBuildDiscFolder(message, false, false);
            if (!started) return CommandResult::Fail("MIPSYNC_BUILD_START", message);
        }
        if (request.command == "build.cancel") {
            if (!builds.IsBuildInProgress()) return CommandResult::Fail("MIPSYNC_BUILD_NOT_RUNNING", "No build is running.");
            builds.CommandCancelBuild();
            std::string error;
            builds.CommandCloseBuildDialog(error);
        }
        const auto& pc = builds.GetPcBuildResult();
        const auto& ps1 = builds.GetPs1BuildResult();
        const std::string state = builds.IsBuildInProgress() ? "running" : !builds.HasBuildFinished() ? "idle" :
            builds.WasBuildCancelled() ? "cancelled" : builds.WasBuildSuccessful() ? "complete" : "failed";
        Json value = {{"state", state}, {"running", builds.IsBuildInProgress()}, {"finished", builds.HasBuildFinished()},
            {"success", builds.WasBuildSuccessful()}, {"progress", builds.GetBuildProgress()},
            {"cancelled", builds.WasBuildCancelled()},
            {"exitCode", builds.HasBuildFinished() ? Json(builds.GetBuildExitCode()) : Json(nullptr)},
            {"stage", builds.GetBuildStage()}, {"message", builds.GetBuildMessage()},
            {"log", builds.GetBuildLog()}, {"dialogVisible", builds.HasBuildDialog()},
            {"output", pc.outputDirectory.empty() ? ps1.outputDirectory : pc.outputDirectory},
            {"executable", pc.executablePath.empty() ? ps1.psxExePath : pc.executablePath},
            {"cue", ps1.discCuePath}, {"discFolder", ps1.discFolderPath}};
        auto result = builds.HasBuildFinished() && !builds.WasBuildSuccessful() && request.command != "build.cancel"
            ? CommandResult::Fail(builds.WasBuildCancelled() ? "MIPSYNC_BUILD_CANCELLED" : "MIPSYNC_BUILD_FAILED", builds.GetBuildMessage())
            : CommandResult::Ok();
        result.value = std::move(value);
        result.status = builds.IsBuildInProgress() ? "pending" : state;
        return result;
    }
    if (request.command == "scene.inspect") {
        return CommandResult::Ok({
            {"path", editor.GetSceneFilePath()}, {"entityCount", scene.GetEntities().size()},
            {"dirty", editor.IsSceneDirty()}, {"playing", editor.IsPlaying()}, {"paused", editor.IsPaused()},
        });
    }
    if (request.command == "scene.get-json") {
        std::string serialized;
        std::string error;
        if (!SceneIO::SerializeSceneFingerprint(scene, serialized, error))
            return CommandResult::Fail("MIPSYNC_SCENE_SERIALIZE", error);
        return CommandResult::Ok({{"scene", Json::parse(serialized)}});
    }
    if (request.command == "scene.open") {
        CommandResult blocked;
        if (!RequireEditMode(editor, blocked)) return blocked;
        const auto root = std::filesystem::weakly_canonical(PathUtf8::FromString(m_Engine.GetProjectPath()));
        const auto input = PathUtf8::FromString(request.arguments.at("path").get<std::string>());
        if (input.empty())
            return CommandResult::Fail("MIPSYNC_SCENE_PATH", "Scene path cannot be empty.");
        const auto path = std::filesystem::weakly_canonical(input.is_absolute() ? input : root / input);
        const auto relative = path.lexically_relative(root);
        if (relative.empty() || relative.is_absolute() || *relative.begin() == ".." ||
            Lower(PathUtf8::ToString(path.extension())) != ".nscene")
            return CommandResult::Fail("MIPSYNC_SCENE_PATH", "Scene must be a .nscene file inside the current project.");
        std::string error;
        if (!editor.CommandOpenScene(PathUtf8::ToString(path), request.arguments.value("save-current", false), error)) {
            const bool saveRequired = editor.IsSceneDirty() && !request.arguments.value("save-current", false);
            auto result = CommandResult::Fail(saveRequired ? "MIPSYNC_SAVE_REQUIRED" : "MIPSYNC_SCENE_OPEN", error);
            if (saveRequired) { result.status = "save_required"; result.value = {{"state", "save_required"}, {"path", editor.GetSceneFilePath()}}; }
            return result;
        }
        return CommandResult::Ok({{"path", editor.GetSceneFilePath()}, {"opened", true},
            {"entityCount", scene.GetEntities().size()}, {"dirty", editor.IsSceneDirty()}});
    }
    if (request.command == "scene.patch") {
        CommandResult blocked;
        if (!RequireEditMode(editor, blocked)) return blocked;
        if (!request.arguments.value("confirm", false))
            return CommandResult::Fail("MIPSYNC_CONFIRM_REQUIRED", "Scene patching requires --confirm true.");
        const Json patch = request.arguments.at("patch");
        if (!patch.is_array())
            return CommandResult::Fail("MIPSYNC_SCENE_PATCH", "Patch must be an RFC 6902 JSON Patch array.");
        std::string serialized;
        std::string error;
        if (!SceneIO::SerializeSceneFingerprint(scene, serialized, error))
            return CommandResult::Fail("MIPSYNC_SCENE_SERIALIZE", error);
        Json patched;
        try {
            patched = Json::parse(serialized).patch(patch);
        } catch (const std::exception& ex) {
            return CommandResult::Fail("MIPSYNC_SCENE_PATCH", ex.what());
        }
        Scene validationScene;
        if (!SceneIO::LoadFromJsonString(validationScene, patched.dump(), error))
            return CommandResult::Fail("MIPSYNC_SCENE_PATCH_VALIDATE", error);
        editor.RecordUndoSnapshot();
        if (!SceneIO::LoadFromJsonString(scene, patched.dump(), error))
            return CommandResult::Fail("MIPSYNC_SCENE_PATCH_APPLY", error);
        editor.AfterSceneRestoredFromHistory(false);
        return CommandResult::Ok({{"applied", true}, {"operations", patch.size()},
                                  {"entityCount", scene.GetEntities().size()}, {"dirty", editor.IsSceneDirty()}});
    }
    if (request.command == "scene.save") {
        std::string error;
        if (!editor.CommandSaveScene(error))
            return CommandResult::Fail("MIPSYNC_SCENE_SAVE", error);
        return CommandResult::Ok({{"path", editor.GetSceneFilePath()}, {"saved", true}, {"dirty", editor.IsSceneDirty()}});
    }
    if (request.command == "editor.undo" || request.command == "editor.redo") {
        std::string error;
        const bool undo = request.command == "editor.undo";
        const bool success = undo ? editor.CommandUndo(error) : editor.CommandRedo(error);
        if (!success)
            return CommandResult::Fail(undo ? "MIPSYNC_UNDO" : "MIPSYNC_REDO", error);
        return CommandResult::Ok({{"action", undo ? "undo" : "redo"}, {"dirty", editor.IsSceneDirty()}});
    }
    if (request.command == "entity.list") {
        Json entities = Json::array();
        for (const auto& entity : scene.GetEntities()) {
            entities.push_back({
                {"id", entity->GetID()}, {"name", EntityName(*entity)}, {"active", entity->IsActive()},
                {"parentId", entity->GetParentID()}, {"components", ComponentNames(*entity)},
            });
        }
        return CommandResult::Ok({{"entities", std::move(entities)}, {"count", scene.GetEntities().size()}});
    }
    if (request.command == "entity.inspect") {
        const std::string query = request.arguments.at("entity").get<std::string>();
        std::string lookupError;
        Entity* entity = FindEntity(scene, query, &lookupError);
        if (!entity) return CommandResult::Fail("MIPSYNC_ENTITY_NOT_FOUND", lookupError);
        auto value=DescribeEntity(*entity);
        if(request.arguments.value("include-components",false)) {
            const auto serialized=sceneJson(scene);
            for(const auto& item:serialized["entities"])if(item["id"]==entity->GetID())value["serialized"]=item;
        }
        return CommandResult::Ok(value);
    }
    if (request.command == "entity.select") {
        const std::string query = request.arguments.at("entity").get<std::string>();
        std::string lookupError;
        Entity* entity = FindEntity(scene, query, &lookupError);
        if (!entity) return CommandResult::Fail("MIPSYNC_ENTITY_NOT_FOUND", lookupError);
        editor.CommandRevealEntity(entity->GetID(), true);
        return CommandResult::Ok(DescribeEntity(*entity));
    }
    if (request.command == "audio.set") {
        CommandResult blocked; if (!RequireEditMode(editor, blocked)) return blocked;
        std::string error;
        Entity* entity = FindEntity(scene, request.arguments.at("entity").get<std::string>(), &error);
        auto* source = entity ? entity->GetComponent<AudioSourceComponent>() : nullptr;
        if (!source) return CommandResult::Fail("MIPSYNC_AUDIO_SOURCE", "Choose an entity with an AudioSource component.");
        auto type = source->type;
        auto clip = source->clipPath;
        if (request.arguments.contains("clip")) {
            clip = request.arguments.at("clip").get<std::string>();
            if (Lower(clip) == "none") clip.clear();
            if (IsMidiPath(clip) && !request.arguments.contains("type")) type = AudioSourceType::SFX;
        }
        if (request.arguments.contains("type")) {
            auto value = Lower(request.arguments.at("type").get<std::string>());
            if (value != "bgm" && value != "sfx" && value != "sample") return CommandResult::Fail("MIPSYNC_AUDIO_TYPE", "Type must be bgm, sfx or sample.");
            type = value == "sfx" ? AudioSourceType::SFX : value == "sample" ? AudioSourceType::SampleSFX : AudioSourceType::BGM;
        }
        float volume = request.arguments.value("volume", source->volume);
        if (!std::isfinite(volume) || volume < 0 || volume > 1) return CommandResult::Fail("MIPSYNC_AUDIO_VOLUME", "Volume must be between 0 and 1.");
        if (!clip.empty()) {
            if ((type == AudioSourceType::SFX) != IsMidiPath(clip)) return CommandResult::Fail("MIPSYNC_AUDIO_CLIP", "SFX requires .mid/.midi; BGM requires recorded audio.");
            auto path = PathUtf8::FromString(clip);
            if (path.is_absolute()) return CommandResult::Fail("MIPSYNC_AUDIO_CLIP", "Use a project-relative clip path.");
            path = PathUtf8::FromString(AssetManager::Get().GetProjectRoot()) / path;
            std::error_code ec;
            if (!std::filesystem::is_regular_file(path, ec)) return CommandResult::Fail("MIPSYNC_AUDIO_CLIP", "Audio asset does not exist.");
            if (type == AudioSourceType::SFX) {
                MidiSequence midi; if (!ReadMidi(PathUtf8::ToString(path), midi, error)) return CommandResult::Fail("MIPSYNC_MIDI", error);
            }
        }
        editor.RecordUndoSnapshot();
        if (type != AudioSourceType::BGM && source->type != type) source->playOnAwake = false;
        source->type = type; source->clipPath = clip; source->volume = volume;
        if (request.arguments.contains("play-on-awake")) source->playOnAwake = request.arguments.at("play-on-awake").get<bool>();
        if (request.arguments.contains("loop")) source->loop = request.arguments.at("loop").get<bool>();
        if (request.arguments.contains("mute")) source->mute = request.arguments.at("mute").get<bool>();
        editor.CommandMarkSceneDirty();
        return CommandResult::Ok({{"entity", entity->GetID()}, {"type", type == AudioSourceType::SFX ? "sfx" : type == AudioSourceType::SampleSFX ? "sample" : "bgm"},
            {"clip", clip}, {"volume", source->volume}, {"playOnAwake", source->playOnAwake}, {"loop", source->loop}, {"mute", source->mute}});
    }
    if (request.command == "ui.button-group-sounds") {
        CommandResult blocked; if (!RequireEditMode(editor, blocked)) return blocked;
        std::string error;
        Entity* entity = FindEntity(scene, request.arguments.at("entity").get<std::string>(), &error);
        auto* group = entity ? entity->GetComponent<UIButtonGroupComponent>() : nullptr;
        if (!group) return CommandResult::Fail("MIPSYNC_BUTTON_GROUP", "Choose an entity with a UIButtonGroup component.");
        uint32_t navigation = group->navigationAudioSourceId, press = group->confirmAudioSourceId;
        auto resolve = [&](const char* key, uint32_t& id) {
            if (!request.arguments.contains(key)) return true;
            auto query = request.arguments.at(key).get<std::string>();
            if (query == "0" || Lower(query) == "none") {id=0;return true;}
            Entity* target = FindEntity(scene, query, &error);
            auto* audio = target ? target->GetComponent<AudioSourceComponent>() : nullptr;
            if (!audio || audio->type == AudioSourceType::BGM) {error="Button sounds require an SFX AudioSource, or none.";return false;}
            id=target->GetID();return true;
        };
        if (!resolve("navigation",navigation) || !resolve("press",press)) return CommandResult::Fail("MIPSYNC_BUTTON_SOUND",error);
        editor.RecordUndoSnapshot(); group->navigationAudioSourceId=navigation;group->confirmAudioSourceId=press;
        editor.CommandMarkSceneDirty();
        return CommandResult::Ok({{"entity",entity->GetID()},{"navigationAudioSource",navigation},{"confirmAudioSource",press}});
    }
    if (request.command == "entity.set") {
        CommandResult blocked;
        if (!RequireEditMode(editor, blocked)) return blocked;
        const std::string query = request.arguments.at("entity").get<std::string>();
        std::string lookupError;
        Entity* entity = FindEntity(scene, query, &lookupError);
        if (!entity) return CommandResult::Fail("MIPSYNC_ENTITY_NOT_FOUND", lookupError);
        const char* properties[] = {"name", "active", "static", "tag", "layer"};
        bool hasValue = false;
        for (const char* property : properties) hasValue |= request.arguments.contains(property);
        if (!hasValue) return CommandResult::Fail("MIPSYNC_ENTITY_SET_EMPTY", "Supply at least one entity property.");
        if (request.arguments.contains("name") && request.arguments.at("name").get<std::string>().empty())
            return CommandResult::Fail("MIPSYNC_ENTITY_NAME", "Entity name cannot be empty.");

        editor.RecordUndoSnapshot();
        if (request.arguments.contains("name"))
            entity->GetComponent<TagComponent>()->tag = request.arguments.at("name").get<std::string>();
        if (request.arguments.contains("active")) entity->SetActive(request.arguments.at("active").get<bool>());
        if (request.arguments.contains("static")) entity->SetStatic(request.arguments.at("static").get<bool>());
        if (request.arguments.contains("tag")) entity->SetEditorTag(request.arguments.at("tag").get<std::string>());
        if (request.arguments.contains("layer")) entity->SetEditorLayer(request.arguments.at("layer").get<std::string>());
        editor.CommandMarkSceneDirty();
        editor.CommandRevealEntity(entity->GetID(), false);
        return CommandResult::Ok(DescribeEntity(*entity));
    }
    if (request.command == "entity.duplicate") {
        CommandResult blocked;
        if (!RequireEditMode(editor, blocked)) return blocked;
        const std::string query = request.arguments.at("entity").get<std::string>();
        std::string lookupError;
        Entity* source = FindEntity(scene, query, &lookupError);
        if (!source) return CommandResult::Fail("MIPSYNC_ENTITY_NOT_FOUND", lookupError);
        if (request.arguments.contains("name") && request.arguments.at("name").get<std::string>().empty())
            return CommandResult::Fail("MIPSYNC_ENTITY_NAME", "Entity name cannot be empty.");
        editor.RecordUndoSnapshot();
        Entity* duplicate = scene.DuplicateEntity(*source);
        if (!duplicate) return CommandResult::Fail("MIPSYNC_ENTITY_DUPLICATE", "Unable to duplicate entity.");
        if (request.arguments.contains("name")) {
            const std::string name = request.arguments.at("name").get<std::string>();
            duplicate->GetComponent<TagComponent>()->tag = name;
        }
        editor.CommandMarkSceneDirty();
        editor.CommandRevealEntity(duplicate->GetID(), true);
        return CommandResult::Ok(DescribeEntity(*duplicate));
    }
    if (request.command == "entity.create") {
        CommandResult blocked;
        if (!RequireEditMode(editor, blocked)) return blocked;
        const std::string name = request.arguments.at("name").get<std::string>();
        if (name.empty()) return CommandResult::Fail("MIPSYNC_ENTITY_NAME", "Entity name cannot be empty.");
        const std::string primitive = Lower(request.arguments.value("primitive", std::string{"empty"}));
        if (primitive != "empty" && primitive != "cube" && primitive != "sphere" && primitive != "plane")
            return CommandResult::Fail("MIPSYNC_ENTITY_PRIMITIVE", "Primitive must be empty, cube, sphere or plane.");
        Entity* parent = nullptr;
        if (request.arguments.contains("parent")) {
            const std::string parentQuery = request.arguments.at("parent").get<std::string>();
            std::string lookupError;
            parent = FindEntity(scene, parentQuery, &lookupError);
            if (!parent) return CommandResult::Fail("MIPSYNC_ENTITY_NOT_FOUND", "Parent " + lookupError);
        }

        editor.RecordUndoSnapshot();
        Entity* entity = scene.CreateEntity(name);
        auto* transform = entity->GetComponent<TransformComponent>();
        transform->position = {
            static_cast<float>(request.arguments.value("x", 0.0)),
            static_cast<float>(request.arguments.value("y", 0.0)),
            static_cast<float>(request.arguments.value("z", 0.0)),
        };
        transform->rotation = {
            static_cast<float>(request.arguments.value("rx", 0.0)),
            static_cast<float>(request.arguments.value("ry", 0.0)),
            static_cast<float>(request.arguments.value("rz", 0.0)),
        };
        transform->scale = {
            static_cast<float>(request.arguments.value("sx", 1.0)),
            static_cast<float>(request.arguments.value("sy", 1.0)),
            static_cast<float>(request.arguments.value("sz", 1.0)),
        };
        if (primitive != "empty") {
            auto& renderer = entity->AddComponent<MeshRendererComponent>();
            std::string display = primitive;
            display.front() = static_cast<char>(std::toupper(static_cast<unsigned char>(display.front())));
            renderer.SetPrimitive(display, 1.0f);
            if (request.arguments.contains("material")) {
                std::string materialError;
                if (!ApplyMaterial(*entity, request.arguments.at("material").get<std::string>(), materialError)) {
                    scene.DestroyEntity(entity);
                    return CommandResult::Fail("MIPSYNC_MATERIAL_APPLY", materialError);
                }
            }
        }
        if (parent) scene.SetParent(entity, parent);
        editor.CommandMarkSceneDirty();
        editor.CommandRevealEntity(entity->GetID(), true);
        return CommandResult::Ok(DescribeEntity(*entity));
    }
    if (request.command == "entity.transform") {
        CommandResult blocked;
        if (!RequireEditMode(editor, blocked)) return blocked;
        const std::string query = request.arguments.at("entity").get<std::string>();
        std::string lookupError;
        Entity* entity = FindEntity(scene, query, &lookupError);
        if (!entity) return CommandResult::Fail("MIPSYNC_ENTITY_NOT_FOUND", lookupError);
        const char* axes[] = {"x", "y", "z", "rx", "ry", "rz", "sx", "sy", "sz"};
        bool hasValue = false;
        for (const char* axis : axes) hasValue |= request.arguments.contains(axis);
        if (!hasValue) return CommandResult::Fail("MIPSYNC_TRANSFORM_EMPTY", "Supply at least one transform axis.");

        editor.RecordUndoSnapshot();
        auto* transform = entity->GetComponent<TransformComponent>();
        auto set = [&](const char* name, float& target) {
            if (request.arguments.contains(name)) target = static_cast<float>(request.arguments.at(name).get<double>());
        };
        set("x", transform->position.x); set("y", transform->position.y); set("z", transform->position.z);
        set("rx", transform->rotation.x); set("ry", transform->rotation.y); set("rz", transform->rotation.z);
        set("sx", transform->scale.x); set("sy", transform->scale.y); set("sz", transform->scale.z);
        editor.CommandMarkSceneDirty();
        editor.CommandRevealEntity(entity->GetID(), false);
        return CommandResult::Ok(DescribeEntity(*entity));
    }
    if (request.command == "entity.set-parent") {
        CommandResult blocked;
        if (!RequireEditMode(editor, blocked)) return blocked;
        const std::string query = request.arguments.at("entity").get<std::string>();
        std::string lookupError;
        Entity* entity = FindEntity(scene, query, &lookupError);
        if (!entity) return CommandResult::Fail("MIPSYNC_ENTITY_NOT_FOUND", lookupError);
        const std::string parentQuery = request.arguments.at("parent").get<std::string>();
        const std::string normalizedParent = Lower(parentQuery);
        Entity* parent = nullptr;
        if (normalizedParent != "none" && normalizedParent != "null" && normalizedParent != "0") {
            std::string parentLookupError;
            parent = FindEntity(scene, parentQuery, &parentLookupError);
            if (!parent) return CommandResult::Fail("MIPSYNC_ENTITY_NOT_FOUND", "Parent " + parentLookupError);
        }
        if (parent == entity) return CommandResult::Fail("MIPSYNC_PARENT_CYCLE", "An entity cannot parent itself.");
        for (Entity* cursor = parent; cursor; cursor = scene.FindEntity(cursor->GetParentID())) {
            if (cursor == entity)
                return CommandResult::Fail("MIPSYNC_PARENT_CYCLE", "Parenting would create a hierarchy cycle.");
        }
        editor.RecordUndoSnapshot();
        if (!scene.SetParent(entity, parent, true))
            return CommandResult::Fail("MIPSYNC_PARENT_CYCLE", "Parenting would create an invalid hierarchy cycle.");
        editor.CommandMarkSceneDirty();
        editor.CommandRevealEntity(entity->GetID(), false);
        return CommandResult::Ok(DescribeEntity(*entity));
    }
    if (request.command == "entity.delete") {
        CommandResult blocked;
        if (!RequireEditMode(editor, blocked)) return blocked;
        if (!request.arguments.value("confirm", false))
            return CommandResult::Fail("MIPSYNC_CONFIRM_REQUIRED", "Deletion requires --confirm true.");
        const std::string query = request.arguments.at("entity").get<std::string>();
        std::string lookupError;
        Entity* entity = FindEntity(scene, query, &lookupError);
        if (!entity) return CommandResult::Fail("MIPSYNC_ENTITY_NOT_FOUND", lookupError);
        const uint32_t id = entity->GetID();
        const std::string name = EntityName(*entity);
        const size_t before = scene.GetEntities().size();
        editor.RecordUndoSnapshot();
        editor.ClearEntitySelection();
        scene.DestroyEntity(entity);
        editor.CommandMarkSceneDirty();
        return CommandResult::Ok({
            {"deleted", true}, {"id", id}, {"name", name},
            {"removedEntityCount", before - scene.GetEntities().size()},
        });
    }
    if (request.command == "component.add") {
        CommandResult blocked;
        if (!RequireEditMode(editor, blocked)) return blocked;
        const std::string query = request.arguments.at("entity").get<std::string>();
        std::string lookupError;
        Entity* entity = FindEntity(scene, query, &lookupError);
        if (!entity) return CommandResult::Fail("MIPSYNC_ENTITY_NOT_FOUND", lookupError);
        std::string componentError;
        editor.RecordUndoSnapshot();
        if (!AddNamedComponent(*entity, request.arguments.at("component").get<std::string>(), componentError))
            return CommandResult::Fail("MIPSYNC_COMPONENT_ADD", componentError);
        editor.CommandMarkSceneDirty();
        editor.CommandRevealEntity(entity->GetID(), false);
        return CommandResult::Ok(DescribeEntity(*entity));
    }
    if (request.command == "component.remove") {
        CommandResult blocked;
        if (!RequireEditMode(editor, blocked)) return blocked;
        if (!request.arguments.value("confirm", false))
            return CommandResult::Fail("MIPSYNC_CONFIRM_REQUIRED", "Component removal requires --confirm true.");
        const std::string query = request.arguments.at("entity").get<std::string>();
        std::string lookupError;
        Entity* entity = FindEntity(scene, query, &lookupError);
        if (!entity) return CommandResult::Fail("MIPSYNC_ENTITY_NOT_FOUND", lookupError);
        const std::string component = request.arguments.at("component").get<std::string>();
        const std::string normalized = Lower(component);
        if (normalized == "transform" || normalized == "tag")
            return CommandResult::Fail("MIPSYNC_COMPONENT_REQUIRED", "Transform and Tag cannot be removed.");
        editor.RecordUndoSnapshot();
        if (!RemoveNamedComponent(*entity, component))
            return CommandResult::Fail("MIPSYNC_COMPONENT_NOT_FOUND", "Unsupported or missing component: " + component);
        editor.CommandMarkSceneDirty();
        editor.CommandRevealEntity(entity->GetID(), false);
        return CommandResult::Ok(DescribeEntity(*entity));
    }
    if (request.command == "component.set-enabled") {
        CommandResult blocked;
        if (!RequireEditMode(editor, blocked)) return blocked;
        const std::string query = request.arguments.at("entity").get<std::string>();
        std::string lookupError;
        Entity* entity = FindEntity(scene, query, &lookupError);
        if (!entity) return CommandResult::Fail("MIPSYNC_ENTITY_NOT_FOUND", lookupError);
        const std::string component = request.arguments.at("component").get<std::string>();
        editor.RecordUndoSnapshot();
        if (!SetNamedComponentEnabled(*entity, component, request.arguments.at("enabled").get<bool>()))
            return CommandResult::Fail("MIPSYNC_COMPONENT_NOT_FOUND", "Unsupported or missing component: " + component);
        editor.CommandMarkSceneDirty();
        editor.CommandRevealEntity(entity->GetID(), false);
        return CommandResult::Ok(DescribeEntity(*entity));
    }
    if (request.command == "mesh.set") {
        CommandResult blocked;
        if (!RequireEditMode(editor, blocked)) return blocked;
        const std::string query = request.arguments.at("entity").get<std::string>();
        std::string lookupError;
        Entity* entity = FindEntity(scene, query, &lookupError);
        if (!entity) return CommandResult::Fail("MIPSYNC_ENTITY_NOT_FOUND", lookupError);
        auto* renderer = entity->GetComponent<MeshRendererComponent>();
        if (!renderer) return CommandResult::Fail("MIPSYNC_COMPONENT_NOT_FOUND", "Entity has no MeshRenderer component.");
        const char* properties[] = {"primitive", "size", "material", "enabled", "editor-only", "type-preset", "view-model"};
        bool hasValue = false;
        for (const char* property : properties) hasValue |= request.arguments.contains(property);
        if (!hasValue) return CommandResult::Fail("MIPSYNC_MESH_SET_EMPTY", "Supply at least one renderer property.");
        editor.RecordUndoSnapshot();
        if (request.arguments.contains("primitive") || request.arguments.contains("size")) {
            std::string primitive = request.arguments.value("primitive", renderer->meshPrimitive);
            primitive = Lower(primitive);
            if (primitive != "cube" && primitive != "sphere" && primitive != "plane")
                return CommandResult::Fail("MIPSYNC_ENTITY_PRIMITIVE", "Primitive must be cube, sphere or plane.");
            primitive.front() = static_cast<char>(std::toupper(static_cast<unsigned char>(primitive.front())));
            renderer->SetPrimitive(primitive, static_cast<float>(request.arguments.value("size", static_cast<double>(renderer->meshSize))));
        }
        if (request.arguments.contains("material")) {
            std::string materialError;
            if (!ApplyMaterial(*entity, request.arguments.at("material").get<std::string>(), materialError))
                return CommandResult::Fail("MIPSYNC_MATERIAL_APPLY", materialError);
        }
        if (request.arguments.contains("enabled")) renderer->enabled = request.arguments.at("enabled").get<bool>();
        if (request.arguments.contains("editor-only")) renderer->editorOnly = request.arguments.at("editor-only").get<bool>();
        if (request.arguments.contains("type-preset")) {
            const std::string preset = Lower(request.arguments.at("type-preset").get<std::string>());
            if (preset == "prop") renderer->typePreset = MeshRendererComponent::TypePreset::Prop;
            else if (preset == "corridor") renderer->typePreset = MeshRendererComponent::TypePreset::Corridor;
            else if (preset == "character") renderer->typePreset = MeshRendererComponent::TypePreset::Character;
            else if (preset == "viewmodel" || preset == "view-model")
                renderer->typePreset = MeshRendererComponent::TypePreset::Viewmodel;
            else if (preset == "floor")
                renderer->typePreset = MeshRendererComponent::TypePreset::Floor;
            else
                return CommandResult::Fail("MIPSYNC_RENDER_PRESET",
                    "Type preset must be prop, corridor, character, viewmodel or floor.");
        }
        if (request.arguments.contains("view-model")) {
            renderer->typePreset = request.arguments.at("view-model").get<bool>()
                ? MeshRendererComponent::TypePreset::Viewmodel
                : MeshRendererComponent::TypePreset::Prop;
        }
        editor.CommandMarkSceneDirty();
        editor.CommandRevealEntity(entity->GetID(), false);
        return CommandResult::Ok(DescribeEntity(*entity));
    }
    if (request.command == "material.create") {
        CommandResult blocked;
        if (!RequireEditMode(editor, blocked)) return blocked;
        std::string relative;
        std::string materialError;
        if (!ResolveMaterialPath(request.arguments.at("path").get<std::string>(), relative, materialError))
            return CommandResult::Fail("MIPSYNC_MATERIAL_PATH", materialError);
        const double r = request.arguments.at("r").get<double>();
        const double g = request.arguments.at("g").get<double>();
        const double b = request.arguments.at("b").get<double>();
        const double a = request.arguments.value("a", 1.0);
        if (r < 0 || r > 1 || g < 0 || g > 1 || b < 0 || b > 1 || a < 0 || a > 1)
            return CommandResult::Fail("MIPSYNC_MATERIAL_COLOR", "Material color channels must be between 0 and 1.");
        Material material;
        material.color = {static_cast<float>(r), static_cast<float>(g), static_cast<float>(b), static_cast<float>(a)};
        if (request.arguments.contains("texture"))
            material.texturePath = request.arguments.at("texture").get<std::string>();
        if (!Material::Save(AssetManager::Get().ToAbsolute(relative), material, materialError))
            return CommandResult::Fail("MIPSYNC_MATERIAL_SAVE", materialError);
        AssetManager::Get().ApplyMaterialToSceneUsers(scene, relative, material);
        editor.CommandMarkSceneDirty();
        return CommandResult::Ok({{"path", relative}, {"color", {r, g, b, a}}, {"updatedSceneUsers", true}});
    }
    if (request.command == "material.apply") {
        CommandResult blocked;
        if (!RequireEditMode(editor, blocked)) return blocked;
        const std::string query = request.arguments.at("entity").get<std::string>();
        std::string lookupError;
        Entity* entity = FindEntity(scene, query, &lookupError);
        if (!entity) return CommandResult::Fail("MIPSYNC_ENTITY_NOT_FOUND", lookupError);
        std::string materialError;
        editor.RecordUndoSnapshot();
        if (!ApplyMaterial(*entity, request.arguments.at("material").get<std::string>(), materialError))
            return CommandResult::Fail("MIPSYNC_MATERIAL_APPLY", materialError);
        editor.CommandMarkSceneDirty();
        editor.CommandRevealEntity(entity->GetID(), false);
        return CommandResult::Ok(DescribeEntity(*entity));
    }
    if (request.command == "ide.open") {
        const std::string file = request.arguments.at("file").get<std::string>();
        const int line = static_cast<int>(request.arguments.value("line", int64_t{1}));
        const int column = static_cast<int>(request.arguments.value("column", int64_t{1}));
        if (!MipsEditorIntegration::OpenScriptInIde(file, line, column))
            return CommandResult::Fail("MIPSYNC_IDE_OPEN", "No supported IDE was found. Install VS Code or Cursor.");
        return CommandResult::Ok({{"opened", true}, {"file", file}, {"line", line}, {"column", column}});
    }
    if (request.command == "runtime.play") {
        editor.CommandRefreshSceneDirtyState();
        if(editor.IsSceneDirty() && !editor.IsPlaying() && !editor.IsPreparingPlay()) {
            if(!request.arguments.value("save-current",false)) {
                auto result=CommandResult::Fail("MIPSYNC_SAVE_REQUIRED","Save the scene or use runtime.play --save-current true.");
                result.status="save_required";result.value={{"state","save_required"},{"playing",false},{"preparing",false}};return result;
            }
            std::string error;if(!editor.CommandSaveScene(error))return CommandResult::Fail("MIPSYNC_SAVE_FAILED",error);
        }
        editor.CommandStartPlayMode();
        auto result=CommandResult::Ok({{"state",editor.IsPreparingPlay()?"preparing":editor.IsPlaying()?"playing":"failed"},
            {"playing", editor.IsPlaying()},{"preparing",editor.IsPreparingPlay()}, {"paused", editor.IsPaused()}});
        if(editor.IsPreparingPlay())result.status="pending";
        return result;
    }
    if (request.command == "runtime.stop") {
        editor.CommandStopPlayMode();
        return CommandResult::Ok({{"playing", editor.IsPlaying()}, {"paused", editor.IsPaused()}});
    }
    if (request.command == "runtime.inspect") {
        const auto& sync = m_Engine.GetPSyncState();
        const auto& w = sync.workload;
        Json animators=Json::array();
        if(!editor.IsPreparingPlay())for(const auto& entity:scene.GetEntities())if(auto* a=entity->GetComponent<AnimatorComponent>()) {
            auto value=InspectAnimator(*a);value["entity"]=entity->GetID();animators.push_back(value);
        }
        return CommandResult::Ok({{"playing", editor.IsPlaying()}, {"paused", editor.IsPaused()},
            {"state",editor.IsPreparingPlay()?"preparing":editor.IsPlaying()?"playing":editor.GetPlaySetupError().empty()?"stopped":"failed"},
            {"preparing",editor.IsPreparingPlay()},{"error",editor.GetPlaySetupError()},
            {"queuedFrames",m_Engine.GetQueuedCliFrames()},
            {"animators",animators},
            {"execution",editor.IsPreparingPlay()?Json::object():m_Engine.GetMipsRuntime().InspectExecution()},
            {"input", {{"enabled", Input::IsGameInputEnabled()}, {"cursorLocked", Input::IsCursorLocked()},
                {"leftStickX", Input::GetPS1Axis("LeftStickX")}, {"leftStickY", Input::GetPS1Axis("LeftStickY")},
                {"rightStickX", Input::GetPS1Axis("RightStickX")}, {"rightStickY", Input::GetPS1Axis("RightStickY")}}},
            {"pSync", {{"enabled", editor.GetGameViewSettings().pSync}, {"sampleValid", sync.sampleValid},
                {"fps", sync.estimate.fps}, {"vblanks", sync.estimate.vblanks},
                {"cpuMs", sync.estimate.cpuMs}, {"gpuMs", sync.estimate.gpuMs}, {"dmaMs", sync.estimate.dmaMs},
                {"submittedTriangles", w.submittedTriangles}, {"drawnTriangles", w.drawnTriangles},
                {"projectedVertices", w.projectedVertices}, {"litVertices", w.litVertices},
                {"litFaces", w.litFaces}, {"pointLights", w.pointLights}, {"spotLights", w.spotLights},
                {"lights", w.lights}, {"colorGradingPixels", w.colorGradingPixels}}}});
    }
    return CommandResult::Fail("MIPSYNC_EDITOR_COMMAND_NOT_FOUND", "Unsupported Editor command: " + request.command);
}

} // namespace MipsyncEngine::Command
