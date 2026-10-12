// Public asset APIs. All paths use the project boundary above; mutations
// participate in the same scene/asset Undo transactions as the inspector.
if(request.command=="model.inspect"||request.command=="animation.list") {
    const auto path=request.arguments.at("model").get<std::string>();projectPath(path);
    auto model=AssetManager::Get().GetSkeletalModel(path);
    if(!model)return CommandResult::Fail("MIPSYNC_MODEL_LOAD","Could not load model: "+path);
    auto result=InspectModel(*model);result["model"]=path;
    if(request.command=="animation.list")result={{"model",path},{"clips",result["clips"]}};
    return CommandResult::Ok(result);
}
if(request.command=="runtime.animator") {
    if(!editor.IsPlaying())return CommandResult::Fail("MIPSYNC_RUNTIME_INACTIVE","Start Play first.");
    auto* entity=entityByArgument();auto* animator=entity->GetComponent<AnimatorComponent>();
    if(!animator)return CommandResult::Fail("MIPSYNC_COMPONENT_MISSING","Animator is not attached.");
    auto result=InspectAnimator(*animator);result["entity"]=entity->GetID();return CommandResult::Ok(result);
}
if(request.command.starts_with("animator.controller.")&&request.command!="animator.controller.schema") {
    const std::string relative=request.arguments.at("path").get<std::string>();
    const auto path=projectPath(relative,".ncontroller");
    Json document;
    if(request.command=="animator.controller.set")document=request.arguments.at("controller");
    else {
        std::ifstream input(path,std::ios::binary);
        if(!input)return CommandResult::Fail("MIPSYNC_ASSET_MISSING","Controller not found.");
        input>>document;
    }
    const bool inspect=request.command=="animator.controller.inspect",validate=request.command=="animator.controller.validate";
    if(!inspect&&!validate) {
        CommandResult blocked;if(!RequireEditMode(editor,blocked))return blocked;
        if(request.command=="animator.controller.state.set"||request.command=="animator.controller.parameter.set") {
            const bool state=request.command=="animator.controller.state.set";
            const char* key=state?"states":"parameters";const auto value=request.arguments.at(state?"state":"parameter");
            const auto name=value.at("name").get<std::string>();
            if(!document.contains(key))document[key]=Json::array();
            bool found=false;for(auto& existing:document[key])if(existing.value("name",std::string{})==name){existing=value;found=true;break;}
            if(!found)document[key].push_back(value);
            if(state&&request.arguments.value("default",false))document["defaultState"]=name;
        } else if(request.command=="animator.controller.transition.set") {
            if(!document.contains("transitions"))document["transitions"]=Json::array();
            auto& transitions=document["transitions"];const auto index=request.arguments.value("index",int64_t{-1});
            if(index < -1 || index>=static_cast<int64_t>(transitions.size()))return CommandResult::Fail("MIPSYNC_TRANSITION_INDEX","Use -1 to append or an existing transition index.");
            if(index==-1)transitions.push_back(request.arguments.at("transition"));else transitions.at(index)=request.arguments.at("transition");
        }
    }
    auto diagnostics=ValidateControllerDocument(document,[&](const std::string& file){
        if(!std::filesystem::is_regular_file(projectPath(file)))throw std::runtime_error("Clip model does not exist: "+file);
    });
    if(inspect)return CommandResult::Ok({{"path",relative},{"controller",document},{"valid",diagnostics.empty()},{"diagnostics",diagnostics},{"schema",AnimationControllerSchema()}});
    if(!diagnostics.empty()) {
        auto result=CommandResult::Fail("MIPSYNC_CONTROLLER_INVALID","Invalid controller; original asset was not changed.");
        result.value={{"valid",false},{"diagnostics",diagnostics}};return result;
    }
    if(validate)return CommandResult::Ok({{"valid",true},{"path",relative},{"stateCount",document["states"].size()},{"diagnostics",diagnostics}});
    if(!editor.RecordAssetUndoSnapshot(PathUtf8::ToString(path)))return CommandResult::Fail("MIPSYNC_ASSET_UNDO","Could not record asset Undo.");
    WriteJsonAsset(path,document);
    ReloadAnimatorsUsingController(scene,relative);
    return CommandResult::Ok({{"path",relative},{"controller",document},{"valid",true}});
}
if(request.command=="model.extract") {
    CommandResult blocked;if(!RequireEditMode(editor,blocked))return blocked;
    const std::string relative=request.arguments.at("path").get<std::string>(),source=request.arguments.at("model").get<std::string>();
    const auto path=projectPath(relative,".nmodel");projectPath(source);
    if(source==relative||std::filesystem::path(source).extension()==".nmodel")return CommandResult::Fail("MIPSYNC_MODEL_SOURCE","Extract from the original FBX, not another derived descriptor.");
    auto model=AssetManager::Get().GetSkeletalModel(source);if(!model)return CommandResult::Fail("MIPSYNC_MODEL_LOAD","Could not load source model.");
    const Json roots=request.arguments.value("bone-roots",Json::array()),parts=request.arguments.value("mesh-parts",Json::array());
    if(!roots.is_array()||!parts.is_array()||(roots.empty()&&parts.empty()))return CommandResult::Fail("MIPSYNC_MODEL_FILTER","Provide bone-roots and/or mesh-parts arrays.");
    for(const auto& root:roots) {
        if(!root.is_string()||std::none_of(model->bones.begin(),model->bones.end(),[&](const auto& bone){return bone.name==root.get<std::string>();}))
            return CommandResult::Fail("MIPSYNC_BONE_UNKNOWN","Unknown bone; use model.inspect for exact names.");
    }
    for(const auto& part:parts)if(!part.is_number_integer()||part.get<int>()<0||part.get<int>()>=static_cast<int>(model->meshParts.size()))return CommandResult::Fail("MIPSYNC_MESH_PART","Invalid mesh part.");
    Json descriptor={{"version",1},{"model",source},{"boneRoots",roots},{"meshParts",parts}};
    const auto partition=PartitionSkeletalModel(*model,roots.get<std::vector<std::string>>(),parts.get<std::vector<int>>());
    if(partition.indices.empty())return CommandResult::Fail("MIPSYNC_MODEL_FILTER","Filter selects no triangles; no asset was written.");
    if(!editor.RecordAssetUndoSnapshot(PathUtf8::ToString(path)))return CommandResult::Fail("MIPSYNC_ASSET_UNDO","Could not record Undo.");
    WriteJsonAsset(path,descriptor);AssetManager::Get().DropSkeletalModel(relative);
    auto derived=AssetManager::Get().GetSkeletalModel(relative);
    if(!derived)return CommandResult::Fail("MIPSYNC_MODEL_FILTER","Filter produced no triangles; undo to remove the descriptor.");
    auto result=InspectModel(*derived);result["path"]=relative;result["sourceModel"]=source;return CommandResult::Ok(result);
}
