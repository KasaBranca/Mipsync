#include "MipsTest.h"
#include "MipsRuntime.h"
#include "VM.h"
#include "../core/Log.h"
#include "../animation/AnimatorControllerIO.h"
#include "../animation/AnimatorRuntime.h"
#include "../assets/AssetManager.h"
#include "../scene/Scene.h"
#include "../scene/SceneIO.h"
#include "../physics/PhysicsWorld.h"
#include "../ps1/PSync.h"
#include "../../tests/SceneHierarchyTests.h"
#include "../../tests/EditorSceneSessionTests.h"
#include "../../tests/AssetBrowserSelectionTests.h"
#include "../../tests/EditorUndoHistoryTests.h"
#include "../../tests/MipsFieldReloadTests.h"
#include "../../tests/FPSControllerInputTests.h"
#include "../../tests/MipsDestroyTests.h"
#include <cmath>
#include <filesystem>
#include <fstream>
#include <vector>

namespace MipsyncEngine::Mips {

void RunMipsPhase1Tests() {
    std::vector<std::string> errors;
    if (auto module = MipsRuntime::CompileScriptFile("Rotator.mips", errors)) {
        MIPSYNC_INFO("[Mips#] Rotator OK ({} method(s), {} field(s))",
                      module->methods.size(), module->fields.size());
    } else {
        for (const auto& err : errors)
            MIPSYNC_WARN("[Mips#] {}", err);
    }
}

bool RunMipsRuntimeRegressionTests(const char* scriptPath,
                                   std::vector<std::string>& errors) {
    std::string hierarchyError;
    if (!RunMipsDestroyTests(hierarchyError)) {
        errors.push_back(hierarchyError); return false;
    }
    if (!RunFPSControllerInputTests(hierarchyError)) {
        errors.push_back(hierarchyError); return false;
    }
    if (!RunMipsFieldReloadTests(hierarchyError)) {
        errors.push_back(hierarchyError); return false;
    }
    if (!RunEditorUndoHistoryTests(hierarchyError)) {
        errors.push_back(hierarchyError); return false;
    }
    if (!RunAssetBrowserSelectionTests(hierarchyError)) {
        errors.push_back(hierarchyError);
        return false;
    }
    if (!RunSceneHierarchyPersistenceTests(hierarchyError)) {
        errors.push_back(hierarchyError);
        return false;
    }
    if (!RunEditorSceneSessionTests(hierarchyError)) {
        errors.push_back(hierarchyError);
        return false;
    }
    auto module = MipsRuntime::CompileScriptFile(scriptPath ? scriptPath : "", errors);
    if (!module || !errors.empty())
        return false;

    ScriptInstance instance;
    instance.module = module;
    instance.fields.assign(module->fields.size(), 0.0);
    instance.runtimeFields.resize(module->fields.size());
    instance.coroutines.reserve(16);
    for (size_t i = 0; i < module->fields.size(); ++i) {
        const uint16_t constant = module->fields[i].defaultConstIndex;
        const double value = constant < module->numberConstants.size()
            ? module->numberConstants[constant] : 0.0;
        instance.fields[i] = value;
        instance.runtimeFields[i].tag = Value::Tag::Number;
        instance.runtimeFields[i].number = value;
    }

    VM vm;
    {
        // P-Sync must count executed operations, including host calls, without
        // changing script semantics or retaining counts across game frames.
        ScriptInstance profiled;
        profiled.module = std::make_shared<CompiledModule>();
        profiled.module->numberConstants = {2.0, 3.0};
        BytecodeWriter writer;
        writer.EmitOp(OpCode::PushConst); writer.EmitU16(0);
        writer.EmitOp(OpCode::PushConst); writer.EmitU16(1);
        writer.EmitOp(OpCode::Add); writer.EmitOp(OpCode::Pop);
        writer.EmitOp(OpCode::CallHost); writer.EmitU16(static_cast<uint16_t>(HostFunc::Time_DeltaTime)); writer.EmitU8(0);
        writer.EmitOp(OpCode::Pop); writer.EmitOp(OpCode::Return);
        CompiledMethod method;
        method.name = "Profile"; method.code = writer.Code();
        profiled.module->methods.push_back(method);
        VM counter;
        counter.SetPerformanceCounting(true);
        if (!counter.RunMethod(profiled, "Profile", errors)) return false;
        uint64_t instructions = 0;
        for (const uint64_t count : counter.GetOpcodeCounts()) instructions += count;
        if (instructions != 7 || counter.GetHostCounts()[static_cast<uint16_t>(HostFunc::Time_DeltaTime)] != 1) {
            errors.push_back("P-Sync regression: executed VM workload wasn't counted correctly");
            return false;
        }
        counter.ResetPerformanceCounts();
        counter.SetPerformanceCounting(false);
        if (!counter.RunMethod(profiled, "Profile", errors)) return false;
        for (const uint64_t count : counter.GetOpcodeCounts()) {
            if (count) { errors.push_back("P-Sync regression: disabled profiling accumulated operations"); return false; }
        }
    }
    {
        // At ~15 FPS, physics must consume the same four-field delta as scripts
        // rather than silently truncating character movement to 50 ms.
        Scene scene;
        Entity* character = scene.CreateEntity("P-Sync timing regression");
        character->AddComponent<ColliderComponent>().shape = ColliderShape::Capsule;
        auto& body = character->AddComponent<RigidbodyComponent>();
        body.characterController = true;
        body.useGravity = false;
        PhysicsWorld physics;
        physics.BeginPlay(scene);
        const float logicDelta = PSyncSimulationDeltaTime(4);
        auto checkMovement = [&](bool sync) {
            const float before = scene.GetWorldPosition(*character).x;
            if (!physics.SetCharacterVelocity(*character, glm::vec3(1, 0, 0))) return false;
            if (sync) physics.Simulate(scene, logicDelta, logicDelta);
            else physics.Simulate(scene, logicDelta);
            const float expected = sync ? logicDelta : 0.05f;
            return std::abs(scene.GetWorldPosition(*character).x - before - expected) < 1e-5f;
        };
        if (!checkMovement(true) || !checkMovement(false)) {
            errors.push_back("P-Sync regression: physics and script frame deltas diverged");
            return false;
        }
    }
    if (!vm.RunMethod(instance, "Start", errors))
        return false;
    const int resultField = module->FindFieldIndex("result");
    auto result = [&]() -> double {
        return resultField >= 0 && static_cast<size_t>(resultField) < instance.fields.size()
            ? instance.fields[static_cast<size_t>(resultField)] : -9999.0;
    };
    if (result() != 31.0 || instance.coroutines.size() != 1) {
        errors.push_back("array regression: expected result=31 and one coroutine");
        return false;
    }

    vm.ResumeCoroutines(instance, 0.0f, errors);   // run to yield return null
    if (result() != 32.0) {
        errors.push_back("coroutine regression: first segment did not run");
        return false;
    }
    vm.ResumeCoroutines(instance, 0.016f, errors); // consume one-frame wait
    if (result() != 32.0) {
        errors.push_back("coroutine regression: yield return null resumed too early");
        return false;
    }
    vm.ResumeCoroutines(instance, 0.016f, errors); // run to WaitForSeconds
    if (result() != 42.0) {
        errors.push_back("coroutine regression: second segment did not run");
        return false;
    }
    vm.ResumeCoroutines(instance, 0.02f, errors);
    vm.ResumeCoroutines(instance, 0.02f, errors);
    if (result() != 42.0) {
        errors.push_back("coroutine regression: WaitForSeconds resumed too early");
        return false;
    }
    vm.ResumeCoroutines(instance, 0.02f, errors); // enter looping coroutine section
    if (result() != 1142.0 || instance.coroutines.empty()) {
        errors.push_back("coroutine regression: loop did not reach its first yield");
        return false;
    }
    for (int iteration = 0; iteration < 2; ++iteration) {
        vm.ResumeCoroutines(instance, 0.016f, errors); // consume frame wait
        vm.ResumeCoroutines(instance, 0.016f, errors); // execute next loop iteration
    }
    vm.ResumeCoroutines(instance, 0.016f, errors); // consume final frame wait
    vm.ResumeCoroutines(instance, 0.016f, errors); // exit loop and yield break
    if (result() != 3142.0 || !instance.coroutines.empty()) {
        errors.push_back("coroutine regression: yielded loop or completion failed");
        return false;
    }
    std::vector<std::string> boundsErrors;
    vm.RunMethod(instance, "BoundsError", boundsErrors);
    if (boundsErrors.empty()) {
        errors.push_back("array regression: out-of-range write was not diagnosed");
        return false;
    }

    // A malformed or hostile gameplay loop must fail deterministically instead
    // of hanging the editor process.
    auto loopModule = std::make_shared<CompiledModule>();
    loopModule->className = "InfiniteLoop";
    CompiledMethod loopMethod;
    loopMethod.name = "Run";
    BytecodeWriter loopWriter;
    loopWriter.EmitOp(OpCode::Jump);
    loopWriter.EmitI32(-5);
    loopMethod.code = loopWriter.Code();
    loopModule->methods.push_back(std::move(loopMethod));
    ScriptInstance loopInstance;
    loopInstance.module = loopModule;
    VM limitedVm;
    VMExecutionLimits limits;
    limits.maxInstructionsPerInvocation = 64;
    limitedVm.SetExecutionLimits(limits);
    std::vector<std::string> limitErrors;
    if (limitedVm.RunMethod(loopInstance, "Run", limitErrors) || limitErrors.empty()) {
        errors.push_back("VM safety regression: infinite loop did not hit instruction budget");
        return false;
    }

    // Scene event bindings must survive the same serialization path used by
    // editor Save/Load and Undo snapshots.
    Scene sourceScene;
    Entity* target = sourceScene.CreateEntity("Target");
    auto& targetScript = target->AddComponent<MipsScriptComponent>();
    targetScript.scriptPath = "assets/scripts/Target.mips";
    Entity* buttonEntity = sourceScene.CreateEntity("Button");
    auto& button = buttonEntity->AddComponent<UIButtonComponent>();
    button.onClick.push_back({ true, target->GetID(), targetScript.scriptPath, "HandleClick" });
    Entity* viewmodelEntity = sourceScene.CreateEntity("Skinned Viewmodel");
    auto& viewmodelRenderer =
        viewmodelEntity->AddComponent<SkinnedMeshRendererComponent>();
    viewmodelRenderer.typePreset = SkinnedMeshRendererComponent::TypePreset::Viewmodel;
    // Keep the headless regression test off the checkerboard texture path,
    // which requires a live graphics context during destruction.
    viewmodelRenderer.materialPath = "assets/materials/headless-missing.nmat";
    Entity* inactiveParent = sourceScene.CreateEntity("Inactive Parent");
    inactiveParent->SetActive(false);
    sourceScene.SetParent(viewmodelEntity, inactiveParent);
    if (sourceScene.IsActiveInHierarchy(*viewmodelEntity)) {
        errors.push_back("active hierarchy regression: inactive parent did not disable child");
        return false;
    }

    const std::filesystem::path scenePath =
        std::filesystem::temp_directory_path() / "mipsync_scene_roundtrip_test.nscene";
    std::string sceneError;
    if (!SceneIO::SaveToFile(sourceScene, scenePath.string(), sceneError)) {
        errors.push_back("scene serialization regression: " + sceneError);
        return false;
    }
    Scene loadedScene;
    const bool loaded = SceneIO::LoadFromFile(loadedScene, scenePath.string(), sceneError);
    std::error_code removeError;
    std::filesystem::remove(scenePath, removeError);
    Entity* loadedButtonEntity = loadedScene.FindEntity(buttonEntity->GetID());
    UIButtonComponent* loadedButton = loadedButtonEntity
        ? loadedButtonEntity->GetComponent<UIButtonComponent>() : nullptr;
    Entity* loadedViewmodelEntity = loadedScene.FindEntity(viewmodelEntity->GetID());
    SkinnedMeshRendererComponent* loadedViewmodel = loadedViewmodelEntity
        ? loadedViewmodelEntity->GetComponent<SkinnedMeshRendererComponent>() : nullptr;
    if (!loaded || !loadedButton || loadedButton->onClick.size() != 1 ||
        loadedButton->onClick.front().targetEntityId != target->GetID() ||
        loadedButton->onClick.front().methodName != "HandleClick" ||
        !loadedViewmodel ||
        loadedViewmodel->typePreset != SkinnedMeshRendererComponent::TypePreset::Viewmodel ||
        loadedScene.IsActiveInHierarchy(*loadedViewmodelEntity)) {
        errors.push_back(
            "scene serialization regression: event, skinned Mesh Type, or active hierarchy did not round-trip");
        return false;
    }

    // Animator state offsets must survive controller serialization and affect
    // the first sampled frame. A normalized offset of 0.5 on a ten-frame clip
    // should start halfway through it.
    AssetManager& assets = AssetManager::Get();
    const std::string previousProjectRoot = assets.GetProjectRoot();
    const std::filesystem::path animatorTestRoot =
        std::filesystem::temp_directory_path() / "mipsync_animator_start_offset_test";
    std::error_code animatorFsError;
    std::filesystem::remove_all(animatorTestRoot, animatorFsError);
    animatorFsError.clear();
    std::filesystem::create_directories(animatorTestRoot, animatorFsError);
    bool animatorOffsetOk = !animatorFsError;
    const std::filesystem::path clipPath = animatorTestRoot / "offset.nanim";
    const std::filesystem::path controllerPath = animatorTestRoot / "offset.ncontroller";
    if (animatorOffsetOk) {
        std::ofstream clipFile(clipPath);
        clipFile << R"({
  "fps": 10,
  "lengthFrames": 10,
  "keys": [
    { "frame": 0, "position": [0, 0, 0], "rotation": [0, 0, 0], "scale": [1, 1, 1] },
    { "frame": 10, "position": [10, 0, 0], "rotation": [0, 0, 0], "scale": [1, 1, 1] }
  ]
})";
        animatorOffsetOk = clipFile.good();
    }

    AnimatorControllerAsset offsetController;
    AnimatorStateDef offsetState;
    offsetState.name = "Offset";
    offsetState.clipName = "offset";
    offsetState.clipSourceModelPath = "offset.nanim";
    offsetState.startOffset = 0.5f;
    offsetController.defaultState = offsetState.name;
    offsetController.states.push_back(offsetState);
    std::string animatorError;
    if (animatorOffsetOk)
        animatorOffsetOk = SaveAnimatorController(
            controllerPath.string(), offsetController, animatorError);
    const auto loadedController = animatorOffsetOk
        ? LoadAnimatorController(controllerPath.string(), animatorError)
        : nullptr;
    animatorOffsetOk = animatorOffsetOk && loadedController &&
        loadedController->states.size() == 1 &&
        std::abs(loadedController->states.front().startOffset - 0.5f) < 1e-6f;

    if (animatorOffsetOk) {
        assets.SetProjectRoot(animatorTestRoot.string());
        Scene animatorScene;
        Entity* animated = animatorScene.CreateEntity("Offset Animated");
        auto& animator = animated->AddComponent<AnimatorComponent>();
        animator.controller = loadedController;
        AnimationSystem::Update(animatorScene, 0.0f);
        const auto* transform = animated->GetComponent<TransformComponent>();
        animatorOffsetOk = transform && std::abs(transform->position.x - 5.0f) < 0.001f;
        animatorOffsetOk = animatorOffsetOk &&
            std::abs(GetAnimatorNormalizedTime(animator, offsetState.name) - 0.5f) < 0.001f &&
            GetAnimatorNormalizedTime(animator, "Inactive") == -1.0f;
        animator.stateTime = 0.75f;
        animatorOffsetOk = animatorOffsetOk &&
            std::abs(GetAnimatorNormalizedTime(animator, offsetState.name) - 0.25f) < 0.001f;
    }
    assets.SetProjectRoot(previousProjectRoot);
    std::filesystem::remove_all(animatorTestRoot, animatorFsError);
    if (!animatorOffsetOk) {
        errors.push_back("animator regression: state start offset did not round-trip and sample");
        if (!animatorError.empty())
            errors.push_back("animator regression detail: " + animatorError);
        return false;
    }
    return errors.empty();
}

} // namespace MipsyncEngine::Mips
