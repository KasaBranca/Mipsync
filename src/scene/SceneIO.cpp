#include "SceneIO.h"
#include "Scene.h"
#include "../mips/MipsRuntime.h"
#include "../renderer/Mesh.h"
#include "../renderer/Texture.h"
#include "../animation/SkeletalModel.h"
#include <algorithm>
#include "../assets/AssetManager.h"
#include "../assets/Material.h"
#include "../core/Log.h"
#include <nlohmann/json.hpp>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <cstdint>

namespace fs = std::filesystem;

namespace MipsyncEngine::SceneIO {

namespace {

using json = nlohmann::json;

json Vec3ToJson(const glm::vec3& v) {
    return json::array({ v.x, v.y, v.z });
}

json Vec4ToJson(const glm::vec4& v) {
    return json::array({ v.x, v.y, v.z, v.w });
}

bool Vec3FromJson(const json& j, glm::vec3& out) {
    if (!j.is_array() || j.size() < 3)
        return false;
    out.x = j[0].get<float>();
    out.y = j[1].get<float>();
    out.z = j[2].get<float>();
    return true;
}

bool Vec4FromJson(const json& j, glm::vec4& out) {
    if (!j.is_array() || j.size() < 4)
        return false;
    out.x = j[0].get<float>();
    out.y = j[1].get<float>();
    out.z = j[2].get<float>();
    out.w = j[3].get<float>();
    return true;
}

json Vec2ToJson(const glm::vec2& v) {
    return json::array({ v.x, v.y });
}

bool Vec2FromJson(const json& j, glm::vec2& out) {
    if (!j.is_array() || j.size() < 2)
        return false;
    out.x = j[0].get<float>();
    out.y = j[1].get<float>();
    return true;
}

uint8_t ColorByte(float value) {
    return static_cast<uint8_t>(std::clamp(value, 0.0f, 1.0f) * 255.0f + 0.5f);
}

uint32_t ColorToPacked(const glm::vec4& color) {
    return (static_cast<uint32_t>(ColorByte(color.r)) << 24u) |
           (static_cast<uint32_t>(ColorByte(color.g)) << 16u) |
           (static_cast<uint32_t>(ColorByte(color.b)) << 8u) |
           static_cast<uint32_t>(ColorByte(color.a));
}

glm::vec4 PackedToColor(uint32_t color) {
    constexpr float inv = 1.0f / 255.0f;
    return {
        static_cast<float>((color >> 24u) & 0xFFu) * inv,
        static_cast<float>((color >> 16u) & 0xFFu) * inv,
        static_cast<float>((color >> 8u) & 0xFFu) * inv,
        static_cast<float>(color & 0xFFu) * inv,
    };
}

std::string NormalizePrimitive(std::string primitive) {
    if (primitive == "sphere") return "Sphere";
    if (primitive == "plane") return "Plane";
    if (primitive == "cube") return "Cube";
    if (primitive == "terrain") return "Terrain";
    if (primitive == "probuilder") return "ProBuilder";
    if (primitive == "file") return "File";
    return primitive;
}

bool IsCameraTriggerTag(const json& ent) {
    std::string tag;
    if (ent.contains("unityTag") && ent["unityTag"].is_string())
        tag = ent["unityTag"].get<std::string>();
    else if (ent.contains("tag") && ent["tag"].is_string())
        tag = ent["tag"].get<std::string>();
    std::transform(tag.begin(), tag.end(), tag.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return tag == "mipsynccameratrigger" ||
           tag == "mipsyncshottrigger" ||
           tag == "cameratrigger" ||
           tag == "shottrigger";
}

bool AssetFileExists(const AssetManager& assets, const std::string& projectRelPath) {
    if (projectRelPath.empty())
        return false;
    std::error_code ec;
    return fs::exists(PathUtf8::FromString(assets.ToAbsolute(projectRelPath)), ec);
}

std::string ResolveAssetPathWithAssetsFallback(const AssetManager& assets,
                                               const std::string& projectRelPath) {
    if (projectRelPath.empty() || AssetFileExists(assets, projectRelPath))
        return projectRelPath;

    const bool alreadyAssetsRelative =
        projectRelPath.rfind("assets/", 0) == 0 ||
        projectRelPath.rfind("assets\\", 0) == 0;
    if (alreadyAssetsRelative)
        return projectRelPath;

    const std::string assetsPrefixed = "assets/" + projectRelPath;
    return AssetFileExists(assets, assetsPrefixed) ? assetsPrefixed : projectRelPath;
}

uint64_t HashLegacyMaterial(const Material& material, const char* rendererKind) {
    uint64_t hash = 1469598103934665603ull;
    const auto addBytes = [&hash](const void* data, size_t size) {
        const auto* bytes = static_cast<const uint8_t*>(data);
        for (size_t i = 0; i < size; ++i) {
            hash ^= bytes[i];
            hash *= 1099511628211ull;
        }
    };
    addBytes(rendererKind, std::char_traits<char>::length(rendererKind));
    addBytes(material.texturePath.data(), material.texturePath.size());
    addBytes(&material.color, sizeof(material.color));
    addBytes(&material.mainTextureTiling, sizeof(material.mainTextureTiling));
    addBytes(&material.mainTextureOffset, sizeof(material.mainTextureOffset));
    return hash;
}

std::string MigrateLegacyAppearanceToMaterial(AssetManager& assets, const char* rendererKind,
                                              const std::string& texturePath,
                                              const glm::vec4& color,
                                              const glm::vec2& tiling,
                                              const glm::vec2& offset) {
    Material material;
    material.color = color;
    material.texturePath = texturePath;
    material.mainTextureTiling = tiling;
    material.mainTextureOffset = offset;
    const std::string materialPath = "assets/materials/Migrated_" +
        std::to_string(HashLegacyMaterial(material, rendererKind)) + ".nmat";

    std::string error;
    if (!Material::Save(assets.ToAbsolute(materialPath), material, error)) {
        MIPSYNC_WARN("Could not migrate legacy {} appearance to material: {}",
                     rendererKind, error);
        return {};
    }
    MIPSYNC_INFO("Migrated legacy {} appearance to '{}'", rendererKind, materialPath);
    return materialPath;
}

/// Fix scenes saved before meshPrimitive was set on hierarchy-created objects.
std::string InferPrimitiveFromEntityName(const std::string& entityName, const std::string& stored) {
    if (entityName == "Sphere") return "Sphere";
    if (entityName == "Plane" || entityName == "Floor") return "Plane";
    if (entityName == "Terrain") return "Terrain";
    if (entityName == "Cube") return "Cube";
    return stored;
}

json EntityComponentsToJson(const Entity& entity) {
    json ent;

    if (!entity.IsActive()) ent["active"] = false;
    if (entity.IsStatic()) ent["static"] = true;
    if (entity.GetEditorTag() != "Untagged") ent["unityTag"] = entity.GetEditorTag();
    if (entity.GetEditorLayer() != "Default") ent["layer"] = entity.GetEditorLayer();

    if (const auto* tag = const_cast<Entity&>(entity).GetComponent<TagComponent>())
        ent["name"] = tag->tag;

    if (const auto* transform = const_cast<Entity&>(entity).GetComponent<TransformComponent>()) {
        ent["transform"] = {
            { "position", Vec3ToJson(transform->position) },
            { "rotation", Vec3ToJson(transform->rotation) },
            { "scale",    Vec3ToJson(transform->scale) },
        };
    }

    if (const auto* mesh = const_cast<Entity&>(entity).GetComponent<MeshRendererComponent>()) {
        json mr;
        if (!mesh->enabled)
            mr["enabled"] = false;
        mr["primitive"] = mesh->meshPrimitive;
        mr["size"] = mesh->meshSize;
        if (!mesh->materialPath.empty()) mr["material"] = mesh->materialPath;
        if (mesh->viewModel)
            mr["viewModel"] = true;
        if (mesh->editorOnly)
            mr["editorOnly"] = true;
        if (mesh->prerenderOccluder)
            mr["prerenderOccluder"] = true;
        if (mesh->ps1SeamFill)
            mr["ps1SeamFill"] = true;
        if (!mesh->meshPath.empty()) mr["mesh"] = mesh->meshPath;
        ent["meshRenderer"] = mr;
    }

    if (const auto* terrain = const_cast<Entity&>(entity).GetComponent<TerrainComponent>()) {
        auto& mutableTerrain = const_cast<TerrainComponent&>(*terrain);
        mutableTerrain.EnsureData();
        json terrainJson = {
            { "size", terrain->size },
            { "subdivisions", terrain->subdivisions },
        };
        terrainJson["brushEnabled"] = terrain->brushEnabled;
        terrainJson["brushMode"] = static_cast<int>(terrain->brushMode);
        terrainJson["brushRadius"] = terrain->brushRadius;
        terrainJson["brushStrength"] = terrain->brushStrength;
        terrainJson["brushColor"] = Vec4ToJson(terrain->brushColor);
        if (!terrain->heights.empty()) {
            terrainJson["heights"] = json::array();
            for (float h : terrain->heights)
                terrainJson["heights"].push_back(h);
        }
        if (!terrain->paintColors.empty()) {
            terrainJson["colors"] = json::array();
            for (const glm::vec4& color : terrain->paintColors)
                terrainJson["colors"].push_back(ColorToPacked(color));
        }
        if (!terrain->enabled)
            terrainJson["enabled"] = false;
        ent["terrain"] = terrainJson;
    }

    if (const auto* pb = const_cast<Entity&>(entity).GetComponent<ProModelerComponent>()) {
        json pbJson = {
            { "shape", static_cast<int>(pb->shape) },
            { "size", Vec3ToJson(pb->size) },
            { "steps", pb->steps },
            { "extrudeAmount", pb->extrudeAmount },
        };
        if (!pb->enabled)
            pbJson["enabled"] = false;
        pbJson["vertices"] = json::array();
        for (const ProModelerVertex& v : pb->vertices) {
            pbJson["vertices"].push_back({
                { "position", Vec3ToJson(v.position) },
                { "normal", Vec3ToJson(v.normal) },
                { "uv", Vec2ToJson(v.uv) },
                { "color", Vec4ToJson(v.color) },
            });
        }
        pbJson["indices"] = json::array();
        for (uint32_t index : pb->indices)
            pbJson["indices"].push_back(index);
        pbJson["faceIds"] = pb->triangleFaceIds;
        pbJson["nextFaceId"] = pb->nextFaceId;
        pbJson["faceMaterials"] = json::array();
        for (const auto& [faceId, materialPath] : pb->faceMaterialPaths) {
            if (!materialPath.empty())
                pbJson["faceMaterials"].push_back({
                    { "faceId", faceId }, { "material", materialPath }
                });
        }
        ent["proModeler"] = pbJson;
    }

    if (const auto* skinned = const_cast<Entity&>(entity).GetComponent<SkinnedMeshRendererComponent>()) {
        json sm;
        if (!skinned->enabled)
            sm["enabled"] = false;
        sm["color"] = Vec4ToJson(skinned->color);
        if (!skinned->modelPath.empty()) sm["model"] = skinned->modelPath;
        if (!skinned->materialPath.empty()) sm["material"] = skinned->materialPath;
        if (skinned->meshPartIndex >= 0) sm["meshPart"] = skinned->meshPartIndex;
        sm["ps1ExportMode"] = static_cast<int>(skinned->ps1ExportMode);
        if (skinned->ps1VertexAnimFps != 30)
            sm["ps1VertexAnimFps"] = skinned->ps1VertexAnimFps;
        if (skinned->ps1VertexAnimMaxFrames != 96)
            sm["ps1VertexAnimMaxFrames"] = skinned->ps1VertexAnimMaxFrames;
        if (skinned->ps1VertexAnimTargetTris != 320)
            sm["ps1VertexAnimTargetTris"] = skinned->ps1VertexAnimTargetTris;
        if (skinned->ps1VertexAnimTargetVerts != 1000)
            sm["ps1VertexAnimTargetVerts"] = skinned->ps1VertexAnimTargetVerts;
        if (std::abs(skinned->ps1VertexAnimMaxSourceMB - 4.0f) > 0.001f)
            sm["ps1VertexAnimMaxSourceMB"] = skinned->ps1VertexAnimMaxSourceMB;
        if (skinned->ps1SeamFill)
            sm["ps1SeamFill"] = true;
        ent["skinnedMeshRenderer"] = sm;
    }

    if (const auto* animator = const_cast<Entity&>(entity).GetComponent<AnimatorComponent>()) {
        json anim;
        if (!animator->enabled)
            anim["enabled"] = false;
        if (!animator->modelPath.empty()) anim["model"] = animator->modelPath;
        if (!animator->controllerPath.empty()) anim["controller"] = animator->controllerPath;
        if (!animator->currentState.empty()) anim["state"] = animator->currentState;
        if (animator->speed != 1.0f) anim["speed"] = animator->speed;
        if (animator->animationFps != 30.0f) anim["animationFps"] = animator->animationFps;
        if (!animator->parameters.floats.empty()) {
            json params;
            for (const auto& [k, v] : animator->parameters.floats)
                params[k] = v;
            anim["floatParams"] = params;
        }
        if (!animator->parameters.bools.empty()) {
            json params;
            for (const auto& [k, v] : animator->parameters.bools)
                params[k] = v;
            anim["boolParams"] = params;
        }
        ent["animator"] = anim;
    }

    if (const auto* bone = const_cast<Entity&>(entity).GetComponent<BoneComponent>()) {
        json boneJson;
        if (!bone->modelPath.empty()) boneJson["model"] = bone->modelPath;
        if (bone->boneIndex >= 0) boneJson["boneIndex"] = bone->boneIndex;
        ent["bone"] = boneJson;
    }

    if (const auto* camera = const_cast<Entity&>(entity).GetComponent<CameraComponent>()) {
        json camJson = {
            { "primary",  camera->primary },
            { "fov",      camera->camera.fov },
            { "nearClip", camera->camera.nearClip },
            { "farClip",  camera->camera.farClip },
        };
        if (!camera->prerenderedBackgroundPath.empty())
            camJson["prerenderedBackground"] = camera->prerenderedBackgroundPath;
        if (camera->shotTriggerEntityId != 0 || camera->shotPriority != 0) {
            camJson["shot"] = {
                { "trigger", camera->shotTriggerEntityId },
                { "priority", camera->shotPriority },
            };
        }
        if (!camera->enabled)
            camJson["enabled"] = false;
        ent["camera"] = camJson;
    }

    json scriptsJson = json::array();
    for (const auto* script : const_cast<Entity&>(entity).GetComponents<MipsScriptComponent>()) {
        json scriptJson;
        if (!script->enabled)
            scriptJson["enabled"] = false;
        scriptJson["path"] = script->scriptPath;
        if (script->module && script->fieldValues.size() == script->module->fields.size()) {
            json fields;
            for (size_t i = 0; i < script->module->fields.size(); ++i) {
                const auto& field = script->module->fields[i];
                if (field.valueKind == Mips::FieldValueKind::AudioClip)
                    fields[field.name] = i < script->fieldAssetPaths.size()
                        ? script->fieldAssetPaths[i] : std::string{};
                else if (field.valueKind == Mips::FieldValueKind::Array)
                    fields[field.name] = json::array();
                else
                    fields[field.name] = script->fieldValues[i];
            }
            scriptJson["fields"] = fields;
        }
        scriptsJson.push_back(std::move(scriptJson));
    }
    if (!scriptsJson.empty())
        ent["mipsScripts"] = std::move(scriptsJson);

    if (const auto* col = const_cast<Entity&>(entity).GetComponent<ColliderComponent>()) {
        json colJson;
        if (!col->enabled)
            colJson["enabled"] = false;
        colJson["shape"] = static_cast<int>(col->shape);
        colJson["center"] = Vec3ToJson(col->center);
        colJson["halfExtents"] = Vec3ToJson(col->halfExtents);
        colJson["radius"] = col->radius;
        colJson["capsuleHeight"] = col->capsuleHeight;
        colJson["convex"] = col->convex;
        colJson["isTrigger"] = col->isTrigger;
        if (col->cameraShotTrigger)
            colJson["cameraTrigger"] = true;
        if (col->cameraTargetEntityId != 0)
            colJson["cameraTarget"] = col->cameraTargetEntityId;
        ent["collider"] = colJson;
    }

    if (const auto* light = const_cast<Entity&>(entity).GetComponent<LightComponent>()) {
        json lightJson = {
            { "type", static_cast<int>(light->type) },
            { "color", Vec3ToJson(light->color) },
            { "intensity", light->intensity },
            { "range", light->range },
            { "spotAngle", light->spotAngle },
            { "spotInnerAngle", light->spotInnerAngle },
        };
        if (!light->enabled)
            lightJson["enabled"] = false;
        ent["light"] = lightJson;
    }

    if (const auto* post = const_cast<Entity&>(entity).GetComponent<PostProcessVolumeComponent>()) {
        json postJson = {
            { "isGlobal", post->isGlobal },
            { "priority", post->priority },
            { "fogEnabled", post->fogEnabled },
            { "fogColor", Vec3ToJson(post->fogColor) },
            { "fogStart", post->fogStart },
            { "fogEnd", post->fogEnd },
            { "colorGradingEnabled", post->colorGradingEnabled },
            { "exposure", post->exposure },
            { "contrast", post->contrast },
            { "saturation", post->saturation },
            { "colorFilter", Vec3ToJson(post->colorFilter) },
            { "vignetteEnabled", post->vignetteEnabled },
            { "vignetteColor", Vec3ToJson(post->vignetteColor) },
            { "vignetteIntensity", post->vignetteIntensity },
            { "vignetteSmoothness", post->vignetteSmoothness },
            { "skyboxEnabled", post->skyboxEnabled },
            { "skyboxRotation", post->skyboxRotationDegrees },
            { "skyboxExposure", post->skyboxExposure },
            { "skyboxTint", Vec3ToJson(post->skyboxTint) },
        };
        if (!post->skyboxTexturePath.empty())
            postJson["skyboxTexture"] = post->skyboxTexturePath;
        if (!post->enabled)
            postJson["enabled"] = false;
        ent["postProcessVolume"] = postJson;
    }

    if (const auto* audio = const_cast<Entity&>(entity).GetComponent<AudioSourceComponent>()) {
        json audioJson = {
            { "clip", audio->clipPath },
            { "playOnAwake", audio->playOnAwake },
            { "loop", audio->loop },
            { "mute", audio->mute },
            { "volume", audio->volume },
        };
        if (!audio->enabled)
            audioJson["enabled"] = false;
        ent["audioSource"] = audioJson;
    }

    if (const auto* rb = const_cast<Entity&>(entity).GetComponent<RigidbodyComponent>()) {
        json rbJson = {
            { "bodyType", static_cast<int>(rb->bodyType) },
            { "mass", rb->mass },
            { "useGravity", rb->useGravity },
            { "linearDrag", rb->linearDrag },
            { "bounciness", rb->bounciness },
            { "freezeRotation", rb->freezeRotation },
        };
        if (rb->characterController)
            rbJson["characterController"] = true;
        if (!rb->enabled)
            rbJson["enabled"] = false;
        ent["rigidbody"] = rbJson;
    }

    if (const auto* canvas = const_cast<Entity&>(entity).GetComponent<CanvasComponent>()) {
        json canvasJson;
        if (!canvas->enabled)
            canvasJson["enabled"] = false;
        canvasJson["renderMode"] = static_cast<int>(canvas->renderMode);
        canvasJson["scaleMode"] = static_cast<int>(canvas->scaleMode);
        canvasJson["sortOrder"] = canvas->sortOrder;
        if (canvas->eventCameraEntityId != 0)
            canvasJson["eventCamera"] = canvas->eventCameraEntityId;
        canvasJson["referenceResolution"] = Vec2ToJson(canvas->referenceResolution);
        canvasJson["matchWidthOrHeight"] = canvas->matchWidthOrHeight;
        canvasJson["planeDistance"] = canvas->planeDistance;
        ent["canvas"] = canvasJson;
    }

    if (const auto* rect = const_cast<Entity&>(entity).GetComponent<RectTransformComponent>()) {
        json rectJson = {
            { "anchorMin", Vec2ToJson(rect->anchorMin) },
            { "anchorMax", Vec2ToJson(rect->anchorMax) },
            { "pivot", Vec2ToJson(rect->pivot) },
            { "anchoredPosition", Vec2ToJson(rect->anchoredPosition) },
            { "sizeDelta", Vec2ToJson(rect->sizeDelta) },
        };
        if (!rect->enabled)
            rectJson["enabled"] = false;
        ent["rectTransform"] = rectJson;
    }

    if (const auto* image = const_cast<Entity&>(entity).GetComponent<UIImageComponent>()) {
        json imageJson = {
            { "color", Vec4ToJson(image->color) },
            { "preserveAspect", image->preserveAspect },
        };
        if (!image->enabled)
            imageJson["enabled"] = false;
        if (!image->texturePath.empty())
            imageJson["texture"] = image->texturePath;
        ent["uiImage"] = imageJson;
    }

    if (const auto* text = const_cast<Entity&>(entity).GetComponent<UITextComponent>()) {
        json textJson = {
            { "text", text->text },
            { "color", Vec4ToJson(text->color) },
            { "fontSize", text->fontSize },
            { "alignment", static_cast<int>(text->alignment) },
        };
        if (!text->enabled)
            textJson["enabled"] = false;
        ent["uiText"] = textJson;
    }

    if (const auto* group = const_cast<Entity&>(entity).GetComponent<UIButtonGroupComponent>()) {
        json groupJson = {
            { "selectedIndex", group->selectedIndex },
            { "wrapNavigation", group->wrapNavigation },
            { "keyboardNavigation", group->keyboardNavigation },
            { "gamepadNavigation", group->gamepadNavigation },
            { "keyboardConfirm", group->keyboardConfirm },
            { "gamepadConfirm", group->gamepadConfirm },
            { "confirmKey", static_cast<int>(group->confirmKey) },
            { "confirmButton", static_cast<int>(group->confirmButton) },
            { "cursorOffset", Vec2ToJson(group->cursorOffset) },
            { "cursorSize", Vec2ToJson(group->cursorSize) },
        };
        if (!group->enabled)
            groupJson["enabled"] = false;
        if (!group->cursorTexturePath.empty())
            groupJson["cursorTexture"] = group->cursorTexturePath;
        ent["uiButtonGroup"] = groupJson;
    }

    if (const auto* button = const_cast<Entity&>(entity).GetComponent<UIButtonComponent>()) {
        json buttonJson = {
            { "interactable", button->interactable },
            { "label", button->label },
            { "preserveAspect", button->preserveAspect },
            { "normalColor", Vec4ToJson(button->normalColor) },
            { "selectedColor", Vec4ToJson(button->selectedColor) },
            { "pressedColor", Vec4ToJson(button->pressedColor) },
            { "textColor", Vec4ToJson(button->textColor) },
            { "fontSize", button->fontSize },
        };
        if (!button->enabled)
            buttonJson["enabled"] = false;
        if (!button->backgroundTexturePath.empty())
            buttonJson["backgroundTexture"] = button->backgroundTexturePath;
        if (!button->onClick.empty()) {
            buttonJson["onClick"] = json::array();
            for (const UIButtonClickEvent& listener : button->onClick) {
                buttonJson["onClick"].push_back({
                    { "enabled", listener.enabled },
                    { "targetEntity", listener.targetEntityId },
                    { "script", listener.scriptPath },
                    { "method", listener.methodName },
                });
            }
        }
        ent["uiButton"] = buttonJson;
    }

    if (const auto* spectrum = const_cast<Entity&>(entity).GetComponent<UIAudioSpectrumComponent>()) {
        json spectrumJson = {
            { "sourceEntity", spectrum->sourceEntityId },
            { "color", Vec4ToJson(spectrum->color) },
            { "backgroundColor", Vec4ToJson(spectrum->backgroundColor) },
            { "barCount", spectrum->barCount },
            { "barGap", spectrum->barGap },
            { "sensitivity", spectrum->sensitivity },
            { "smoothing", spectrum->smoothing },
        };
        if (!spectrum->enabled)
            spectrumJson["enabled"] = false;
        ent["uiAudioSpectrum"] = spectrumJson;
    }

    return ent;
}

void ApplyEntityJson(Entity& entity, const json& ent, const std::string& name) {
    if (ent.contains("transform")) {
        auto* transform = entity.GetComponent<TransformComponent>();
        const json& t = ent["transform"];
        if (transform) {
            if (t.contains("position")) Vec3FromJson(t["position"], transform->position);
            if (t.contains("rotation")) Vec3FromJson(t["rotation"], transform->rotation);
            if (t.contains("scale"))    Vec3FromJson(t["scale"],    transform->scale);
        }
    }

    if (ent.contains("meshRenderer")) {
        const json& mr = ent["meshRenderer"];
        auto& meshComp = entity.AddComponent<MeshRendererComponent>();
        meshComp.enabled = mr.value("enabled", true);
        std::string primitive = NormalizePrimitive(mr.value("primitive", "Cube"));
        primitive = InferPrimitiveFromEntityName(name, primitive);
        meshComp.meshSize = mr.value("size", 1.0f);
        // Renderer-local appearance was supported by older scenes. Keep it only
        // long enough to migrate it into a material asset; it is no longer an
        // independently editable or serialized MeshRenderer property.
        const bool hasLegacyColor = mr.contains("color");
        glm::vec4 legacyColor{ 1.0f };
        if (hasLegacyColor)
            Vec4FromJson(mr["color"], legacyColor);

        AssetManager& assets = AssetManager::Get();
        const std::string legacyTexturePath = ResolveAssetPathWithAssetsFallback(
            assets, mr.value("texture", std::string{}));
        meshComp.meshPath     = mr.value("mesh",     std::string{});
        meshComp.viewModel    = mr.value("viewModel", false);
        meshComp.editorOnly   = mr.value("editorOnly", false);
        meshComp.prerenderOccluder = mr.value("prerenderOccluder", false);
        meshComp.ps1SeamFill  = mr.value("ps1SeamFill", false);
        if (!meshComp.prerenderOccluder) {
            static constexpr const char* kOccluderMarker = "_PrerenderOccluder";
            meshComp.prerenderOccluder =
                name.find(kOccluderMarker) != std::string::npos;
        }

        if (!meshComp.meshPath.empty()) {
            meshComp.SetMeshFile(meshComp.meshPath);
        } else {
            meshComp.SetPrimitive(primitive, meshComp.meshSize);
        }

        meshComp.materialPath = ResolveAssetPathWithAssetsFallback(
            assets, mr.value("material", std::string{}));
        if (mr.contains("tiling"))
            Vec2FromJson(mr["tiling"], meshComp.textureTiling);
        if (mr.contains("offset"))
            Vec2FromJson(mr["offset"], meshComp.textureOffset);

        if (!meshComp.materialPath.empty()) {
            Material mat;
            std::string err;
            if (Material::Load(assets.ToAbsolute(meshComp.materialPath), mat, err)) {
                assets.ApplyMaterialToMeshRenderer(meshComp, mat, meshComp.materialPath);
            } else {
                meshComp.texture = std::make_shared<Texture>(Texture::CreateCheckerboard(128, 16));
            }
        } else if (!legacyTexturePath.empty() || hasLegacyColor ||
                   mr.contains("tiling") || mr.contains("offset")) {
            meshComp.materialPath = MigrateLegacyAppearanceToMaterial(
                assets, "MeshRenderer", legacyTexturePath, legacyColor,
                meshComp.textureTiling, meshComp.textureOffset);
            if (!meshComp.materialPath.empty()) {
                Material migrated;
                std::string error;
                if (Material::Load(assets.ToAbsolute(meshComp.materialPath), migrated, error))
                    assets.ApplyMaterialToMeshRenderer(meshComp, migrated, meshComp.materialPath);
            }
            if (!meshComp.texture)
                meshComp.texture = std::make_shared<Texture>(Texture::CreateCheckerboard(128, 16));
        } else {
            meshComp.texture = std::make_shared<Texture>(Texture::CreateCheckerboard(128, 16));
        }
    }

    if (ent.contains("terrain")) {
        const json& tj = ent["terrain"];
        auto& terrain = entity.AddComponent<TerrainComponent>();
        terrain.enabled = tj.value("enabled", true);
        terrain.size = tj.value("size", terrain.size);
        terrain.subdivisions = std::clamp(tj.value("subdivisions", terrain.subdivisions), 1, 128);
        terrain.heightScale = tj.value("heightScale", terrain.heightScale);
        terrain.noiseScale = tj.value("noiseScale", terrain.noiseScale);
        terrain.seed = tj.value("seed", terrain.seed);
        terrain.flat = tj.value("flat", terrain.flat);
        terrain.brushEnabled = tj.value("brushEnabled", terrain.brushEnabled);
        terrain.brushMode = static_cast<TerrainComponent::BrushMode>(
            std::clamp(tj.value("brushMode", static_cast<int>(terrain.brushMode)), 0, 3));
        terrain.brushRadius = tj.value("brushRadius", terrain.brushRadius);
        terrain.brushStrength = tj.value("brushStrength", terrain.brushStrength);
        if (tj.contains("brushColor"))
            Vec4FromJson(tj["brushColor"], terrain.brushColor);
        if (tj.contains("heights") && tj["heights"].is_array()) {
            terrain.heights.clear();
            terrain.heights.reserve(tj["heights"].size());
            for (const auto& h : tj["heights"]) {
                if (h.is_number())
                    terrain.heights.push_back(h.get<float>());
            }
        }
        if (tj.contains("colors") && tj["colors"].is_array()) {
            terrain.paintColors.clear();
            terrain.paintColors.reserve(tj["colors"].size());
            for (const auto& c : tj["colors"]) {
                if (c.is_number_unsigned())
                    terrain.paintColors.push_back(PackedToColor(c.get<uint32_t>()));
                else if (c.is_number_integer())
                    terrain.paintColors.push_back(PackedToColor(static_cast<uint32_t>(c.get<int64_t>())));
                else if (c.is_array()) {
                    glm::vec4 color(1.0f);
                    Vec4FromJson(c, color);
                    terrain.paintColors.push_back(color);
                }
            }
        }
        if (auto* mr = entity.GetComponent<MeshRendererComponent>())
            terrain.RebuildMesh(*mr);
    }

    // Load ProModelerComponent (also supports old "proBuilder" key for backward compatibility)
    const std::string pmKey = ent.contains("proModeler") ? "proModeler" : (ent.contains("proBuilder") ? "proBuilder" : "");
    if (!pmKey.empty()) {
        const json& pj = ent[pmKey];
        auto& pb = entity.AddComponent<ProModelerComponent>();
        pb.enabled = pj.value("enabled", true);
        pb.shape = static_cast<ProModelerComponent::Shape>(
            std::clamp(pj.value("shape", static_cast<int>(pb.shape)), 0, 4));
        if (pj.contains("size"))
            Vec3FromJson(pj["size"], pb.size);
        pb.steps = std::clamp(pj.value("steps", pb.steps), 1, 32);
        pb.extrudeAmount = pj.value("extrudeAmount", pb.extrudeAmount);

        if (pj.contains("vertices") && pj["vertices"].is_array()) {
            pb.vertices.clear();
            pb.vertices.reserve(pj["vertices"].size());
            for (const json& vj : pj["vertices"]) {
                ProModelerVertex v;
                if (vj.contains("position")) Vec3FromJson(vj["position"], v.position);
                if (vj.contains("normal")) Vec3FromJson(vj["normal"], v.normal);
                if (vj.contains("uv")) Vec2FromJson(vj["uv"], v.uv);
                if (vj.contains("color")) Vec4FromJson(vj["color"], v.color);
                pb.vertices.push_back(v);
            }
        }
        if (pj.contains("indices") && pj["indices"].is_array()) {
            pb.indices.clear();
            pb.indices.reserve(pj["indices"].size());
            for (const json& index : pj["indices"]) {
                if (index.is_number_unsigned())
                    pb.indices.push_back(index.get<uint32_t>());
                else if (index.is_number_integer())
                    pb.indices.push_back(static_cast<uint32_t>(std::max<int64_t>(0, index.get<int64_t>())));
            }
        }
        if (pj.contains("faceIds") && pj["faceIds"].is_array()) {
            pb.triangleFaceIds.clear();
            for (const json& faceId : pj["faceIds"]) {
                if (faceId.is_number_integer())
                    pb.triangleFaceIds.push_back(
                        static_cast<uint32_t>(std::max<int64_t>(1, faceId.get<int64_t>())));
            }
        }
        pb.nextFaceId = std::max<uint32_t>(1, pj.value("nextFaceId", pb.nextFaceId));
        if (pj.contains("faceMaterials") && pj["faceMaterials"].is_array()) {
            pb.faceMaterialPaths.clear();
            for (const json& assignment : pj["faceMaterials"]) {
                if (!assignment.is_object())
                    continue;
                const uint32_t faceId = assignment.value("faceId", 0u);
                const std::string materialPath = assignment.value("material", std::string{});
                if (faceId != 0u && !materialPath.empty())
                    pb.faceMaterialPaths[faceId] = materialPath;
            }
        }
        pb.EnsureFaceTopology();
        auto* mr = entity.GetComponent<MeshRendererComponent>();
        if (!mr)
            mr = &entity.AddComponent<MeshRendererComponent>();
        pb.RebuildMesh(*mr);
    }

    if (ent.contains("skinnedMeshRenderer")) {
        AssetManager& assets = AssetManager::Get();
        const json& sm = ent["skinnedMeshRenderer"];
        auto& skinComp = entity.AddComponent<SkinnedMeshRendererComponent>();
        skinComp.enabled = sm.value("enabled", true);
        if (sm.contains("color"))
            Vec4FromJson(sm["color"], skinComp.color);
        skinComp.modelPath = sm.value("model", std::string{});
        const std::string legacyTexturePath = ResolveAssetPathWithAssetsFallback(
            assets, sm.value("texture", std::string{}));
        skinComp.materialPath = ResolveAssetPathWithAssetsFallback(
            assets, sm.value("material", std::string{}));
        skinComp.meshPartIndex = sm.value("meshPart", -1);
        skinComp.ps1ExportMode = static_cast<SkinnedMeshRendererComponent::Ps1ExportMode>(
            std::clamp(sm.value("ps1ExportMode", static_cast<int>(skinComp.ps1ExportMode)), 0, 2));
        skinComp.ps1SeamFill = sm.value("ps1SeamFill", false);
        skinComp.ps1VertexAnimFps = std::clamp(sm.value("ps1VertexAnimFps", skinComp.ps1VertexAnimFps), 1, 30);
        skinComp.ps1VertexAnimMaxFrames =
            std::clamp(sm.value("ps1VertexAnimMaxFrames", skinComp.ps1VertexAnimMaxFrames), 1, 120);
        skinComp.ps1VertexAnimTargetTris =
            std::clamp(sm.value("ps1VertexAnimTargetTris", skinComp.ps1VertexAnimTargetTris), 32, 2000);
        skinComp.ps1VertexAnimTargetVerts =
            std::clamp(sm.value("ps1VertexAnimTargetVerts", skinComp.ps1VertexAnimTargetVerts), 64, 2600);
        skinComp.ps1VertexAnimMaxSourceMB =
            std::clamp(sm.value("ps1VertexAnimMaxSourceMB", skinComp.ps1VertexAnimMaxSourceMB), 0.25f, 64.0f);
        if (sm.contains("tiling"))
            Vec2FromJson(sm["tiling"], skinComp.textureTiling);
        if (sm.contains("offset"))
            Vec2FromJson(sm["offset"], skinComp.textureOffset);
        if (!skinComp.modelPath.empty()) {
            skinComp.SetModelFile(skinComp.modelPath);
            ApplySkeletalModelFitScale(entity, skinComp.modelPath);
        }
        if (!skinComp.materialPath.empty()) {
            Material mat;
            std::string err;
            if (Material::Load(assets.ToAbsolute(skinComp.materialPath), mat, err))
                assets.ApplyMaterialToSkinnedMeshRenderer(skinComp, mat, skinComp.materialPath);
        } else if (!legacyTexturePath.empty()) {
            skinComp.materialPath = MigrateLegacyAppearanceToMaterial(
                assets, "SkinnedMeshRenderer", legacyTexturePath, skinComp.color,
                skinComp.textureTiling, skinComp.textureOffset);
            if (!skinComp.materialPath.empty()) {
                Material migrated;
                std::string error;
                if (Material::Load(assets.ToAbsolute(skinComp.materialPath), migrated, error))
                    assets.ApplyMaterialToSkinnedMeshRenderer(skinComp, migrated, skinComp.materialPath);
            }
            if (!skinComp.texture)
                skinComp.texture = std::make_shared<Texture>(Texture::CreateCheckerboard(128, 16));
        } else {
            skinComp.texture = std::make_shared<Texture>(Texture::CreateCheckerboard(128, 16));
        }
    }

    if (ent.contains("animator")) {
        const json& aj = ent["animator"];
        auto& animComp = entity.AddComponent<AnimatorComponent>();
        animComp.enabled = aj.value("enabled", true);
        animComp.modelPath = aj.value("model", std::string{});
        if (animComp.modelPath.empty()) {
            if (auto* skinned = entity.GetComponent<SkinnedMeshRendererComponent>())
                animComp.modelPath = skinned->modelPath;
        }
        animComp.controllerPath = aj.value("controller", std::string{});
        animComp.speed = aj.value("speed", 1.0f);
        animComp.animationFps = aj.value("animationFps", 30.0f);
        if (aj.contains("floatParams") && aj["floatParams"].is_object()) {
            for (auto it = aj["floatParams"].begin(); it != aj["floatParams"].end(); ++it)
                animComp.parameters.floats[it.key()] = it.value().get<float>();
        }
        if (aj.contains("boolParams") && aj["boolParams"].is_object()) {
            for (auto it = aj["boolParams"].begin(); it != aj["boolParams"].end(); ++it)
                animComp.parameters.bools[it.key()] = it.value().get<bool>();
        }
        animComp.ReloadAssets();
        if (aj.contains("state")) {
            const std::string savedState = aj["state"].get<std::string>();
            if (animComp.controller) {
                for (const auto& st : animComp.controller->states) {
                    if (st.name == savedState) {
                        animComp.currentState = savedState;
                        break;
                    }
                }
            }
        }
    }

    if (ent.contains("bone")) {
        const json& bj = ent["bone"];
        auto& boneComp = entity.AddComponent<BoneComponent>();
        boneComp.modelPath = bj.value("model", std::string{});
        boneComp.boneIndex = bj.value("boneIndex", -1);
    }

    if (ent.contains("light")) {
        const json& lj = ent["light"];
        auto& lightComp = entity.AddComponent<LightComponent>();
        lightComp.enabled = lj.value("enabled", true);
        lightComp.type = static_cast<LightType>(lj.value("type", static_cast<int>(LightType::Point)));
        if (lj.contains("color"))
            Vec3FromJson(lj["color"], lightComp.color);
        lightComp.intensity = lj.value("intensity", 1.0f);
        lightComp.range = lj.value("range", 10.0f);
        lightComp.spotAngle = lj.value("spotAngle", 45.0f);
        lightComp.spotInnerAngle = lj.value("spotInnerAngle", 30.0f);
    }

    if (ent.contains("postProcessVolume")) {
        const json& pj = ent["postProcessVolume"];
        auto& post = entity.AddComponent<PostProcessVolumeComponent>();
        post.enabled = pj.value("enabled", true);
        post.isGlobal = pj.value("isGlobal", true);
        post.priority = pj.value("priority", 0);
        post.fogEnabled = pj.value("fogEnabled", post.fogEnabled);
        if (pj.contains("fogColor")) Vec3FromJson(pj["fogColor"], post.fogColor);
        post.fogStart = pj.value("fogStart", post.fogStart);
        post.fogEnd = pj.value("fogEnd", post.fogEnd);
        post.colorGradingEnabled = pj.value("colorGradingEnabled", post.colorGradingEnabled);
        post.exposure = pj.value("exposure", post.exposure);
        post.contrast = pj.value("contrast", post.contrast);
        post.saturation = pj.value("saturation", post.saturation);
        if (pj.contains("colorFilter")) Vec3FromJson(pj["colorFilter"], post.colorFilter);
        post.vignetteEnabled = pj.value("vignetteEnabled", post.vignetteEnabled);
        if (pj.contains("vignetteColor")) Vec3FromJson(pj["vignetteColor"], post.vignetteColor);
        post.vignetteIntensity = pj.value("vignetteIntensity", post.vignetteIntensity);
        post.vignetteSmoothness = pj.value("vignetteSmoothness", post.vignetteSmoothness);
        post.skyboxEnabled = pj.value("skyboxEnabled", post.skyboxEnabled);
        post.skyboxTexturePath = pj.value("skyboxTexture", std::string{});
        post.skyboxRotationDegrees = pj.value("skyboxRotation", post.skyboxRotationDegrees);
        post.skyboxExposure = pj.value("skyboxExposure", post.skyboxExposure);
        if (pj.contains("skyboxTint")) Vec3FromJson(pj["skyboxTint"], post.skyboxTint);
        if (!post.skyboxTexturePath.empty())
            post.skyboxTexture = AssetManager::Get().GetSkyboxTexture(post.skyboxTexturePath);
    }

    if (ent.contains("audioSource")) {
        const json& audioJson = ent["audioSource"];
        auto& audio = entity.AddComponent<AudioSourceComponent>();
        audio.enabled = audioJson.value("enabled", true);
        audio.clipPath = audioJson.value("clip", std::string{});
        audio.playOnAwake = audioJson.value("playOnAwake", true);
        audio.loop = audioJson.value("loop", false);
        audio.mute = audioJson.value("mute", false);
        audio.volume = std::clamp(audioJson.value("volume", 1.0f), 0.0f, 1.0f);
    }

    if (ent.contains("camera")) {
        const json& cam = ent["camera"];
        auto& cameraComp = entity.AddComponent<CameraComponent>();
        cameraComp.enabled        = cam.value("enabled", true);
        cameraComp.primary        = cam.value("primary", false);
        cameraComp.camera.fov     = cam.value("fov", 60.0f);
        cameraComp.camera.nearClip = cam.value("nearClip", 0.1f);
        cameraComp.camera.farClip  = cam.value("farClip", 100.0f);
        cameraComp.prerenderedBackgroundPath = cam.value("prerenderedBackground", std::string{});
        if (cam.contains("shot") && cam["shot"].is_object()) {
            const json& shot = cam["shot"];
            cameraComp.shotTriggerEntityId = shot.value("trigger", 0u);
            cameraComp.shotPriority = shot.value("priority", 0);
        }
        if (auto* transform = entity.GetComponent<TransformComponent>())
            cameraComp.camera.SyncFromTransform(transform->position, transform->rotation);
    }

    auto loadMipsScript = [&](const json& scriptJson) {
        auto& script = entity.AddComponent<MipsScriptComponent>();
        script.enabled = scriptJson.value("enabled", true);
        script.scriptPath = scriptJson.value("path", "");

        std::vector<std::string> compileErrors;
        if (!script.scriptPath.empty())
            Mips::MipsRuntime::EnsureScriptReady(script, compileErrors);

        if (scriptJson.contains("fields") && scriptJson["fields"].is_object() && script.module) {
            script.fieldValues.resize(script.module->fields.size());
            script.fieldAssetPaths.resize(script.module->fields.size());
            for (size_t i = 0; i < script.module->fields.size(); ++i) {
                const auto& field = script.module->fields[i];
                const std::string& fieldName = field.name;
                const bool hasSaved = scriptJson["fields"].contains(fieldName);
                if (field.valueKind == Mips::FieldValueKind::AudioClip) {
                    if (hasSaved && scriptJson["fields"][fieldName].is_string())
                        script.fieldAssetPaths[i] = scriptJson["fields"][fieldName].get<std::string>();
                    script.fieldValues[i] = 0.0;
                } else if (field.valueKind == Mips::FieldValueKind::Array) {
                    script.fieldValues[i] = 0.0;
                } else if (hasSaved && scriptJson["fields"][fieldName].is_number()) {
                    script.fieldValues[i] = scriptJson["fields"][fieldName].get<double>();
                } else {
                    const uint16_t cidx = script.module->fields[i].defaultConstIndex;
                    script.fieldValues[i] = (cidx < script.module->numberConstants.size())
                        ? script.module->numberConstants[cidx] : 0.0;
                }
            }
        }
    };
    if (ent.contains("mipsScripts") && ent["mipsScripts"].is_array()) {
        for (const auto& scriptJson : ent["mipsScripts"])
            if (scriptJson.is_object()) loadMipsScript(scriptJson);
    } else if (ent.contains("mipsScript") && ent["mipsScript"].is_object()) {
        loadMipsScript(ent["mipsScript"]);
    }

    if (ent.contains("collider")) {
        const json& colJson = ent["collider"];
        auto& col = entity.AddComponent<ColliderComponent>();
        col.enabled = colJson.value("enabled", true);
        col.shape = static_cast<ColliderShape>(colJson.value("shape", 0));
        if (colJson.contains("center")) Vec3FromJson(colJson["center"], col.center);
        if (colJson.contains("halfExtents")) Vec3FromJson(colJson["halfExtents"], col.halfExtents);
        col.radius = colJson.value("radius", col.radius);
        col.capsuleHeight = colJson.value("capsuleHeight", col.capsuleHeight);
        col.convex = colJson.value("convex", true);
        col.isTrigger = colJson.value("isTrigger", false);
        col.cameraShotTrigger =
            colJson.value("cameraTrigger", colJson.value("shotTrigger", IsCameraTriggerTag(ent)));
        col.cameraTargetEntityId = colJson.value("cameraTarget", 0u);
        if (col.cameraShotTrigger)
            col.isTrigger = true;
    }

    if (ent.contains("rigidbody")) {
        const json& rbJson = ent["rigidbody"];
        auto& rb = entity.AddComponent<RigidbodyComponent>();
        rb.enabled = rbJson.value("enabled", true);
        rb.bodyType = static_cast<RigidbodyType>(rbJson.value("bodyType", 0));
        rb.mass = rbJson.value("mass", rb.mass);
        rb.useGravity = rbJson.value("useGravity", rb.useGravity);
        rb.linearDrag = rbJson.value("linearDrag", rb.linearDrag);
        rb.bounciness = rbJson.value("bounciness", rb.bounciness);
        rb.freezeRotation = rbJson.value("freezeRotation", rb.freezeRotation);
        rb.characterController = rbJson.value("characterController", rb.characterController);
    }

    if (ent.contains("canvas")) {
        const json& canvasJson = ent["canvas"];
        auto& canvas = entity.AddComponent<CanvasComponent>();
        canvas.enabled = canvasJson.value("enabled", true);
        canvas.renderMode = static_cast<UICanvasRenderMode>(canvasJson.value("renderMode", 0));
        canvas.scaleMode = static_cast<UICanvasScaleMode>(canvasJson.value("scaleMode", 1));
        canvas.sortOrder = canvasJson.value("sortOrder", 0);
        canvas.eventCameraEntityId = canvasJson.value("eventCamera", 0u);
        if (canvasJson.contains("referenceResolution"))
            Vec2FromJson(canvasJson["referenceResolution"], canvas.referenceResolution);
        canvas.matchWidthOrHeight = canvasJson.value("matchWidthOrHeight", canvas.matchWidthOrHeight);
        canvas.planeDistance = canvasJson.value("planeDistance", canvas.planeDistance);
    }

    if (ent.contains("rectTransform")) {
        const json& rectJson = ent["rectTransform"];
        auto& rect = entity.AddComponent<RectTransformComponent>();
        rect.enabled = rectJson.value("enabled", true);
        if (rectJson.contains("anchorMin")) Vec2FromJson(rectJson["anchorMin"], rect.anchorMin);
        if (rectJson.contains("anchorMax")) Vec2FromJson(rectJson["anchorMax"], rect.anchorMax);
        if (rectJson.contains("pivot")) Vec2FromJson(rectJson["pivot"], rect.pivot);
        if (rectJson.contains("anchoredPosition")) Vec2FromJson(rectJson["anchoredPosition"], rect.anchoredPosition);
        if (rectJson.contains("sizeDelta")) Vec2FromJson(rectJson["sizeDelta"], rect.sizeDelta);
    }

    if (ent.contains("uiImage")) {
        const json& imageJson = ent["uiImage"];
        auto& image = entity.AddComponent<UIImageComponent>();
        image.enabled = imageJson.value("enabled", true);
        if (imageJson.contains("color"))
            Vec4FromJson(imageJson["color"], image.color);
        image.texturePath = imageJson.value("texture", std::string{});
        image.preserveAspect = imageJson.value("preserveAspect", image.preserveAspect);
        if (!image.texturePath.empty())
            image.texture = AssetManager::Get().GetTexture(image.texturePath);
    }

    if (ent.contains("uiText")) {
        const json& textJson = ent["uiText"];
        auto& text = entity.AddComponent<UITextComponent>();
        text.enabled = textJson.value("enabled", true);
        text.text = textJson.value("text", text.text);
        if (textJson.contains("color"))
            Vec4FromJson(textJson["color"], text.color);
        text.fontSize = textJson.value("fontSize", text.fontSize);
        text.alignment = static_cast<UITextAlignment>(textJson.value("alignment", 0));
    }

    if (ent.contains("uiButtonGroup")) {
        const json& groupJson = ent["uiButtonGroup"];
        auto& group = entity.AddComponent<UIButtonGroupComponent>();
        group.enabled = groupJson.value("enabled", true);
        group.selectedIndex = groupJson.value("selectedIndex", group.selectedIndex);
        group.wrapNavigation = groupJson.value("wrapNavigation", group.wrapNavigation);
        group.keyboardNavigation = groupJson.value("keyboardNavigation", group.keyboardNavigation);
        group.gamepadNavigation = groupJson.value("gamepadNavigation", group.gamepadNavigation);
        group.keyboardConfirm = groupJson.value("keyboardConfirm", group.keyboardConfirm);
        group.gamepadConfirm = groupJson.value("gamepadConfirm", group.gamepadConfirm);
        group.confirmKey = static_cast<UIConfirmKey>(
            std::clamp(groupJson.value("confirmKey", static_cast<int>(group.confirmKey)), 0, 5));
        group.confirmButton = static_cast<UIConfirmGamepadButton>(
            std::clamp(groupJson.value("confirmButton", static_cast<int>(group.confirmButton)), 0, 4));
        group.cursorTexturePath = groupJson.value("cursorTexture", std::string{});
        if (groupJson.contains("cursorOffset"))
            Vec2FromJson(groupJson["cursorOffset"], group.cursorOffset);
        if (groupJson.contains("cursorSize"))
            Vec2FromJson(groupJson["cursorSize"], group.cursorSize);
        if (!group.cursorTexturePath.empty())
            group.cursorTexture = AssetManager::Get().GetTexture(group.cursorTexturePath);
    }

    if (ent.contains("uiButton")) {
        const json& buttonJson = ent["uiButton"];
        auto& button = entity.AddComponent<UIButtonComponent>();
        button.enabled = buttonJson.value("enabled", true);
        button.interactable = buttonJson.value("interactable", true);
        button.label = buttonJson.value("label", button.label);
        button.backgroundTexturePath = buttonJson.value("backgroundTexture", std::string{});
        button.preserveAspect = buttonJson.value("preserveAspect", button.preserveAspect);
        if (buttonJson.contains("normalColor"))
            Vec4FromJson(buttonJson["normalColor"], button.normalColor);
        if (buttonJson.contains("selectedColor"))
            Vec4FromJson(buttonJson["selectedColor"], button.selectedColor);
        if (buttonJson.contains("pressedColor"))
            Vec4FromJson(buttonJson["pressedColor"], button.pressedColor);
        if (buttonJson.contains("textColor"))
            Vec4FromJson(buttonJson["textColor"], button.textColor);
        button.fontSize = buttonJson.value("fontSize", button.fontSize);
        if (buttonJson.contains("onClick") && buttonJson["onClick"].is_array()) {
            for (const json& listenerJson : buttonJson["onClick"]) {
                if (!listenerJson.is_object())
                    continue;
                UIButtonClickEvent listener;
                listener.enabled = listenerJson.value("enabled", true);
                listener.targetEntityId = listenerJson.value("targetEntity", 0u);
                listener.scriptPath = listenerJson.value("script", std::string{});
                listener.methodName = listenerJson.value("method", std::string{});
                button.onClick.push_back(std::move(listener));
            }
        }
        if (!button.backgroundTexturePath.empty())
            button.backgroundTexture = AssetManager::Get().GetTexture(button.backgroundTexturePath);
    }

    if (ent.contains("uiAudioSpectrum")) {
        const json& spectrumJson = ent["uiAudioSpectrum"];
        auto& spectrum = entity.AddComponent<UIAudioSpectrumComponent>();
        spectrum.enabled = spectrumJson.value("enabled", true);
        spectrum.sourceEntityId = spectrumJson.value("sourceEntity", 0u);
        if (spectrumJson.contains("color"))
            Vec4FromJson(spectrumJson["color"], spectrum.color);
        if (spectrumJson.contains("backgroundColor"))
            Vec4FromJson(spectrumJson["backgroundColor"], spectrum.backgroundColor);
        spectrum.barCount = std::clamp(spectrumJson.value("barCount", spectrum.barCount), 4, 32);
        spectrum.barGap = spectrumJson.value("barGap", spectrum.barGap);
        spectrum.sensitivity = spectrumJson.value("sensitivity", spectrum.sensitivity);
        spectrum.smoothing = spectrumJson.value("smoothing", spectrum.smoothing);
    }
}

} // namespace

bool BuildSceneJson(Scene& scene, json& root, std::string& outError) {
    try {
        root = json::object();
        root["version"] = 1;
        root["entities"] = json::array();

        for (const auto& entityPtr : scene.GetEntities()) {
            Entity* entity = entityPtr.get();
          for (auto* script : entity->GetComponents<MipsScriptComponent>()) {
            if (!script->scriptPath.empty()) {
                std::vector<std::string> compileErrors;
                Mips::MipsRuntime::EnsureScriptReady(*script, compileErrors);
            }
          }

            json ent = EntityComponentsToJson(*entity);
            ent["id"] = entity->GetID();
            if (entity->GetParentID() != 0)
                ent["parent"] = entity->GetParentID();
            root["entities"].push_back(ent);
        }
        return true;
    } catch (const std::exception& ex) {
        outError = ex.what();
        return false;
    }
}

bool SerializeSceneFingerprint(Scene& scene, std::string& outFingerprint, std::string& outError) {
    json root;
    if (!BuildSceneJson(scene, root, outError))
        return false;
    outFingerprint = root.dump();
    return true;
}

bool SaveToFile(Scene& scene, const std::string& path, std::string& outError) {
    try {
        json root;
        if (!BuildSceneJson(scene, root, outError))
            return false;

        fs::path filePath = PathUtf8::FromString(path);
        if (filePath.has_parent_path())
            fs::create_directories(filePath.parent_path());

        std::ofstream file(filePath);
        if (!file.is_open()) {
            outError = "failed to open file for writing: " + path;
            return false;
        }
        file << root.dump(2);
        return true;
    } catch (const std::exception& ex) {
        outError = ex.what();
        return false;
    }
}

namespace {

bool LoadSceneFromJsonRoot(Scene& scene, const json& root, std::string& outError) {
    if (!root.contains("entities") || !root["entities"].is_array()) {
        outError = "invalid scene: missing entities array";
        return false;
    }

    scene.Clear();

    std::vector<std::pair<uint32_t, uint32_t>> parentLinks;
    for (const json& ent : root["entities"]) {
        const uint32_t id = ent.value("id", 0u);
        const std::string name = ent.value("name", "Entity");
        Entity* entity = scene.CreateEntityWithId(id, name);
        entity->SetActive(ent.value("active", true));
        entity->SetStatic(ent.value("static", false));
        entity->SetEditorTag(ent.value("unityTag", std::string("Untagged")));
        entity->SetEditorLayer(ent.value("layer", std::string("Default")));
        ApplyEntityJson(*entity, ent, name);
        if (ent.contains("parent"))
            parentLinks.emplace_back(id, ent["parent"].get<uint32_t>());
    }

    for (const auto& [childId, parentId] : parentLinks) {
        Entity* child = scene.FindEntity(childId);
        Entity* parent = scene.FindEntity(parentId);
        if (child && parent)
            scene.SetParent(child, parent);
    }

    scene.NormalizePrimaryCameras();
    return true;
}

} // namespace

bool LoadFromJsonString(Scene& scene, const std::string& jsonText, std::string& outError) {
    try {
        json root = json::parse(jsonText);
        return LoadSceneFromJsonRoot(scene, root, outError);
    } catch (const std::exception& ex) {
        outError = ex.what();
        return false;
    }
}

bool LoadFromFile(Scene& scene, const std::string& path, std::string& outError) {
    try {
        std::ifstream file(PathUtf8::FromString(path));
        if (!file.is_open()) {
            outError = "failed to open file: " + path;
            return false;
        }

        json root;
        file >> root;
        return LoadSceneFromJsonRoot(scene, root, outError);
    } catch (const std::exception& ex) {
        outError = ex.what();
        return false;
    }
}

// Forward declaration for mutual recursion.
json EntityToJsonRecursive(const Entity& entity, const Scene& scene);

json EntityToJsonRecursive(const Entity& entity, const Scene& scene) {
  for (auto* script : const_cast<Entity&>(entity).GetComponents<MipsScriptComponent>()) {
    if (!script->scriptPath.empty()) {
        std::vector<std::string> compileErrors;
        Mips::MipsRuntime::EnsureScriptReady(*script, compileErrors);
    }
  }

    json ent = EntityComponentsToJson(entity);

    const std::vector<uint32_t>& childIds = entity.GetChildIDs();
    if (!childIds.empty()) {
        json children = json::array();
        for (uint32_t childId : childIds) {
            if (const Entity* child = const_cast<Scene&>(scene).FindEntity(childId))
                children.push_back(EntityToJsonRecursive(*child, scene));
        }
        ent["children"] = children;
    }
    return ent;
}

bool SaveEntityToFile(const Entity& entity, const Scene& scene,
                      const std::string& path, std::string& outError) {
    try {
        json ent = EntityToJsonRecursive(entity, scene);
        json root;
        root["version"] = 2;
        root["kind"] = "prefab";
        root["entity"] = ent;

        fs::path filePath = PathUtf8::FromString(path);
        if (filePath.has_parent_path())
            fs::create_directories(filePath.parent_path());

        std::ofstream file(filePath);
        if (!file.is_open()) {
            outError = "failed to open file for writing: " + path;
            return false;
        }
        file << root.dump(2);
        return true;
    } catch (const std::exception& ex) {
        outError = ex.what();
        return false;
    }
}

Entity* SpawnModelFromAsset(Scene& scene, const std::string& projectRelPath, std::string& outError) {
    if (projectRelPath.empty()) {
        outError = "empty model path";
        return nullptr;
    }

    AssetManager& assets = AssetManager::Get();
    const std::string abs = assets.ToAbsolute(projectRelPath);

    std::string entityName = "Model";
    const fs::path rel = PathUtf8::FromString(projectRelPath);
    if (!rel.stem().empty())
        entityName = PathUtf8::ToString(rel.stem());

    if (SkeletalModelFileHasSkin(abs)) {
        auto skeletal = assets.GetSkeletalModel(projectRelPath);
        if (!skeletal) {
            outError = "failed to load skeletal model: " + projectRelPath;
            return nullptr;
        }

        Entity* entity = scene.CreateEntity(entityName);
        PopulateSkeletalCharacterHierarchy(scene, *entity, skeletal, projectRelPath);
        return entity;
    }

    auto meshPtr = assets.GetMesh(projectRelPath);
    if (!meshPtr) {
        outError = "failed to load model: " + projectRelPath;
        return nullptr;
    }

    Entity* entity = scene.CreateEntity(entityName);
    auto& meshComp = entity->AddComponent<MeshRendererComponent>();
    meshComp.SetMeshFile(projectRelPath);
    meshComp.texture = std::make_shared<Texture>(Texture::CreateCheckerboard(128, 16));
    return entity;
}

// Forward declaration for mutual recursion.
Entity* InstantiateEntityJsonRecursive(Scene& scene, const json& ent,
                                       Entity* parent, const std::string& prefabPath);

Entity* InstantiateEntityJsonRecursive(Scene& scene, const json& ent,
                                       Entity* parent, const std::string& prefabPath) {
    const std::string name = ent.value("name", "Prefab");
    Entity* entity = scene.CreateEntity(name);
    entity->SetActive(ent.value("active", true));
    entity->SetStatic(ent.value("static", false));
    entity->SetEditorTag(ent.value("unityTag", std::string("Untagged")));
    entity->SetEditorLayer(ent.value("layer", std::string("Default")));
    if (parent)
        scene.SetParent(entity, parent);
    ApplyEntityJson(*entity, ent, name);
    entity->SetPrefabSourcePath(prefabPath);

    if (ent.contains("children") && ent["children"].is_array()) {
        for (const json& childJson : ent["children"])
            InstantiateEntityJsonRecursive(scene, childJson, entity, prefabPath);
    }
    return entity;
}

Entity* InstantiateFromFile(Scene& scene, const std::string& path, std::string& outError) {
    try {
        std::ifstream file(PathUtf8::FromString(path));
        if (!file.is_open()) {
            outError = "failed to open file: " + path;
            return nullptr;
        }

        json root;
        file >> root;
        if (!root.contains("entity") || !root["entity"].is_object()) {
            outError = "invalid prefab: missing 'entity'";
            return nullptr;
        }

        return InstantiateEntityJsonRecursive(scene, root["entity"], nullptr, path);
    } catch (const std::exception& ex) {
        outError = ex.what();
        return nullptr;
    }
}

bool ApplyPrefabToFile(Entity& entity, const Scene& scene,
                       const std::string& path, std::string& outError) {
    if (!SaveEntityToFile(entity, scene, path, outError))
        return false;
    // Update prefabSourcePath on all descendant instances in the scene.
    std::function<void(Entity&)> markTree = [&](Entity& e) {
        e.SetPrefabSourcePath(path);
        for (uint32_t childId : e.GetChildIDs()) {
            if (Entity* child = const_cast<Scene&>(scene).FindEntity(childId))
                markTree(*child);
        }
    };
    markTree(entity);
    return true;
}

Entity* RevertEntityFromPrefab(Scene& scene, Entity& instance, std::string& outError) {
    const std::string prefabPath = instance.GetPrefabSourcePath();
    if (prefabPath.empty()) {
        outError = "entity has no associated prefab";
        return nullptr;
    }

    Entity* parent = scene.FindEntity(instance.GetParentID());

    // Collect old child IDs to preserve slot order if needed (not strictly needed).
    scene.DestroyEntity(&instance);

    Entity* fresh = InstantiateFromFile(scene, prefabPath, outError);
    if (!fresh)
        return nullptr;

    if (parent)
        scene.SetParent(fresh, parent);
    return fresh;
}

} // namespace MipsyncEngine::SceneIO
