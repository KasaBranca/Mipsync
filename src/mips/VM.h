#pragma once

#include "Bytecode.h"
#include "Value.h"
#include <memory>
#include <string>
#include <vector>

namespace MipsyncEngine {

class Entity;
class Scene;
class PhysicsWorld;
struct MipsScriptComponent;

namespace Mips {

struct CoroutineState {
    std::string methodName;
    size_t instruction = 0;
    std::vector<Value> stack;
    std::vector<Value> locals;
    std::vector<std::string> runtimeStrings;
    float waitSeconds = 0.0f;
    uint8_t waitFrames = 0;
    bool completed = false;
};

struct ScriptInstance {
    std::shared_ptr<CompiledModule> module;
    Entity* entity = nullptr;
    MipsScriptComponent* sourceComponent = nullptr;
    std::vector<double> fields;
    std::vector<Value> runtimeFields;
    std::vector<std::string> assetFields;
    std::vector<CoroutineState> coroutines;
};

class VM {
public:
    void SetDeltaTime(float dt) { m_DeltaTime = dt; }
    void SetScene(MipsyncEngine::Scene* scene) { m_Scene = scene; }
    void SetPhysicsWorld(MipsyncEngine::PhysicsWorld* world) { m_PhysicsWorld = world; }
    void SetActiveInstances(std::vector<ScriptInstance>* instances) { m_ActiveInstances = instances; }
    void SetPhysicsOtherEntityId(uint32_t entityId) { m_PhysicsOtherEntityId = entityId; }

    bool RunMethod(ScriptInstance& instance, const std::string& methodName,
                   std::vector<std::string>& outErrors);
    void ResumeCoroutines(ScriptInstance& instance, float deltaTime,
                          std::vector<std::string>& outErrors);

private:
    bool Execute(ScriptInstance& instance, const CompiledMethod& method,
                 std::vector<std::string>& outErrors, CoroutineState* coroutine = nullptr);

    Value Pop();
    void Push(const Value& value);
    bool IsTruthy(const Value& value) const;

    float m_DeltaTime = 0.0f;
    uint32_t m_PhysicsOtherEntityId = 0;
    Scene* m_Scene = nullptr;
    PhysicsWorld* m_PhysicsWorld = nullptr;
    ScriptInstance* m_Instance = nullptr;
    std::vector<ScriptInstance>* m_ActiveInstances = nullptr;
    const CompiledModule* m_Module = nullptr;
    std::vector<Value> m_Stack;
    std::vector<Value> m_Locals; // re-used per Execute (supports Host refs)
    std::vector<std::string> m_RuntimeStrings;

    const std::string& ResolveString(const Value& value) const;
    Value MakeRuntimeString(const std::string& text);
    static Value MakeValueVec3(double x, double y, double z);
    static bool ReadVec3(const Value& value, double& x, double& y, double& z);
};

} // namespace Mips
} // namespace MipsyncEngine
