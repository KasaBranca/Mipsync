// Included inside ExecuteEditorCommand: all authoring uses the editor's scene
// and Undo transaction, never edits .nscene files behind the editor's back.
auto sceneJson=[&](Scene& value) {
    std::string json,error;
    if(!SceneIO::SerializeSceneFingerprint(value,json,error))throw std::runtime_error(error);
    return Json::parse(json);
};
auto componentKey=[](std::string name) {
    if(name=="UIImage")return std::string("uiImage");
    if(name=="UIText")return std::string("uiText");
    if(name=="UIButton")return std::string("uiButton");
    if(name=="UIButtonGroup")return std::string("uiButtonGroup");
    if(name=="UIAudioSpectrum")return std::string("uiAudioSpectrum");
    if(name=="MipsScript")return std::string("mipsScripts");
    if(!name.empty())name[0]=static_cast<char>(std::tolower(static_cast<unsigned char>(name[0])));
    return name;
};
auto entityByArgument=[&]() {std::string error;auto* e=FindEntity(scene,request.arguments.at("entity").get<std::string>(),&error);if(!e)throw std::runtime_error(error);return e;};
auto projectPath=[&](const std::string& input,const char* extension="") {
    auto root=std::filesystem::weakly_canonical(PathUtf8::FromString(m_Engine.GetProjectPath()));
    auto relative=PathUtf8::FromString(input);
    auto path=std::filesystem::weakly_canonical(root/relative);
    auto inside=path.lexically_relative(root);
    if(input.empty()||relative.is_absolute()||inside.empty()||*inside.begin()==".."||
        (*extension&&Lower(PathUtf8::ToString(path.extension()))!=extension))
        throw std::runtime_error("Path must be project-relative and inside the project with the requested extension.");
    return path;
};
#include "EditorAnimationCommands.inl"
if(request.command=="component.describe") {
    const auto name=request.arguments.at("component").get<std::string>();
    Json descriptor=ComponentSchema(name);
    if(descriptor.is_null())return CommandResult::Fail("MIPSYNC_COMPONENT_UNKNOWN","Use component.list for supported component names.");
    Scene defaults;auto* e=defaults.CreateEntity("Defaults");std::string error;
    if(name!="Transform"&&!AddNamedComponent(*e,name,error))return CommandResult::Fail("MIPSYNC_COMPONENT_DEFAULTS",error);
    Json data=sceneJson(defaults).at("entities").at(0).value(componentKey(name),Json::object());
    if(data.is_array())data=data.at(0);
    data["enabled"]=true;
    for(auto it=data.begin();it!=data.end();++it) {
        const auto& v=it.value();auto& p=descriptor["properties"][it.key()];
        p.update({{"type",v.is_boolean()?"boolean":v.is_number_integer()?"integer":v.is_number()?"number":v.is_string()?"string":v.is_array()?"array":"object"},{"default",v}});
        if(v.is_array())p["length"]=v.size();
        if(it.key()=="color"||it.key()=="anchorMin"||it.key()=="anchorMax"||it.key()=="pivot"||it.key()=="matchWidthOrHeight"){p["min"]=0;p["max"]=1;}
        if(it.key()=="fontSize"||it.key()=="planeDistance")p["exclusiveMin"]=0;
        if(it.key()=="alignment"||it.key()=="renderMode"){p["min"]=0;p["max"]=2;}
        if(it.key()=="scaleMode"){p["min"]=0;p["max"]=1;}
    }
    return CommandResult::Ok(descriptor);
}
if(request.command=="component.get") {
    auto* entity=entityByArgument();auto name=request.arguments.at("component").get<std::string>();
    if(ComponentSchema(name).is_null())return CommandResult::Fail("MIPSYNC_COMPONENT_UNKNOWN","Unsupported component name.");
    const auto serializedScene=sceneJson(scene);
    for(const auto& e:serializedScene["entities"])if(e["id"]==entity->GetID()) {
        const auto key=componentKey(name);
        if(!e.contains(key))return CommandResult::Fail("MIPSYNC_COMPONENT_MISSING","Component is not attached.");
        auto data=e[key];
        if(data.is_array()){auto i=request.arguments.value("index",int64_t{0});if(i<0||i>=static_cast<int64_t>(data.size()))return CommandResult::Fail("MIPSYNC_COMPONENT_INDEX","Index is out of range.");data=data.at(i);}
        return CommandResult::Ok({{"entity",entity->GetID()},{"component",name},{"properties",data}});
    }
}
if(request.command=="ui.rect"||request.command=="ui.text"||request.command=="ui.image"||request.command=="canvas.set"||
   request.command=="animator.set"||request.command=="skinned-mesh.set"||request.command=="component.set") {
    CommandResult blocked;if(!RequireEditMode(editor,blocked))return blocked;
    auto* entity=entityByArgument();
    const std::string type=request.command=="component.set"?request.arguments.at("component").get<std::string>():
        request.command=="animator.set"?"Animator":request.command=="skinned-mesh.set"?"SkinnedMeshRenderer":
        request.command=="ui.rect"?"RectTransform":request.command=="ui.text"?"UIText":request.command=="ui.image"?"UIImage":"Canvas";
    const auto& properties=request.arguments.at("properties");
    if(!properties.is_object())return CommandResult::Fail("MIPSYNC_PROPERTIES","properties must be an object.");
    const auto schema=ComponentSchema(type);
    if(schema.is_null()||schema["properties"].empty())return CommandResult::Fail("MIPSYNC_COMPONENT_UNKNOWN","This component has no typed setter; use component.describe for supported properties.");
    for(auto it=properties.begin();it!=properties.end();++it) {
        if(!schema["properties"].contains(it.key()))return CommandResult::Fail("MIPSYNC_PROPERTY_UNKNOWN","Unknown property: "+it.key());
        const auto& def=schema["properties"][it.key()]["default"];const auto& value=it.value();
        bool valid=def.is_object()?value.is_object():def.is_array()?value.is_array()&&value.size()==def.size():def.is_boolean()?value.is_boolean():def.is_string()?value.is_string():def.is_number_integer()?value.is_number_integer():value.is_number();
        if(schema["properties"][it.key()].contains("enum")) {
            const auto& choices=schema["properties"][it.key()]["enum"];
            valid &= std::find(choices.begin(),choices.end(),value)!=choices.end();
        }
        auto numberValid=[&](const Json& v){if(!v.is_number()||!std::isfinite(v.get<double>()))return false;double n=v.get<double>();
            if(it.key()=="color"||it.key()=="anchorMin"||it.key()=="anchorMax"||it.key()=="pivot"||it.key()=="matchWidthOrHeight")return n>=0&&n<=1;
            if(it.key()=="fontSize"||it.key()=="planeDistance")return n>0;
            if(it.key()=="sizeDelta"||it.key()=="referenceResolution")return n>=0;
            if(it.key()=="alignment"||it.key()=="renderMode")return n>=0&&n<=2;
            if(it.key()=="scaleMode")return n>=0&&n<=1;
            return true;};
        if(value.is_array())for(const auto& v:value)valid&=numberValid(v);
        else if(value.is_number())valid&=numberValid(value);
        if(it.key()=="meshPart")valid &= value.is_number_integer() && value.get<int64_t>()>=-1;
        if(it.key()=="animationFps"||it.key()=="ps1VertexAnimFps")valid &= value.is_number() && value.get<double>()>0 && value.get<double>()<=60;
        if(it.key()=="ps1VertexAnimMaxFrames"||it.key()=="ps1VertexAnimTargetTris"||it.key()=="ps1VertexAnimTargetVerts"||it.key()=="ps1VertexAnimMaxSourceMB")valid &= value.is_number() && value.get<double>()>0;
        if(value.is_object())for(const auto& v:value)valid &= it.key()=="boolParams"?v.is_boolean():v.is_number()&&std::isfinite(v.get<double>());
        if(!valid)return CommandResult::Fail("MIPSYNC_PROPERTY_VALUE","Invalid type, size or range for "+it.key());
        if(it.key()=="texture"&&!value.get<std::string>().empty()) {
            auto path=projectPath(value.get<std::string>());
            if(!std::filesystem::is_regular_file(path))return CommandResult::Fail("MIPSYNC_ASSET_MISSING","Texture does not exist.");
        }
        if((it.key()=="model"||it.key()=="mesh"||it.key()=="controller"||it.key()=="material")&&!value.get<std::string>().empty()) {
            auto path=projectPath(value.get<std::string>(),it.key()=="controller"?".ncontroller":it.key()=="material"?".nmat":"");
            if(!std::filesystem::is_regular_file(path))return CommandResult::Fail("MIPSYNC_ASSET_MISSING","Asset does not exist: "+value.get<std::string>());
        }
    }
    auto data=sceneJson(scene);
    for(auto& e:data["entities"])if(e["id"]==entity->GetID()) {
        const auto key=componentKey(type);
        if(!e.contains(key))e[key]=Json::object();
        e[key].update(properties);
        if((type.starts_with("UI")||type=="Canvas")&&!e.contains("rectTransform"))e["rectTransform"]=Json::object();
    }
    Scene candidate;std::string error;
    if(type=="SkinnedMeshRenderer") {
        const auto& props=properties;
        auto* skinned=entity->GetComponent<SkinnedMeshRendererComponent>();
        const std::string modelPath=props.value("model",skinned?skinned->modelPath:std::string{});
        if(!modelPath.empty()) {
            auto model=AssetManager::Get().GetSkeletalModel(modelPath);
            const int part=props.value("meshPart",skinned?skinned->meshPartIndex:-1);
            if(!model||part>=static_cast<int>(model->meshParts.size()))return CommandResult::Fail("MIPSYNC_MESH_PART","Model is invalid or meshPart is out of range; use model.inspect.");
        }
    }
    if(type=="Animator")for(const auto& e:data["entities"])if(e["id"]==entity->GetID()) {
        const auto& settings=e["animator"];const auto controller=settings.value("controller",std::string{});
        if(!controller.empty()) {
            Json document;std::ifstream file(projectPath(controller,".ncontroller"));file>>document;
            auto diagnostics=ValidateControllerDocument(document,[&](const std::string& path){
                if(!std::filesystem::is_regular_file(projectPath(path)))throw std::runtime_error("Model does not exist.");
            });
            const auto state=settings.value("state",std::string{});
            if(!state.empty()&&std::none_of(document["states"].begin(),document["states"].end(),[&](const auto& s){return s.value("name",std::string{})==state;}))
                diagnostics.push_back({{"path","/state"},{"message","State does not exist in the controller."}});
            if(!diagnostics.empty()) {
                auto result=CommandResult::Fail("MIPSYNC_CONTROLLER_INVALID","Animator references an invalid controller or state.");
                result.value={{"diagnostics",diagnostics}};return result;
            }
        }
    }
    if(!SceneIO::LoadFromJsonString(candidate,data.dump(),error))return CommandResult::Fail("MIPSYNC_COMPONENT_VALIDATE",error);
    const auto id=entity->GetID();editor.RecordUndoSnapshot();scene=std::move(candidate);editor.AfterSceneRestoredFromHistory(false);editor.CommandMarkSceneDirty();
    return CommandResult::Ok({{"entity",id},{"component",type},{"properties",properties}});
}
if(request.command=="script.attach"||request.command=="script.fields") {
    CommandResult blocked;if(!RequireEditMode(editor,blocked))return blocked;
    auto* entity=entityByArgument();MipsScriptComponent candidate;MipsScriptComponent* target=nullptr;
    if(request.command=="script.attach") {
        auto path=projectPath(request.arguments.at("file").get<std::string>(),".mips");
        candidate.scriptPath=PathUtf8::ToString(path.lexically_relative(PathUtf8::FromString(m_Engine.GetProjectPath())));
        for(auto* s:entity->GetComponents<MipsScriptComponent>())if(s->scriptPath==candidate.scriptPath)return CommandResult::Fail("MIPSYNC_SCRIPT_EXISTS","Script is already attached; use script.fields.");
    } else {
        auto scripts=entity->GetComponents<MipsScriptComponent>();auto index=request.arguments.value("index",int64_t{0});
        if(index<0||index>=static_cast<int64_t>(scripts.size()))return CommandResult::Fail("MIPSYNC_SCRIPT_INDEX","Script index is out of range.");
        target=scripts.at(index);candidate.scriptPath=target->scriptPath;candidate.module=target->module;candidate.fieldValues=target->fieldValues;candidate.fieldAssetPaths=target->fieldAssetPaths;
    }
    std::vector<std::string> errors;
    if(!Mips::MipsRuntime::EnsureScriptReady(candidate,errors))return CommandResult::Fail("MIPSYNC_SCRIPT_COMPILE",errors.empty()?"Script could not be compiled.":errors.front());
    const Json fields=request.arguments.value("fields",Json::object());
    if(!fields.is_object())return CommandResult::Fail("MIPSYNC_SCRIPT_FIELDS","fields must be an object.");
    for(auto it=fields.begin();it!=fields.end();++it) {
        int index=candidate.module->FindFieldIndex(it.key());if(index<0)return CommandResult::Fail("MIPSYNC_FIELD_UNKNOWN","Unknown field: "+it.key());
        const auto& field=candidate.module->fields[index];
        if(!field.hasSetter)return CommandResult::Fail("MIPSYNC_FIELD_READONLY",it.key()+" is read-only.");
        if(field.valueKind==Mips::FieldValueKind::Array)return CommandResult::Fail("MIPSYNC_FIELD_ARRAY","Serialized array overrides are not supported; initialize arrays in Start.");
        if(field.valueKind==Mips::FieldValueKind::AudioClip) {
            if(!it.value().is_string())return CommandResult::Fail("MIPSYNC_FIELD_TYPE","AudioClip fields require asset path strings.");
            const auto value=it.value().get<std::string>();if(!value.empty()&&!std::filesystem::is_regular_file(projectPath(value)))return CommandResult::Fail("MIPSYNC_ASSET_MISSING","AudioClip asset does not exist.");
            candidate.fieldAssetPaths[index]=value;
        } else {
            if(field.valueKind==Mips::FieldValueKind::Bool) {
                if(!it.value().is_boolean())return CommandResult::Fail("MIPSYNC_FIELD_TYPE",it.key()+" requires a boolean.");
                candidate.fieldValues[index]=it.value().get<bool>()?1:0;
            } else {
                if(!it.value().is_number()||!std::isfinite(it.value().get<double>()))return CommandResult::Fail("MIPSYNC_FIELD_TYPE",it.key()+" requires a finite number.");
                candidate.fieldValues[index]=it.value().get<double>();
            }
        }
    }
    editor.RecordUndoSnapshot();if(!target)target=&entity->AddComponent<MipsScriptComponent>();
    target->scriptPath=candidate.scriptPath;target->module=candidate.module;target->fieldValues=candidate.fieldValues;target->fieldAssetPaths=candidate.fieldAssetPaths;editor.CommandMarkSceneDirty();
    return CommandResult::Ok({{"entity",entity->GetID()},{"path",target->scriptPath},{"fields",fields}});
}
if(request.command=="script.reload") {
    std::vector<std::string> errors;
    const auto file=request.arguments.value("file",std::string{});
    if(!file.empty())projectPath(file,".mips");
    editor.RecordUndoSnapshot();
    if(!m_Engine.GetMipsRuntime().ReloadScripts(scene,file,errors))return CommandResult::Fail("MIPSYNC_SCRIPT_COMPILE",errors.empty()?"No matching scripts attached.":errors.front());
    return CommandResult::Ok({{"reloaded",true},{"file",file},{"playing",editor.IsPlaying()}});
}
if(request.command=="entity.order"||request.command=="entity.grid") {
    CommandResult blocked;if(!RequireEditMode(editor,blocked))return blocked;
    auto* entity=entityByArgument();auto* parent=scene.FindEntity(entity->GetParentID());
    if(request.command=="entity.order") {
        std::vector<Entity*> siblings;
        if(parent){for(auto id:parent->GetChildIDs())if(auto* e=scene.FindEntity(id))if(e!=entity)siblings.push_back(e);}
        else {for(const auto& e:scene.GetEntities())if(e->GetParentID()==0&&e.get()!=entity)siblings.push_back(e.get());}
        auto index=request.arguments.at("index").get<int64_t>();
        if(index<0||index>static_cast<int64_t>(siblings.size()))return CommandResult::Fail("MIPSYNC_ORDER_RANGE","index must be within the sibling list.");
        editor.RecordUndoSnapshot();scene.ReorderEntity(entity,parent,index==static_cast<int64_t>(siblings.size())?nullptr:siblings[index],false,false);editor.CommandMarkSceneDirty();
        return CommandResult::Ok({{"entity",entity->GetID()},{"index",index},{"parent",entity->GetParentID()}});
    }
    const auto rows=request.arguments.at("rows").get<int64_t>(),columns=request.arguments.at("columns").get<int64_t>();
    const double dx=request.arguments.value("dx",100.),dy=request.arguments.value("dy",-100.);
    if(rows<1||rows>4096||columns<1||columns>4096||rows*columns>4096||!std::isfinite(dx)||!std::isfinite(dy))return CommandResult::Fail("MIPSYNC_GRID_RANGE","Grid must contain 1..4096 cells and finite spacing.");
    size_t hierarchySize=0;std::vector<uint32_t> pending{entity->GetID()};std::set<uint32_t> visited;
    while(!pending.empty()){auto id=pending.back();pending.pop_back();if(!visited.insert(id).second)continue;if(auto* e=scene.FindEntity(id)){++hierarchySize;pending.insert(pending.end(),e->GetChildIDs().begin(),e->GetChildIDs().end());}}
    if(hierarchySize*static_cast<size_t>(rows*columns)>4096)return CommandResult::Fail("MIPSYNC_GRID_RANGE","Copies including descendants must contain at most 4096 entities.");
    const auto base=request.arguments.value("name",EntityName(*entity));
    const auto* rect=entity->GetComponent<RectTransformComponent>();const auto rectPosition=rect?rect->anchoredPosition:glm::vec2(0);
    const auto position=entity->GetComponent<TransformComponent>()->position;Json ids=Json::array();
    editor.RecordUndoSnapshot();
    for(int64_t r=0;r<rows;++r)for(int64_t c=0;c<columns;++c){
        auto* copy=scene.DuplicateEntity(*entity);if(!copy)throw std::runtime_error("Grid duplication failed.");
        copy->GetComponent<TagComponent>()->tag=base+" ["+std::to_string(r)+","+std::to_string(c)+"]";
        if(auto* t=copy->GetComponent<RectTransformComponent>())t->anchoredPosition=rectPosition+glm::vec2(c*dx,r*dy);
        else copy->GetComponent<TransformComponent>()->position=position+glm::vec3(c*dx,r*dy,0);
        scene.ReorderEntity(copy,parent,nullptr,false,false);ids.push_back(copy->GetID());
    }
    editor.CommandMarkSceneDirty();return CommandResult::Ok({{"ids",ids},{"rows",rows},{"columns",columns},{"count",ids.size()},{"order","row-major"}});
}
if(request.command=="batch.run") {
    CommandResult blocked;if(!RequireEditMode(editor,blocked))return blocked;
    if(request.arguments.contains("file")==request.arguments.contains("operations"))return CommandResult::Fail("MIPSYNC_BATCH_ARGUMENT","Supply exactly one of operations or file.");
    Json ops;
    if(request.arguments.contains("file")){std::ifstream file(projectPath(request.arguments.at("file").get<std::string>(),".json"));if(!file)return CommandResult::Fail("MIPSYNC_BATCH_FILE","Cannot open batch JSON.");file>>ops;}
    else ops=request.arguments.at("operations");
    if(!ops.is_array()||ops.empty()||ops.size()>4096)return CommandResult::Fail("MIPSYNC_BATCH_FORMAT","Expected 1..4096 operations.");
    const std::set<std::string> allowed={"entity.create","entity.duplicate","entity.set","entity.transform","entity.set-parent","entity.order","entity.grid","entity.delete","component.add","component.remove","component.set-enabled","component.set","animator.set","skinned-mesh.set","ui.rect","ui.text","ui.image","canvas.set","script.attach","script.fields","mesh.set","material.apply","audio.set","ui.button-group-sounds"};
    for(const auto& op:ops)if(!op.is_object()||!op.contains("command")||!op["command"].is_string()||!allowed.contains(op["command"].get<std::string>())||!op.value("arguments",Json::object()).is_object())return CommandResult::Fail("MIPSYNC_BATCH_COMMAND","Batch permits only scene authoring commands with argument objects.");
    const auto original=sceneJson(scene);const auto dirty=editor.IsSceneDirty();Json results=Json::array();editor.RecordUndoSnapshot();
    for(size_t i=0;i<ops.size();++i){CommandRequest sub=request;sub.command=ops[i]["command"];sub.arguments=ops[i].value("arguments",Json::object());auto result=m_Executor.Execute(sub,m_Context);
        if(!result.success){std::string error;if(!SceneIO::LoadFromJsonString(scene,original.dump(),error))throw std::runtime_error("Batch rollback failed: "+error);editor.AfterSceneRestoredFromHistory(false);if(dirty)editor.CommandMarkSceneDirty();result.value={{"rolledBack",true},{"failedIndex",i},{"command",sub.command}};return result;}
        results.push_back(result.value);
    }
    editor.CommandMarkSceneDirty();return CommandResult::Ok({{"results",results},{"count",results.size()}});
}
if(request.command=="runtime.fields") {
    if(!editor.IsPlaying())return CommandResult::Fail("MIPSYNC_RUNTIME_INACTIVE","Start Play first.");
    auto* entity=entityByArgument();auto scripts=entity->GetComponents<MipsScriptComponent>();auto index=request.arguments.value("index",int64_t{0});
    if(index<0||index>=static_cast<int64_t>(scripts.size()))return CommandResult::Fail("MIPSYNC_SCRIPT_INDEX","Script index is out of range.");
    return CommandResult::Ok(m_Engine.GetMipsRuntime().InspectFields(*scripts[index]));
}
if(request.command=="runtime.pause") {
    if(!editor.IsPlaying())return CommandResult::Fail("MIPSYNC_RUNTIME_INACTIVE","Start Play first.");
    editor.SetPlayPaused(request.arguments.value("paused",true));m_Engine.ClearCliSteps();return CommandResult::Ok({{"paused",editor.IsPaused()}});
}
if(request.command=="runtime.step") {
    if(!editor.IsPlaying()||!editor.IsPaused())return CommandResult::Fail("MIPSYNC_RUNTIME_PAUSE","runtime.step requires paused Play.");
    auto frames=request.arguments.value("frames",int64_t{1});auto delta=request.arguments.value("delta",1./60.);
    if(frames<1||frames>600||!std::isfinite(delta)||delta<=0||delta>1)return CommandResult::Fail("MIPSYNC_STEP_RANGE","frames must be 1..600 and delta in (0,1].");
    if(m_Engine.GetQueuedCliFrames())return CommandResult::Fail("MIPSYNC_FRAME_PENDING","Wait for queuedFrames to reach zero before changing input/stepping.");
    if(request.arguments.contains("buttons")||request.arguments.contains("axes")) {
        CommandRequest input=request;input.command="runtime.input";input.arguments=Json::object();
        for(const char* key:{"buttons","axes"})if(request.arguments.contains(key))input.arguments[key]=request.arguments[key];
        auto applied=ExecuteEditorCommand(input);if(!applied.success)return applied;
    }
    m_Engine.QueueCliSteps(static_cast<unsigned>(frames),static_cast<float>(delta));
    auto result=CommandResult::Ok({{"queuedFrames",m_Engine.GetQueuedCliFrames()},{"delta",delta}});result.status="pending";return result;
}
if(request.command=="runtime.input") {
    if(!editor.IsPlaying()||!editor.IsPaused())return CommandResult::Fail("MIPSYNC_RUNTIME_PAUSE","Input injection requires paused Play; consume it with runtime.step.");
    if(m_Engine.GetQueuedCliFrames())return CommandResult::Fail("MIPSYNC_FRAME_PENDING","Wait for queued frames before replacing injected input.");
    const Json buttons=request.arguments.value("buttons",Json::array()),axes=request.arguments.value("axes",Json::object());
    if(!buttons.is_array()||!axes.is_object())return CommandResult::Fail("MIPSYNC_INPUT_TYPE","buttons must be an array and axes an object.");
    uint16_t held=0;std::array<float,4> stick{};
    for(const auto& button:buttons){if(!button.is_string())return CommandResult::Fail("MIPSYNC_BUTTON","Expected PS1 button names.");int index=PS1ButtonIndex(button.get<std::string>());if(index<0)return CommandResult::Fail("MIPSYNC_BUTTON","Unknown PS1 button.");held|=1u<<index;}
    const std::array<std::string,4> names={"LeftStickX","LeftStickY","RightStickX","RightStickY"};
    for(auto it=axes.begin();it!=axes.end();++it){auto found=std::find(names.begin(),names.end(),it.key());if(found==names.end()||!it.value().is_number()||!std::isfinite(it.value().get<double>())||std::abs(it.value().get<double>())>1)return CommandResult::Fail("MIPSYNC_AXIS","Use PS1 stick names with finite values in -1..1.");stick[found-names.begin()]=it.value().get<float>();}
    Input::SetInjectedPS1Input(held,stick);return CommandResult::Ok({{"buttons",buttons},{"axes",axes}});
}
if(request.command=="runtime.capture") {
    const auto path=projectPath(request.arguments.at("file").get<std::string>(),".png");std::string error;
    if(!editor.CaptureGameView(PathUtf8::ToString(path),error))return CommandResult::Fail("MIPSYNC_CAPTURE",error);
    return CommandResult::Ok({{"file",PathUtf8::ToString(path)},{"captured",true}});
}
