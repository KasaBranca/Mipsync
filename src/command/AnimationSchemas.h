#pragma once
#include "CommandTypes.h"
namespace MipsyncEngine::Command {
inline Json AnimationControllerSchema() {
    Json state={{"type","object"},{"required",{"name","clip"}},{"properties",{
        {"name",{{"type","string"}}},{"clip",{{"type","string"}}},{"clipModel",{{"type","string"}}},
        {"stack",{{"type","integer"},{"minimum",0}}},{"speed",{{"type","number"},{"exclusiveMinimum",0}}},
        {"loop",{{"type","boolean"}}},{"startOffset",{{"type","number"},{"minimum",0},{"maximum",1}}},
        {"position",{{"type","array"},{"minItems",2},{"maxItems",2},{"items",{{"type","number"}}}}}}}};
    Json condition={{"type","object"},{"required",{"param","mode"}},{"properties",{
        {"param",{{"type","string"}}},{"mode",{{"enum",{"greater","less","equals","notEquals","ifTrue","ifFalse"}}}},
        {"threshold",{{"type","number"}}}}}};
    Json transition={{"type","object"},{"required",{"from","to"}},{"properties",{
        {"from",{{"type","string"},{"description","State name or Any State"}}},{"to",{{"type","string"}}},
        {"duration",{{"type","number"},{"minimum",0}}},{"exitTime",{{"type","number"},{"minimum",0}}},
        {"hasExitTime",{{"type","boolean"}}},{"conditions",{{"type","array"},{"items",condition}}}}}};
    return {{"$schema","https://json-schema.org/draft/2020-12/schema"},{"type","object"},
        {"required",{"defaultState","states"}},{"properties",{
        {"version",{{"const",1}}},{"model",{{"type","string"}}},{"defaultState",{{"type","string"}}},
        {"states",{{"type","array"},{"items",state}}},{"transitions",{{"type","array"},{"items",transition}}},
        {"parameters",{{"type","array"},{"items",{{"type","object"},{"required",{"name","type"}},
            {"properties",{{"name",{{"type","string"}}},{"type",{{"enum",{"float","int","bool","trigger"}}}},
            {"default",{{"type",{"number","boolean"}}}}}}}}}},
        {"editorLayout",{{"type","object"}}}}}};
}
}
