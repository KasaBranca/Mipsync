#pragma once
#include "CommandTypes.h"
#include "../mips/PS1Export.h"
#include "../../templates/ps1/starter/runtime/bytecode_abi.h"

namespace MipsyncEngine::Command {
inline Json Ps1Limits() {
    return {{"scriptModules",MIPSYNC_PS1_MODULE_CAP},{"scriptInstances",MIPSYNC_PS1_INSTANCE_CAP},
        {"arrayElements",VM_ARRAY_LENGTH},{"arraysPerInstance",VM_ARRAY_CAP},
        {"numberConstants",VM_NUMBER_CAP},{"stringConstants",VM_STRING_CAP},{"stringBytes",VM_STRING_LEN-1},
        {"nameConstants",VM_NAME_CAP},{"fields",VM_FIELD_CAP},{"methods",VM_METHOD_CAP},
        {"methodBytecodeBytes",VM_METHOD_CODE_CAP},{"locals",VM_LOCAL_CAP},{"stackValues",VM_STACK_CAP},
        {"coroutines",VM_COROUTINE_CAP},{"dynamicUIElements",MIPSYNC_UI_DYNAMIC_CAP},
        {"dynamicTextBytes",MIPSYNC_UI_TEXT_BYTES-1},{"desktopInstructionsPerInvocation",100000}};
}
inline const Json& ComponentCatalog() {
    static const Json catalog=[] {
        Json result=Json::array();
        for (const char* name : {"Transform","MeshRenderer","SkinnedMeshRenderer","Animator","Camera",
            "PostProcessVolume","Collider","Rigidbody","Light","AudioSource","Terrain","ProModeler",
            "Canvas","RectTransform","UIImage","UIText","UIButton","UIButtonGroup","UIAudioSpectrum","MipsScript"})
            result.push_back({{"name",name},{"multiple",std::string(name)=="MipsScript"},
                {"requiredComponents",std::string(name).starts_with("UI")||std::string(name)=="Canvas"?Json::array({"RectTransform"}):Json::array()},
                {"properties",Json::object()}});
        auto props=[&](const char* name,Json defaults) {
            for(auto& component:result) if(component["name"]==name) {
                for(auto it=defaults.begin();it!=defaults.end();++it) {
                    const auto& v=it.value();
                    component["properties"][it.key()]={{"type",v.is_object()?"object":v.is_array()?"number[]":v.is_boolean()?"boolean":v.is_number_integer()?"integer":v.is_number()?"number":"string"},{"default",v}};
                }
                component["properties"]["enabled"]={{"type","boolean"},{"default",true}};
            }
        };
        props("Transform",{{"position",{0,0,0}},{"rotation",{0,0,0}},{"scale",{1,1,1}}});
        props("RectTransform",{{"anchorMin",{.5,.5}},{"anchorMax",{.5,.5}},{"pivot",{.5,.5}},
            {"anchoredPosition",{0,0}},{"sizeDelta",{100,100}}});
        props("UIImage",{{"color",{1,1,1,1}},{"texture",""},{"preserveAspect",false}});
        props("UIText",{{"text","New Text"},{"color",{1,1,1,1}},{"fontSize",18.0},{"alignment",1}});
        props("Canvas",{{"renderMode",0},{"scaleMode",0},{"sortOrder",0},{"eventCamera",0},
            {"referenceResolution",{1920,1080}},{"matchWidthOrHeight",.5},{"planeDistance",1.0}});
        props("MipsScript",{{"path",""}});
        props("Animator",{{"model",""},{"controller",""},{"state",""},{"speed",1.0},{"animationFps",30.0},
            {"floatParams",Json::object()},{"boolParams",Json::object()}});
        props("MeshRenderer",{{"primitive","Cube"},{"size",1.0},{"mesh",""},{"material",""},
            {"prerenderOccluder",false},{"typePreset","Prop"},{"renderType","Lit"},{"editorOnly",false}});
        props("SkinnedMeshRenderer",{{"model",""},{"material",""},{"color",{1,1,1,1}},{"meshPart",-1},
            {"typePreset","Character"},{"renderType","Lit"},{"ps1ExportMode",2},{"ps1VertexAnimFps",30},
            {"ps1VertexAnimMaxFrames",96},{"ps1VertexAnimTargetTris",320},{"ps1VertexAnimTargetVerts",1000},
            {"ps1VertexAnimMaxSourceMB",4.0},{"ps1SeamFill",false}});
        for(auto& c:result) {
            if(c["properties"].contains("renderType")) c["properties"]["renderType"]["enum"]={"Lit","Unlit"};
            if(c["properties"].contains("typePreset")) c["properties"]["typePreset"]["enum"]={"Prop","Corridor","Character","Viewmodel","Floor"};
            if(c["name"]=="SkinnedMeshRenderer") {
                c["properties"]["ps1ExportMode"]["enum"]={0,1,2};
                c["properties"]["ps1ExportMode"]["enumNames"]={"Off","StaticPose","RigidBones"};
                c["properties"]["meshPart"]["min"]=-1;
            }
        }
        return result;
    }();
    return catalog;
}
inline Json ComponentSchema(const std::string& name) {
    for(const auto& c:ComponentCatalog()) if(c["name"]==name)return c;
    return nullptr;
}
}
