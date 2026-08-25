#include "Engine.h"
#include "Log.h"
#include "mips/MipsTest.h"
#include "mips/MipsRuntime.h"
#include "mips/MipsPhysicsEvents.h"
#include "../physics/PhysicsWorld.h"
#include "../physics/ColliderUtils.h"
#include "../audio/AudioSystem.h"
#include "../scene/SceneIO.h"
#include "../bootstrap/ProjectBootstrap.h"
#include "../assets/AssetManager.h"
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <utility>

namespace MipsyncEngine {

Engine* Engine::s_Instance = nullptr;

Engine::Engine(const std::string& projectPath, EngineLaunchMode launchMode,
               const std::string& playerDataDirectory)
    : m_LaunchMode(launchMode), m_PlayerDataDirectory(playerDataDirectory) {
    s_Instance = this;
    if (!Log::GetEngineLogger())
        Log::Init();

    ProjectBootstrapResult bootstrap = ProjectBootstrap::Resolve(
        projectPath, m_PlayerDataDirectory, m_LaunchMode == EngineLaunchMode::Player);
    m_ProjectPath = std::move(bootstrap.projectPath);
    m_PlayerDataDirectory = std::move(bootstrap.playerDataDirectory);
    m_ProjectName = std::move(bootstrap.projectName);
    m_BuildScenes = std::move(bootstrap.buildScenes);
    std::string startupSceneRel = std::move(bootstrap.startupSceneRelativePath);
    ProjectInfo projectInfo = std::move(bootstrap.projectInfo);

    MIPSYNC_INFO("Creating Mipsync Engine...");

    WindowProps props;
    props.title = (m_LaunchMode == EngineLaunchMode::Player)
        ? m_ProjectName
        : ("Mipsync Engine — " + m_ProjectName);
    props.width = (m_LaunchMode == EngineLaunchMode::Player) ? 1280 : 1600;
    props.height = (m_LaunchMode == EngineLaunchMode::Player) ? 720 : 900;
#ifdef _WIN32
    props.appUserModelId = L"Mipsync.MipsyncEngine.Editor";
#endif

    m_Window = std::make_unique<Window>(props);
    Input::Init(m_Window->GetNativeWindow());
    Time::Init();

    m_Renderer = std::make_unique<Renderer>();
    m_Renderer->Init();

    m_UIRenderer = std::make_unique<UIRenderer>();
    m_UIRenderer->Init();

    AssetManager::Get().SetProjectRoot(m_ProjectPath);
    Log::AddFileSink(PathUtf8::ToString(PathUtf8::FromString(m_ProjectPath) / "mipsync.log"));

    ProjectBootstrap::EnsureProjectDirectories(m_ProjectPath);

    const bool builtinScriptsUpdated = EnsureBuiltinScripts();
    const bool demoContentUpdated = EnsureDemoContent();

    m_Scene = std::make_unique<Scene>();
    m_MipsRuntime = std::make_unique<Mips::MipsRuntime>();
    m_PhysicsWorld = std::make_unique<PhysicsWorld>();
    m_AudioSystem = std::make_unique<AudioSystem>();
    m_MipsRuntime->SetPhysicsWorld(m_PhysicsWorld.get());
    m_PhysicsWorld->SetContactCallback(
        [this](uint32_t selfEntityId, uint32_t otherEntityId, bool isTrigger, bool isEnter) {
            MipsPhysicsEvent event;
            event.selfEntityId = selfEntityId;
            event.otherEntityId = otherEntityId;
            if (isTrigger) {
                event.kind = isEnter ? MipsPhysicsEvent::Kind::TriggerEnter
                                     : MipsPhysicsEvent::Kind::TriggerExit;
            } else {
                event.kind = isEnter ? MipsPhysicsEvent::Kind::CollisionEnter
                                     : MipsPhysicsEvent::Kind::CollisionExit;
            }
            m_MipsRuntime->PhysicsEvents().Push(event);
        });

    const std::filesystem::path projectRoot =
        m_ProjectPath.empty() ? std::filesystem::current_path() : PathUtf8::FromString(m_ProjectPath);
    const std::string defaultSceneRelPath =
        !startupSceneRel.empty()
            ? startupSceneRel
            : (projectInfo.defaultScene.empty() ? "scenes/default.nscene" : projectInfo.defaultScene);
    const std::string defaultScenePath =
        PathUtf8::ToString(projectRoot / PathUtf8::FromString(defaultSceneRelPath));

    std::error_code fsEc;
    if (std::filesystem::exists(PathUtf8::FromString(defaultScenePath), fsEc)) {
        std::string loadError;
        if (SceneIO::LoadFromFile(*m_Scene, defaultScenePath, loadError))
            MIPSYNC_INFO("Loaded scene: {}", defaultScenePath);
        else
            MIPSYNC_WARN("Scene load failed: {}", loadError);
    } else {
        SetupDefaultScene();
        std::string saveError;
        if (SceneIO::SaveToFile(*m_Scene, defaultScenePath, saveError))
            MIPSYNC_INFO("Wrote default scene: {}", defaultScenePath);
    }

    if (builtinScriptsUpdated) {
        m_MipsRuntime->ResetScriptsByFileName(*m_Scene, "FirstPersonController.mips");
        m_MipsRuntime->ResetScriptsByFileName(*m_Scene, "RadioController.mips");
    }
    if (demoContentUpdated)
        m_MipsRuntime->ResetScriptsByFileName(*m_Scene, "AnimatedCharacter.mips");

    m_MipsRuntime->SyncEditSnapshot(*m_Scene);

    Mips::RunMipsPhase1Tests();

    m_Editor = std::make_unique<EditorApp>(this);
    if (m_LaunchMode == EngineLaunchMode::Player)
        m_Editor->SetAutoPlayOnStart(true);
    m_Editor->Init();
}

Engine::~Engine() {
    // Construction performs several fallible platform/graphics operations.
    // Keep teardown valid even when an exception interrupts initialization.
    if (m_AudioSystem)
        m_AudioSystem->EndPlay();
    if (m_Editor)
        m_Editor->Shutdown();
    if (m_UIRenderer)
        m_UIRenderer->Shutdown();
    if (m_Renderer)
        m_Renderer->Shutdown();
    if (s_Instance == this)
        s_Instance = nullptr;
    Log::Shutdown();
}

bool Engine::EnsureBuiltinScripts() {
    namespace fs = std::filesystem;
    const fs::path scriptsDir = PathUtf8::FromString(m_ProjectPath) / "assets" / "scripts";
    const fs::path fpsPath = scriptsDir / "FirstPersonController.mips";
    const fs::path radioPath = scriptsDir / "RadioController.mips";

    // Bump this any time the embedded source below changes; existing files with
    // an older marker will be overwritten so users get fixes automatically.
    static constexpr const char* kFpsScriptVersion = "// @mipsync-builtin v9";
    static constexpr const char* kRadioScriptVersion = "// @mipsync-builtin-radio v22";

    static constexpr const char* kFpsScript = R"(// @mipsync-builtin v9
class FirstPersonController : MipsBehaviour
{
    public float moveSpeed = 5.0;
    public float mouseSensitivity = 0.15;
    public float minPitch = -85.0;
    public float maxPitch = 85.0;
    public float jumpSpeed = 6.0;
    public float gravity = 18.0;
    public float verticalSpeed = 0.0;

    void Start()
    {
        Input.SetCursorLocked(1);
    }

    void OnDestroy()
    {
        Input.SetCursorLocked(0);
    }

    void Update()
    {
        var dt = Time.deltaTime;

        // Mouse look: yaw = rotation.y (world up), pitch = rotation.x (after yaw).
        transform.rotation.y = transform.rotation.y - Input.mouseDeltaX * mouseSensitivity;
        var pitch = transform.rotation.x + Input.mouseDeltaY * mouseSensitivity;
        transform.rotation.x = Mathf.Clamp(pitch, minPitch, maxPitch);

        // Forward / right from current yaw (Y-up, -Z forward).
        var yawRad = transform.rotation.y * 0.01745329;
        var sinY = Mathf.Sin(yawRad);
        var cosY = Mathf.Cos(yawRad);
        var forwardX = 0.0 - sinY;
        var forwardZ = 0.0 - cosY;
        var rightX = cosY;
        var rightZ = 0.0 - sinY;

        // WASD planar input → unit direction.
        var dx = 0.0;
        var dz = 0.0;
        if (Input.GetKey("W")) { dx = dx + forwardX; dz = dz + forwardZ; }
        if (Input.GetKey("S")) { dx = dx - forwardX; dz = dz - forwardZ; }
        if (Input.GetKey("D")) { dx = dx + rightX;   dz = dz + rightZ;   }
        if (Input.GetKey("A")) { dx = dx - rightX;   dz = dz - rightZ;   }
        var len = Mathf.Sqrt(dx * dx + dz * dz);
        if (len > 0.0)
        {
            dx = dx / len;
            dz = dz / len;
        }

        // Vertical velocity: gravity in air, jump impulse on ground.
        var grounded = Physics.IsGrounded();
        if (grounded != 0.0)
        {
            if (Input.GetKeyDown("Space"))
                verticalSpeed = jumpSpeed;
            else if (verticalSpeed < 0.0)
                verticalSpeed = 0.0;
        }
        else
        {
            verticalSpeed = verticalSpeed - gravity * dt;
        }

        // Set the desired velocity (m/s). The CharacterVirtual integrates this
        // over dt, slides along walls/slopes, and pushes dynamic bodies.
        Physics.Move(dx * moveSpeed, verticalSpeed, dz * moveSpeed);
    }
}
)";

    static constexpr const char* kRadioScript = R"(// @mipsync-builtin-radio v22
class RadioController : MipsBehaviour
{
    public float moveSpeed = 3.0;
    public float aimTurnSpeed = 90.0;
    public float turnSmoothSpeed = 540.0;
    public float modelForwardYawOffset = 0.0;
    public float fixedY = 0.0;
    public int lockY = 0;
    public int driveAnimator = 1;
    public float walkThreshold = 0.01;
    public int aimStopsMovement = 1;
    public int snapCameraDirections = 1;
    public int heldW = 0;
    public int heldA = 0;
    public int heldS = 0;
    public int heldD = 0;
    public float heldWX = 0.0;
    public float heldWZ = -1.0;
    public float heldAX = -1.0;
    public float heldAZ = 0.0;
    public float heldSX = 0.0;
    public float heldSZ = 1.0;
    public float heldDX = 1.0;
    public float heldDZ = 0.0;
    public int invertTurnDirection = 0;
    public float activeTurnTarget = 9999.0;
    public float activeTurnDirection = 0.0;

    void Update()
    {
        var dt = Time.deltaTime;
        var controllerYaw = transform.rotation.y - modelForwardYawOffset;
        if (controllerYaw > 180.0) controllerYaw = controllerYaw - 360.0;
        if (controllerYaw < -180.0) controllerYaw = controllerYaw + 360.0;

        var dx = 0.0;
        var dz = 0.0;
        if (Input.GetKey("D")) dx = dx + 1.0;
        if (Input.GetKey("A")) dx = dx - 1.0;
        if (Input.GetKey("W")) dz = dz - 1.0;
        if (Input.GetKey("S")) dz = dz + 1.0;

        // Fixed-camera movement stays on the world's cardinal axes even when
        // the shot itself is diagonal. One key therefore always moves in a
        // straight line; diagonals are produced only by pressing two keys.
        var cameraForwardX = Camera.forwardX;
        var cameraForwardZ = Camera.forwardZ;
        var cameraRightX = Camera.rightX;
        var cameraRightZ = Camera.rightZ;
        if (snapCameraDirections != 0)
        {
            var absForwardX = cameraForwardX;
            var absForwardZ = cameraForwardZ;
            if (absForwardX < 0.0) absForwardX = 0.0 - absForwardX;
            if (absForwardZ < 0.0) absForwardZ = 0.0 - absForwardZ;
            if (absForwardX > absForwardZ)
            {
                if (cameraForwardX < 0.0) cameraForwardX = -1.0;
                else cameraForwardX = 1.0;
                cameraForwardZ = 0.0;
            }
            else
            {
                cameraForwardX = 0.0;
                if (cameraForwardZ < 0.0) cameraForwardZ = -1.0;
                else cameraForwardZ = 1.0;
            }
            cameraRightX = 0.0 - cameraForwardZ;
            cameraRightZ = cameraForwardX;
        }

        // Lock each key to the camera basis from the moment that key was
        // pressed. Keys already held through a camera cut keep their old
        // direction, while newly pressed keys immediately use the new camera.
        if (Input.GetKey("W"))
        {
            if (heldW == 0)
            {
                heldWX = cameraForwardX;
                heldWZ = cameraForwardZ;
                heldW = 1;
            }
        }
        else heldW = 0;
        if (Input.GetKey("S"))
        {
            if (heldS == 0)
            {
                heldSX = 0.0 - cameraForwardX;
                heldSZ = 0.0 - cameraForwardZ;
                heldS = 1;
            }
        }
        else heldS = 0;
        if (Input.GetKey("D"))
        {
            if (heldD == 0)
            {
                heldDX = cameraRightX;
                heldDZ = cameraRightZ;
                heldD = 1;
            }
        }
        else heldD = 0;
        if (Input.GetKey("A"))
        {
            if (heldA == 0)
            {
                heldAX = 0.0 - cameraRightX;
                heldAZ = 0.0 - cameraRightZ;
                heldA = 1;
            }
        }
        else heldA = 0;

        var aiming = Input.GetKey("LeftShift");
        var aimTurn = 0.0;
        if (aiming != 0)
        {
            aimTurn = dx;
            controllerYaw = controllerYaw - aimTurn * aimTurnSpeed * dt;
            if (controllerYaw > 180.0) controllerYaw = controllerYaw - 360.0;
            if (controllerYaw < -180.0) controllerYaw = controllerYaw + 360.0;
            transform.rotation.y = controllerYaw + modelForwardYawOffset;
            if (aimStopsMovement != 0)
            {
                dx = 0.0;
                dz = 0.0;
            }
        }

        var speed = 0.0;
        var inputLen = Mathf.Sqrt(dx * dx + dz * dz);
        if (inputLen > 0.0)
        {
            dx = dx / inputLen;
            dz = dz / inputLen;

            var worldX = 0.0;
            var worldZ = 0.0;
            if (heldW != 0) { worldX = worldX + heldWX; worldZ = worldZ + heldWZ; }
            if (heldS != 0) { worldX = worldX + heldSX; worldZ = worldZ + heldSZ; }
            if (heldD != 0) { worldX = worldX + heldDX; worldZ = worldZ + heldDZ; }
            if (heldA != 0) { worldX = worldX + heldAX; worldZ = worldZ + heldAZ; }
            var worldLen = Mathf.Sqrt(worldX * worldX + worldZ * worldZ);
            if (worldLen > 0.0001)
            {
                worldX = worldX / worldLen;
                worldZ = worldZ / worldLen;
                speed = 1.0;

            var targetYaw = Mathf.Atan2(0.0 - worldX, 0.0 - worldZ) * 57.2957795;
            if (targetYaw > 180.0) targetYaw = targetYaw - 360.0;
            if (targetYaw < -180.0) targetYaw = targetYaw + 360.0;
            if (controllerYaw > 180.0) controllerYaw = controllerYaw - 360.0;
            if (controllerYaw < -180.0) controllerYaw = controllerYaw + 360.0;

            if (activeTurnTarget != targetYaw)
            {
                var shortestYawDelta = targetYaw - controllerYaw;
                if (shortestYawDelta > 180.0) shortestYawDelta = shortestYawDelta - 360.0;
                if (shortestYawDelta < -180.0) shortestYawDelta = shortestYawDelta + 360.0;

                activeTurnDirection = 0.0;
                if (shortestYawDelta > 0.0) activeTurnDirection = 1.0;
                if (shortestYawDelta < 0.0) activeTurnDirection = -1.0;

                // Only a 180-degree turn has two equally short routes.
                // Let the camera-relative turn preference choose that tie,
                // but never turn 270/315 degrees for ordinary direction changes.
                var oppositeDirection = 0;
                if (shortestYawDelta >= 179.999) oppositeDirection = 1;
                if (shortestYawDelta <= -179.999) oppositeDirection = 1;
                if (oppositeDirection != 0)
                {
                    activeTurnDirection = 1.0;
                    if (invertTurnDirection != 0)
                        activeTurnDirection = -1.0;
                }
                activeTurnTarget = targetYaw;
            }

            var yawDelta = targetYaw - controllerYaw;
            if (activeTurnDirection > 0.0)
            {
                if (yawDelta < 0.0)
                    yawDelta = yawDelta + 360.0;
            }
            if (activeTurnDirection < 0.0)
            {
                if (yawDelta > 0.0)
                    yawDelta = yawDelta - 360.0;
            }
            var maxYawStep = turnSmoothSpeed * dt;
            var reachedTargetYaw = 0;
            if (yawDelta <= maxYawStep)
            {
                if (yawDelta >= 0.0 - maxYawStep)
                    reachedTargetYaw = 1;
            }
            if (reachedTargetYaw != 0)
            {
                controllerYaw = targetYaw;
                activeTurnDirection = 0.0;
            }
            else
            {
                controllerYaw = controllerYaw + activeTurnDirection * maxYawStep;
            }
            transform.rotation.y = controllerYaw + modelForwardYawOffset;

            Physics.Move(worldX * moveSpeed, 0.0, worldZ * moveSpeed);
            }
            else
            {
                activeTurnTarget = 9999.0;
                activeTurnDirection = 0.0;
            }
        }
        else
        {
            activeTurnTarget = 9999.0;
            activeTurnDirection = 0.0;
            Physics.Move(0.0, 0.0, 0.0);
        }

        if (lockY != 0) transform.position.y = fixedY;

        if (driveAnimator != 0)
        {
            Animator.SetFloat("Speed", speed);
            Animator.SetBool("Moving", speed > walkThreshold);
            Animator.SetFloat("Move", (0.0 - dz) * speed);
            Animator.SetFloat("Turn", dx * speed);
            Animator.SetBool("Aiming", aiming != 0);
            Animator.SetFloat("Aim", aiming);
        }
    }
}
)";

    std::error_code ec;
    fs::create_directories(scriptsDir, ec);

    auto writeIfMissingOrStale = [&](const fs::path& path, const char* marker,
                                     const char* content) -> bool {
        bool needsWrite = !fs::exists(path, ec);
        if (!needsWrite) {
            std::ifstream existing(path);
            std::string firstLine;
            std::getline(existing, firstLine);
            if (firstLine.find(marker) == std::string::npos)
                needsWrite = true;
        }

        if (!needsWrite)
            return false;

        std::ofstream file(path);
        if (file.is_open()) {
            file << content;
            MIPSYNC_INFO("Wrote built-in script: {}", PathUtf8::ToString(path));
            return true;
        }
        return false;
    };

    bool updated = false;
    updated |= writeIfMissingOrStale(fpsPath, kFpsScriptVersion, kFpsScript);
    updated |= writeIfMissingOrStale(radioPath, kRadioScriptVersion, kRadioScript);
    return updated;
}

bool Engine::EnsureDemoContent() {
    namespace fs = std::filesystem;
    const fs::path root = PathUtf8::FromString(m_ProjectPath);
    const fs::path scriptsDir = root / "assets" / "scripts";
    const fs::path animDir = root / "assets" / "animations";
    const fs::path scenesDir = root / "scenes";

    static constexpr const char* kDemoScriptVersion = "// @mipsync-demo v1";

    static constexpr const char* kAnimatedCharacterScript = R"(// @mipsync-demo v1
class AnimatedCharacter : MipsBehaviour
{
    void Update()
    {
        var moving = 0.0;
        if (Input.GetKey("W") != 0.0) moving = 1.0;
        if (Input.GetKey("A") != 0.0) moving = 1.0;
        if (Input.GetKey("S") != 0.0) moving = 1.0;
        if (Input.GetKey("D") != 0.0) moving = 1.0;
        Animator.SetFloat("Speed", moving);
    }
}
)";

    static constexpr const char* kAnimatedCharacterController = R"({
  "defaultState": "Capoeira",
  "model": "assets/models/Capoeira.fbx",
  "parameters": [
    { "name": "Speed", "type": "float", "default": 0.0 }
  ],
  "states": [
    {
      "name": "Capoeira",
      "clip": "mixamo.com",
      "clipModel": "assets/models/Capoeira.fbx",
      "speed": 1.0,
      "loop": true
    },
    {
      "name": "HipHop",
      "clip": "mixamo.com",
      "clipModel": "assets/models/Hip Hop Dancing.fbx",
      "speed": 1.0,
      "loop": true
    }
  ],
  "transitions": [
    {
      "from": "Capoeira",
      "to": "HipHop",
      "duration": 0.25,
      "conditions": [{ "param": "Speed", "mode": "greater", "threshold": 0.5 }]
    },
    {
      "from": "HipHop",
      "to": "Capoeira",
      "duration": 0.25,
      "conditions": [{ "param": "Speed", "mode": "less", "threshold": 0.5 }]
    }
  ]
}
)";

    static constexpr const char* kAnimatedCharacterScene = R"({
  "version": 1,
  "entities": [
    {
      "id": 1,
      "name": "First Person Controller",
      "transform": { "position": [0.0, 0.2, 4.0], "rotation": [0.0, 180.0, 0.0], "scale": [1.0, 1.0, 1.0] },
      "camera": { "primary": true, "fov": 70.0, "nearClip": 0.1, "farClip": 200.0 },
      "collider": {
        "shape": 2,
        "center": [0.0, -0.85, 0.0],
        "halfExtents": [0.35, 0.5, 0.35],
        "radius": 0.35,
        "capsuleHeight": 1.0,
        "isTrigger": false
      },
      "rigidbody": {
        "bodyType": 1,
        "mass": 70.0,
        "useGravity": false,
        "linearDrag": 0.05,
        "bounciness": 0.2,
        "freezeRotation": true,
        "characterController": true
      },
      "mipsScript": { "path": "assets/scripts/FirstPersonController.mips" }
    },
    {
      "id": 2,
      "name": "Floor",
      "transform": { "position": [0.0, -1.5, 0.0], "rotation": [0.0, 0.0, 0.0], "scale": [1.0, 1.0, 1.0] },
      "meshRenderer": { "primitive": "Plane", "size": 20.0, "color": [0.35, 0.35, 0.38, 1.0] },
      "collider": {
        "shape": 0,
        "center": [0.0, 0.0, 0.0],
        "halfExtents": [10.0, 0.05, 10.0],
        "radius": 0.5,
        "capsuleHeight": 1.0,
        "isTrigger": false
      },
      "rigidbody": { "bodyType": 0, "mass": 1.0, "useGravity": true, "linearDrag": 0.05, "bounciness": 0.2, "freezeRotation": false }
    },
    {
      "id": 3,
      "name": "Animated Character",
      "transform": { "position": [0.0, 0.0, 0.0], "rotation": [0.0, 180.0, 0.0], "scale": [1.0, 1.0, 1.0] },
      "skinnedMeshRenderer": {
        "model": "assets/models/Capoeira.fbx",
        "color": [1.0, 1.0, 1.0, 1.0]
      },
      "animator": {
        "controller": "assets/animations/AnimatedCharacter.ncontroller",
        "model": "assets/models/Capoeira.fbx",
        "floatParams": { "Speed": 0.0 }
      },
      "mipsScript": { "path": "assets/scripts/AnimatedCharacter.mips" }
    }
  ]
}
)";

    auto writeIfMissingOrStale = [&](const fs::path& path, const char* marker,
                                     const char* content) -> bool {
        std::error_code ec;
        fs::create_directories(path.parent_path(), ec);
        bool needsWrite = !fs::exists(path, ec);
        if (!needsWrite) {
            std::ifstream existing(path);
            std::string firstLine;
            std::getline(existing, firstLine);
            if (marker && firstLine.find(marker) == std::string::npos)
                needsWrite = true;
        }
        if (!needsWrite)
            return false;
        std::ofstream file(path);
        if (!file.is_open())
            return false;
        file << content;
        MIPSYNC_INFO("Wrote demo asset: {}", PathUtf8::ToString(path));
        return true;
    };

    bool updated = false;
    updated |= writeIfMissingOrStale(scriptsDir / "AnimatedCharacter.mips", kDemoScriptVersion,
                                     kAnimatedCharacterScript);
    updated |= writeIfMissingOrStale(animDir / "AnimatedCharacter.ncontroller", nullptr,
                                     kAnimatedCharacterController);
    updated |= writeIfMissingOrStale(scenesDir / "AnimatedCharacterDemo.nscene", nullptr,
                                     kAnimatedCharacterScene);
    return updated;
}

void Engine::SetupDefaultScene() {
    // Create a rotating cube with Mips# Rotator script
    auto cubeEntity = m_Scene->CreateEntity("PS1 Cube");
    auto& renderer = cubeEntity->AddComponent<MeshRendererComponent>();
    renderer.SetPrimitive("Cube", 2.0f);
    renderer.texture = std::make_shared<Texture>(Texture::CreateCheckerboard(128, 16));
    auto& mipsScript = cubeEntity->AddComponent<MipsScriptComponent>();
    mipsScript.scriptPath = "Rotator.mips";

    // Create a plane
    auto planeEntity = m_Scene->CreateEntity("Floor");
    auto& pRenderer = planeEntity->AddComponent<MeshRendererComponent>();
    pRenderer.SetPrimitive("Plane", 20.0f);
    pRenderer.texture = std::make_shared<Texture>(Texture::CreateCheckerboard(256, 32));
    planeEntity->GetComponent<TransformComponent>()->position.y = -1.5f;
    {
        auto& col = planeEntity->AddComponent<ColliderComponent>();
        if (pRenderer.mesh)
            ColliderUtils::FitColliderToMesh(col, *pRenderer.mesh);
        col.halfExtents.y = std::max(col.halfExtents.y, 0.25f);
        auto& rb = planeEntity->AddComponent<RigidbodyComponent>();
        rb.bodyType = RigidbodyType::Static;
    }

    {
        auto& col = cubeEntity->AddComponent<ColliderComponent>();
        if (renderer.mesh)
            ColliderUtils::FitColliderToMesh(col, *renderer.mesh);
        auto& rb = cubeEntity->AddComponent<RigidbodyComponent>();
        rb.bodyType = RigidbodyType::Dynamic;
        rb.mass = 2.0f;
        cubeEntity->GetComponent<TransformComponent>()->position.y = 2.0f;
    }

    // Main game camera
    auto* cameraEntity = m_Scene->CreateEntity("Main Camera");
    auto& cameraComp = cameraEntity->AddComponent<CameraComponent>();
    cameraComp.primary = true;
    cameraComp.camera.SetPosition({ 0.0f, 2.0f, 6.0f });
    cameraComp.camera.LookAt({ 0.0f, 0.0f, 0.0f });
    cameraComp.camera.SyncTransformFromCamera(*cameraEntity->GetComponent<TransformComponent>());
}

void Engine::Run() {
    while (m_Running && !m_Window->ShouldClose()) {
        Time::Update();

        m_Editor->BeginFrame();
        Input::Update();
        Update();
        m_Editor->OnImGuiRender();
        m_Editor->EndFrame();

        m_Window->OnUpdate();
    }
}

void Engine::RequestSceneLoad(const std::string& projectRelativePath) {
    if (projectRelativePath.empty())
        return;
    m_PendingSceneLoad = projectRelativePath;
}

void Engine::RequestSceneLoadBuildIndex(int buildIndex) {
    if (m_BuildScenes.empty())
        return;
    const int idx =
        std::clamp(buildIndex, 0, static_cast<int>(m_BuildScenes.size()) - 1);
    RequestSceneLoad(m_BuildScenes[static_cast<size_t>(idx)]);
}

void Engine::RequestQuit() {
    m_RequestQuit = true;
}

void Engine::ProcessDeferredActions() {
    if (!m_PendingSceneLoad.empty()) {
        const std::string path = m_PendingSceneLoad;
        m_PendingSceneLoad.clear();
        const bool restartPlay = m_Editor->IsPlaying();
        m_Editor->LoadSceneFromPath(
            PathUtf8::ToString(PathUtf8::FromString(m_ProjectPath) /
                               PathUtf8::FromString(path)),
            restartPlay);
    }
    if (m_RequestQuit)
        Quit();
}

void Engine::Update() {
    const float dt = Time::GetDeltaTime();
    m_Editor->TickAutoPlayOnStart();
    bool stepOneFrame = false;
    if (m_Editor) {
        m_Editor->HandlePlayModeShortcuts();
        stepOneFrame = m_Editor->ConsumeSingleStepRequest();
        if (stepOneFrame)
            m_MipsRuntime->SetPaused(false);
        m_Editor->TickPendingPlaySetup();
    }

    const bool animateCharacters = m_Editor && m_Editor->IsPlaying();
    m_Scene->SetAnimateCharacters(animateCharacters);
    m_MipsRuntime->ReloadChangedScripts(*m_Scene);

    // Read pause state after shortcuts: a pause pressed this frame must also
    // freeze the animator this frame. Keep m_AnimateCharacters enabled so the
    // renderer continues displaying the current pose instead of snapping to
    // the controller's default-state preview.
    const bool playing = m_MipsRuntime->IsPlaying() && !m_MipsRuntime->IsPaused();
    const bool playPaused = animateCharacters && m_MipsRuntime->IsPaused();

    if (playing) {
        // Scripts run before AnimationSystem so Animator.Set* applies same frame.
        m_MipsRuntime->Update(*m_Scene, dt);
        m_Scene->Update(dt);
        m_MipsRuntime->LateUpdate(*m_Scene, dt);
        m_PhysicsWorld->Simulate(*m_Scene, dt);
        m_MipsRuntime->DispatchPhysicsEvents(*m_Scene);
    } else if (!playPaused) {
        m_Scene->Update(dt);
    }
    if (m_Editor && m_Editor->IsPlaying())
        m_AudioSystem->Update(*m_Scene, m_Editor->IsPaused());
    if (stepOneFrame)
        m_MipsRuntime->SetPaused(true);
    m_Scene->SyncCamerasToWorldTransforms();
    ProcessDeferredActions();
}

void Engine::Quit() {
    m_Running = false;
}

} // namespace MipsyncEngine
