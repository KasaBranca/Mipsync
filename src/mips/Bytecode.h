#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace MipsyncEngine::Mips {

enum class OpCode : uint8_t {
    PushConst,      // u16 number const index
    PushBool,       // u8 0/1
    PushString,     // u16 string const index
    PushField,      // u16 field index
    SetField,       // u16 field index — pops value
    PushLocal,      // u16 local index
    SetLocal,       // u16 local index — pops value
    Pop,

    GetGlobal,      // u16 name index (string pool)
    GetComponent,   // u16 script class name index
    GetMember,      // u16 member name index
    GetVec3Axis,    // u8 axis (0=x,1=y,2=z)
    SetVec3Axis,    // u8 axis — pops value, pops vec3 host
    SetVec3FromValue, // pops ValueVec3, pops Vec3 member host

    Add, Sub, Mul, Div, Mod,
    Neg, Not,
    Eq, Ne, Lt, Gt, Le, Ge,
    And, Or,

    CallHost,       // u16 host id, u8 argc
    Return,
    Jump,           // i32 offset
    JumpIfFalse,    // i32 offset

    NewArray,       // u16 element count; pops elements in source order
    NewArraySized,  // pops numeric size
    GetIndex,       // pops index, array; pushes element
    SetIndex,       // pops value, index, array
    ArrayLength,    // pops array; pushes length
    ArrayAdd,       // pops value, array; pushes nil
    ArrayRemoveAt,  // pops index, array; pushes nil
    ArrayClear,     // pops array; pushes nil

    StartCoroutine, // u16 method name index; pushes nil
    StopAllCoroutines, // pushes nil
    YieldNext,
    YieldSeconds,   // pops seconds
    YieldBreak,
};

enum class HostFunc : uint16_t {
    Log_Info = 0,
    Time_DeltaTime,

    Input_GetKey,        // 1 arg (string)
    Input_MouseDeltaX,   // 0 args
    Input_MouseDeltaY,   // 0 args
    Input_SetCursorLocked, // 1 arg (number 0/1)
    Input_GetCursorLocked, // 0 args

    Mathf_Sin,           // 1 arg
    Mathf_Cos,           // 1 arg
    Mathf_Sqrt,          // 1 arg
    Mathf_Abs,           // 1 arg
    Mathf_Clamp,         // 3 args

    Vector3_Create,      // 3 args (x,y,z) -> ValueVec3 host
    Vector3_Add,         // 2 args
    Vector3_Sub,         // 2 args
    Vector3_Scale,       // 2 args (vec, scalar)
    Vector3_Length,      // 1 arg
    Vector3_Normalize,   // 1 arg
    Vector3_Up,          // 0 args
    Vector3_Forward,     // 0 args
    Vector3_Right,       // 0 args

    Physics_Raycast,     // 7 args: ox,oy,oz, dx,dy,dz, maxDist -> entity id (0 = miss)
    Physics_Move,        // 3 args: dx,dy,dz world displacement (collision-resolved)
    Physics_IsGrounded,  // 0 args -> 1.0 / 0.0

    Input_GetKeyDown,    // 1 arg (string key name)

    Entity_GetId,        // 1 arg (Entity host)
    Entity_GetName,      // 1 arg (Entity host) -> string

    Animator_SetFloat,   // 2 args (string name, number value) — same entity
    Animator_SetBool,    // 2 args (string name, number 0/1)
    Animator_SetInt,     // 2 args (string name, number value)
    Animator_SetTrigger, // 1 arg (string name)

    Scene_Load,          // 1 arg project-relative scene path
    Scene_LoadBuildIndex, // 1 arg build index (Scenes In Build order)
    Application_Quit,    // 0 args

    Save_GetInt,         // 2 args (key, default)
    Save_SetInt,         // 2 args (key, value)
    Save_GetFloat,
    Save_SetFloat,
    Save_GetString,      // 2 args (key, default string)
    Save_SetString,      // 2 args (key, value string)
    Save_GetBool,
    Save_SetBool,
    Save_Write,          // 1 arg file path (relative to project/saves/)
    Save_Read,           // 1 arg file path

    Physics_OtherEntityId, // 0 args during collision/trigger callback

    Camera_RightX,       // 0 args, active game camera planar right.x
    Camera_RightZ,       // 0 args, active game camera planar right.z
    Camera_ForwardX,     // 0 args, active game camera planar forward.x
    Camera_ForwardZ,     // 0 args, active game camera planar forward.z
    Camera_Yaw,          // 0 args, yaw in TransformComponent degrees for active camera forward

    // Appended to preserve the numeric values of existing host functions.
    Mathf_Atan2,         // 2 args (y, x), result in radians

    AudioSource_Play,            // target AudioSource host
    AudioSource_Stop,
    AudioSource_Pause,
    AudioSource_UnPause,
    AudioSource_SetClip,         // target, string
    AudioSource_SetVolume,       // target, number 0..1
    AudioSource_SetLoop,         // target, bool
    AudioSource_SetMute,         // target, bool
    AudioSource_SetPlayOnAwake,  // target, bool
    AudioSource_SetEnabled,      // target, bool

    Input_GetKeyUp,              // 1 arg (string)
    Input_GetAxis,               // 1 arg (Horizontal/Vertical)
    Mathf_Min,                   // 2 args
    Mathf_Max,                   // 2 args
    Mathf_Lerp,                  // 3 args
    Mathf_Floor,                 // 1 arg
    Mathf_Ceil,                  // 1 arg
    Mathf_Round,                 // 1 arg
    Mathf_Sign,                  // 1 arg
};

struct CompiledMethod {
    std::string name;
    std::vector<uint8_t> code;
    uint32_t localCount = 0;
};

enum class FieldValueKind : uint8_t { Number = 0, Bool, AudioClip, Array };

struct CompiledField {
    std::string name;
    std::string typeName;
    FieldValueKind valueKind = FieldValueKind::Number;
    uint16_t defaultConstIndex = 0;
    bool hasGetter = true;
    bool hasSetter = true;
};

struct CompiledModule {
    std::string className;
    std::vector<CompiledField> fields;
    std::vector<CompiledMethod> methods;
    std::vector<double> numberConstants;
    std::vector<std::string> stringConstants;
    std::vector<std::string> nameConstants; // globals / members

    const CompiledMethod* FindMethod(const std::string& name) const;
    int FindFieldIndex(const std::string& name) const;
};

class BytecodeWriter {
public:
    std::vector<uint8_t>& Code() { return m_Code; }
    const std::vector<uint8_t>& Code() const { return m_Code; }

    void EmitOp(OpCode op);
    void EmitU8(uint8_t value);
    void EmitU16(uint16_t value);
    void EmitI32(int32_t value);

    size_t EmitJumpPlaceholder();
    size_t EmitJumpIfFalsePlaceholder();
    void PatchJump(size_t jumpOffset, size_t targetOffset);

    size_t CurrentOffset() const { return m_Code.size(); }

private:
    std::vector<uint8_t> m_Code;
};

} // namespace MipsyncEngine::Mips
