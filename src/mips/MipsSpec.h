#pragma once
// ─────────────────────────────────────────────────
// Mips# Language Specification (v0.6)
// Unity C#-like syntax for Mipsync Engine component scripts
// ─────────────────────────────────────────────────
//
// File extension: .mips
// One class per file; class name must match file name (enforced later).
//
// Inheritance:
//   class Name : MipsBehaviour { ... }
//
// Lifecycle methods (all optional):
//   void Awake();
//   void Start();
//   void Update();
//   void LateUpdate();
//   void OnDestroy();
//   void OnCollisionEnter();
//   void OnCollisionExit();
//   void OnTriggerEnter();
//   void OnTriggerExit();
//
// Fields (Inspector-serialized):
//   public float speed = 90.0;
//   public bool grounded = true;
//   public int phase = 0;
//   public AudioClip music;  // Inspector audio asset drag-and-drop
//
// Auto-properties (backing field, Unity-style):
//   public float Speed { get; set; }
//   public bool Grounded { get; private set; }  // setter optional
//
// Enums (file scope):
//   enum Locomotion { Idle, Walk, Run }
//   // use Idle or Locomotion.Idle in expressions
//
// Types:
//   void, bool, int, float, string
//   Entity (via gameObject), Transform, Vector3
//   User script class names (GetComponent<ScriptName>())
//   Engine components: Animator, AudioSource, Collider, Rigidbody, Transform
//
// Built-in globals:
//   transform, gameObject, Time, Input, Log, Mathf, Vector3, Physics, Animator
//
// Locals:
//   var x = 1.0;   // stores any value including component host refs
//   Locals are block scoped.
//
// Control flow:
//   if / else, while, for, break, continue, return
//
// Arrays:
//   int[] values = [1, 2, 3];
//   int[] empty = new int[8];
//   values[0] = 4; var first = values[0];
//   values.Length / values.Count
//   values.Add(5); values.RemoveAt(1); values.Clear();
//   PS1: up to 8 arrays per script instance and 32 elements per array.
//
// Coroutines:
//   StartCoroutine(Routine()); StopAllCoroutines();
//   public IEnumerator Routine() {
//       yield return null;                         // next frame
//       yield return new WaitForSeconds(0.5);      // scaled seconds
//       yield break;
//   }
//   PS1: up to 4 simultaneous coroutines per script instance.
//
// Vector3:
//   Vector3(x, y, z)
//   Vector3.up / forward / right
//   Vector3.Add(a, b), Sub, Scale, Length, Normalize
//   transform.position = Vector3(...)  (whole-vector assign)
//   v.x, v.y, v.z on transform members and stack vectors
//
// gameObject:
//   gameObject.id, gameObject.name
//
// Strings:
//   "a" + "b", "hp=" + value  (concatenation)
//   Log.Info("msg", value, gameObject)
//
// Physics:
//   Physics.Raycast(ox, oy, oz, dx, dy, dz, maxDist) -> entity id (0 = miss)
//   Physics.Move(vx, vy, vz) — desired velocity in m/s (CharacterVirtual)
//   Physics.IsGrounded() -> 1.0 / 0.0 (Jolt raycast from capsule feet)
//   Physics.otherEntityId — other entity id during OnCollision*/OnTrigger* callbacks
//
// Scene / application:
//   Scene.Load("scenes/level2.nscene") — deferred load; restarts play if active
//   Scene.LoadBuildIndex(1) — load by index in Build Settings scene list
//   Application.Quit()
//
// Save (JSON under project/saves/):
//   Save.SetInt("key", 1);  Save.GetInt("key", 0)
//   Save.SetFloat / GetFloat, SetBool / GetBool, SetString / GetString
//   Save.Write("slot1.json");  Save.Read("slot1.json") -> 1.0 on success
//
// Input:
//   Input.GetKey("W"), Input.GetKeyDown("Space"), Input.GetKeyUp("Space")
//   Input.GetAxis("Horizontal"), Input.GetAxis("Vertical")
//   Input.mouseDeltaX / mouseDeltaY, Input.SetCursorLocked(1)
//
// Common helpers:
//   Debug.Log("value=", value)  // alias of Log.Info
//   Mathf.Min / Max / Lerp / Floor / Ceil / Round / Sign
//
// Animator (same entity, or via GetComponent<Animator>()):
//   Animator.SetFloat("Speed", 1.0)
//   Animator.SetBool("Grounded", 1)
//   Animator.SetInt("Phase", 2)
//   Animator.SetTrigger("Jump")
//   var anim = GetComponent<Animator>();
//   anim.SetFloat("Speed", 1.0);
//
// AudioSource (same entity, or via GetComponent<AudioSource>()):
//   AudioSource.Play();  AudioSource.Stop();
//   AudioSource.Pause(); AudioSource.UnPause();
//   var audio = GetComponent<AudioSource>();
//   audio.volume = 0.7; audio.loop = true; audio.mute = false;
//   audio.clip = "assets/audio/bgm.mp3";
//   audio.playOnAwake = true; audio.enabled = true;
//   audio.isPlaying  // read-only bool
//
// Planned:
//   struct, properties with custom get/set bodies
//   Animator.GetFloat, Entity host for Physics.otherEntityId

namespace MipsyncEngine::Mips {

inline constexpr const char* kLanguageVersion = "0.6";
inline constexpr const char* kBaseClassName = "MipsBehaviour";

} // namespace MipsyncEngine::Mips
