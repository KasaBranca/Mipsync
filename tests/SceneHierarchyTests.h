#pragma once
#include "scene/Scene.h"
#include "scene/SceneIO.h"
#include "editor/EditorUndoStack.h"
#include <nlohmann/json.hpp>
#include <chrono>
#include <filesystem>
#include <stdexcept>

namespace MipsyncEngine {
// Runs without a renderer, editor window or user project.
inline bool RunSceneHierarchyPersistenceTests(std::string& error) {
    namespace fs = std::filesystem;
    try {
        auto require = [](bool condition, const char* message) {
            if (!condition) throw std::runtime_error(message);
        };
        Scene scene;
        Entity* rootA = scene.CreateEntity("Root A");
        Entity* rootB = scene.CreateEntity("Root B");
        Entity* rootC = scene.CreateEntity("Root C");
        Entity* a = scene.CreateEntity("Child A");
        Entity* b = scene.CreateEntity("Child B");
        Entity* c = scene.CreateEntity("Child C");
        Entity* nestedA = scene.CreateEntity("Nested A");
        Entity* nestedB = scene.CreateEntity("Nested B");
        scene.SetParent(a, rootA); scene.SetParent(b, rootA); scene.SetParent(c, rootA);
        scene.SetParent(nestedA, b); scene.SetParent(nestedB, b);
        b->SetEditorLayer("Characters"); b->SetEditorTag("Player"); b->SetStatic(true);
        rootA->GetComponent<TransformComponent>()->position = {10, 3, -2};
        a->GetComponent<TransformComponent>()->position = {1, 2, 3};
        const auto world = scene.GetWorldPosition(*a);
        std::string original;
        require(SceneIO::SerializeSceneFingerprint(scene, original, error), "initial fingerprint failed");
        EditorUndoStack undo; undo.PushState(scene);
        require(scene.ReorderEntity(c, rootA, a, false), "child reorder failed");
        require(scene.ReorderEntity(nestedB, b, nestedA, false), "nested reorder failed");
        require(scene.ReorderEntity(rootC, nullptr, rootA, false), "root reorder failed");
        std::string reordered;
        require(SceneIO::SerializeSceneFingerprint(scene, reordered, error), "reordered fingerprint failed");
        require(reordered != original, "hierarchy-only reorder did not mark scene dirty");
        const auto parentId = rootA->GetID(), bId = b->GetID(), aId = a->GetID();
        const auto cId = c->GetID(), rootCId = rootC->GetID(), rootBId = rootB->GetID();
        const auto nestedAId = nestedA->GetID(), nestedBId = nestedB->GetID();
        auto roots = [](const Scene& value) {
            std::vector<uint32_t> result;
            for (const auto& e : value.GetEntities()) if (!e->GetParentID()) result.push_back(e->GetID());
            return result;
        };
        auto checkReordered = [&](Scene& restored) {
            require(roots(restored) == std::vector<uint32_t>{rootCId, parentId, rootBId}, "root order lost");
            require(restored.FindEntity(parentId)->GetChildIDs() == std::vector<uint32_t>{cId, aId, bId}, "child order lost");
            require(restored.FindEntity(bId)->GetChildIDs() == std::vector<uint32_t>{nestedBId, nestedAId}, "nested order lost");
            require(restored.FindEntity(bId)->GetParentID() == parentId && restored.FindEntity(nestedBId)->GetParentID() == bId,
                "parent/child depth lost");
            const auto* entity = restored.FindEntity(bId);
            require(entity->GetEditorLayer() == "Characters" && entity->GetEditorTag() == "Player" && entity->IsStatic(), "layer/tag/static lost");
            require(glm::length(restored.GetWorldPosition(*restored.FindEntity(aId)) - world) < .0001f, "local/world transform changed on reload");
        };
        Scene restored;
        require(SceneIO::LoadFromJsonString(restored, reordered, error), "JSON load failed");
        checkReordered(restored);
        std::string roundTrip;
        require(SceneIO::SerializeSceneFingerprint(restored, roundTrip, error) && roundTrip == reordered, "round-trip changed fingerprint");
        require(undo.Undo(scene, error), "hierarchy Undo failed");
        require(scene.FindEntity(parentId)->GetChildIDs() == std::vector<uint32_t>{aId, bId, cId}, "Undo did not restore child order");
        require(scene.FindEntity(bId)->GetChildIDs() == std::vector<uint32_t>{nestedAId, nestedBId}, "Undo did not restore nested order");
        require(undo.Redo(scene, error), "hierarchy Redo failed"); checkReordered(scene);
        auto legacy = nlohmann::json::parse(reordered);
        for (auto& entity : legacy["entities"]) entity.erase("childOrder");
        require(SceneIO::LoadFromJsonString(restored, legacy.dump(), error), "legacy scene load failed");
        require(restored.FindEntity(parentId)->GetChildIDs() == std::vector<uint32_t>{aId, bId, cId}, "legacy array-order compatibility broken");
        require(restored.FindEntity(nestedBId)->GetParentID() == bId && roots(restored).front() == rootCId, "legacy parent/root order lost");
        auto damaged = nlohmann::json::parse(reordered);
        for (auto& entity : damaged["entities"]) if (entity["id"] == parentId)
            entity["childOrder"] = nlohmann::json::array({bId, bId, 999999, nestedAId, -1, "bad", 1.5, 1e20, uint64_t(-1)});
        require(SceneIO::LoadFromJsonString(restored, damaged.dump(), error), "partial order metadata broke scene load");
        require(restored.FindEntity(parentId)->GetChildIDs() == std::vector<uint32_t>{bId, aId, cId}, "invalid order metadata lost/duplicated/reparented children");
        // Detaching to the hierarchy's empty area places the entity at the bottom.
        Entity* detached = scene.FindEntity(aId);
        const auto oldWorld = scene.GetWorldPosition(*detached);
        require(scene.ReorderEntity(detached, nullptr, nullptr, false, true), "unparent-to-end failed");
        require(roots(scene).back() == aId && detached->GetParentID() == 0 &&
            glm::length(scene.GetWorldPosition(*detached) - oldWorld) < .0001f, "unparent lost root order/world transform");
        require(scene.ReorderEntity(scene.FindEntity(rootCId), nullptr, nullptr, false), "root-to-bottom move not reported as changed");
        require(roots(scene).back() == rootCId, "root-to-bottom did not move");
        std::string beforeNoOp, afterNoOp;
        require(SceneIO::SerializeSceneFingerprint(scene, beforeNoOp, error), "before no-op fingerprint failed");
        require(!scene.ReorderEntity(scene.FindEntity(rootCId), nullptr, nullptr, false), "unchanged root order reported as changed");
        require(SceneIO::SerializeSceneFingerprint(scene, afterNoOp, error) && beforeNoOp == afterNoOp, "no-op changed serialized entity order");
        require(SceneIO::LoadFromJsonString(scene, reordered, error), "restore fixture failed");
        const std::string unique = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
        const fs::path scenePath = fs::temp_directory_path() / ("mipsync-hierarchy-" + unique + ".nscene");
        const fs::path prefabPath = fs::temp_directory_path() / ("mipsync-hierarchy-" + unique + ".nprefab");
        require(SceneIO::SaveToFile(scene, scenePath.string(), error), "disk save failed");
        const bool loaded = SceneIO::LoadFromFile(restored, scenePath.string(), error);
        fs::remove(scenePath);
        require(loaded, "disk reload failed"); checkReordered(restored);
        require(SceneIO::SaveEntityToFile(*scene.FindEntity(parentId), scene, prefabPath.string(), error), "prefab save failed");
        Scene prefabScene;
        auto* prefab = SceneIO::InstantiateFromFile(prefabScene, prefabPath.string(), error);
        fs::remove(prefabPath);
        require(prefab && prefab->GetChildIDs().size() == 3, "prefab reload lost children");
        std::vector<std::string> childNames;
        for (auto id : prefab->GetChildIDs()) childNames.push_back(prefabScene.FindEntity(id)->GetComponent<TagComponent>()->tag);
        require(childNames == std::vector<std::string>{"Child C", "Child A", "Child B"}, "prefab order changed");
        // Duplicating a subtree keeps local transforms, child names and sibling
        // placement; numbered names must also survive persistence and history.
        Scene copies;
        auto* parent = copies.CreateEntity("Group");
        auto* otherParent = copies.CreateEntity("Other Group");
        auto* source = copies.CreateEntity("Object"); copies.SetParent(source, parent);
        auto* taken = copies.CreateEntity("Object (1)"); copies.SetParent(taken, parent);
        auto* unrelated = copies.CreateEntity("Object (2)"); copies.SetParent(unrelated, otherParent);
        auto* child = copies.CreateEntity("Child"); copies.SetParent(child, source);
        source->GetComponent<TransformComponent>()->position = {2, 3, 4};
        source->AddComponent<ColliderComponent>().radius = .75f;
        const auto sourceId = source->GetID(), groupId = parent->GetID();
        EditorUndoStack copyUndo; copyUndo.PushState(copies);
        auto* copy = copies.DuplicateEntity(*source);
        const auto copyId = copy->GetID();
        require(copy->GetComponent<TagComponent>()->tag == "Object (2)" && copy->GetParentID() == groupId,
            "duplicate name collision or parent preservation failed");
        require(parent->GetChildIDs() == std::vector<uint32_t>{sourceId, copyId, taken->GetID()},
            "duplicate not placed immediately after source");
        require(copy->GetChildIDs().size() == 1 &&
            copies.FindEntity(copy->GetChildIDs().front())->GetComponent<TagComponent>()->tag == "Child" &&
            copy->GetComponent<TransformComponent>()->position == glm::vec3(2, 3, 4) &&
            copy->GetComponent<ColliderComponent>()->radius == .75f,
            "duplicate changed subtree names or component values");
        require(copyUndo.Undo(copies, error) && !copies.FindEntity(copyId), "duplicate Undo failed");
        require(copyUndo.Redo(copies, error) && copies.FindEntity(copyId)->GetParentID() == groupId &&
            copies.FindEntity(copyId)->GetComponent<TagComponent>()->tag == "Object (2)", "duplicate Redo lost name or parent");
        auto* nextCopy = copies.DuplicateEntity(*copies.FindEntity(copyId));
        require(nextCopy->GetComponent<TagComponent>()->tag == "Object (3)", "copy-of-copy stacked suffixes");
        auto* repeatedCopy = copies.DuplicateEntity(*copies.FindEntity(sourceId));
        require(repeatedCopy->GetComponent<TagComponent>()->tag == "Object (4)", "repeated paste reused an occupied number");
        const auto rootId = copies.CreateEntity("Root")->GetID();
        auto* rootCopy = copies.DuplicateEntity(*copies.FindEntity(rootId));
        require(rootCopy->GetComponent<TagComponent>()->tag == "Root (1)" && !rootCopy->GetParentID(),
            "root duplicate numbering failed");
        auto* literal = copies.CreateEntity("Object (preview)");
        require(copies.DuplicateEntity(*literal)->GetComponent<TagComponent>()->tag == "Object (preview) (1)",
            "non-numeric name suffix was removed");
        std::string copyJson;
        require(SceneIO::SerializeSceneFingerprint(copies, copyJson, error) &&
            SceneIO::LoadFromJsonString(restored, copyJson, error) &&
            restored.FindEntity(copyId)->GetComponent<TagComponent>()->tag == "Object (2)" &&
            restored.FindEntity(copyId)->GetParentID() == groupId, "numbered duplicate persistence failed");
        // The new Preferences undo limit applies to both retained histories.
        Scene historyScene;
        const uint32_t historyId = historyScene.CreateEntity("History")->GetID();
        EditorUndoStack limitedHistory; limitedHistory.SetMaxSteps(8);
        for (int step = 0; step < 12; ++step) {
            historyScene.FindEntity(historyId)->GetComponent<TransformComponent>()->position.x = static_cast<float>(step);
            limitedHistory.PushState(historyScene);
        }
        int undoCount = 0;
        while (limitedHistory.CanUndo()) { require(limitedHistory.Undo(historyScene, error), "limited Undo failed"); ++undoCount; }
        require(undoCount == 8, "Preferences undo history limit did not apply");
        limitedHistory.SetMaxSteps(2);
        int redoCount = 0;
        while (limitedHistory.CanRedo()) { require(limitedHistory.Redo(historyScene, error), "limited Redo failed"); ++redoCount; }
        require(redoCount == 2, "lowered limit did not trim Redo history");
        return true;
    } catch (const std::exception& exception) {
        error = std::string("hierarchy persistence regression: ") + exception.what();
        return false;
    }
}
} // namespace MipsyncEngine
