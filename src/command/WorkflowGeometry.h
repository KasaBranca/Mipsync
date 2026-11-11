#pragma once
#include "CommandTypes.h"
#include "../scene/Scene.h"
#include "../animation/SkinnedMesh.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <sstream>

namespace MipsyncEngine::Command {
inline glm::vec3 ParseVector(const std::string& text) {
    std::istringstream input(text); glm::vec3 value{}; char comma=0;
    if(!(input>>value.x>>comma)||comma!=','||!(input>>value.y>>comma)||comma!=','||!(input>>value.z)||
        !std::isfinite(value.x)||!std::isfinite(value.y)||!std::isfinite(value.z))
        throw std::runtime_error("Expected three finite coordinates: x,y,z.");
    input>>std::ws;if(!input.eof())throw std::runtime_error("Unexpected characters after x,y,z.");
    return value;
}
struct RenderBounds {
    glm::vec3 min{std::numeric_limits<float>::max()},max{std::numeric_limits<float>::lowest()};
    bool valid=false;
    void Add(const glm::vec3& point) { min=glm::min(min,point);max=glm::max(max,point);valid=true; }
    void Add(const RenderBounds& b) { if(b.valid){Add(b.min);Add(b.max);} }
    Json ToJson(bool world=true) const {
        if(!valid)return nullptr;
        auto vector=[](glm::vec3 v){return Json::array({v.x,v.y,v.z});};
        return {{"min",vector(min)},{"max",vector(max)},{"center",vector((min+max)*0.5f)},{"size",vector(max-min)},
            {"space",world?"world":"local"},{"source","render-aabb"}};
    }
    bool Contains(glm::vec3 p) const {return valid&&glm::all(glm::greaterThanEqual(p,min))&&glm::all(glm::lessThanEqual(p,max));}
};
inline RenderBounds EntityRenderBounds(Scene& scene,Entity& entity,bool world=true) {
    RenderBounds local;
    if(auto* pm=entity.GetComponent<ProModelerComponent>()) {
        for(const auto& vertex:pm->vertices)local.Add(vertex.position);
    } else if(auto* mr=entity.GetComponent<MeshRendererComponent>();mr&&mr->mesh) {
        local.Add(mr->mesh->GetBoundsMin());local.Add(mr->mesh->GetBoundsMax());
    }
    if(auto* skinned=entity.GetComponent<SkinnedMeshRendererComponent>();skinned&&skinned->mesh) {
        local.Add(skinned->mesh->GetBoundsMin());local.Add(skinned->mesh->GetBoundsMax());
    }
    if(!world||!local.valid)return local;
    const auto matrix=scene.GetWorldMatrix(entity);RenderBounds result;
    for(int i=0;i<8;++i)result.Add(glm::vec3(matrix*glm::vec4(i&1?local.max.x:local.min.x,i&2?local.max.y:local.min.y,i&4?local.max.z:local.min.z,1)));
    return result;
}
inline bool BoundsRay(const RenderBounds& b,glm::vec3 origin,glm::vec3 direction,float maximum,float& distance) {
    if(!b.valid)return false;
    float nearDistance=0,farDistance=maximum;
    for(int axis=0;axis<3;++axis) {
        if(std::abs(direction[axis])<1e-8f) {if(origin[axis]<b.min[axis]||origin[axis]>b.max[axis])return false;continue;}
        float a=(b.min[axis]-origin[axis])/direction[axis],z=(b.max[axis]-origin[axis])/direction[axis];
        if(a>z)std::swap(a,z);nearDistance=std::max(nearDistance,a);farDistance=std::min(farDistance,z);if(nearDistance>farDistance)return false;
    }
    distance=nearDistance;return true;
}
inline Json ProModelerShapes() {
    return {{"shapeEnum",{{"0","box"},{"1","plane"},{"2","wedge"},{"3","stairs"},{"4","custom"},{"5","cylinder"},{"6","cone"},{"7","pyramid"}}},
        {"shapes",{"box","plane","wedge","stairs","cylinder","cone","pyramid"}},
        {"aliases",{{"ramp","wedge"}}},{"size","positive x,y,z; centered local geometry"},
        {"sides",{{"min",3},{"max",64},{"default",16}}},{"steps",{{"min",1},{"max",32},{"default",4}}},
        {"custom","Edited geometry; original primitive cannot be inferred from shape=4."}};
}
inline void CompactMeshData(Json& value) {
    if(!value.is_object())return;
    for(auto it=value.begin();it!=value.end();++it) {
        if((it.key()=="proModeler"||it.key()=="proBuilder")&&it.value().is_object()) {
            auto& pm=it.value();pm["vertexCount"]=pm.value("vertices",Json::array()).size();
            pm["indexCount"]=pm.value("indices",Json::array()).size();
            const int shape=pm.value("shape",4);const auto names=ProModelerShapes()["shapeEnum"];
            pm["shapeName"]=names.value(std::to_string(shape),std::string("unknown"));
            for(const char* key:{"vertices","indices","faceIds"})pm.erase(key);
        } else if(it.key()=="vertices"||it.key()=="indices"||it.key()=="skinnedMesh"||it.key()=="boneMatrices") {
            it.value()={{"omitted",true},{"count",it.value().size()}};
        } else if(it.value().is_object())CompactMeshData(it.value());
    }
}
} // namespace MipsyncEngine::Command
