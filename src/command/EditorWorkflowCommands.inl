// Included inside ExecuteEditorCommand, using the shared scene/path helpers.
if(request.command=="promodeler.describe")return CommandResult::Ok(ProModelerShapes());
if(request.command=="scene.summary") {
    Json counts=Json::object();RenderBounds bounds;
    for(const auto& entity:scene.GetEntities()) {
        for(const auto& name:ComponentNames(*entity))counts[name]=counts.value(name,0)+1;
        bounds.Add(EntityRenderBounds(scene,*entity));
    }
    return CommandResult::Ok({{"path",editor.GetSceneFilePath()},{"entityCount",scene.GetEntities().size()},
        {"dirty",editor.IsSceneDirty()},{"dirtyReason",editor.IsSceneDirty()?Json("scene_differs_from_saved_snapshot"):Json(nullptr)},
        {"playing",editor.IsPlaying()},{"componentCounts",counts},{"bounds",bounds.ToJson()}});
}
if(request.command=="entity.bounds") {
    auto* entity=entityByArgument();const bool world=request.arguments.value("world",true);
    return CommandResult::Ok({{"id",entity->GetID()},{"name",EntityName(*entity)},{"bounds",EntityRenderBounds(scene,*entity,world).ToJson(world)}});
}
if(request.command=="entity.overlap") {
    auto* entity=entityByArgument();std::string error;auto* other=FindEntity(scene,request.arguments.at("other").get<std::string>(),&error);
    if(!other)return CommandResult::Fail("MIPSYNC_ENTITY_NOT_FOUND",error);
    const auto a=EntityRenderBounds(scene,*entity),b=EntityRenderBounds(scene,*other);
    const bool overlap=a.valid&&b.valid&&glm::all(glm::lessThanEqual(a.min,b.max))&&glm::all(glm::lessThanEqual(b.min,a.max));
    return CommandResult::Ok({{"entity",entity->GetID()},{"other",other->GetID()},{"overlap",overlap},{"method","render-aabb"},{"bounds",{a.ToJson(),b.ToJson()}}});
}
if(request.command=="entity.find") {
    const auto component=Lower(request.arguments.value("component",std::string{}));
    const bool hasPoint=request.arguments.contains("bounds-contains");
    const auto point=hasPoint?ParseVector(request.arguments.at("bounds-contains").get<std::string>()):glm::vec3(0);
    Json entities=Json::array();
    for(const auto& e:scene.GetEntities()) {
        const auto names=ComponentNames(*e);
        if(!component.empty()&&std::none_of(names.begin(),names.end(),[&](const auto& name){return Lower(name)==component;}))continue;
        const auto bounds=EntityRenderBounds(scene,*e);if(hasPoint&&!bounds.Contains(point))continue;
        entities.push_back({{"id",e->GetID()},{"name",EntityName(*e)},{"bounds",bounds.ToJson()}});
    }
    return CommandResult::Ok({{"entities",entities},{"count",entities.size()}});
}
if(request.command=="scene.raycast") {
    const auto origin=ParseVector(request.arguments.at("origin").get<std::string>());
    auto direction=ParseVector(request.arguments.at("direction").get<std::string>());
    const auto maximum=request.arguments.value("max-distance",10000.0);
    if(glm::length(direction)<1e-8f||!std::isfinite(maximum)||maximum<=0||maximum>1e9)
        return CommandResult::Fail("MIPSYNC_RAY","direction must be nonzero and max-distance in (0,1e9].");
    direction=glm::normalize(direction);std::vector<Json> hits;
    for(const auto& e:scene.GetEntities()) {
        float distance=0;if(!BoundsRay(EntityRenderBounds(scene,*e),origin,direction,static_cast<float>(maximum),distance))continue;
        hits.push_back({{"id",e->GetID()},{"name",EntityName(*e)},{"distance",distance},{"point",Vec3(origin+direction*distance)}});
    }
    std::stable_sort(hits.begin(),hits.end(),[](const Json& a,const Json& b){return a["distance"].get<float>()<b["distance"].get<float>();});
    return CommandResult::Ok({{"hits",hits},{"count",hits.size()},{"method","render-aabb"}});
}
if(request.command=="material.list"||request.command=="asset.list") {
    const auto type=request.command=="material.list"?std::string("material"):Lower(request.arguments.value("type",std::string("all")));
    if(type!="material"&&type!="texture"&&type!="all")return CommandResult::Fail("MIPSYNC_ASSET_TYPE","type must be all, material or texture.");
    const auto root=PathUtf8::FromString(m_Engine.GetProjectPath());Json assets=Json::array();std::vector<std::string> paths;
    const auto assetRoot=root/"assets";
    if(std::filesystem::is_directory(assetRoot))for(const auto& entry:std::filesystem::recursive_directory_iterator(assetRoot)) {
        if(!entry.is_regular_file())continue;
        const auto ext=Lower(PathUtf8::ToString(entry.path().extension()));
        const bool material=ext==".nmat",texture=ext==".png"||ext==".jpg"||ext==".jpeg"||ext==".bmp"||ext==".tga"||ext==".dds"||ext==".psd"||ext==".hdr";
        if((type=="material"&&!material)||(type=="texture"&&!texture))continue;
        paths.push_back(PathUtf8::ToString(entry.path().lexically_relative(root)));
    }
    std::sort(paths.begin(),paths.end());for(const auto& path:paths)assets.push_back({{"path",path}});
    return CommandResult::Ok({{request.command=="material.list"?"materials":"assets",assets},{"count",assets.size()},{"type",type}});
}
if(request.command=="material.inspect") {
    std::string relative,error;if(!ResolveMaterialPath(request.arguments.at("path").get<std::string>(),relative,error))return CommandResult::Fail("MIPSYNC_MATERIAL_PATH",error);
    const auto path=projectPath(relative,".nmat");std::ifstream file(path,std::ios::binary);
    if(!file)return CommandResult::Fail("MIPSYNC_MATERIAL_FILE","Cannot open material.");
    Json material;file>>material;return CommandResult::Ok({{"path",relative},{"material",material}});
}
if(request.command=="promodeler.create"||request.command=="promodeler.set") {
    CommandResult blocked;if(!RequireEditMode(editor,blocked))return blocked;
    const bool create=request.command=="promodeler.create";
    Entity* entity=create?nullptr:entityByArgument();
    const auto* old=entity?entity->GetComponent<ProModelerComponent>():nullptr;
    if(!create&&!old)return CommandResult::Fail("MIPSYNC_COMPONENT_MISSING","Entity has no ProModeler.");
    const auto name=request.arguments.value("name",std::string{});
    if(create&&name.empty())return CommandResult::Fail("MIPSYNC_ENTITY_NAME","Name cannot be empty.");
    auto shape=Lower(request.arguments.value("shape",std::string(create?"box":"")));
    if(shape=="ramp")shape="wedge";
    const bool regenerate=create||request.arguments.contains("shape")||request.arguments.contains("size")||request.arguments.contains("sides")||request.arguments.contains("steps");
    if(shape.empty()&&old&&regenerate)shape=ProModelerShapes()["shapeEnum"].value(std::to_string(static_cast<int>(old->shape)),std::string("custom"));
    if(regenerate&&shape!="box"&&shape!="plane"&&shape!="wedge"&&shape!="stairs"&&shape!="cylinder"&&shape!="cone"&&shape!="pyramid")
        return CommandResult::Fail("MIPSYNC_SHAPE","Specify box, plane, wedge, stairs, cylinder, cone or pyramid. Custom geometry requires an explicit shape to regenerate.");
    const auto size=request.arguments.contains("size")?ParseVector(request.arguments.at("size").get<std::string>()):old?old->size:glm::vec3(1);
    const int sides=request.arguments.value("sides",old?old->sides:16),steps=request.arguments.value("steps",old?old->steps:4);
    if(glm::any(glm::lessThanEqual(size,glm::vec3(0)))||glm::any(glm::greaterThan(size,glm::vec3(1e6)))||sides<3||sides>64||steps<1||steps>32)
        return CommandResult::Fail("MIPSYNC_SHAPE_RANGE","Size must be in (0,1e6], sides 3..64 and steps 1..32.");
    const auto renderType=Lower(request.arguments.value("render-type",std::string("Lit")));
    if(renderType!="lit"&&renderType!="unlit")return CommandResult::Fail("MIPSYNC_RENDER_TYPE","render-type must be Lit or Unlit.");
    // Validate material and parent before touching scene/Undo.
    if(request.arguments.contains("material")) {
        const auto input=request.arguments.at("material").get<std::string>();
        if(Lower(input)!="none"&&Lower(input)!="null") {std::string relative,error;Material material;
            if(!ResolveMaterialPath(input,relative,error)||!Material::Load(AssetManager::Get().ToAbsolute(relative),material,error))return CommandResult::Fail("MIPSYNC_MATERIAL_APPLY",error);}
    }
    Entity* parent=nullptr;if(create&&request.arguments.contains("parent")) {
        std::string error;parent=FindEntity(scene,request.arguments.at("parent").get<std::string>(),&error);if(!parent)return CommandResult::Fail("MIPSYNC_ENTITY_NOT_FOUND",error);
    }
    editor.RecordUndoSnapshot();if(create)entity=scene.CreateEntity(name);
    auto* pm=entity->GetComponent<ProModelerComponent>();if(!pm)pm=&entity->AddComponent<ProModelerComponent>();
    auto* mr=entity->GetComponent<MeshRendererComponent>();if(!mr)mr=&entity->AddComponent<MeshRendererComponent>();
    if(regenerate) {
        pm->size=size;pm->sides=sides;pm->steps=steps;
        if(shape=="box")pm->ResetBox();else if(shape=="plane")pm->ResetPlane();else if(shape=="wedge")pm->ResetRamp();
        else if(shape=="stairs")pm->ResetStairs();else if(shape=="cylinder")pm->ResetCylinder();else pm->ResetCone(shape=="pyramid");
        pm->RebuildMesh(*mr);
    }
    if(request.arguments.contains("material")){std::string error;if(!ApplyMaterial(*entity,request.arguments.at("material").get<std::string>(),error))throw std::runtime_error(error);}
    if(create||request.arguments.contains("render-type"))mr->renderType=renderType=="unlit"?RendererType::Unlit:RendererType::Lit;
    if(parent)scene.SetParent(entity,parent);editor.CommandMarkSceneDirty();editor.CommandRevealEntity(entity->GetID(),create);
    auto value=DescribeEntity(*entity);value["shapeName"]=ProModelerShapes()["shapeEnum"].value(std::to_string(static_cast<int>(pm->shape)),std::string("unknown"));
    value["bounds"]=EntityRenderBounds(scene,*entity).ToJson();return CommandResult::Ok(value);
}
if(request.command=="scene.capture"||request.command=="entity.frame"||
    (request.command=="runtime.capture"&&(request.arguments.contains("position")||request.arguments.contains("target")))) {
    const bool frame=request.command=="entity.frame",runtime=request.command=="runtime.capture";
    if(!runtime){CommandResult blocked;if(!RequireEditMode(editor,blocked))return blocked;}
    Entity* entity=frame?entityByArgument():nullptr;
    if(frame&&!request.arguments.contains("capture")){editor.CommandRevealEntity(entity->GetID(),true);return CommandResult::Ok(DescribeEntity(*entity));}
    const auto file=request.arguments.at(frame?"capture":"file").get<std::string>();const auto path=projectPath(file,".png");
    const int width=request.arguments.value("width",960),height=request.arguments.value("height",720);
    if(width<16||width>4096||height<16||height>4096)return CommandResult::Fail("MIPSYNC_CAPTURE_SIZE","Width/height must be 16..4096.");
    auto view=Lower(request.arguments.value("view",std::string("scene")));
    const std::set<std::string> views={"scene","top","bottom","front","back","left","right","iso"};
    if(!views.contains(view))return CommandResult::Fail("MIPSYNC_CAPTURE_VIEW","Unknown view preset.");
    const auto projection=Lower(request.arguments.value("projection",std::string("perspective")));
    if(projection!="perspective"&&projection!="orthographic")return CommandResult::Fail("MIPSYNC_CAPTURE_PROJECTION","projection must be perspective or orthographic.");
    if(request.arguments.contains("position")&&!request.arguments.contains("target"))return CommandResult::Fail("MIPSYNC_CAPTURE_CAMERA","position requires target.");
    RenderBounds bounds;std::set<uint32_t> included;
    if(entity){included.insert(entity->GetID());bool added=true;while(added){added=false;for(const auto& e:scene.GetEntities())if(included.contains(e->GetParentID())&&included.insert(e->GetID()).second)added=true;}}
    for(const auto& e:scene.GetEntities())if(!entity||included.contains(e->GetID()))bounds.Add(EntityRenderBounds(scene,*e));
    const auto target=request.arguments.contains("target")?ParseVector(request.arguments.at("target").get<std::string>()):bounds.valid?(bounds.min+bounds.max)*0.5f:entity?scene.GetWorldPosition(*entity):glm::vec3(0);
    Camera camera=editor.CommandSceneCamera();const float aspect=static_cast<float>(width)/height;
    const float radius=bounds.valid?std::max(0.5f,glm::length(bounds.max-bounds.min)*0.5f):5.0f;
    const float fitRadius=radius*1.2f/std::min(1.0f,aspect);
    float orthoSize=request.arguments.value("ortho-size",static_cast<double>(fitRadius*2));
    if(!std::isfinite(orthoSize)||orthoSize<=0||orthoSize>1e9)return CommandResult::Fail("MIPSYNC_CAPTURE_SIZE","ortho-size must be positive and finite.");
    const bool custom=request.arguments.contains("position")||request.arguments.contains("target")||view!="scene"||frame;
    if(custom) {
        glm::vec3 offset(1,0.8f,1);
        if(view=="top")offset={0,1,0};else if(view=="bottom")offset={0,-1,0};else if(view=="front")offset={0,0,1};else if(view=="back")offset={0,0,-1};else if(view=="left")offset={-1,0,0};else if(view=="right")offset={1,0,0};
        const auto position=request.arguments.contains("position")?ParseVector(request.arguments.at("position").get<std::string>()):target+glm::normalize(offset)*fitRadius*2.5f;
        const auto delta=target-position;if(glm::length(delta)<1e-5f)return CommandResult::Fail("MIPSYNC_CAPTURE_CAMERA","Camera position and target must differ.");
        const auto forward=glm::normalize(delta);const auto up=std::abs(forward.y)>0.999f?glm::vec3(0,0,-1):glm::vec3(0,1,0);
        camera.SyncFromWorldMatrix(position,glm::inverse(glm::lookAt(position,target,up)));
    }
    const float farDistance=std::max(1000.0f,glm::length(camera.GetPosition()-target)+radius*4);
    if(projection=="orthographic")camera.SetOrthographic(orthoSize,aspect,0.01f,farDistance);else camera.SetPerspective(camera.fov,aspect,0.01f,farDistance);
    std::string error;if(!editor.CaptureSceneView(PathUtf8::ToString(path),camera,width,height,!runtime,error))return CommandResult::Fail("MIPSYNC_CAPTURE",error);
    return CommandResult::Ok({{"file",PathUtf8::ToString(path)},{"captured",true},{"width",width},{"height",height},{"position",Vec3(camera.GetPosition())},{"target",custom?Vec3(target):Vec3(camera.GetPosition()+camera.GetForward())},{"projection",projection}});
}
