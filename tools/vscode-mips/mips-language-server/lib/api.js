"use strict";

const KEYWORDS = [
  "class",
  "public",
  "private",
  "void",
  "bool",
  "int",
  "float",
  "string",
  "var",
  "enum",
  "if",
  "else",
  "while",
  "for",
  "break",
  "continue",
  "return",
  "true",
  "false",
  "new",
  "null",
  "get",
  "set",
  "yield",
  "extends",
];

const TYPES = [
  "MipsBehaviour",
  "IEnumerator",
  "WaitForSeconds",
  "AudioClip",
  "Entity",
  "Transform",
  "Vector3",
  "Animator",
  "AudioSource",
  "Collider",
  "Rigidbody",
  "Camera",
];

const GLOBALS = {
  transform: {
    detail: "Transform transform",
    documentation: "現在のオブジェクトの Transform。",
    members: {
      position: "Vector3 position",
      worldPosition: "Vector3 worldPosition",
      rotation: "Vector3 rotation",
      scale: "Vector3 scale",
    },
  },
  gameObject: {
    detail: "Entity gameObject",
    documentation: "このスクリプトがアタッチされている Entity。",
    members: {
      id: "int id",
      name: "string name",
    },
  },
  Time: {
    detail: "Time",
    documentation: "フレーム時間 API。",
    members: {
      deltaTime: "float deltaTime",
    },
  },
  Input: {
    detail: "Input",
    documentation: "PS1 ボタン・スティック入力 API。Game View のキーボード／マウス割り当ては Preferences で変更できます。",
    members: {
      GetButton: "bool GetButton(string button)",
      GetButtonDown: "bool GetButtonDown(string button)",
      GetButtonUp: "bool GetButtonUp(string button)",
      GetKey: "bool GetKey(string key)",
      GetKeyDown: "bool GetKeyDown(string key)",
      GetKeyUp: "bool GetKeyUp(string key)",
      GetAxis: "float GetAxis(string axis)",
      SetCursorLocked: "void SetCursorLocked(bool locked)",
      cursorLocked: "bool cursorLocked",
      mouseDeltaX: "float mouseDeltaX",
      mouseDeltaY: "float mouseDeltaY",
    },
  },
  Log: {
    detail: "Log",
    documentation: "エディタ/ランタイムログ出力。",
    members: {
      Info: "void Info(params values)",
    },
  },
  Debug: {
    detail: "Debug",
    documentation: "Log.Info の Unity 風 alias。",
    members: {
      Log: "void Log(params values)",
    },
  },
  Mathf: {
    detail: "Mathf",
    documentation: "数学ヘルパー。",
    members: {
      Sin: "float Sin(float v)",
      Cos: "float Cos(float v)",
      Sqrt: "float Sqrt(float v)",
      Abs: "float Abs(float v)",
      Clamp: "float Clamp(float value, float min, float max)",
      Atan2: "float Atan2(float y, float x)",
      Min: "float Min(float a, float b)",
      Max: "float Max(float a, float b)",
      Lerp: "float Lerp(float a, float b, float t)",
      Floor: "float Floor(float v)",
      Ceil: "float Ceil(float v)",
      Round: "float Round(float v)",
      Sign: "float Sign(float v)",
    },
  },
  Vector3: {
    detail: "Vector3",
    documentation: "3D ベクトル。Vector3(x, y, z) で生成できる。",
    members: {
      up: "Vector3 up",
      forward: "Vector3 forward",
      right: "Vector3 right",
      Add: "Vector3 Add(Vector3 a, Vector3 b)",
      Sub: "Vector3 Sub(Vector3 a, Vector3 b)",
      Scale: "Vector3 Scale(Vector3 a, float s)",
      Length: "float Length(Vector3 v)",
      Normalize: "Vector3 Normalize(Vector3 v)",
      x: "float x",
      y: "float y",
      z: "float z",
    },
  },
  Physics: {
    detail: "Physics",
    documentation: "PS1/エディタ共通の物理 API。",
    members: {
      Raycast: "int Raycast(float ox, float oy, float oz, float dx, float dy, float dz, float maxDist)",
      Move: "void Move(float vx, float vy, float vz)",
      IsGrounded: "bool IsGrounded()",
      IsGroundedWithin: "bool IsGroundedWithin(float distance)",
      UseCharacterController: "void UseCharacterController()",
      UseKinematicController: "void UseKinematicController()",
      MoveKinematic: "void MoveKinematic(float vx, float vy, float vz)",
      otherEntityId: "int otherEntityId",
    },
  },
  Animator: {
    detail: "Animator",
    documentation: "同じ Entity の Animator を操作するショートカット。",
    members: {
      GetNormalizedTime: "float GetNormalizedTime(string stateName)",
      SetFloat: "void SetFloat(string name, float value)",
      SetBool: "void SetBool(string name, bool value)",
      SetInt: "void SetInt(string name, int value)",
      SetTrigger: "void SetTrigger(string name)",
      SetTriggerHeld: "void SetTriggerHeld(string name)",
      ReleaseTrigger: "void ReleaseTrigger(string name)",
    },
  },
  AudioSource: {
    detail: "AudioSource",
    documentation: "同じ Entity の AudioSource を操作するショートカット。",
    members: {
      Play: "void Play()",
      Stop: "void Stop()",
      Pause: "void Pause()",
      UnPause: "void UnPause()",
      volume: "float volume",
      loop: "bool loop",
      mute: "bool mute",
      clip: "string clip",
      playOnAwake: "bool playOnAwake",
      enabled: "bool enabled",
      isPlaying: "bool isPlaying",
    },
  },
  Camera: {
    detail: "Camera",
    documentation: "カメラ方向と追従。",
    members: {
      Follow: "void Follow(Camera camera, float yaw, float distance, float height, float lookHeight, float sharpness, float pitchOffset = 0.0)",
      RotatePitch: "float RotatePitch(Camera camera, float deltaDegrees, float minPitch, float maxPitch)",
      rightX: "float rightX", rightZ: "float rightZ", forwardX: "float forwardX", forwardZ: "float forwardZ", yaw: "float yaw",
    },
  },
  UI: {
    detail: "UI", documentation: "UI の表示切り替え。",
    members: { SetTextVisible: "bool SetTextVisible(int entityId, bool visible)", SetImageVisible: "bool SetImageVisible(int entityId, bool visible)", SetText: "bool SetText(int entityId, string text)", SetTextNumber: "bool SetTextNumber(int entityId, float number)", SetColor: "bool SetColor(int entityId, float r, float g, float b, float a)" },
  },
  GetComponent: { detail: "T GetComponent<T>()", documentation: "同じ Entity のコンポーネントを取得。", members: {} },
  StartCoroutine: { detail: "void StartCoroutine(IEnumerator routine)", documentation: "コルーチンを開始。", members: {} },
  StopAllCoroutines: { detail: "void StopAllCoroutines()", documentation: "このスクリプトのコルーチンを停止。", members: {} },
  Scene: {
    detail: "Scene",
    documentation: "シーン遷移 API。",
    members: {
      Load: "void Load(string scenePath)",
      LoadBuildIndex: "void LoadBuildIndex(int index)",
    },
  },
  Application: {
    detail: "Application",
    documentation: "アプリケーション制御。",
    members: {
      Quit: "void Quit()",
    },
  },
  Save: {
    detail: "Save",
    documentation: "JSON セーブデータ API。",
    members: {
      SetInt: "void SetInt(string key, int value)",
      GetInt: "int GetInt(string key, int defaultValue)",
      SetFloat: "void SetFloat(string key, float value)",
      GetFloat: "float GetFloat(string key, float defaultValue)",
      SetBool: "void SetBool(string key, bool value)",
      GetBool: "bool GetBool(string key, bool defaultValue)",
      SetString: "void SetString(string key, string value)",
      GetString: "string GetString(string key, string defaultValue)",
      Write: "void Write(string path)",
      Read: "bool Read(string path)",
    },
  },
};

const INSTANCE_MEMBERS = {
  Transform: GLOBALS.transform.members,
  Vector3: GLOBALS.Vector3.members,
  Animator: GLOBALS.Animator.members,
  AudioSource: GLOBALS.AudioSource.members,
  Entity: GLOBALS.gameObject.members,
  Array: {
    Length: "int Length",
    Count: "int Count",
    Add: "void Add(value)",
    RemoveAt: "void RemoveAt(int index)",
    Clear: "void Clear()",
  },
};

const SNIPPETS = [
  {
    label: "MipsBehaviour class",
    insertText:
      "class ${1:PlayerController} : MipsBehaviour {\n    public float speed = 3.0;\n\n    void Start() {\n        $0\n    }\n\n    void Update() {\n    }\n}",
    documentation: "Mips# component script template.",
  },
  {
    label: "Coroutine",
    insertText:
      "public IEnumerator ${1:Routine}() {\n    yield return new WaitForSeconds(${2:0.5});\n    $0\n}",
    documentation: "Mips# coroutine template.",
  },
  {
    label: "StartCoroutine",
    insertText: "StartCoroutine(${1:Routine}());",
    documentation: "Start a no-argument IEnumerator coroutine.",
  },
  {
    label: "yield return null",
    insertText: "yield return null;",
    documentation: "Resume on next frame.",
  },
  {
    label: "yield return WaitForSeconds",
    insertText: "yield return new WaitForSeconds(${1:0.5});",
    documentation: "Resume after scaled seconds.",
  },
];

const ATTRIBUTES = {
  Header: { signature: 'Header(string title)', snippet: 'Header("${1:Settings}")', documentation: 'Show a section heading before this public field.' },
  Space: { signature: 'Space(float pixels = 8)', snippet: 'Space(${1:8})', documentation: 'Add vertical space before this field (0 to 1000 pixels, scaled with the editor font).' },
  Tooltip: { signature: 'Tooltip(string text)', snippet: 'Tooltip("${1:Description}")', documentation: 'Show help when hovering over the field label or control, including read-only fields.' },
  Range: { signature: 'Range(float min, float max)', snippet: 'Range(${1:0}, ${2:1})', documentation: 'Display an int or float field as a slider. Bounds apply to Inspector edits only.' },
  Min: { signature: 'Min(float value)', snippet: 'Min(${1:0})', documentation: 'Set a minimum for Inspector edits to an int or float field. Script assignments remain unrestricted.' },
  ReadOnly: { signature: 'ReadOnly', snippet: 'ReadOnly', documentation: 'Display a field without allowing Inspector edits. Script code can still assign it.' },
  HideInInspector: { signature: 'HideInInspector', snippet: 'HideInInspector', documentation: 'Hide a public field in the Inspector while preserving its stored value and script access.' },
  InspectorName: { signature: 'InspectorName(string label)', snippet: 'InspectorName("${1:Display Name}")', documentation: 'Change the visible label without renaming the field or losing saved values.' },
  Order: { signature: 'Order(int value)', snippet: 'Order(${1:0})', documentation: 'Sort visible fields by this value, then declaration order. Does not change serialization or VM field indices.' },
};

module.exports = {
  ATTRIBUTES,
  KEYWORDS,
  TYPES,
  GLOBALS,
  INSTANCE_MEMBERS,
  SNIPPETS,
};
