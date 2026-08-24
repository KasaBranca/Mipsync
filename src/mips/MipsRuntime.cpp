#include "MipsRuntime.h"
#include "Compiler.h"
#include "Lexer.h"
#include "Parser.h"
#include "../core/Log.h"
#include "../assets/AssetManager.h"
#include "../scene/Scene.h"
#include "../physics/ColliderUtils.h"
#include <fstream>
#include <sstream>
#include <filesystem>
#include <vector>

namespace MipsyncEngine::Mips {

namespace {

bool ReadFile(const std::string& path, std::string& out) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open())
        return false;
    std::ostringstream buffer;
    buffer << file.rdbuf();
    out = buffer.str();
    return true;
}

std::string FileNameOnly(const std::string& path) {
    const size_t pos = path.find_last_of("/\\");
    return pos == std::string::npos ? path : path.substr(pos + 1);
}

bool FileExists(const std::string& path) {
    std::error_code ec;
    return std::filesystem::exists(path, ec);
}

std::string ResolveScriptPath(const std::string& path) {
    const std::string fileName = FileNameOnly(path);

    static const char* kRoots[] = {
        ".",
        "..",
        "../..",
        "../../..",
        "../../../..",
    };

    std::vector<std::string> candidates;
    candidates.push_back(path);

    for (const char* root : kRoots) {
        candidates.push_back(std::string(root) + "/" + path);
        candidates.push_back(std::string(root) + "/scripts/" + fileName);
        candidates.push_back(std::string(root) + "/examples/" + fileName);
    }

    for (const auto& candidate : candidates) {
        if (FileExists(candidate))
            return candidate;
    }

    return path;
}

void InvokeLifecycle(VM& vm, ScriptInstance& instance, const char* method,
                     std::vector<std::string>& errors) {
    if (instance.module->FindMethod(method))
        vm.RunMethod(instance, method, errors);
}

double FieldDefault(const CompiledModule& module, size_t fieldIndex) {
    if (fieldIndex >= module.fields.size())
        return 0.0;
    const uint16_t cidx = module.fields[fieldIndex].defaultConstIndex;
    return (cidx < module.numberConstants.size()) ? module.numberConstants[cidx] : 0.0;
}

void SyncFieldValuesFromModule(MipsScriptComponent& script) {
    if (!script.module)
        return;
    script.fieldValues.resize(script.module->fields.size());
    script.fieldAssetPaths.resize(script.module->fields.size());
    for (size_t i = 0; i < script.module->fields.size(); ++i)
        script.fieldValues[i] = FieldDefault(*script.module, i);
}

} // namespace

std::shared_ptr<CompiledModule> MipsRuntime::CompileScriptFile(const std::string& path,
                                                             std::vector<std::string>& errors) {
    const std::string resolved = ResolveScriptPath(path);
    std::string source;
    if (!ReadFile(resolved, source)) {
        errors.push_back("failed to open script: " + path + " (resolved: " + resolved + ")");
        return nullptr;
    }

    Lexer lexer(source, resolved);
    const std::vector<Token>& tokens = lexer.Tokenize();
    errors.insert(errors.end(), lexer.GetErrors().begin(), lexer.GetErrors().end());
    if (!lexer.GetErrors().empty())
        return nullptr;

    Parser parser(tokens, resolved);
    auto program = parser.ParseProgram();
    errors.insert(errors.end(), parser.GetErrors().begin(), parser.GetErrors().end());
    if (!program || program->classes.empty() || !parser.GetErrors().empty())
        return nullptr;

    return CompileClass(*program->classes[0], program.get(), resolved, errors);
}

void MipsRuntime::ResetScriptsByFileName(Scene& scene, const std::string& fileName) {
    std::vector<std::string> errors;
    for (const auto& entityPtr : scene.GetEntities()) {
      for (auto* script : entityPtr->GetComponents<MipsScriptComponent>()) {
        if (script->scriptPath.empty()) continue;
        if (script->scriptPath.size() < fileName.size())
            continue;
        if (script->scriptPath.compare(script->scriptPath.size() - fileName.size(),
                                       fileName.size(), fileName) != 0)
            continue;

        script->module = nullptr;
        script->fieldValues.clear();
        if (!EnsureScriptReady(*script, errors)) {
            for (const std::string& err : errors)
                MIPSYNC_WARN("[Mips] {}", err);
            errors.clear();
        }
      }
    }
}

bool MipsRuntime::EnsureScriptReady(MipsScriptComponent& script, std::vector<std::string>& errors) {
    if (script.scriptPath.empty())
        return false;

    if (!script.module) {
        script.module = CompileScriptFile(script.scriptPath, errors);
        if (!script.module)
            return false;
        SyncFieldValuesFromModule(script);
        return true;
    }

    if (script.fieldValues.size() != script.module->fields.size() ||
        script.fieldAssetPaths.size() != script.module->fields.size())
        SyncFieldValuesFromModule(script);
    return true;
}

void MipsRuntime::ApplyFieldOverrides(Entity* entity, const MipsScriptComponent& script) {
    if (!entity || script.fieldValues.size() != script.module->fields.size())
        return;

    for (auto& instance : m_Instances) {
        if (instance.entity != entity || instance.sourceComponent != &script || !instance.module)
            continue;
        if (instance.fields.size() != script.fieldValues.size())
            instance.fields.resize(script.fieldValues.size());
        instance.fields = script.fieldValues;
        instance.runtimeFields.resize(instance.fields.size());
        for (size_t i = 0; i < instance.fields.size(); ++i) {
            Value& value = instance.runtimeFields[i];
            if (value.tag == Value::Tag::Array)
                continue;
            if (i < instance.module->fields.size() &&
                instance.module->fields[i].valueKind == FieldValueKind::Bool) {
                value.tag = Value::Tag::Bool;
                value.boolValue = instance.fields[i] != 0.0;
            } else if (i < instance.module->fields.size() &&
                       instance.module->fields[i].valueKind != FieldValueKind::Array) {
                value.tag = Value::Tag::Number;
                value.number = instance.fields[i];
            }
        }
        instance.assetFields = script.fieldAssetPaths;
        break;
    }
}

void MipsRuntime::SyncEditSnapshot(Scene& scene) {
    if (m_Playing)
        return;
    CaptureSceneSnapshot(scene);
}

void MipsRuntime::CaptureSceneSnapshot(Scene& scene) {
    m_EditSnapshot.clear();
    for (const auto& entityPtr : scene.GetEntities()) {
        Entity* entity = entityPtr.get();
        if (!entity->IsActive()) continue;
        auto* transform = entity->GetComponent<TransformComponent>();
        if (!transform)
            continue;
        TransformSnapshot snap;
        snap.entityId = entity->GetID();
        snap.position = transform->position;
        snap.rotation = transform->rotation;
        snap.scale = transform->scale;
        m_EditSnapshot.push_back(snap);
    }
}

void MipsRuntime::RestoreSceneSnapshot(Scene& scene) {
    for (const TransformSnapshot& snap : m_EditSnapshot) {
        for (const auto& entityPtr : scene.GetEntities()) {
            Entity* entity = entityPtr.get();
            if (entity->GetID() != snap.entityId)
                continue;
            if (auto* transform = entity->GetComponent<TransformComponent>()) {
                transform->position = snap.position;
                transform->rotation = snap.rotation;
                transform->scale = snap.scale;
            }
            break;
        }
    }
}

void MipsRuntime::CollectInstances(Scene& scene) {
    m_Instances.clear();

    for (const auto& entityPtr : scene.GetEntities()) {
        Entity* entity = entityPtr.get();
      for (auto* script : entity->GetComponents<MipsScriptComponent>()) {
        if (!script->enabled || script->scriptPath.empty()) continue;

        std::vector<std::string> errors;
        if (!script->module) {
            script->module = CompileScriptFile(script->scriptPath, errors);
            for (const auto& e : errors)
                MIPSYNC_WARN("[Mips#] {}", e);
        }
        if (!script->module)
            continue;

        if (script->module->className == "FirstPersonController") {
            ColliderUtils::EnsureFirstPersonPhysics(*entity);
        } else if (script->module->className == "RadioController") {
            // Migrate scenes that still carry the old CharacterVirtual flag.
            // Radio movement is horizontal/kinematic; penetration recovery on
            // a stale character controller changed Y as soon as Play began.
            if (auto* rb = entity->GetComponent<RigidbodyComponent>()) {
                rb->characterController = false;
                rb->bodyType = RigidbodyType::Kinematic;
                rb->useGravity = false;
            }
        }

        ScriptInstance instance;
        instance.module = script->module;
        instance.entity = entity;
        instance.sourceComponent = script;
        instance.fields.resize(script->module->fields.size());
        instance.runtimeFields.resize(script->module->fields.size());
        instance.assetFields.resize(script->module->fields.size());
        instance.coroutines.reserve(16);
        const bool useInspectorValues =
            script->fieldValues.size() == script->module->fields.size();
        for (size_t i = 0; i < script->module->fields.size(); ++i) {
            instance.fields[i] = useInspectorValues
                ? script->fieldValues[i]
                : FieldDefault(*script->module, i);
            if (script->module->fields[i].valueKind == FieldValueKind::Bool) {
                instance.runtimeFields[i].tag = Value::Tag::Bool;
                instance.runtimeFields[i].boolValue = instance.fields[i] != 0.0;
            } else if (script->module->fields[i].valueKind != FieldValueKind::AudioClip &&
                       script->module->fields[i].valueKind != FieldValueKind::Array) {
                instance.runtimeFields[i].tag = Value::Tag::Number;
                instance.runtimeFields[i].number = instance.fields[i];
            }
            if (useInspectorValues && i < script->fieldAssetPaths.size())
                instance.assetFields[i] = script->fieldAssetPaths[i];
        }
        m_Instances.push_back(std::move(instance));
      }
    }
}

void MipsRuntime::ReloadChangedScripts(MipsyncEngine::Scene& scene) {
    namespace fs = std::filesystem;
    AssetManager& assets = AssetManager::Get();

    for (auto& entityPtr : scene.GetEntities()) {
      for (auto* script : entityPtr->GetComponents<MipsScriptComponent>()) {
        if (script->scriptPath.empty()) continue;

        const std::string abs = assets.ToAbsolute(script->scriptPath);
        std::error_code ec;
        if (!fs::exists(abs, ec))
            continue;

        const auto mtime = fs::last_write_time(abs, ec);
        if (ec)
            continue;

        auto it = m_ScriptFileTimes.find(script->scriptPath);
        if (it == m_ScriptFileTimes.end()) {
            m_ScriptFileTimes[script->scriptPath] = mtime;
            continue;
        }
        if (it->second == mtime)
            continue;
        it->second = mtime;

        std::vector<std::string> errors;
        auto newModule = CompileScriptFile(script->scriptPath, errors);
        for (const auto& e : errors)
            MIPSYNC_WARN("[Mips#] {}", e);
        if (!newModule)
            continue;

        const std::vector<double> oldFields = script->fieldValues;
        const std::vector<std::string> oldAssetFields = script->fieldAssetPaths;
        script->module = newModule;
        script->fieldValues.resize(newModule->fields.size());
        script->fieldAssetPaths.resize(newModule->fields.size());
        for (size_t i = 0; i < newModule->fields.size(); ++i) {
            if (i < oldFields.size())
                script->fieldValues[i] = oldFields[i];
            else
                script->fieldValues[i] = FieldDefault(*newModule, i);
            if (i < oldAssetFields.size())
                script->fieldAssetPaths[i] = oldAssetFields[i];
        }

        for (auto& instance : m_Instances) {
            if (instance.entity == entityPtr.get() && instance.sourceComponent == script) {
                instance.module = script->module;
                instance.fields = script->fieldValues;
                instance.runtimeFields.clear();
                instance.coroutines.clear();
                instance.assetFields = script->fieldAssetPaths;
            }
        }

        MIPSYNC_INFO("[Mips#] Reloaded script: {}", script->scriptPath);
      }
    }
}

void MipsRuntime::InvokeLifecycleAll(const char* method, MipsyncEngine::Scene& scene, float deltaTime) {
    m_VM.SetScene(&scene);
    m_VM.SetPhysicsWorld(m_PhysicsWorld);
    m_VM.SetDeltaTime(deltaTime);
    m_VM.SetActiveInstances(&m_Instances);
    std::vector<std::string> errors;
    for (auto& instance : m_Instances) {
        if (instance.module && instance.module->FindMethod(method))
            m_VM.RunMethod(instance, method, errors);
    }
    for (const auto& e : errors)
        MIPSYNC_WARN("[Mips#] {}", e);
}

void MipsRuntime::OnPlayStarted(Scene& scene) {
    m_ScriptFileTimes.clear();
    // The currently visible edit scene is authoritative. Restoring a cached
    // baseline here caused an immediate jump when Play was pressed.
    CaptureSceneSnapshot(scene);

    m_Playing = true;
    m_Paused = false;
    CollectInstances(scene);
    m_VM.SetActiveInstances(&m_Instances);

    m_VM.SetScene(&scene);
    m_VM.SetPhysicsWorld(m_PhysicsWorld);
    std::vector<std::string> errors;
    m_VM.SetDeltaTime(0.0f);
    for (auto& instance : m_Instances) {
        InvokeLifecycle(m_VM, instance, "Awake", errors);
        InvokeLifecycle(m_VM, instance, "Start", errors);
    }
    for (const auto& e : errors)
        MIPSYNC_WARN("[Mips#] {}", e);

    MIPSYNC_INFO("[Mips#] Play started ({} script instance(s))", m_Instances.size());
}

void MipsRuntime::OnPlayStopped(Scene& scene) {
    if (m_Playing) {
        std::vector<std::string> errors;
        for (auto& instance : m_Instances) {
            InvokeLifecycle(m_VM, instance, "OnDestroy", errors);
        }
        for (const auto& e : errors)
            MIPSYNC_WARN("[Mips#] {}", e);
    }

    if (!m_EditSnapshot.empty())
        RestoreSceneSnapshot(scene);
    CaptureSceneSnapshot(scene);

    m_Instances.clear();
    m_Playing = false;
    m_Paused = false;
    MIPSYNC_INFO("[Mips#] Play stopped");
}

void MipsRuntime::DispatchPhysicsEvents(Scene& scene) {
    if (m_PhysicsEvents.Events().empty())
        return;

    m_VM.SetScene(&scene);
    m_VM.SetPhysicsWorld(m_PhysicsWorld);
    m_VM.SetActiveInstances(&m_Instances);
    std::vector<std::string> errors;

    for (const MipsPhysicsEvent& ev : m_PhysicsEvents.Events()) {
        Entity* self = scene.FindEntity(ev.selfEntityId);
        if (!self)
            continue;

        const char* methodName = nullptr;
        switch (ev.kind) {
        case MipsPhysicsEvent::Kind::CollisionEnter: methodName = "OnCollisionEnter"; break;
        case MipsPhysicsEvent::Kind::CollisionExit: methodName = "OnCollisionExit"; break;
        case MipsPhysicsEvent::Kind::TriggerEnter: methodName = "OnTriggerEnter"; break;
        case MipsPhysicsEvent::Kind::TriggerExit: methodName = "OnTriggerExit"; break;
        }

        if (!methodName)
            continue;

        for (auto& instance : m_Instances) {
            if (instance.entity != self || !instance.module)
                continue;
            if (!instance.module->FindMethod(methodName))
                continue;
            m_VM.SetPhysicsOtherEntityId(ev.otherEntityId);
            m_VM.RunMethod(instance, methodName, errors);
        }
    }

    for (const auto& e : errors)
        MIPSYNC_WARN("[Mips#] {}", e);
    m_PhysicsEvents.Clear();
}

void MipsRuntime::Update(Scene& scene, float deltaTime) {
    if (!m_Playing || m_Paused)
        return;

    if (m_Instances.empty())
        CollectInstances(scene);

    InvokeLifecycleAll("Update", scene, deltaTime);
    std::vector<std::string> coroutineErrors;
    for (auto& instance : m_Instances)
        m_VM.ResumeCoroutines(instance, deltaTime, coroutineErrors);
    for (const auto& error : coroutineErrors)
        MIPSYNC_WARN("[Mips#] {}", error);
}

void MipsRuntime::LateUpdate(Scene& scene, float deltaTime) {
    if (!m_Playing || m_Paused)
        return;
    InvokeLifecycleAll("LateUpdate", scene, deltaTime);
}

} // namespace MipsyncEngine::Mips
