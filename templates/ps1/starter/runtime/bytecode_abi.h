#ifndef MIPSYNC_BYTECODE_ABI_H
#define MIPSYNC_BYTECODE_ABI_H

/*
 * Single source of truth for the desktop compiler / PS1 VM bytecode ABI.
 * Keep entries append-only. Renaming is safe; changing a numeric value is not.
 */
#define MIPSYNC_OPCODE_LIST(X) \
    X(PUSH_CONST, PushConst, 0) \
    X(PUSH_BOOL, PushBool, 1) \
    X(PUSH_STRING, PushString, 2) \
    X(PUSH_FIELD, PushField, 3) \
    X(SET_FIELD, SetField, 4) \
    X(PUSH_LOCAL, PushLocal, 5) \
    X(SET_LOCAL, SetLocal, 6) \
    X(POP, Pop, 7) \
    X(GET_GLOBAL, GetGlobal, 8) \
    X(GET_COMPONENT, GetComponent, 9) \
    X(GET_MEMBER, GetMember, 10) \
    X(GET_VEC3_AXIS, GetVec3Axis, 11) \
    X(SET_VEC3_AXIS, SetVec3Axis, 12) \
    X(SET_VEC3_FROM_VALUE, SetVec3FromValue, 13) \
    X(ADD, Add, 14) \
    X(SUB, Sub, 15) \
    X(MUL, Mul, 16) \
    X(DIV, Div, 17) \
    X(MOD, Mod, 18) \
    X(NEG, Neg, 19) \
    X(NOT, Not, 20) \
    X(EQ, Eq, 21) \
    X(NE, Ne, 22) \
    X(LT, Lt, 23) \
    X(GT, Gt, 24) \
    X(LE, Le, 25) \
    X(GE, Ge, 26) \
    X(AND, And, 27) \
    X(OR, Or, 28) \
    X(CALL_HOST, CallHost, 29) \
    X(RETURN, Return, 30) \
    X(JUMP, Jump, 31) \
    X(JUMP_IF_FALSE, JumpIfFalse, 32) \
    X(NEW_ARRAY, NewArray, 33) \
    X(NEW_ARRAY_SIZED, NewArraySized, 34) \
    X(GET_INDEX, GetIndex, 35) \
    X(SET_INDEX, SetIndex, 36) \
    X(ARRAY_LENGTH, ArrayLength, 37) \
    X(ARRAY_ADD, ArrayAdd, 38) \
    X(ARRAY_REMOVE_AT, ArrayRemoveAt, 39) \
    X(ARRAY_CLEAR, ArrayClear, 40) \
    X(START_COROUTINE, StartCoroutine, 41) \
    X(STOP_ALL_COROUTINES, StopAllCoroutines, 42) \
    X(YIELD_NEXT, YieldNext, 43) \
    X(YIELD_SECONDS, YieldSeconds, 44) \
    X(YIELD_BREAK, YieldBreak, 45)

#define MIPSYNC_HOST_FUNC_LIST(X) \
    X(LOG_INFO, Log_Info, 0) \
    X(TIME_DELTATIME, Time_DeltaTime, 1) \
    X(INPUT_GETKEY, Input_GetKey, 2) \
    X(INPUT_MOUSEDX, Input_MouseDeltaX, 3) \
    X(INPUT_MOUSEDY, Input_MouseDeltaY, 4) \
    X(INPUT_SETCURSORLOCK, Input_SetCursorLocked, 5) \
    X(INPUT_GETCURSORLOCK, Input_GetCursorLocked, 6) \
    X(MATHF_SIN, Mathf_Sin, 7) \
    X(MATHF_COS, Mathf_Cos, 8) \
    X(MATHF_SQRT, Mathf_Sqrt, 9) \
    X(MATHF_ABS, Mathf_Abs, 10) \
    X(MATHF_CLAMP, Mathf_Clamp, 11) \
    X(VEC3_CREATE, Vector3_Create, 12) \
    X(VEC3_ADD, Vector3_Add, 13) \
    X(VEC3_SUB, Vector3_Sub, 14) \
    X(VEC3_SCALE, Vector3_Scale, 15) \
    X(VEC3_LENGTH, Vector3_Length, 16) \
    X(VEC3_NORMALIZE, Vector3_Normalize, 17) \
    X(VEC3_UP, Vector3_Up, 18) \
    X(VEC3_FORWARD, Vector3_Forward, 19) \
    X(VEC3_RIGHT, Vector3_Right, 20) \
    X(PHYSICS_RAYCAST, Physics_Raycast, 21) \
    X(PHYSICS_MOVE, Physics_Move, 22) \
    X(PHYSICS_IS_GROUNDED, Physics_IsGrounded, 23) \
    X(INPUT_GETKEYDOWN, Input_GetKeyDown, 24) \
    X(ENTITY_GETID, Entity_GetId, 25) \
    X(ENTITY_GETNAME, Entity_GetName, 26) \
    X(ANIMATOR_SETFLOAT, Animator_SetFloat, 27) \
    X(ANIMATOR_SETBOOL, Animator_SetBool, 28) \
    X(ANIMATOR_SETINT, Animator_SetInt, 29) \
    X(ANIMATOR_SETTRIGGER, Animator_SetTrigger, 30) \
    X(SCENE_LOAD, Scene_Load, 31) \
    X(SCENE_LOAD_BUILD_INDEX, Scene_LoadBuildIndex, 32) \
    X(APPLICATION_QUIT, Application_Quit, 33) \
    X(SAVE_GET_INT, Save_GetInt, 34) \
    X(SAVE_SET_INT, Save_SetInt, 35) \
    X(SAVE_GET_FLOAT, Save_GetFloat, 36) \
    X(SAVE_SET_FLOAT, Save_SetFloat, 37) \
    X(SAVE_GET_STRING, Save_GetString, 38) \
    X(SAVE_SET_STRING, Save_SetString, 39) \
    X(SAVE_GET_BOOL, Save_GetBool, 40) \
    X(SAVE_SET_BOOL, Save_SetBool, 41) \
    X(SAVE_WRITE, Save_Write, 42) \
    X(SAVE_READ, Save_Read, 43) \
    X(PHYSICS_OTHER_ENTITY_ID, Physics_OtherEntityId, 44) \
    X(CAMERA_RIGHTX, Camera_RightX, 45) \
    X(CAMERA_RIGHTZ, Camera_RightZ, 46) \
    X(CAMERA_FORWARDX, Camera_ForwardX, 47) \
    X(CAMERA_FORWARDZ, Camera_ForwardZ, 48) \
    X(CAMERA_YAW, Camera_Yaw, 49) \
    X(MATHF_ATAN2, Mathf_Atan2, 50) \
    X(AUDIO_PLAY, AudioSource_Play, 51) \
    X(AUDIO_STOP, AudioSource_Stop, 52) \
    X(AUDIO_PAUSE, AudioSource_Pause, 53) \
    X(AUDIO_UNPAUSE, AudioSource_UnPause, 54) \
    X(AUDIO_SET_CLIP, AudioSource_SetClip, 55) \
    X(AUDIO_SET_VOLUME, AudioSource_SetVolume, 56) \
    X(AUDIO_SET_LOOP, AudioSource_SetLoop, 57) \
    X(AUDIO_SET_MUTE, AudioSource_SetMute, 58) \
    X(AUDIO_SET_AWAKE, AudioSource_SetPlayOnAwake, 59) \
    X(AUDIO_SET_ENABLED, AudioSource_SetEnabled, 60) \
    X(INPUT_GETKEYUP, Input_GetKeyUp, 61) \
    X(INPUT_GETAXIS, Input_GetAxis, 62) \
    X(MATHF_MIN, Mathf_Min, 63) \
    X(MATHF_MAX, Mathf_Max, 64) \
    X(MATHF_LERP, Mathf_Lerp, 65) \
    X(MATHF_FLOOR, Mathf_Floor, 66) \
    X(MATHF_CEIL, Mathf_Ceil, 67) \
    X(MATHF_ROUND, Mathf_Round, 68) \
    X(MATHF_SIGN, Mathf_Sign, 69)

/* PS1 runtime capacities. The exporter validates these before emitting C. */
#define MIPSYNC_PS1_MODULE_CAP      4
#define MIPSYNC_PS1_INSTANCE_CAP   32
#define VM_STACK_CAP               64
#define VM_LOCAL_CAP               64
#define VM_FIELD_CAP               32
#define VM_NUMBER_CAP             128
#define VM_STRING_CAP             128
#define VM_STRING_LEN              96
#define VM_NAME_CAP               128
#define VM_NAME_LEN                32
#define VM_CLASS_NAME_LEN          32
#define VM_METHOD_CAP              16
#define VM_METHOD_NAME_LEN         24
#define VM_FIELD_NAME_LEN          24
#define VM_METHOD_CODE_CAP       4096
#define VM_ARRAY_CAP                8
#define VM_ARRAY_LENGTH            32
#define VM_COROUTINE_CAP            4

#endif /* MIPSYNC_BYTECODE_ABI_H */
