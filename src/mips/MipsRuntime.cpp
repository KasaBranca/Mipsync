#include "../core/PreparationCancellation.h"
#include "MipsRuntime.h"
#include "MipsScriptLoader.h"
#include "../core/Log.h"
#include "../assets/AssetManager.h"
#include "../scene/Scene.h"
#include "../physics/PhysicsWorld.h"
#include <filesystem>
#include <vector>
#include <unordered_set>

namespace MipsyncEngine::Mips {

namespace {

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

void RestoreNamedFields(MipsScriptComponent& script, const std::shared_ptr<CompiledModule>& oldModule,
                        const std::vector<double>& oldValues, const std::vector<std::string>& oldAssets) {
    SyncFieldValuesFromModule(script);
    script.fieldAssetPaths.assign(script.module->fields.size(),{});
    if (!oldModule) return;
    for (size_t i=0; i<script.module->fields.size(); ++i) {
        const auto& field=script.module->fields[i];
        for (size_t old=0; old<oldModule->fields.size(); ++old) {
            if (oldModule->fields[old].name!=field.name || oldModule->fields[old].valueKind!=field.valueKind) continue;
            if (old<oldValues.size()) script.fieldValues[i]=oldValues[old];
            if (old<oldAssets.size()) script.fieldAssetPaths[i]=oldAssets[old];
            break;
        }
    }
}

} // namespace

std::shared_ptr<CompiledModule> MipsRuntime::CompileScriptFile(const std::string& path,
                                                             std::vector<std::string>& errors) {
    return MipsScriptLoader::CompileFile(
        PathUtf8::FromString(AssetManager::Get().GetProjectRoot()), path, errors);
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

        auto newModule=CompileScriptFile(script->scriptPath,errors);
        if (!newModule) {
            for (const std::string& err : errors)
                MIPSYNC_WARN("[Mips] {}", err);
            errors.clear();
        } else {
            const auto oldModule=script->module;
            const auto oldValues=script->fieldValues;
            const auto oldAssets=script->fieldAssetPaths;
            script->module=std::move(newModule);
            RestoreNamedFields(*script,oldModule,oldValues,oldAssets);
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
    m_EditSnapshot.Capture(scene);
}

void MipsRuntime::CollectInstances(Scene& scene) {
    m_Instances.clear();

    for (const auto& entityPtr : scene.GetEntities()) {
        CheckPreparationCancelled();
        Entity* entity = entityPtr.get();
      for (auto* script : entity->GetComponents<MipsScriptComponent>()) {
        if (!script->enabled || script->scriptPath.empty()) continue;
        AddInstance(entity, *script);
      }
    }
}

bool MipsRuntime::AddInstance(Entity* entity, MipsScriptComponent& component) {
        auto* script = &component;

        std::vector<std::string> errors;
        if (!script->module) {
            script->module = CompileScriptFile(script->scriptPath, errors);
            for (const auto& e : errors)
                MIPSYNC_CONSOLE_WARN("{}", e);
        }
        if (!script->module)
            return false;

        ScriptInstance instance;
        instance.module = script->module;
        instance.entity = entity;
        instance.entityId = entity->GetID();
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
        return true;
}

void MipsRuntime::RefreshPlayInstances(Scene& scene, bool addNew) {
    if (!m_Playing) return;
    std::unordered_map<MipsScriptComponent*, Entity*> scripts;
    std::unordered_set<Entity*> entities;
    std::unordered_set<TransformComponent*> transforms;
    std::unordered_set<float*> vectorAxes;
    for (const auto& entity : scene.GetEntities()) {
        entities.insert(entity.get());
        if (auto* transform = entity->GetComponent<TransformComponent>()) {
            transforms.insert(transform);
            for (glm::vec3* vector : {&transform->position, &transform->rotation, &transform->scale})
                for (int axis = 0; axis < 3; ++axis) vectorAxes.insert(&(*vector)[axis]);
        }
        for (auto* script : entity->GetComponents<MipsScriptComponent>())
            if (!script->scriptPath.empty()) scripts.emplace(script, entity.get());
    }
    // References to another script point into this vector, so retain their
    // component identities before erasing or growing the instance storage.
    std::unordered_map<ScriptInstance*, MipsScriptComponent*> referencedScripts;
    for (auto& instance : m_Instances) referencedScripts.emplace(&instance, instance.sourceComponent);
    std::erase_if(m_Instances, [&](const ScriptInstance& instance) {
        const auto found = scripts.find(instance.sourceComponent);
        return found == scripts.end() || found->second != instance.entity ||
               found->second->GetID() != instance.entityId ||
               instance.sourceComponent->module != instance.module;
    });
    std::unordered_set<MipsScriptComponent*> attached;
    for (const auto& instance : m_Instances) attached.insert(instance.sourceComponent);
    std::vector<size_t> newInstances;
    if (addNew) {
        for (const auto& entity : scene.GetEntities())
            for (auto* script : entity->GetComponents<MipsScriptComponent>())
                if (script->enabled && !script->scriptPath.empty() && !attached.contains(script)) {
                    const size_t index = m_Instances.size();
                    if (AddInstance(entity.get(), *script)) newInstances.push_back(index);
                }
    }
    std::unordered_map<MipsScriptComponent*, ScriptInstance*> currentScripts;
    for (auto& instance : m_Instances) currentScripts.emplace(instance.sourceComponent, &instance);
    std::unordered_set<ArrayValue*> arrays;
    auto repairReference = [&](auto&& self, Value& value) -> void {
        if (value.tag == Value::Tag::Array && value.array && arrays.insert(value.array.get()).second)
            for (auto& item : value.array->elements) self(self, item);
        if (value.tag != Value::Tag::Host) return;
        auto& host = value.host;
        if ((host.entity && !entities.contains(host.entity)) ||
            (host.transform && !transforms.contains(host.transform)) ||
            (host.kind == HostKind::Vec3 &&
             (!vectorAxes.contains(host.vec3.x) || !vectorAxes.contains(host.vec3.y) ||
              !vectorAxes.contains(host.vec3.z)))) {
            value = {}; return;
        }
        if (host.kind == HostKind::MipsScript && host.script) {
            const auto old = referencedScripts.find(host.script);
            if (old == referencedScripts.end() || !currentScripts.contains(old->second)) value = {};
            else host.script = currentScripts.at(old->second);
        }
    };
    for (auto& instance : m_Instances) {
        for (auto& value : instance.runtimeFields) repairReference(repairReference, value);
        for (auto& coroutine : instance.coroutines) {
            for (auto& value : coroutine.stack) repairReference(repairReference, value);
            for (auto& value : coroutine.locals) repairReference(repairReference, value);
        }
    }
    m_VM.SetScene(&scene); m_VM.SetPhysicsWorld(m_PhysicsWorld);
    m_VM.SetActiveInstances(&m_Instances); m_VM.SetDeltaTime(0.0f);
    std::vector<std::string> errors;
    for (size_t index : newInstances) {
        if (m_VM.IsDestroyPending(m_Instances[index].entityId)) continue;
        InvokeLifecycle(m_VM, m_Instances[index], "Awake", errors);
        if (!m_VM.IsDestroyPending(m_Instances[index].entityId))
            InvokeLifecycle(m_VM, m_Instances[index], "Start", errors);
    }
    for (const auto& error : errors) MIPSYNC_CONSOLE_WARN("{}", error);
}

void MipsRuntime::ReloadChangedScripts(MipsyncEngine::Scene& scene) {
    RefreshPlayInstances(scene);
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
            MIPSYNC_CONSOLE_WARN("{}", e);
        if (!newModule)
            continue;

        const std::vector<double> oldFields = script->fieldValues;
        const std::vector<std::string> oldAssetFields = script->fieldAssetPaths;
        const auto oldModule=script->module;
        script->module = newModule;
        RestoreNamedFields(*script,oldModule,oldFields,oldAssetFields);

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
        if (!m_VM.IsDestroyPending(instance.entityId) &&
            scene.IsActiveInHierarchy(*instance.entity) && instance.sourceComponent->enabled &&
            instance.module && instance.module->FindMethod(method))
            m_VM.RunMethod(instance, method, errors);
    }
    if(std::string(method)=="Update")m_ExecutionErrors.clear();
    m_ExecutionErrors.insert(m_ExecutionErrors.end(),errors.begin(),errors.end());
    for (const auto& e : errors)
        MIPSYNC_CONSOLE_WARN("{}", e);
}

void MipsRuntime::OnPlayStarted(Scene& scene) {
    m_VM.ClearDestroyRequests();
    m_PhysicsEvents.Clear();
    m_ScriptFileTimes.clear();
    std::vector<std::string> compileErrors;
    if (!ReloadScripts(scene, {}, compileErrors)) {
        std::string message = "Cannot compile scripts before Play:";
        for (const auto& error : compileErrors) message += "\n" + error;
        throw std::runtime_error(message);
    }
    // The currently visible edit scene is authoritative. Restoring a cached
    // baseline here caused an immediate jump when Play was pressed.
    m_EditSnapshot.Capture(scene);

    m_Playing = true;
    m_Paused = false;
    CollectInstances(scene);
    m_VM.SetActiveInstances(&m_Instances);

    m_VM.SetScene(&scene);
    m_VM.SetPhysicsWorld(m_PhysicsWorld);
    std::vector<std::string> errors;
    m_VM.SetDeltaTime(0.0f);
    for (auto& instance : m_Instances) {
        CheckPreparationCancelled();
        if (m_VM.IsDestroyPending(instance.entityId)) continue;
        InvokeLifecycle(m_VM, instance, "Awake", errors);
        if (!m_VM.IsDestroyPending(instance.entityId))
            InvokeLifecycle(m_VM, instance, "Start", errors);
    }
    for (const auto& e : errors)
        MIPSYNC_CONSOLE_WARN("{}", e);

    m_ExecutionErrors=errors;
    if(!errors.empty())throw std::runtime_error("Awake/Start failed: "+errors.front());
    FlushDestroyRequests(scene);
    MIPSYNC_INFO("[Mips#] Play started ({} script instance(s))", m_Instances.size());
}

bool MipsRuntime::ReloadScripts(Scene& scene, const std::string& path, std::vector<std::string>& errors) {
    std::unordered_map<std::string,std::shared_ptr<CompiledModule>> compiled;
    std::vector<MipsScriptComponent*> scripts;
    for(const auto& entity:scene.GetEntities())for(auto* script:entity->GetComponents<MipsScriptComponent>()) {
        if(script->scriptPath.empty() || (!path.empty() && script->scriptPath!=path))continue;
        scripts.push_back(script);
        if(compiled.contains(script->scriptPath))continue;
        CheckPreparationCancelled();
        auto module=CompileScriptFile(script->scriptPath,errors);
        if(!module)return false;
        compiled.emplace(script->scriptPath,std::move(module));
    }
    if(!path.empty()&&scripts.empty()){errors.push_back("No attached script matches "+path);return false;}
    for(auto* script:scripts) {
        const auto old=script->module;const auto fields=script->fieldValues;const auto assets=script->fieldAssetPaths;
        script->module=compiled.at(script->scriptPath);RestoreNamedFields(*script,old,fields,assets);
        std::error_code error;auto time=std::filesystem::last_write_time(AssetManager::Get().ToAbsolute(script->scriptPath),error);
        if(!error)m_ScriptFileTimes[script->scriptPath]=time;
    }
    if(m_Playing)RefreshPlayInstances(scene);
    return true;
}

nlohmann::json MipsRuntime::InspectFields(const MipsScriptComponent& script) const {
    using Json=nlohmann::json;
    for(const auto& instance:m_Instances)if(instance.sourceComponent==&script) {
        auto value=[&](auto&& self,const Value& v,int depth)->Json {
            if(depth>16)return {{"truncated",true}};
            switch(v.tag){
            case Value::Tag::Bool:return v.boolValue;
            case Value::Tag::Number:return v.number;
            case Value::Tag::Array:{Json values=Json::array();if(v.array)for(const auto& item:v.array->elements)values.push_back(self(self,item,depth+1));return values;}
            case Value::Tag::String:if(!v.runtimeString&&v.stringIndex<instance.module->stringConstants.size())return instance.module->stringConstants[v.stringIndex];return {{"runtimeString",true}};
            default:return nullptr;
            }
        };
        Json fields=Json::object();
        for(size_t i=0;i<instance.module->fields.size();++i){
            if(instance.module->fields[i].valueKind==FieldValueKind::AudioClip)fields[instance.module->fields[i].name]=i<instance.assetFields.size()?instance.assetFields[i]:"";
            else if(i<instance.runtimeFields.size()&&instance.runtimeFields[i].tag!=Value::Tag::Nil)fields[instance.module->fields[i].name]=value(value,instance.runtimeFields[i],0);
            else fields[instance.module->fields[i].name]=i<instance.fields.size()?instance.fields[i]:0.;
        }
        return {{"entity",instance.entityId},{"class",instance.module->className},{"fields",fields}};
    }
    return {{"active",false},{"fields",Json::object()}};
}

nlohmann::json MipsRuntime::InspectExecution() const {
    return {{"instructionsPerInvocationLimit",m_VM.GetExecutionLimits().maxInstructionsPerInvocation},
        {"lastInvocationInstructions",m_VM.GetLastInvocationInstructions()},
        {"diagnostics",m_ExecutionErrors}};
}

void MipsRuntime::OnPlayStopped(Scene& scene) {
    if (m_Playing) {
        RefreshPlayInstances(scene, false);
        std::vector<std::string> errors;
        for (auto& instance : m_Instances) {
            InvokeLifecycle(m_VM, instance, "OnDestroy", errors);
        }
        for (const auto& e : errors)
            MIPSYNC_CONSOLE_WARN("{}", e);
    }

    if (!m_EditSnapshot.Empty())
        m_EditSnapshot.Restore(scene);
    m_EditSnapshot.Capture(scene);

    m_Instances.clear();
    m_VM.ClearDestroyRequests();
    m_PhysicsEvents.Clear();
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
        if (!self || !scene.IsActiveInHierarchy(*self) ||
            !scene.FindEntity(ev.otherEntityId) || m_VM.IsDestroyPending(ev.selfEntityId) ||
            m_VM.IsDestroyPending(ev.otherEntityId))
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
            if (instance.entity != self || !instance.module || !instance.sourceComponent->enabled ||
                m_VM.IsDestroyPending(instance.entityId))
                continue;
            if (!instance.module->FindMethod(methodName))
                continue;
            m_VM.SetPhysicsOtherEntityId(ev.otherEntityId);
            m_VM.RunMethod(instance, methodName, errors);
        }
    }

    for (const auto& e : errors)
        MIPSYNC_CONSOLE_WARN("{}", e);
    m_PhysicsEvents.Clear();
    m_VM.SetPhysicsOtherEntityId(0);
    FlushDestroyRequests(scene);
}

bool MipsRuntime::InvokeButtonEvent(Scene& scene, uint32_t targetEntityId,
                                    const std::string& scriptPath, const std::string& methodName,
                                    std::vector<std::string>& errors) {
    if (!m_Playing) {
        errors.push_back("Button On Click can only run in Play mode");
        return false;
    }
    if (targetEntityId == 0 || methodName.empty()) {
        errors.push_back("Button On Click listener has no target or method");
        return false;
    }

    RefreshPlayInstances(scene);

    Entity* target = scene.FindEntity(targetEntityId);
    if (!target) {
        errors.push_back("Button On Click target entity no longer exists");
        return false;
    }

    m_VM.SetScene(&scene);
    m_VM.SetPhysicsWorld(m_PhysicsWorld);
    m_VM.SetActiveInstances(&m_Instances);
    for (auto& instance : m_Instances) {
        if (instance.entity != target || !instance.module || !instance.sourceComponent)
            continue;
        if (!scriptPath.empty() && instance.sourceComponent->scriptPath != scriptPath)
            continue;
        const CompiledMethod* method = instance.module->FindMethod(methodName);
        if (!method || !method->isPublic || method->parameterCount != 0 ||
            method->returnType != "void")
            continue;
        const bool ok = m_VM.RunMethod(instance, methodName, errors);
        FlushDestroyRequests(scene);
        return ok;
    }

    errors.push_back("Button On Click method is unavailable on target: " + methodName);
    return false;
}

void MipsRuntime::Update(Scene& scene, float deltaTime) {
    if (!m_Playing || m_Paused)
        return;

    RefreshPlayInstances(scene);

    InvokeLifecycleAll("Update", scene, deltaTime);
    std::vector<std::string> coroutineErrors;
    for (auto& instance : m_Instances)
        if (!m_VM.IsDestroyPending(instance.entityId) &&
            scene.IsActiveInHierarchy(*instance.entity) && instance.sourceComponent->enabled)
            m_VM.ResumeCoroutines(instance, deltaTime, coroutineErrors);
    for (const auto& error : coroutineErrors)
        MIPSYNC_CONSOLE_WARN("{}", error);
    FlushDestroyRequests(scene);
}

void MipsRuntime::LateUpdate(Scene& scene, float deltaTime) {
    if (!m_Playing || m_Paused)
        return;
    InvokeLifecycleAll("LateUpdate", scene, deltaTime);
    FlushDestroyRequests(scene);
}

void MipsRuntime::FlushDestroyRequests(Scene& scene) {
    std::unordered_set<uint32_t> destroyed;
    std::vector<uint32_t> ordered;
    std::vector<std::string> errors;
    // Complete all OnDestroy callbacks (including requests they enqueue) while
    // every entity and script instance still exists. Never erase inside VM loops.
    auto collect = [&](auto&& self, uint32_t id) -> void {
        Entity* entity = scene.FindEntity(id);
        if (!entity || !destroyed.insert(id).second) return;
        for (uint32_t child : entity->GetChildIDs()) self(self, child);
        ordered.push_back(id);
    };
    while (true) {
        auto requests = m_VM.TakeDestroyRequests();
        if (requests.empty()) break;
        const size_t first = ordered.size();
        for (uint32_t id : requests) collect(collect, id);
        for (size_t i = first; i < ordered.size(); ++i)
            for (auto& instance : m_Instances)
                if (instance.entityId == ordered[i])
                    InvokeLifecycle(m_VM, instance, "OnDestroy", errors);
    }
    if (ordered.empty()) return;
    for (uint32_t id : ordered) {
        if (m_PhysicsWorld) m_PhysicsWorld->RemoveEntity(id);
        scene.DestroyEntity(scene.FindEntity(id));
    }
    RefreshPlayInstances(scene, false); // invalidate script/entity/transform references
    m_ExecutionErrors.insert(m_ExecutionErrors.end(), errors.begin(), errors.end());
    for (const auto& error : errors) MIPSYNC_CONSOLE_WARN("{}", error);
}

} // namespace MipsyncEngine::Mips
