#include "VM.h"
#include "../core/Engine.h"
#include "../core/Log.h"
#include "../core/Input.h"
#include "../game/GameSave.h"
#include "../assets/AssetManager.h"
#include "../audio/AudioSystem.h"
#include "../scene/Scene.h"
#include "../editor/Raycast.h"
#include "../physics/ColliderUtils.h"
#include "../physics/PhysicsWorld.h"
#include <GLFW/glfw3.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <unordered_map>
#include <utility>

namespace MipsyncEngine::Mips {

namespace {

uint16_t ReadU16(const std::vector<uint8_t>& code, size_t& ip) {
    const uint16_t v = static_cast<uint16_t>(code[ip]) |
                       (static_cast<uint16_t>(code[ip + 1]) << 8);
    ip += 2;
    return v;
}

uint8_t ReadU8(const std::vector<uint8_t>& code, size_t& ip) {
    return code[ip++];
}

int32_t ReadI32(const std::vector<uint8_t>& code, size_t& ip) {
    int32_t v = 0;
    std::memcpy(&v, &code[ip], sizeof(int32_t));
    ip += 4;
    return v;
}

TransformComponent* GetTransform(Entity* entity) {
    return entity ? entity->GetComponent<TransformComponent>() : nullptr;
}

struct SimpleColliderAabb {
    glm::vec3 min{0.0f};
    glm::vec3 max{0.0f};
};

SimpleColliderAabb ComputeSimpleColliderAabb(const Scene& scene, const Entity& entity,
                                             const ColliderComponent& collider) {
    const ColliderUtils::ColliderWorldPose pose =
        ColliderUtils::ComputeWorldPose(scene, entity, collider);
    glm::vec3 localHalf = collider.halfExtents;
    if (collider.shape == ColliderShape::Sphere) {
        localHalf = glm::vec3(collider.radius);
    } else if (collider.shape == ColliderShape::Capsule) {
        localHalf = glm::vec3(
            collider.radius,
            collider.capsuleHeight * 0.5f + collider.radius,
            collider.radius);
    }
    localHalf *= glm::abs(pose.lossyScale);
    const glm::mat3 rotation = glm::mat3_cast(pose.rotation);
    const glm::mat3 absRotation(
        glm::abs(rotation[0]),
        glm::abs(rotation[1]),
        glm::abs(rotation[2]));
    const glm::vec3 worldHalf = absRotation * localHalf;
    return {pose.center - worldHalf, pose.center + worldHalf};
}

bool AabbOverlaps(const SimpleColliderAabb& a, const SimpleColliderAabb& b) {
    return a.min.x < b.max.x && a.max.x > b.min.x &&
           a.min.y < b.max.y && a.max.y > b.min.y &&
           a.min.z < b.max.z && a.max.z > b.min.z;
}

void MoveEntityWithSimpleCollision(Scene& scene, Entity& entity,
                                   const glm::vec3& velocity, float deltaTime) {
    auto* movingCollider = entity.GetComponent<ColliderComponent>();
    if (!movingCollider || !movingCollider->enabled || movingCollider->isTrigger) {
        scene.SetWorldPosition(entity, scene.GetWorldPosition(entity) + velocity * deltaTime);
        return;
    }

    glm::vec3 worldPosition = scene.GetWorldPosition(entity);
    for (int axis = 0; axis < 3; ++axis) {
        const float displacement = velocity[axis] * deltaTime;
        if (std::abs(displacement) <= 1e-7f)
            continue;

        const float axisStart = worldPosition[axis];
        const SimpleColliderAabb previousAabb =
            ComputeSimpleColliderAabb(scene, entity, *movingCollider);
        worldPosition[axis] += displacement;
        scene.SetWorldPosition(entity, worldPosition);
        SimpleColliderAabb movingAabb =
            ComputeSimpleColliderAabb(scene, entity, *movingCollider);

        for (const auto& otherPtr : scene.GetEntities()) {
            Entity* other = otherPtr.get();
            if (!other || other == &entity)
                continue;
            auto* otherCollider = other->GetComponent<ColliderComponent>();
            if (!otherCollider || !otherCollider->enabled || otherCollider->isTrigger)
                continue;

            const SimpleColliderAabb otherAabb =
                ComputeSimpleColliderAabb(scene, *other, *otherCollider);
            if (axis != 1 && otherCollider->shape == ColliderShape::Mesh &&
                otherAabb.max.y - otherAabb.min.y <= 0.001f)
                continue;
            if (!AabbOverlaps(movingAabb, otherAabb))
                continue;

            if (AabbOverlaps(previousAabb, otherAabb)) {
                const float previousDepth =
                    std::min(previousAabb.max[axis], otherAabb.max[axis]) -
                    std::max(previousAabb.min[axis], otherAabb.min[axis]);
                const float currentDepth =
                    std::min(movingAabb.max[axis], otherAabb.max[axis]) -
                    std::max(movingAabb.min[axis], otherAabb.min[axis]);

                // Existing contact on another axis (most commonly the floor)
                // must not eject the character to that collider's far edge.
                if (currentDepth <= previousDepth + 1e-6f)
                    continue;

                worldPosition[axis] = axisStart;
                scene.SetWorldPosition(entity, worldPosition);
                break;
            }

            float correction = displacement > 0.0f
                ? movingAabb.max[axis] - otherAabb.min[axis]
                : otherAabb.max[axis] - movingAabb.min[axis];
            correction = std::clamp(correction, 0.0f, std::abs(displacement));
            worldPosition[axis] += displacement > 0.0f ? -correction : correction;
            scene.SetWorldPosition(entity, worldPosition);
            movingAabb = ComputeSimpleColliderAabb(scene, entity, *movingCollider);
        }
    }
}

std::string ResolveSavePath(const std::string& path) {
    namespace fs = std::filesystem;
    fs::path p = PathUtf8::FromString(path);
    if (p.is_absolute())
        return PathUtf8::ToString(p);
    const std::string root = AssetManager::Get().GetProjectRoot();
    return PathUtf8::ToString(PathUtf8::FromString(root) / "saves" / p);
}

int MapKeyName(const std::string& name) {
    static const std::unordered_map<std::string, int> kMap = {
        {"W", GLFW_KEY_W}, {"A", GLFW_KEY_A}, {"S", GLFW_KEY_S}, {"D", GLFW_KEY_D},
        {"Q", GLFW_KEY_Q}, {"E", GLFW_KEY_E}, {"R", GLFW_KEY_R}, {"F", GLFW_KEY_F},
        {"G", GLFW_KEY_G}, {"H", GLFW_KEY_H}, {"I", GLFW_KEY_I}, {"J", GLFW_KEY_J},
        {"K", GLFW_KEY_K}, {"L", GLFW_KEY_L},
        {"Space", GLFW_KEY_SPACE},
        {"Aim", GLFW_KEY_LEFT_SHIFT}, {"L1", GLFW_KEY_LEFT_SHIFT},
        {"LeftShift", GLFW_KEY_LEFT_SHIFT}, {"RightShift", GLFW_KEY_RIGHT_SHIFT},
        {"LeftControl", GLFW_KEY_LEFT_CONTROL}, {"RightControl", GLFW_KEY_RIGHT_CONTROL},
        {"LeftAlt", GLFW_KEY_LEFT_ALT}, {"RightAlt", GLFW_KEY_RIGHT_ALT},
        {"Up", GLFW_KEY_UP}, {"Down", GLFW_KEY_DOWN},
        {"Left", GLFW_KEY_LEFT}, {"Right", GLFW_KEY_RIGHT},
        {"Escape", GLFW_KEY_ESCAPE}, {"Enter", GLFW_KEY_ENTER},
        {"Tab", GLFW_KEY_TAB}, {"Backspace", GLFW_KEY_BACKSPACE},
        {"0", GLFW_KEY_0}, {"1", GLFW_KEY_1}, {"2", GLFW_KEY_2},
        {"3", GLFW_KEY_3}, {"4", GLFW_KEY_4}, {"5", GLFW_KEY_5},
        {"6", GLFW_KEY_6}, {"7", GLFW_KEY_7}, {"8", GLFW_KEY_8}, {"9", GLFW_KEY_9},
    };
    auto it = kMap.find(name);
    return it != kMap.end() ? it->second : -1;
}

double NumberValue(const Value& v) {
    if (v.tag == Value::Tag::Bool)
        return v.boolValue ? 1.0 : 0.0;
    return v.tag == Value::Tag::Number ? v.number : 0.0;
}

Entity* EntityFromHost(const Value& v, Entity* fallback) {
    if (v.tag != Value::Tag::Host)
        return fallback;
    if (v.host.entity)
        return v.host.entity;
    return fallback;
}

Entity* AudioEntityFromHost(const Value& v, Entity* fallback) {
    if (v.tag == Value::Tag::Host && v.host.kind == HostKind::AudioSource && v.host.entity)
        return v.host.entity;
    return fallback;
}

} // namespace

Value VM::Pop() {
    if (m_Stack.empty())
        return Value{};
    Value v = m_Stack.back();
    m_Stack.pop_back();
    return v;
}

void VM::Push(const Value& value) {
    m_Stack.push_back(value);
}

bool VM::IsTruthy(const Value& value) const {
    switch (value.tag) {
        case Value::Tag::Nil: return false;
        case Value::Tag::Bool: return value.boolValue;
        case Value::Tag::Number: return value.number != 0.0;
        case Value::Tag::String: return true;
        case Value::Tag::Host: return value.host.kind != HostKind::None;
        case Value::Tag::Array: return value.array != nullptr;
        default: return false;
    }
}

bool VM::RunMethod(ScriptInstance& instance, const std::string& methodName,
                   std::vector<std::string>& outErrors) {
    if (!instance.module) {
        outErrors.push_back("script has no compiled module");
        return false;
    }

    const CompiledMethod* method = instance.module->FindMethod(methodName);
    if (!method) {
        outErrors.push_back("method not found: " + methodName);
        return false;
    }

    return Execute(instance, *method, outErrors);
}

bool VM::Execute(ScriptInstance& instance, const CompiledMethod& method,
                  std::vector<std::string>& outErrors, CoroutineState* coroutine) {
    m_Instance = &instance;
    m_Module = instance.module.get();
    if (coroutine) {
        m_Stack = coroutine->stack;
        m_Locals = coroutine->locals;
        m_RuntimeStrings = coroutine->runtimeStrings;
    } else {
        m_Stack.clear();
        m_Locals.assign(method.localCount, Value{});
        m_RuntimeStrings.clear();
    }
    m_RuntimeStringBytes = 0;
    for (const std::string& value : m_RuntimeStrings)
        m_RuntimeStringBytes += value.size();
    m_RuntimeStringLimitExceeded = false;

    const auto& code = method.code;
    size_t ip = coroutine ? coroutine->instruction : 0;
    size_t executedInstructions = 0;

    while (ip < code.size()) {
        if (++executedInstructions > m_Limits.maxInstructionsPerInvocation) {
            outErrors.push_back("instruction budget exceeded in " + method.name);
            if (coroutine)
                coroutine->completed = true;
            return false;
        }
        if (m_Stack.size() > m_Limits.maxStackValues) {
            outErrors.push_back("VM stack limit exceeded in " + method.name);
            if (coroutine)
                coroutine->completed = true;
            return false;
        }
        if (m_RuntimeStringLimitExceeded) {
            outErrors.push_back("runtime string memory limit exceeded in " + method.name);
            if (coroutine)
                coroutine->completed = true;
            return false;
        }
        const OpCode op = static_cast<OpCode>(code[ip++]);

        switch (op) {
        case OpCode::PushConst: {
            const uint16_t idx = ReadU16(code, ip);
            Value v;
            v.tag = Value::Tag::Number;
            v.number = (idx < m_Module->numberConstants.size()) ? m_Module->numberConstants[idx] : 0.0;
            Push(v);
            break;
        }
        case OpCode::PushBool: {
            const uint8_t b = ReadU8(code, ip);
            Value v;
            v.tag = Value::Tag::Bool;
            v.boolValue = b != 0;
            Push(v);
            break;
        }
        case OpCode::PushString: {
            const uint16_t idx = ReadU16(code, ip);
            Value v;
            v.tag = Value::Tag::String;
            v.stringIndex = idx;
            Push(v);
            break;
        }
        case OpCode::PushField: {
            const uint16_t idx = ReadU16(code, ip);
            if (idx < m_Module->fields.size() &&
                m_Module->fields[idx].valueKind == FieldValueKind::AudioClip) {
                Push(MakeRuntimeString(idx < instance.assetFields.size()
                    ? instance.assetFields[idx] : std::string{}));
                break;
            }
            if (idx < instance.runtimeFields.size()) {
                Push(instance.runtimeFields[idx]);
                break;
            }
            Value v;
            const double raw = (idx < instance.fields.size()) ? instance.fields[idx] : 0.0;
            if (idx < m_Module->fields.size() &&
                m_Module->fields[idx].valueKind == FieldValueKind::Bool) {
                v.tag = Value::Tag::Bool;
                v.boolValue = raw != 0.0;
            } else {
                v.tag = Value::Tag::Number;
                v.number = raw;
            }
            Push(v);
            break;
        }
        case OpCode::SetField: {
            const uint16_t idx = ReadU16(code, ip);
            Value val = Pop();
            if (idx < m_Module->fields.size() &&
                m_Module->fields[idx].valueKind == FieldValueKind::AudioClip) {
                if (idx < instance.assetFields.size() && val.tag == Value::Tag::String)
                    instance.assetFields[idx] = ResolveString(val);
            } else {
                if (idx < instance.runtimeFields.size())
                    instance.runtimeFields[idx] = val;
                if (idx >= instance.fields.size())
                    break;
                if (val.tag == Value::Tag::Bool)
                    instance.fields[idx] = val.boolValue ? 1.0 : 0.0;
                else if (val.tag == Value::Tag::Number)
                    instance.fields[idx] = val.number;
            }
            break;
        }
        case OpCode::PushLocal: {
            const uint16_t idx = ReadU16(code, ip);
            if (idx < m_Locals.size())
                Push(m_Locals[idx]);
            else
                Push(Value{});
            break;
        }
        case OpCode::SetLocal: {
            const uint16_t idx = ReadU16(code, ip);
            Value val = Pop();
            if (idx < m_Locals.size())
                m_Locals[idx] = val;
            break;
        }
        case OpCode::Pop:
            Pop();
            break;
        case OpCode::GetGlobal: {
            const uint16_t nameIdx = ReadU16(code, ip);
            const std::string& name = m_Module->nameConstants[nameIdx];
            Value v;
            if (name == "transform") {
                if (TransformComponent* t = GetTransform(instance.entity)) {
                    v.tag = Value::Tag::Host;
                    v.host.kind = HostKind::Transform;
                    v.host.transform = t;
                }
            } else if (name == "gameObject" && instance.entity) {
                v.tag = Value::Tag::Host;
                v.host.kind = HostKind::Entity;
                v.host.entity = instance.entity;
            } else if (name == "AudioSource" && instance.entity) {
                v.tag = Value::Tag::Host;
                v.host.kind = HostKind::AudioSource;
                v.host.entity = instance.entity;
            }
            Push(v);
            break;
        }
        case OpCode::GetComponent: {
            const uint16_t nameIdx = ReadU16(code, ip);
            const std::string& className = m_Module->nameConstants[nameIdx];
            ScriptInstance* found = nullptr;
            if (m_ActiveInstances && instance.entity) {
                for (ScriptInstance& inst : *m_ActiveInstances) {
                    if (inst.entity == instance.entity && inst.module &&
                        inst.module->className == className) {
                        found = &inst;
                        break;
                    }
                }
            }
            Value v;
            v.tag = Value::Tag::Host;
            if (found) {
                v.host.kind = HostKind::MipsScript;
                v.host.script = found;
            } else if (className == "Animator" && instance.entity) {
                v.host.kind = HostKind::Animator;
                v.host.entity = instance.entity;
            } else if (className == "AudioSource" && instance.entity) {
                v.host.kind = HostKind::AudioSource;
                v.host.entity = instance.entity;
            } else if (className == "Collider" && instance.entity) {
                v.host.kind = HostKind::Collider;
                v.host.entity = instance.entity;
            } else if (className == "Rigidbody" && instance.entity) {
                v.host.kind = HostKind::Rigidbody;
                v.host.entity = instance.entity;
            } else if (className == "Transform" && instance.entity) {
                if (TransformComponent* t = GetTransform(instance.entity)) {
                    v.host.kind = HostKind::Transform;
                    v.host.transform = t;
                }
            }
            Push(v);
            break;
        }
        case OpCode::GetMember: {
            const uint16_t memberIdx = ReadU16(code, ip);
            const std::string& member = m_Module->nameConstants[memberIdx];
            Value obj = Pop();
            if (obj.tag == Value::Tag::Host && obj.host.kind == HostKind::MipsScript && obj.host.script) {
                const int fieldIdx = obj.host.script->module->FindFieldIndex(member);
                if (fieldIdx >= 0 && static_cast<size_t>(fieldIdx) < obj.host.script->runtimeFields.size())
                    Push(obj.host.script->runtimeFields[static_cast<size_t>(fieldIdx)]);
                else {
                    Value v; v.tag = Value::Tag::Number;
                    v.number = (fieldIdx >= 0 && static_cast<size_t>(fieldIdx) < obj.host.script->fields.size())
                        ? obj.host.script->fields[static_cast<size_t>(fieldIdx)] : 0.0;
                    Push(v);
                }
                break;
            }
            if (obj.tag == Value::Tag::Host && obj.host.kind == HostKind::Transform &&
                member == "worldPosition") {
                Value v;
                v.tag = Value::Tag::Host;
                v.host.kind = HostKind::WorldVec3;
                v.host.transform = obj.host.transform;
                v.host.worldMember = WorldVecMember::Position;
                Push(v);
                break;
            }
            if (obj.tag == Value::Tag::Host && obj.host.kind == HostKind::Transform &&
                (member == "rotation" || member == "position" || member == "scale")) {
                Value v;
                v.tag = Value::Tag::Host;
                v.host.kind = HostKind::Vec3;
                if (obj.host.transform) {
                    glm::vec3* target = nullptr;
                    if (member == "rotation")      target = &obj.host.transform->rotation;
                    else if (member == "position") target = &obj.host.transform->position;
                    else                            target = &obj.host.transform->scale;
                    v.host.vec3.x = &target->x;
                    v.host.vec3.y = &target->y;
                    v.host.vec3.z = &target->z;
                }
                Push(v);
                break;
            }
            if (obj.tag == Value::Tag::Host && obj.host.kind == HostKind::Entity && obj.host.entity) {
                if (member == "id") {
                    Value v;
                    v.tag = Value::Tag::Number;
                    v.number = static_cast<double>(obj.host.entity->GetID());
                    Push(v);
                    break;
                }
                if (member == "name") {
                    std::string entityName = "Entity";
                    if (auto* tag = obj.host.entity->GetComponent<TagComponent>())
                        entityName = tag->tag;
                    Push(MakeRuntimeString(entityName));
                    break;
                }
            }
            if (obj.tag == Value::Tag::Host && obj.host.kind == HostKind::AudioSource &&
                obj.host.entity) {
                auto* audio = obj.host.entity->GetComponent<AudioSourceComponent>();
                if (!audio) {
                    Push(Value{});
                    break;
                }
                if (member == "clip") {
                    Push(MakeRuntimeString(audio->clipPath));
                    break;
                }
                Value v;
                if (member == "volume") {
                    v.tag = Value::Tag::Number;
                    v.number = audio->volume;
                } else if (member == "loop") {
                    v.tag = Value::Tag::Bool;
                    v.boolValue = audio->loop;
                } else if (member == "mute") {
                    v.tag = Value::Tag::Bool;
                    v.boolValue = audio->mute;
                } else if (member == "playOnAwake") {
                    v.tag = Value::Tag::Bool;
                    v.boolValue = audio->playOnAwake;
                } else if (member == "enabled") {
                    v.tag = Value::Tag::Bool;
                    v.boolValue = audio->enabled;
                } else if (member == "isPlaying") {
                    v.tag = Value::Tag::Bool;
                    v.boolValue = Engine::Get().GetAudioSystem().IsPlaying(obj.host.entity->GetID());
                }
                Push(v);
                break;
            }
            Push(Value{});
            break;
        }
        case OpCode::GetVec3Axis: {
            const uint8_t axis = ReadU8(code, ip);
            Value obj = Pop();
            Value v;
            v.tag = Value::Tag::Number;
            if (obj.tag == Value::Tag::Host && obj.host.kind == HostKind::Vec3) {
                float* comp = axis == 0 ? obj.host.vec3.x : (axis == 1 ? obj.host.vec3.y : obj.host.vec3.z);
                v.number = comp ? static_cast<double>(*comp) : 0.0;
            } else if (obj.tag == Value::Tag::Host && obj.host.kind == HostKind::ValueVec3) {
                v.number = obj.host.valueVec3[axis];
            } else if (obj.tag == Value::Tag::Host && obj.host.kind == HostKind::WorldVec3 &&
                       m_Scene && obj.host.transform && obj.host.transform->entity) {
                const glm::vec3 world = m_Scene->GetWorldPosition(*obj.host.transform->entity);
                const float comp = axis == 0 ? world.x : (axis == 1 ? world.y : world.z);
                v.number = static_cast<double>(comp);
            }
            Push(v);
            break;
        }
        case OpCode::SetVec3Axis: {
            const uint8_t axis = ReadU8(code, ip);
            // Stack: [value, vec3] with vec3 on top — pop vec3 first, then value.
            Value obj = Pop();
            Value val = Pop();
            if (obj.tag == Value::Tag::Host && obj.host.kind == HostKind::Vec3 &&
                val.tag == Value::Tag::Number) {
                float* comp = axis == 0 ? obj.host.vec3.x : (axis == 1 ? obj.host.vec3.y : obj.host.vec3.z);
                if (comp)
                    *comp = static_cast<float>(val.number);
            } else if (obj.tag == Value::Tag::Host && obj.host.kind == HostKind::WorldVec3 &&
                       val.tag == Value::Tag::Number && m_Scene &&
                       obj.host.transform && obj.host.transform->entity) {
                glm::vec3 world = m_Scene->GetWorldPosition(*obj.host.transform->entity);
                if (axis == 0) world.x = static_cast<float>(val.number);
                else if (axis == 1) world.y = static_cast<float>(val.number);
                else world.z = static_cast<float>(val.number);
                m_Scene->SetWorldPosition(*obj.host.transform->entity, world);
            }
            break;
        }
        case OpCode::SetVec3FromValue: {
            Value target = Pop();
            Value src = Pop();
            double sx = 0.0, sy = 0.0, sz = 0.0;
            if (!ReadVec3(src, sx, sy, sz))
                break;
            if (target.tag == Value::Tag::Host && target.host.kind == HostKind::Vec3) {
                if (target.host.vec3.x) *target.host.vec3.x = static_cast<float>(sx);
                if (target.host.vec3.y) *target.host.vec3.y = static_cast<float>(sy);
                if (target.host.vec3.z) *target.host.vec3.z = static_cast<float>(sz);
            }
            break;
        }
        case OpCode::Add: {
            Value b = Pop();
            Value a = Pop();
            if (a.tag == Value::Tag::String && b.tag == Value::Tag::String) {
                Push(MakeRuntimeString(ResolveString(a) + ResolveString(b)));
                break;
            }
            if (a.tag == Value::Tag::String && b.tag == Value::Tag::Number) {
                Push(MakeRuntimeString(ResolveString(a) + std::to_string(b.number)));
                break;
            }
            if (a.tag == Value::Tag::Number && b.tag == Value::Tag::String) {
                Push(MakeRuntimeString(std::to_string(a.number) + ResolveString(b)));
                break;
            }
            Value r;
            r.tag = Value::Tag::Number;
            r.number = (a.tag == Value::Tag::Number ? a.number : 0.0) +
                       (b.tag == Value::Tag::Number ? b.number : 0.0);
            Push(r);
            break;
        }
        case OpCode::Sub: {
            Value b = Pop();
            Value a = Pop();
            Value r;
            r.tag = Value::Tag::Number;
            r.number = (a.tag == Value::Tag::Number ? a.number : 0.0) -
                       (b.tag == Value::Tag::Number ? b.number : 0.0);
            Push(r);
            break;
        }
        case OpCode::Mul: {
            Value b = Pop();
            Value a = Pop();
            Value r;
            r.tag = Value::Tag::Number;
            r.number = (a.tag == Value::Tag::Number ? a.number : 0.0) *
                       (b.tag == Value::Tag::Number ? b.number : 0.0);
            Push(r);
            break;
        }
        case OpCode::Div: {
            Value b = Pop();
            Value a = Pop();
            Value r;
            r.tag = Value::Tag::Number;
            const double denom = (b.tag == Value::Tag::Number ? b.number : 1.0);
            r.number = (denom != 0.0) ? (a.tag == Value::Tag::Number ? a.number : 0.0) / denom : 0.0;
            Push(r);
            break;
        }
        case OpCode::Mod: {
            Value b = Pop();
            Value a = Pop();
            Value r;
            r.tag = Value::Tag::Number;
            const double denom = (b.tag == Value::Tag::Number ? b.number : 1.0);
            const double numer = (a.tag == Value::Tag::Number ? a.number : 0.0);
            r.number = (denom != 0.0) ? std::fmod(numer, denom) : 0.0;
            Push(r);
            break;
        }
        case OpCode::Neg: {
            Value a = Pop();
            Value r;
            r.tag = Value::Tag::Number;
            r.number = -(a.tag == Value::Tag::Number ? a.number : 0.0);
            Push(r);
            break;
        }
        case OpCode::Not: {
            Value a = Pop();
            Value r;
            r.tag = Value::Tag::Number;
            r.number = IsTruthy(a) ? 0.0 : 1.0;
            Push(r);
            break;
        }
        case OpCode::Eq: case OpCode::Ne: case OpCode::Lt: case OpCode::Gt: case OpCode::Le: case OpCode::Ge: {
            Value b = Pop();
            Value a = Pop();
            const double av = NumberValue(a);
            const double bv = NumberValue(b);
            bool result = false;
            switch (op) {
                case OpCode::Eq: result = av == bv; break;
                case OpCode::Ne: result = av != bv; break;
                case OpCode::Lt: result = av < bv; break;
                case OpCode::Gt: result = av > bv; break;
                case OpCode::Le: result = av <= bv; break;
                case OpCode::Ge: result = av >= bv; break;
                default: break;
            }
            Value r;
            r.tag = Value::Tag::Number;
            r.number = result ? 1.0 : 0.0;
            Push(r);
            break;
        }
        case OpCode::And: {
            Value b = Pop();
            Value a = Pop();
            Value r;
            r.tag = Value::Tag::Number;
            r.number = (IsTruthy(a) && IsTruthy(b)) ? 1.0 : 0.0;
            Push(r);
            break;
        }
        case OpCode::Or: {
            Value b = Pop();
            Value a = Pop();
            Value r;
            r.tag = Value::Tag::Number;
            r.number = (IsTruthy(a) || IsTruthy(b)) ? 1.0 : 0.0;
            Push(r);
            break;
        }
        case OpCode::CallHost: {
            const uint16_t hostId = ReadU16(code, ip);
            const uint8_t argc = ReadU8(code, ip);
            const HostFunc func = static_cast<HostFunc>(hostId);

            std::vector<Value> args(argc);
            for (int i = static_cast<int>(argc) - 1; i >= 0; --i)
                args[static_cast<size_t>(i)] = Pop();

            auto pushNumber = [&](double n) {
                Value r; r.tag = Value::Tag::Number; r.number = n; Push(r);
            };
            auto activeCameraPlanarAxes = [&]() {
                glm::vec3 right{ 1.0f, 0.0f, 0.0f };
                glm::vec3 forward{ 0.0f, 0.0f, -1.0f };
                Entity* cameraEntity = nullptr;
                if (m_Scene) {
                    const uint32_t activeId = m_Scene->GetActiveCameraEntityId();
                    cameraEntity = activeId != 0 ? m_Scene->FindEntity(activeId) : nullptr;
                    if (!cameraEntity)
                        cameraEntity = m_Scene->GetPrimaryCameraEntity();
                }
                if (cameraEntity) {
                    if (auto* cameraComp = cameraEntity->GetComponent<CameraComponent>()) {
                        right = cameraComp->camera.GetRight();
                        forward = cameraComp->camera.GetForward();
                    }
                }

                right.y = 0.0f;
                forward.y = 0.0f;
                if (glm::dot(right, right) > 1e-8f)
                    right = glm::normalize(right);
                else
                    right = { 1.0f, 0.0f, 0.0f };
                if (glm::dot(forward, forward) > 1e-8f)
                    forward = glm::normalize(forward);
                else
                    forward = { 0.0f, 0.0f, -1.0f };
                return std::pair<glm::vec3, glm::vec3>{ right, forward };
            };

            switch (func) {
            case HostFunc::Log_Info: {
                std::string message;
                for (uint8_t i = 0; i < argc; ++i) {
                    const Value& arg = args[i];
                    if (arg.tag == Value::Tag::String)
                        message += ResolveString(arg);
                    else if (arg.tag == Value::Tag::Number)
                        message += std::to_string(arg.number);
                    else if (arg.tag == Value::Tag::Host && arg.host.kind == HostKind::Entity &&
                             arg.host.entity) {
                        if (auto* tag = arg.host.entity->GetComponent<TagComponent>())
                            message += tag->tag;
                        else
                            message += "Entity";
                    }
                }
                if (!message.empty())
                    MIPSYNC_INFO("[Mips#] {}", message);
                break;
            }
            case HostFunc::Time_DeltaTime:
                pushNumber(static_cast<double>(m_DeltaTime));
                break;
            case HostFunc::Input_GetKey:
            case HostFunc::Input_GetKeyDown:
            case HostFunc::Input_GetKeyUp: {
                bool pressed = false;
                if (argc >= 1 && args[0].tag == Value::Tag::String) {
                    const int code = MapKeyName(ResolveString(args[0]));
                    if (code >= 0) {
                        if (func == HostFunc::Input_GetKeyDown)
                            pressed = Input::IsKeyDown(code);
                        else if (func == HostFunc::Input_GetKeyUp)
                            pressed = Input::IsKeyReleased(code);
                        else
                            pressed = Input::IsKeyPressed(code);
                    }
                }
                pushNumber(pressed ? 1.0 : 0.0);
                break;
            }
            case HostFunc::Input_GetAxis: {
                double axis = 0.0;
                if (argc >= 1 && args[0].tag == Value::Tag::String) {
                    const std::string name = ResolveString(args[0]);
                    if (name == "Horizontal") {
                        if (Input::IsKeyPressed(GLFW_KEY_A) || Input::IsKeyPressed(GLFW_KEY_LEFT)) axis -= 1.0;
                        if (Input::IsKeyPressed(GLFW_KEY_D) || Input::IsKeyPressed(GLFW_KEY_RIGHT)) axis += 1.0;
                    } else if (name == "Vertical") {
                        if (Input::IsKeyPressed(GLFW_KEY_S) || Input::IsKeyPressed(GLFW_KEY_DOWN)) axis -= 1.0;
                        if (Input::IsKeyPressed(GLFW_KEY_W) || Input::IsKeyPressed(GLFW_KEY_UP)) axis += 1.0;
                    }
                }
                pushNumber(axis);
                break;
            }
            case HostFunc::Input_MouseDeltaX:
                pushNumber(static_cast<double>(Input::GetMouseDelta().x));
                break;
            case HostFunc::Input_MouseDeltaY:
                // Screen Y grows downward; negate so mouse-up = positive (look up).
                pushNumber(static_cast<double>(-Input::GetMouseDelta().y));
                break;
            case HostFunc::Input_SetCursorLocked:
                if (argc >= 1)
                    Input::SetCursorLocked(NumberValue(args[0]) != 0.0);
                break;
            case HostFunc::Input_GetCursorLocked:
                pushNumber(Input::IsCursorLocked() ? 1.0 : 0.0);
                break;
            case HostFunc::Mathf_Sin:
                pushNumber(std::sin(argc >= 1 ? NumberValue(args[0]) : 0.0));
                break;
            case HostFunc::Mathf_Cos:
                pushNumber(std::cos(argc >= 1 ? NumberValue(args[0]) : 0.0));
                break;
            case HostFunc::Mathf_Sqrt: {
                const double v = argc >= 1 ? NumberValue(args[0]) : 0.0;
                pushNumber(v >= 0.0 ? std::sqrt(v) : 0.0);
                break;
            }
            case HostFunc::Mathf_Abs:
                pushNumber(std::fabs(argc >= 1 ? NumberValue(args[0]) : 0.0));
                break;
            case HostFunc::Mathf_Clamp: {
                const double v  = argc >= 1 ? NumberValue(args[0]) : 0.0;
                const double lo = argc >= 2 ? NumberValue(args[1]) : 0.0;
                const double hi = argc >= 3 ? NumberValue(args[2]) : 0.0;
                pushNumber(std::clamp(v, lo, hi));
                break;
            }
            case HostFunc::Mathf_Atan2: {
                const double y = argc >= 1 ? NumberValue(args[0]) : 0.0;
                const double x = argc >= 2 ? NumberValue(args[1]) : 0.0;
                pushNumber(std::atan2(y, x));
                break;
            }
            case HostFunc::Mathf_Min:
                pushNumber(std::min(argc >= 1 ? NumberValue(args[0]) : 0.0,
                                    argc >= 2 ? NumberValue(args[1]) : 0.0));
                break;
            case HostFunc::Mathf_Max:
                pushNumber(std::max(argc >= 1 ? NumberValue(args[0]) : 0.0,
                                    argc >= 2 ? NumberValue(args[1]) : 0.0));
                break;
            case HostFunc::Mathf_Lerp: {
                const double a = argc >= 1 ? NumberValue(args[0]) : 0.0;
                const double b = argc >= 2 ? NumberValue(args[1]) : 0.0;
                const double t = std::clamp(argc >= 3 ? NumberValue(args[2]) : 0.0, 0.0, 1.0);
                pushNumber(a + (b - a) * t);
                break;
            }
            case HostFunc::Mathf_Floor:
                pushNumber(std::floor(argc >= 1 ? NumberValue(args[0]) : 0.0));
                break;
            case HostFunc::Mathf_Ceil:
                pushNumber(std::ceil(argc >= 1 ? NumberValue(args[0]) : 0.0));
                break;
            case HostFunc::Mathf_Round:
                pushNumber(std::round(argc >= 1 ? NumberValue(args[0]) : 0.0));
                break;
            case HostFunc::Mathf_Sign: {
                const double value = argc >= 1 ? NumberValue(args[0]) : 0.0;
                pushNumber(value < 0.0 ? -1.0 : (value > 0.0 ? 1.0 : 0.0));
                break;
            }
            case HostFunc::Vector3_Create: {
                const double x = argc >= 1 ? NumberValue(args[0]) : 0.0;
                const double y = argc >= 2 ? NumberValue(args[1]) : 0.0;
                const double z = argc >= 3 ? NumberValue(args[2]) : 0.0;
                Push(MakeValueVec3(x, y, z));
                break;
            }
            case HostFunc::Vector3_Add:
            case HostFunc::Vector3_Sub:
            case HostFunc::Vector3_Scale: {
                double ax = 0, ay = 0, az = 0, bx = 0, by = 0, bz = 0;
                ReadVec3(argc >= 1 ? args[0] : Value{}, ax, ay, az);
                if (func == HostFunc::Vector3_Scale) {
                    const double s = argc >= 2 ? NumberValue(args[1]) : 1.0;
                    Push(MakeValueVec3(ax * s, ay * s, az * s));
                } else {
                    ReadVec3(argc >= 2 ? args[1] : Value{}, bx, by, bz);
                    if (func == HostFunc::Vector3_Add)
                        Push(MakeValueVec3(ax + bx, ay + by, az + bz));
                    else
                        Push(MakeValueVec3(ax - bx, ay - by, az - bz));
                }
                break;
            }
            case HostFunc::Vector3_Length: {
                double x = 0, y = 0, z = 0;
                ReadVec3(argc >= 1 ? args[0] : Value{}, x, y, z);
                pushNumber(std::sqrt(x * x + y * y + z * z));
                break;
            }
            case HostFunc::Vector3_Normalize: {
                double x = 0, y = 0, z = 0;
                ReadVec3(argc >= 1 ? args[0] : Value{}, x, y, z);
                const double len = std::sqrt(x * x + y * y + z * z);
                if (len > 1e-8)
                    Push(MakeValueVec3(x / len, y / len, z / len));
                else
                    Push(MakeValueVec3(0.0, 0.0, 0.0));
                break;
            }
            case HostFunc::Vector3_Up:
                Push(MakeValueVec3(0.0, 1.0, 0.0));
                break;
            case HostFunc::Vector3_Forward:
                Push(MakeValueVec3(0.0, 0.0, -1.0));
                break;
            case HostFunc::Vector3_Right:
                Push(MakeValueVec3(1.0, 0.0, 0.0));
                break;
            case HostFunc::Camera_RightX: {
                const auto axes = activeCameraPlanarAxes();
                pushNumber(static_cast<double>(axes.first.x));
                break;
            }
            case HostFunc::Camera_RightZ: {
                const auto axes = activeCameraPlanarAxes();
                pushNumber(static_cast<double>(axes.first.z));
                break;
            }
            case HostFunc::Camera_ForwardX: {
                const auto axes = activeCameraPlanarAxes();
                pushNumber(static_cast<double>(axes.second.x));
                break;
            }
            case HostFunc::Camera_ForwardZ: {
                const auto axes = activeCameraPlanarAxes();
                pushNumber(static_cast<double>(axes.second.z));
                break;
            }
            case HostFunc::Camera_Yaw: {
                const auto axes = activeCameraPlanarAxes();
                const glm::vec3 forward = axes.second;
                pushNumber(static_cast<double>(
                    glm::degrees(std::atan2(-forward.x, -forward.z))));
                break;
            }
            case HostFunc::Physics_IsGrounded: {
                double grounded = 0.0;
                if (m_Instance && m_Instance->entity && m_PhysicsWorld)
                    grounded = m_PhysicsWorld->IsCharacterGrounded(*m_Instance->entity) ? 1.0 : 0.0;
                pushNumber(grounded);
                break;
            }
            case HostFunc::Physics_Move: {
                // (vx, vy, vz) — desired linear velocity in m/s. Consumed by the next physics step.
                double vx = 0, vy = 0, vz = 0;
                if (argc >= 1) vx = NumberValue(args[0]);
                if (argc >= 2) vy = NumberValue(args[1]);
                if (argc >= 3) vz = NumberValue(args[2]);
                if (m_Instance && m_Instance->entity) {
                    const glm::vec3 velocity(
                        static_cast<float>(vx),
                        static_cast<float>(vy),
                        static_cast<float>(vz));
                    const bool movedByCharacter = m_PhysicsWorld && m_PhysicsWorld->IsActive() &&
                        m_PhysicsWorld->SetCharacterVelocity(*m_Instance->entity, velocity);
                    if (!movedByCharacter && m_Scene) {
                        MoveEntityWithSimpleCollision(
                            *m_Scene, *m_Instance->entity, velocity, m_DeltaTime);
                    }
                }
                pushNumber(0.0);
                break;
            }
            case HostFunc::Physics_Raycast: {
                double ox = 0, oy = 0, oz = 0, dx = 0, dy = 0, dz = -1.0, maxDist = 1000.0;
                if (argc >= 1) ox = NumberValue(args[0]);
                if (argc >= 2) oy = NumberValue(args[1]);
                if (argc >= 3) oz = NumberValue(args[2]);
                if (argc >= 4) dx = NumberValue(args[3]);
                if (argc >= 5) dy = NumberValue(args[4]);
                if (argc >= 6) dz = NumberValue(args[5]);
                if (argc >= 7) maxDist = NumberValue(args[6]);
                double hitId = 0.0;
                if (m_Scene) {
                    const RaycastHit hit = RaycastWorld(
                        *m_Scene,
                        glm::vec3(static_cast<float>(ox), static_cast<float>(oy), static_cast<float>(oz)),
                        glm::vec3(static_cast<float>(dx), static_cast<float>(dy), static_cast<float>(dz)),
                        static_cast<float>(maxDist));
                    if (hit.hit && hit.entity)
                        hitId = static_cast<double>(hit.entity->GetID());
                }
                pushNumber(hitId);
                break;
            }
            case HostFunc::Entity_GetId:
                if (argc >= 1 && args[0].tag == Value::Tag::Host &&
                    args[0].host.kind == HostKind::Entity && args[0].host.entity)
                    pushNumber(static_cast<double>(args[0].host.entity->GetID()));
                else
                    pushNumber(0.0);
                break;
            case HostFunc::Entity_GetName:
                if (argc >= 1 && args[0].tag == Value::Tag::Host &&
                    args[0].host.kind == HostKind::Entity && args[0].host.entity) {
                    std::string entityName = "Entity";
                    if (auto* tag = args[0].host.entity->GetComponent<TagComponent>())
                        entityName = tag->tag;
                    Push(MakeRuntimeString(entityName));
                }
                break;
            case HostFunc::Animator_SetFloat:
            case HostFunc::Animator_SetBool:
            case HostFunc::Animator_SetInt:
            case HostFunc::Animator_SetTrigger: {
                if (!m_Instance)
                    break;
                Entity* entity = m_Instance->entity;
                if (argc >= 3)
                    entity = EntityFromHost(args[0], entity);
                const size_t nameIdx = (argc >= 3) ? 1 : 0;
                const size_t valueIdx = (argc >= 3) ? 2 : 1;
                if (!entity || nameIdx >= args.size() || args[nameIdx].tag != Value::Tag::String)
                    break;
                auto* animator = entity->GetComponent<AnimatorComponent>();
                if (!animator)
                    break;
                const std::string paramName = ResolveString(args[nameIdx]);
                switch (func) {
                case HostFunc::Animator_SetFloat:
                    if (valueIdx < args.size())
                        animator->parameters.floats[paramName] =
                            static_cast<float>(NumberValue(args[valueIdx]));
                    break;
                case HostFunc::Animator_SetBool:
                    if (valueIdx < args.size())
                        animator->parameters.bools[paramName] =
                            NumberValue(args[valueIdx]) != 0.0;
                    break;
                case HostFunc::Animator_SetInt:
                    if (valueIdx < args.size())
                        animator->parameters.ints[paramName] =
                            static_cast<int>(NumberValue(args[valueIdx]));
                    break;
                case HostFunc::Animator_SetTrigger:
                    animator->parameters.triggers[paramName] = true;
                    break;
                default:
                    break;
                }
                break;
            }
            case HostFunc::AudioSource_Play:
            case HostFunc::AudioSource_Stop:
            case HostFunc::AudioSource_Pause:
            case HostFunc::AudioSource_UnPause: {
                Entity* entity = m_Instance ? m_Instance->entity : nullptr;
                if (argc >= 1)
                    entity = AudioEntityFromHost(args[0], nullptr);
                bool result = false;
                if (entity && entity->GetComponent<AudioSourceComponent>()) {
                    AudioSystem& audioSystem = Engine::Get().GetAudioSystem();
                    if (func == HostFunc::AudioSource_Play) {
                        result = audioSystem.Play(*m_Scene, entity->GetID(),
                                                  Engine::Get().GetProjectPath());
                    } else if (func == HostFunc::AudioSource_Stop) {
                        audioSystem.Stop(entity->GetID());
                        result = true;
                    } else if (func == HostFunc::AudioSource_Pause) {
                        audioSystem.Pause(entity->GetID());
                        result = true;
                    } else {
                        audioSystem.Resume(entity->GetID());
                        result = true;
                    }
                }
                pushNumber(result ? 1.0 : 0.0);
                break;
            }
            case HostFunc::AudioSource_SetClip:
            case HostFunc::AudioSource_SetVolume:
            case HostFunc::AudioSource_SetLoop:
            case HostFunc::AudioSource_SetMute:
            case HostFunc::AudioSource_SetPlayOnAwake:
            case HostFunc::AudioSource_SetEnabled: {
                Entity* entity = argc >= 1 ? AudioEntityFromHost(args[0], nullptr) : nullptr;
                auto* audio = entity ? entity->GetComponent<AudioSourceComponent>() : nullptr;
                if (audio && argc >= 2) {
                    if (func == HostFunc::AudioSource_SetClip && args[1].tag == Value::Tag::String) {
                        Engine::Get().GetAudioSystem().Stop(entity->GetID());
                        audio->clipPath = ResolveString(args[1]);
                    } else if (func == HostFunc::AudioSource_SetVolume) {
                        audio->volume = std::clamp(static_cast<float>(NumberValue(args[1])), 0.0f, 1.0f);
                    } else if (func == HostFunc::AudioSource_SetLoop) {
                        audio->loop = NumberValue(args[1]) != 0.0;
                    } else if (func == HostFunc::AudioSource_SetMute) {
                        audio->mute = NumberValue(args[1]) != 0.0;
                    } else if (func == HostFunc::AudioSource_SetPlayOnAwake) {
                        audio->playOnAwake = NumberValue(args[1]) != 0.0;
                    } else if (func == HostFunc::AudioSource_SetEnabled) {
                        audio->enabled = NumberValue(args[1]) != 0.0;
                        if (!audio->enabled)
                            Engine::Get().GetAudioSystem().Stop(entity->GetID());
                    }
                }
                pushNumber(audio ? 1.0 : 0.0);
                break;
            }
            case HostFunc::Scene_Load: {
                if (argc >= 1 && args[0].tag == Value::Tag::String)
                    Engine::Get().RequestSceneLoad(ResolveString(args[0]));
                break;
            }
            case HostFunc::Scene_LoadBuildIndex: {
                if (argc >= 1)
                    Engine::Get().RequestSceneLoadBuildIndex(static_cast<int>(NumberValue(args[0])));
                break;
            }
            case HostFunc::Application_Quit:
                Engine::Get().RequestQuit();
                break;
            case HostFunc::Save_GetInt: {
                const std::string key = (argc >= 1 && args[0].tag == Value::Tag::String)
                    ? ResolveString(args[0]) : "";
                const int def = (argc >= 2) ? static_cast<int>(NumberValue(args[1])) : 0;
                pushNumber(static_cast<double>(GameSave::Get().GetInt(key, def)));
                break;
            }
            case HostFunc::Save_SetInt: {
                if (argc >= 2 && args[0].tag == Value::Tag::String)
                    GameSave::Get().SetInt(ResolveString(args[0]), static_cast<int>(NumberValue(args[1])));
                break;
            }
            case HostFunc::Save_GetFloat: {
                const std::string key = (argc >= 1 && args[0].tag == Value::Tag::String)
                    ? ResolveString(args[0]) : "";
                const float def = (argc >= 2) ? static_cast<float>(NumberValue(args[1])) : 0.0f;
                pushNumber(static_cast<double>(GameSave::Get().GetFloat(key, def)));
                break;
            }
            case HostFunc::Save_SetFloat: {
                if (argc >= 2 && args[0].tag == Value::Tag::String)
                    GameSave::Get().SetFloat(ResolveString(args[0]),
                                             static_cast<float>(NumberValue(args[1])));
                break;
            }
            case HostFunc::Save_GetBool: {
                const std::string key = (argc >= 1 && args[0].tag == Value::Tag::String)
                    ? ResolveString(args[0]) : "";
                const bool def = (argc >= 2) ? (NumberValue(args[1]) != 0.0) : false;
                pushNumber(GameSave::Get().GetBool(key, def) ? 1.0 : 0.0);
                break;
            }
            case HostFunc::Save_SetBool: {
                if (argc >= 2 && args[0].tag == Value::Tag::String)
                    GameSave::Get().SetBool(ResolveString(args[0]), NumberValue(args[1]) != 0.0);
                break;
            }
            case HostFunc::Save_GetString: {
                const std::string key = (argc >= 1 && args[0].tag == Value::Tag::String)
                    ? ResolveString(args[0]) : "";
                const std::string def = (argc >= 2 && args[1].tag == Value::Tag::String)
                    ? ResolveString(args[1]) : "";
                Push(MakeRuntimeString(GameSave::Get().GetString(key, def)));
                break;
            }
            case HostFunc::Save_SetString: {
                if (argc >= 2 && args[0].tag == Value::Tag::String && args[1].tag == Value::Tag::String)
                    GameSave::Get().SetString(ResolveString(args[0]), ResolveString(args[1]));
                break;
            }
            case HostFunc::Save_Write:
            case HostFunc::Save_Read: {
                bool ok = false;
                if (argc >= 1 && args[0].tag == Value::Tag::String) {
                    const std::string filePath = ResolveSavePath(ResolveString(args[0]));
                    std::error_code ec;
                    std::filesystem::create_directories(
                        PathUtf8::FromString(filePath).parent_path(), ec);
                    std::string err;
                    ok = (func == HostFunc::Save_Write)
                        ? GameSave::Get().SaveToFile(filePath, err)
                        : GameSave::Get().LoadFromFile(filePath, err);
                    if (!ok)
                        MIPSYNC_WARN("[Mips#] Save {} failed: {}", func == HostFunc::Save_Write ? "write" : "read",
                                     err);
                }
                pushNumber(ok ? 1.0 : 0.0);
                break;
            }
            case HostFunc::Physics_OtherEntityId:
                pushNumber(static_cast<double>(m_PhysicsOtherEntityId));
                break;
            }
            break;
        }
        case OpCode::Return:
            if (coroutine) coroutine->completed = true;
            return true;
        case OpCode::Jump: {
            const int32_t rel = ReadI32(code, ip);
            ip = static_cast<size_t>(static_cast<int64_t>(ip) + rel);
            break;
        }
        case OpCode::JumpIfFalse: {
            const int32_t rel = ReadI32(code, ip);
            Value cond = Pop();
            if (!IsTruthy(cond))
                ip = static_cast<size_t>(static_cast<int64_t>(ip) + rel);
            break;
        }
        case OpCode::NewArray: {
            const uint16_t count = ReadU16(code, ip);
            if (count > m_Limits.maxArrayElements) {
                outErrors.push_back("array element limit exceeded");
                return false;
            }
            Value result;
            result.tag = Value::Tag::Array;
            result.array = std::make_shared<ArrayValue>();
            result.array->elements.resize(count);
            for (size_t i = count; i > 0; --i)
                result.array->elements[i - 1] = Pop();
            Push(result);
            break;
        }
        case OpCode::NewArraySized: {
            const Value sizeValue = Pop();
            const double requested = NumberValue(sizeValue);
            if (!std::isfinite(requested) || requested < 0.0 ||
                requested > static_cast<double>(m_Limits.maxArrayElements)) {
                outErrors.push_back("array element limit exceeded");
                return false;
            }
            const int requestedSize = static_cast<int>(requested);
            const int size = requestedSize;
            Value result;
            result.tag = Value::Tag::Array;
            result.array = std::make_shared<ArrayValue>();
            result.array->elements.resize(static_cast<size_t>(size));
            Push(result);
            break;
        }
        case OpCode::GetIndex: {
            const int index = static_cast<int>(NumberValue(Pop()));
            const Value array = Pop();
            if (array.tag == Value::Tag::Array && array.array && index >= 0 &&
                static_cast<size_t>(index) < array.array->elements.size())
                Push(array.array->elements[static_cast<size_t>(index)]);
            else {
                outErrors.push_back("array index out of range");
                Push(Value{});
            }
            break;
        }
        case OpCode::SetIndex: {
            const Value value = Pop();
            const int index = static_cast<int>(NumberValue(Pop()));
            const Value array = Pop();
            if (array.tag == Value::Tag::Array && array.array && index >= 0 &&
                static_cast<size_t>(index) < array.array->elements.size())
                array.array->elements[static_cast<size_t>(index)] = value;
            else
                outErrors.push_back("array index out of range");
            break;
        }
        case OpCode::ArrayLength: {
            const Value array = Pop();
            Value length; length.tag = Value::Tag::Number;
            length.number = (array.tag == Value::Tag::Array && array.array)
                ? static_cast<double>(array.array->elements.size()) : 0.0;
            Push(length);
            break;
        }
        case OpCode::ArrayAdd: {
            const Value value = Pop();
            const Value array = Pop();
            if (array.tag == Value::Tag::Array && array.array) {
                if (array.array->elements.size() >= m_Limits.maxArrayElements) {
                    outErrors.push_back("array element limit exceeded");
                    return false;
                }
                array.array->elements.push_back(value);
            }
            else
                outErrors.push_back("Add() target is not an array");
            Push(Value{});
            break;
        }
        case OpCode::ArrayRemoveAt: {
            const int index = static_cast<int>(NumberValue(Pop()));
            const Value array = Pop();
            if (array.tag == Value::Tag::Array && array.array && index >= 0 &&
                static_cast<size_t>(index) < array.array->elements.size())
                array.array->elements.erase(array.array->elements.begin() + index);
            else
                outErrors.push_back("RemoveAt() index out of range");
            Push(Value{});
            break;
        }
        case OpCode::ArrayClear: {
            const Value array = Pop();
            if (array.tag == Value::Tag::Array && array.array)
                array.array->elements.clear();
            else
                outErrors.push_back("Clear() target is not an array");
            Push(Value{});
            break;
        }
        case OpCode::StartCoroutine: {
            const uint16_t nameIndex = ReadU16(code, ip);
            if (nameIndex < m_Module->nameConstants.size() && instance.coroutines.size() < 16) {
                const std::string& methodName = m_Module->nameConstants[nameIndex];
                if (const CompiledMethod* target = m_Module->FindMethod(methodName)) {
                    CoroutineState state;
                    state.methodName = methodName;
                    state.locals.assign(target->localCount, Value{});
                    instance.coroutines.push_back(std::move(state));
                } else {
                    outErrors.push_back("coroutine method not found: " + methodName);
                }
            }
            Push(Value{});
            break;
        }
        case OpCode::StopAllCoroutines:
            if (coroutine) {
                for (CoroutineState& state : instance.coroutines)
                    state.completed = true;
            } else {
                instance.coroutines.clear();
            }
            Push(Value{});
            break;
        case OpCode::YieldNext:
        case OpCode::YieldSeconds:
        case OpCode::YieldBreak: {
            if (!coroutine) {
                outErrors.push_back("yield used outside StartCoroutine");
                return false;
            }
            if (op == OpCode::YieldBreak) {
                coroutine->completed = true;
                return true;
            }
            coroutine->instruction = ip;
            coroutine->stack = m_Stack;
            coroutine->locals = m_Locals;
            coroutine->runtimeStrings = m_RuntimeStrings;
            if (op == OpCode::YieldSeconds) {
                const Value seconds = Pop();
                coroutine->stack = m_Stack;
                coroutine->waitSeconds = std::max(0.0f, static_cast<float>(NumberValue(seconds)));
                coroutine->waitFrames = 0;
            } else {
                coroutine->waitFrames = 1;
                coroutine->waitSeconds = 0.0f;
            }
            return true;
        }
        default:
            outErrors.push_back("unknown opcode");
            return false;
        }
    }

    return true;
}

void VM::ResumeCoroutines(ScriptInstance& instance, float deltaTime,
                          std::vector<std::string>& outErrors) {
    for (size_t i = 0; i < instance.coroutines.size(); ++i) {
        CoroutineState& coroutine = instance.coroutines[i];
        if (coroutine.completed)
            continue;
        if (coroutine.waitFrames > 0) {
            --coroutine.waitFrames;
            continue;
        }
        if (coroutine.waitSeconds > 0.0f) {
            coroutine.waitSeconds -= deltaTime;
            if (coroutine.waitSeconds > 0.0f)
                continue;
            coroutine.waitSeconds = 0.0f;
        }
        const CompiledMethod* method = instance.module
            ? instance.module->FindMethod(coroutine.methodName) : nullptr;
        if (!method) {
            coroutine.completed = true;
            outErrors.push_back("coroutine method not found: " + coroutine.methodName);
            continue;
        }
        Execute(instance, *method, outErrors, &coroutine);
    }
    instance.coroutines.erase(
        std::remove_if(instance.coroutines.begin(), instance.coroutines.end(),
                       [](const CoroutineState& state) { return state.completed; }),
        instance.coroutines.end());
}

const std::string& VM::ResolveString(const Value& value) const {
    if (value.tag == Value::Tag::String && value.runtimeString &&
        value.stringIndex < m_RuntimeStrings.size())
        return m_RuntimeStrings[value.stringIndex];
    if (value.tag == Value::Tag::String &&
        value.stringIndex < m_Module->stringConstants.size())
        return m_Module->stringConstants[value.stringIndex];
    static const std::string kEmpty;
    return kEmpty;
}

Value VM::MakeRuntimeString(const std::string& text) {
    Value v;
    if (m_RuntimeStrings.size() >= m_Limits.maxRuntimeStrings ||
        text.size() > m_Limits.maxRuntimeStringBytes -
            std::min(m_RuntimeStringBytes, m_Limits.maxRuntimeStringBytes)) {
        m_RuntimeStringLimitExceeded = true;
        return v;
    }
    v.tag = Value::Tag::String;
    v.runtimeString = true;
    v.stringIndex = static_cast<uint16_t>(m_RuntimeStrings.size());
    m_RuntimeStrings.push_back(text);
    m_RuntimeStringBytes += text.size();
    return v;
}

Value VM::MakeValueVec3(double x, double y, double z) {
    Value v;
    v.tag = Value::Tag::Host;
    v.host.kind = HostKind::ValueVec3;
    v.host.valueVec3[0] = x;
    v.host.valueVec3[1] = y;
    v.host.valueVec3[2] = z;
    return v;
}

bool VM::ReadVec3(const Value& value, double& x, double& y, double& z) {
    if (value.tag != Value::Tag::Host)
        return false;
    if (value.host.kind == HostKind::ValueVec3) {
        x = value.host.valueVec3[0];
        y = value.host.valueVec3[1];
        z = value.host.valueVec3[2];
        return true;
    }
    if (value.host.kind == HostKind::Vec3) {
        x = value.host.vec3.x ? *value.host.vec3.x : 0.0;
        y = value.host.vec3.y ? *value.host.vec3.y : 0.0;
        z = value.host.vec3.z ? *value.host.vec3.z : 0.0;
        return true;
    }
    return false;
}

} // namespace MipsyncEngine::Mips
