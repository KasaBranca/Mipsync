#pragma once
#include "CommandTypes.h"
#include "AnimationSchemas.h"
#include "../scene/Scene.h"
#include "../animation/SkeletalModel.h"
#include "../animation/SkeletalModelPartition.h"
#include "../animation/AnimatorRuntime.h"
#include "../assets/AssetManager.h"
#include <ufbx.h>
#include <cmath>
#include <set>
#include <fstream>
#include <filesystem>
#include <functional>
#ifdef _WIN32
#include <windows.h>
#endif

namespace MipsyncEngine::Command {
inline Json InspectAnimator(const AnimatorComponent& animator) {
    Json current=nullptr,next=nullptr;
    if(animator.controller)for(const auto& state:animator.controller->states) {
        Json value={{"state",state.name},{"clip",state.clipName},{"stack",state.clipStackIndex},
            {"model",ResolveStateClipModelPath(animator,state)},{"speed",state.speed},{"loop",state.loop}};
        if(state.name==animator.currentState)current=value;
        if(state.name==animator.nextState)next=value;
    }
    Json transitions=Json::array();for(const auto& probe:ProbeOutgoingTransitions(animator))
        transitions.push_back({{"label",probe.label},{"wouldFire",probe.wouldFire},{"detail",probe.detail}});
    return {{"model",animator.modelPath},{"controller",animator.controllerPath},{"currentState",animator.currentState},
        {"nextState",animator.nextState},{"stateTime",animator.stateTime},{"phase",GetAnimatorNormalizedTime(animator)},
        {"currentClip",current},{"nextClip",next},{"inTransition",animator.inTransition},
        {"transitionTime",animator.transitionTime},{"transitionDuration",animator.transitionDuration},
        {"transitionProgress",animator.inTransition&&animator.transitionDuration>0?std::clamp(animator.transitionTime/animator.transitionDuration,0.f,1.f):0.f},
        {"parameters",{{"floats",animator.parameters.floats},{"bools",animator.parameters.bools}}},
        {"heldTriggers",animator.heldTriggers},{"outgoingTransitions",transitions}};
}
inline Json InspectModel(const SkeletalModelAsset& model) {
    Json bones=Json::array(),parts=Json::array(),clips=Json::array();
    for(size_t i=0;i<model.bones.size();++i) {
        const auto& bone=model.bones[i];int parent=-1;
        if(bone.node)for(auto* n=bone.node->parent;n&&parent<0;n=n->parent)
            for(size_t p=0;p<model.bones.size();++p)if(model.bones[p].node==n){parent=static_cast<int>(p);break;}
        bones.push_back({{"index",i},{"name",bone.name},{"parent",parent}});
    }
    for(size_t i=0;i<model.meshParts.size();++i) {
        const auto& p=model.meshParts[i];parts.push_back({{"index",i},{"name",p.name},{"indexOffset",p.indexOffset},
            {"indexCount",p.indexCount},{"triangles",p.indexCount/3},{"skinned",p.skinned},{"materialIndex",p.defaultMaterialIndex}});
    }
    for(size_t i=0;i<model.animationNames.size();++i) {
        const int stack=static_cast<int>(model.animationStackIndices[i]);Json channels=Json::array();
        std::set<std::pair<std::string,std::string>> unique;
        if(const auto* s=model.FindAnimStackByIndex(stack))for(const auto* layer:s->layers)
            for(const auto& prop:layer->anim_props)if(prop.element) {
                const std::string target(prop.element->name.data,prop.element->name.length),property(prop.prop_name.data,prop.prop_name.length);
                if(unique.emplace(target,property).second)channels.push_back({{"target",target},{"property",property}});
            }
        clips.push_back({{"id",model.animationNames[i]},{"stack",stack},{"duration",model.GetClipDurationByStackIndex(stack)},{"channels",channels}});
    }
    return {{"meshParts",parts},{"bones",bones},{"clips",clips},{"hasSkin",model.hasSkin},
        {"vertices",model.sourceVertices.size()},{"triangles",model.sourceIndices.size()/3},
        {"boundsMin",{model.boundsMin.x,model.boundsMin.y,model.boundsMin.z}},
        {"boundsMax",{model.boundsMax.x,model.boundsMax.y,model.boundsMax.z}}};
}
inline Json ValidateControllerDocument(const Json& document, const std::function<void(const std::string&)>& checkPath) {
    Json diagnostics=Json::array();
    auto error=[&](const std::string& path,const std::string& message){diagnostics.push_back({{"path",path},{"message",message}});};
    if(!document.is_object()){error("","Controller must be an object.");return diagnostics;}
    try {
        std::set<std::string> states,parameters;
        const auto& list=document.at("states");if(!list.is_array()||list.empty())throw std::runtime_error("states must be a non-empty array.");
        const auto modelPath=document.value("model",std::string{});
        if(!modelPath.empty())checkPath(modelPath);
        for(size_t i=0;i<list.size();++i) {
            const auto& s=list[i];const std::string prefix="/states/"+std::to_string(i);
            const auto name=s.at("name").get<std::string>(),clip=s.at("clip").get<std::string>();
            if(name.empty()||!states.insert(name).second)error(prefix+"/name","State name is empty or duplicated.");
            if(clip.empty())error(prefix+"/clip","Clip ID must not be empty.");
            for(const char* key:{"speed","startOffset"})if(s.contains(key)&&(!s[key].is_number()||!std::isfinite(s[key].get<double>())))error(prefix+"/"+key,"Expected a finite number.");
            if(s.value("speed",1.0)<=0)error(prefix+"/speed","Speed must be positive.");
            const double offset=s.value("startOffset",0.0);if(offset<0||offset>1)error(prefix+"/startOffset","Expected 0..1.");
            if(s.contains("loop")&&!s["loop"].is_boolean())error(prefix+"/loop","Expected boolean.");
            const auto path=s.value("clipModel",modelPath);
            if(!path.empty()) {
                checkPath(path);
                if(std::filesystem::path(path).extension()!=".nanim") {
                    auto model=AssetManager::Get().GetSkeletalModel(path);
                    if(!model)error(prefix+"/clipModel","Could not load clip model.");
                    else {
                        const int stack=model->ClipStackIndex(clip);
                        if(stack<0)error(prefix+"/clip","Clip ID does not exist; use animation.list.");
                        if(s.contains("stack")&&(!s["stack"].is_number_integer()||s["stack"].get<int>()!=stack))error(prefix+"/stack","Clip ID and stack do not match.");
                    }
                }
            } else error(prefix+"/clipModel","A model or per-state clipModel is required to validate clip IDs.");
        }
        if(!states.contains(document.value("defaultState",std::string{})))error("/defaultState","Default state does not exist.");
        const auto params=document.value("parameters",Json::array());if(!params.is_array())throw std::runtime_error("parameters must be an array.");
        for(const auto& p:params) {
            const auto name=p.at("name").get<std::string>(),type=p.at("type").get<std::string>();
            if(name.empty()||!parameters.insert(name).second)error("/parameters","Empty or duplicate parameter name.");
            if(type!="float"&&type!="int"&&type!="bool"&&type!="trigger")error("/parameters","Unknown parameter type: "+type);
            if(p.contains("default")) {
                const auto& d=p["default"];
                if(type=="bool"||type=="trigger") {if(!d.is_boolean())error("/parameters","Boolean parameter default must be a boolean.");}
                else if(!d.is_number()||!std::isfinite(d.get<double>())||(type=="int"&&!d.is_number_integer()))error("/parameters","Invalid numeric default.");
            }
        }
        const auto transitions=document.value("transitions",Json::array());if(!transitions.is_array())throw std::runtime_error("transitions must be an array.");
        for(size_t i=0;i<transitions.size();++i) {
            const auto& tr=transitions[i];const auto prefix="/transitions/"+std::to_string(i);
            const auto from=tr.at("from").get<std::string>(),to=tr.at("to").get<std::string>();
            if(from!=kAnimatorAnyStateName&&!states.contains(from))error(prefix+"/from","Source state does not exist.");
            if(!states.contains(to))error(prefix+"/to","Destination state does not exist.");
            for(const char* key:{"duration","exitTime"})if(tr.contains(key)&&(!tr[key].is_number()||!std::isfinite(tr[key].get<double>())||tr[key].get<double>()<0))error(prefix+"/"+key,"Expected a finite non-negative number.");
            if(tr.contains("hasExitTime")&&!tr["hasExitTime"].is_boolean())error(prefix+"/hasExitTime","Expected boolean.");
            for(const auto& c:tr.value("conditions",Json::array())) {
                if(!parameters.contains(c.at("param").get<std::string>()))error(prefix+"/conditions","Unknown parameter.");
                const auto mode=c.at("mode").get<std::string>();
                if(mode!="greater"&&mode!="less"&&mode!="equals"&&mode!="notEquals"&&mode!="ifTrue"&&mode!="ifFalse")error(prefix+"/conditions","Unknown comparison mode.");
                if(c.contains("threshold")&&(!c["threshold"].is_number()||!std::isfinite(c["threshold"].get<double>())))error(prefix+"/conditions","Threshold must be finite.");
            }
        }
    } catch(const std::exception& ex){error("",ex.what());}
    return diagnostics;
}
inline void WriteJsonAsset(const std::filesystem::path& path,const Json& value) {
    std::filesystem::create_directories(path.parent_path());auto temp=path;temp+=".cli-pending";
    std::ofstream out(temp,std::ios::binary|std::ios::trunc);out<<value.dump(2)<<'\n';out.flush();
    if(!out.good())throw std::runtime_error("Could not write asset: "+PathUtf8::ToString(path));out.close();
#ifdef _WIN32
    if(!MoveFileExW(temp.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)) {
        std::error_code ignored;std::filesystem::remove(temp,ignored);throw std::runtime_error("Could not replace asset; original preserved.");
    }
#else
    std::filesystem::rename(temp,path);
#endif
}
}
