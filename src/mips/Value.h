#pragma once

#include <cstdint>
#include <string>
#include <memory>
#include <vector>

namespace MipsyncEngine {

class Entity;
class TransformComponent;

namespace Mips {

class ScriptInstance;
struct ArrayValue;

enum class HostKind : uint8_t {
    None,
    Transform,
    Vec3,
    WorldVec3,
    MipsScript,
    ValueVec3,  // stack vec3 (Vector3.Create / math)
    Entity,     // gameObject
    Animator,   // Animator on host.entity (GetComponent<Animator> / global)
    AudioSource,// AudioSource on host.entity (GetComponent<AudioSource> / global)
    Collider,
    Rigidbody,
};

enum class WorldVecMember : uint8_t {
    Position,
};

struct HostRef {
    HostKind kind = HostKind::None;
    TransformComponent* transform = nullptr;
    struct { float* x; float* y; float* z; } vec3{};
    WorldVecMember worldMember = WorldVecMember::Position;
    ScriptInstance* script = nullptr;
    Entity* entity = nullptr;
    double valueVec3[3] = { 0.0, 0.0, 0.0 };
};

struct Value {
    enum class Tag : uint8_t { Nil, Bool, Number, String, Host, Array } tag = Tag::Nil;

    bool boolValue = false;
    double number = 0.0;
    uint16_t stringIndex = 0;
    bool runtimeString = false;
    HostRef host;
    std::shared_ptr<ArrayValue> array;
};

struct ArrayValue {
    std::vector<Value> elements;
};

} // namespace Mips
} // namespace MipsyncEngine
