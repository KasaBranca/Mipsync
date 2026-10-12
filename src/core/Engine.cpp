#include "AssetFiles.h"
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
#include "../bootstrap/AgentIntegration.h"
#include "../assets/AssetManager.h"
#include "RuntimePaths.h"
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <thread>
#include <chrono>
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
    Log::AddFileSink(PathUtf8::ToString((IsPlayerMode() ? GetExeDirectory() : PathUtf8::FromString(m_ProjectPath)) / "mipsync.log"));

    if (!IsPlayerMode()) ProjectBootstrap::EnsureProjectDirectories(m_ProjectPath);

    if (m_LaunchMode != EngineLaunchMode::Player) {
        const AgentIntegrationResult agentIntegration = EnsureAgentIntegration(
            PathUtf8::FromString(m_ProjectPath), GetExeDirectory());
        if (!agentIntegration.success) {
            MIPSYNC_WARN("Agent Skill setup failed: {}", agentIntegration.error);
        } else if (agentIntegration.skillUpdated || agentIntegration.instructionsUpdated) {
            MIPSYNC_INFO("Synchronized project Agent Skill: {}",
                         PathUtf8::ToString(agentIntegration.skillPath));
        }
    }

    const bool builtinScriptsUpdated = !IsPlayerMode() && EnsureBuiltinScripts();

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
    if (AssetFiles::Exists(PathUtf8::FromString(defaultScenePath))) {
        std::string loadError;
        if (SceneIO::LoadFromFile(*m_Scene, defaultScenePath, loadError))
            MIPSYNC_INFO("Loaded scene: {}", defaultScenePath);
        else
            MIPSYNC_WARN("Scene load failed: {}", loadError);
    } else if (IsPlayerMode()) {
        throw std::runtime_error("Startup scene is missing from player data");
    } else {
        SetupDefaultScene();
        std::string saveError;
        if (SceneIO::SaveToFile(*m_Scene, defaultScenePath, saveError))
            MIPSYNC_INFO("Wrote default scene: {}", defaultScenePath);
    }

    if (builtinScriptsUpdated) {
        m_MipsRuntime->ResetScriptsByFileName(*m_Scene, "FirstPersonController.mips");
    }

    m_MipsRuntime->SyncEditSnapshot(*m_Scene);

    if (!IsPlayerMode()) Mips::RunMipsPhase1Tests();

    m_Editor = std::make_unique<EditorApp>(this);
    if (m_LaunchMode == EngineLaunchMode::Player)
        m_Editor->SetAutoPlayOnStart(true);
    m_Editor->Init();
}

Engine::~Engine() {
    // Construction performs several fallible platform/graphics operations.
    // Keep teardown valid even when an exception interrupts initialization.
    if (m_Editor)
        m_Editor->Shutdown();
    if (m_AudioSystem)
        m_AudioSystem->EndPlay();
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

    // Older engine-managed scripts migrate automatically. A project-owned
    // script without the builtin marker must never be overwritten by name.
    static constexpr const char* kFpsScriptVersion = "// @mipsync-builtin v15";

    static constexpr const char* kFpsScript = R"(// @mipsync-builtin v15
class FirstPersonController : MipsBehaviour
{
    // Movement
    [Header("Movement")]
    [Space(8)]
    [Order(0)]
    [InspectorName("Move Speed")]
    [Tooltip("Movement speed in world units per second.")]
    [Min(0)]
    public float moveSpeed = 5.0;

    // Right stick look
    [Header("Look")]
    [Space(8)]
    [Order(10)]
    [InspectorName("View Camera")]
    [Tooltip("Child camera used for vertical look. Leave None for a legacy camera on this object.")]
    public Camera viewCamera = 0;
    [Order(10)]
    [InspectorName("Look Speed")]
    [Tooltip("Right stick look speed in degrees per second.")]
    [Min(0)]
    public float lookSpeed = 144.0;
    [Order(10)]
    [InspectorName("Min Pitch")]
    [Tooltip("Lowest vertical look angle in degrees.")]
    [Range(-89, 89)]
    public float minPitch = -85.0;
    [Order(10)]
    [InspectorName("Max Pitch")]
    [Tooltip("Highest vertical look angle in degrees.")]
    [Range(-89, 89)]
    public float maxPitch = 85.0;

    // Jump and physics
    [Header("Jump and Gravity")]
    [Space(8)]
    [Order(20)]
    [InspectorName("Jump Speed")]
    [Tooltip("Initial upward speed in world units per second.")]
    [Min(0)]
    public float jumpSpeed = 6.0;
    [Order(20)]
    [InspectorName("Gravity")]
    [Tooltip("Downward acceleration in world units per second squared.")]
    [Min(0)]
    public float gravity = 18.0;
    [Header("Runtime")]
    [Space(8)]
    [Order(1000)]
    [InspectorName("Vertical Speed")]
    [Tooltip("Current vertical speed, updated by the script.")]
    [ReadOnly]
    public float verticalSpeed = 0.0;
    [Order(1000)]
    [InspectorName("View Pitch")]
    [Tooltip("Current vertical camera angle in degrees.")]
    [ReadOnly]
    public float viewPitch = 0.0;

    void Start()
    {
        Physics.UseCharacterController();
        Input.SetCursorLocked(1);
    }

    void Update()
    {
        float dt = Time.deltaTime;

        // The body turns around Y; the child camera handles vertical look.
        float lookX = Input.GetAxis("RightStickX");
        float lookY = Input.GetAxis("RightStickY");
        // Digital PS1 pads can look with L1/R1 and Triangle/Circle.
        if (Input.GetButton("L1")) lookX = lookX - 1.0;
        if (Input.GetButton("R1")) lookX = lookX + 1.0;
        if (Input.GetButton("Triangle")) lookY = lookY + 1.0;
        if (Input.GetButton("Circle")) lookY = lookY - 1.0;
        float lookLength = Mathf.Sqrt(lookX * lookX + lookY * lookY);
        if (lookLength > 1.0)
        {
            lookX = lookX / lookLength;
            lookY = lookY / lookLength;
        }
        transform.rotation.y = transform.rotation.y - lookX * lookSpeed * dt;
        viewPitch = Camera.RotatePitch(viewCamera, lookY * lookSpeed * dt, minPitch, maxPitch);

        // Forward / right from current yaw (Y-up, -Z forward).
        float yawRad = transform.rotation.y * 0.01745329;
        float sinY = Mathf.Sin(yawRad);
        float cosY = Mathf.Cos(yawRad);
        float forwardX = 0.0 - sinY;
        float forwardZ = 0.0 - cosY;
        float rightX = cosY;
        float rightZ = 0.0 - sinY;

        // PS1 left stick / D-pad movement.
        float moveX = Input.GetAxis("LeftStickX");
        float moveY = Input.GetAxis("LeftStickY");
        float dx = forwardX * moveY + rightX * moveX;
        float dz = forwardZ * moveY + rightZ * moveX;
        float len = Mathf.Sqrt(dx * dx + dz * dz);
        if (len > 1.0)
        {
            dx = dx / len;
            dz = dz / len;
        }

        // Vertical velocity: gravity in air, jump impulse on ground.
        bool grounded = Physics.IsGrounded();
        if (grounded != 0.0)
        {
            if (Input.GetButtonDown("Cross"))
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

    std::error_code ec;
    fs::create_directories(scriptsDir, ec);

    auto writeIfMissingOrStale = [&](const fs::path& path, const char* marker,
                                     const char* content) -> bool {
        bool needsWrite = !fs::exists(path, ec);
        if (!needsWrite) {
            std::ifstream existing(path);
            std::string firstLine;
            std::getline(existing, firstLine);
            if (firstLine.rfind("// @mipsync-builtin v", 0) == 0 &&
                firstLine.find(marker) == std::string::npos)
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

        // Never render, inspect the scene, sample gameplay, or process editor
        // commands while preparation owns scene/assets and the GL context.
        // The UI thread remains free to dispatch paint/focus/window messages.
        m_Editor->TickPendingPlaySetup();
        if (m_Editor->IsPlayPreparationRunning()) {
            m_Editor->PumpPreparationCommands();
            glfwPollEvents();
            m_PSyncLastTime = glfwGetTime();
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            continue;
        }

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
    const bool cliStep = m_CliFrames > 0 && m_Editor && m_Editor->IsPlaying() && m_Editor->IsPaused();
    if (m_Editor && (!m_Editor->IsPlaying() || !m_Editor->IsPaused())) m_CliFrames = 0;
    if (m_Editor) {
        m_Editor->HandlePlayModeShortcuts();
        stepOneFrame = m_Editor->ConsumeSingleStepRequest() || cliStep;
        if (stepOneFrame)
            m_MipsRuntime->SetPaused(false);
        if (m_Editor->IsPreparingPlay()) {
            // Preparation may take seconds. Do not advance scripts/animation,
            // physics, scene requests or game input with that elapsed time.
            m_PSyncLastTime = glfwGetTime();
            Time::Init();
            Input::EndGameFrame();
            return;
        }
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

    const double now = glfwGetTime();
    const double elapsed = m_PSyncLastTime > 0.0 ? now - m_PSyncLastTime : 0.0;
    m_PSyncLastTime = now;
    const bool sync = !IsPlayerMode() && m_Editor->GetGameViewSettings().pSync;
    if (!sync || !playing || stepOneFrame) {
        m_PSync.clock.Reset();
    }
    m_PSync.frameDue = !sync || !playing || stepOneFrame ||
        m_PSync.clock.Advance(elapsed, m_PSync.estimate.frameSeconds);
    const float gameDt = cliStep ? m_CliFrameDelta : sync && playing
        ? (stepOneFrame ? PSyncSimulationDeltaTime(m_PSync.estimate.vblanks) : m_PSync.clock.SimulationDeltaTime()) : dt;

    if (playing && m_PSync.frameDue) {
        const bool inputWasEnabled = Input::IsGameInputEnabled();
        if (cliStep) Input::SetGameInputEnabled(true);
        Input::BeginGameFrame(sync);
        if (cliStep) Input::BeginInjectedPS1Frame();
        m_MipsRuntime->BeginPerformanceFrame(sync);
        // Scripts run before AnimationSystem so Animator.Set* applies same frame.
        m_MipsRuntime->Update(*m_Scene, gameDt);
        m_Scene->Update(gameDt);
        m_PhysicsWorld->Simulate(*m_Scene, gameDt, sync ? PSyncSimulationDeltaTime(4) : 1.0f / 20.0f);
        m_MipsRuntime->DispatchPhysicsEvents(*m_Scene);
        // Camera and other follow behaviours must observe transforms written
        // back by the current physics step, especially characters riding
        // kinematic platforms.
        m_MipsRuntime->LateUpdate(*m_Scene, gameDt);
        Input::EndGameFrame();
        if (cliStep) { --m_CliFrames; Input::SetGameInputEnabled(inputWasEnabled); }
    } else if (!playing && !playPaused) {
        m_Scene->Update(dt);
    }
    if (!playing) Input::EndGameFrame();
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
