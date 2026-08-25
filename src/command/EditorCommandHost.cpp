#include "EditorCommandHost.h"

#include "CommandLine.h"
#include "ResultRenderer.h"
#include "../assets/AssetManager.h"
#include "../assets/Material.h"
#include "../core/Engine.h"
#include "../core/Log.h"
#include "../editor/EditorApp.h"
#include "../editor/MipsEditorIntegration.h"
#include "../scene/Scene.h"
#include "../scene/SceneIO.h"
#include "EngineVersion.h"

#include <charconv>
#include <chrono>
#include <algorithm>
#include <cctype>
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
    m_Instance.projectId = std::filesystem::path(m_Instance.projectPath).filename().string();
    m_Instance.engineVersion = MIPSYNC_ENGINE_VERSION;
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

void EditorCommandHost::Pump() {
    std::queue<std::shared_ptr<PendingRequest>> pending;
    {
        std::lock_guard lock(m_QueueMutex);
        std::swap(pending, m_Pending);
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
        const auto result = m_Executor.Execute(request->request, m_Context);
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
    return RenderHuman(parsed.request.command, m_Executor.Execute(parsed.request, m_Context));
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
        editor.AfterSceneRestoredFromHistory();
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
        return CommandResult::Ok(DescribeEntity(*entity));
    }
    if (request.command == "entity.select") {
        const std::string query = request.arguments.at("entity").get<std::string>();
        std::string lookupError;
        Entity* entity = FindEntity(scene, query, &lookupError);
        if (!entity) return CommandResult::Fail("MIPSYNC_ENTITY_NOT_FOUND", lookupError);
        editor.CommandRevealEntity(entity->GetID(), true);
        return CommandResult::Ok(DescribeEntity(*entity));
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
        scene.SetParent(duplicate, scene.FindEntity(source->GetParentID()));
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
        if (!scene.SetParent(entity, parent))
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
        const char* properties[] = {"primitive", "size", "material", "enabled", "editor-only", "view-model"};
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
        if (request.arguments.contains("view-model")) renderer->viewModel = request.arguments.at("view-model").get<bool>();
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
        editor.CommandStartPlayMode();
        return CommandResult::Ok({{"playing", editor.IsPlaying()}, {"paused", editor.IsPaused()}});
    }
    if (request.command == "runtime.stop") {
        editor.CommandStopPlayMode();
        return CommandResult::Ok({{"playing", editor.IsPlaying()}, {"paused", editor.IsPaused()}});
    }
    if (request.command == "runtime.inspect")
        return CommandResult::Ok({{"playing", editor.IsPlaying()}, {"paused", editor.IsPaused()}});
    return CommandResult::Fail("MIPSYNC_EDITOR_COMMAND_NOT_FOUND", "Unsupported Editor command: " + request.command);
}

} // namespace MipsyncEngine::Command
