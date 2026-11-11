// Included in RegisterCoreCommands, keeping CLI and editor descriptors identical.
commands.Register({"commands","List command IDs or short descriptions.","Use describe <id> for full parameter metadata.",
    {Param("path",ValueType::String,false,"Optional namespace."),Param("ids",ValueType::Boolean,false,"IDs only.",false),Param("summary",ValueType::Boolean,false,"Short descriptions (default).",true)},
    "command_list",ExecutionMode::Local,{SideEffect::ReadOnly},{"mipsync commands --ids --json"},
    [](const CommandRequest& r,CommandContext& c){Json items=Json::array();for(const auto* d:c.commands->Under(r.arguments.value("path",std::string{}))) {
        if(r.arguments.value("ids",false))items.push_back(d->id);else items.push_back({{"id",d->id},{"summary",d->summary}});
    }return CommandResult::Ok({{"commands",items},{"count",items.size()},{"outputOptions",{"--json","--fields <paths>","--select <paths>","--out <file>","--quiet","--max-depth <0..64>","--output-format compact|pretty"}}});}});
editorCommand("scene.summary","Summarize scene state, component counts and world render bounds without mesh buffers.",{},false);
auto cameraParameters=std::vector<ParameterDescriptor>{Param("file",ValueType::Path,true,"Project-relative PNG path."),
    Param("position",ValueType::String,false,"Camera x,y,z; requires target."),Param("target",ValueType::String,false,"Look-at x,y,z (default scene center)."),
    Param("view",ValueType::String,false,"scene, top, bottom, front, back, left, right or iso.","scene"),
    Param("projection",ValueType::String,false,"perspective or orthographic.","perspective"),Param("ortho-size",ValueType::Number,false,"Orthographic vertical extent; default fits bounds."),
    Param("width",ValueType::Integer,false,"PNG width, 16..4096.",960),Param("height",ValueType::Integer,false,"PNG height, 16..4096.",720)};
commands.Register({"scene.capture","Render the edit scene directly to PNG with an arbitrary camera.","No Play, save or scene/camera mutation required. Includes Scene View geometry without editor chrome.",cameraParameters,"capture",ExecutionMode::Editor,{SideEffect::FilesystemWrite},{"mipsync scene capture --file tools/top.png --view top --projection orthographic --json","mipsync scene capture --file tools/preview.png --position 8,8,8 --target 0,2,0 --json"},ForwardEditor});
cameraParameters.front()=Param("capture",ValueType::Path,false,"Optional project-relative PNG path.");
cameraParameters.insert(cameraParameters.begin(),Param("entity",ValueType::String,true,"Entity ID/name; bounds include descendants."));
editorCommand("entity.frame","Frame an entity hierarchy in Scene View, or capture it without changing the viewport.",cameraParameters,false);
editorCommand("entity.bounds","Read local or world render geometry AABB (not collider/triangle intersection).",{Param("entity",ValueType::String,true,"Entity ID/name."),Param("world",ValueType::Boolean,false,"Transform bounds through parent hierarchy.",true)},false);
editorCommand("entity.overlap","Test world render AABB overlap; conservative, not exact mesh/collider collision.",{Param("entity",ValueType::String,true,"First entity."),Param("other",ValueType::String,true,"Second entity.")},false);
editorCommand("entity.find","Filter by component and/or point contained in world render AABB.",{Param("component",ValueType::String,false,"Component name."),Param("bounds-contains",ValueType::String,false,"World point x,y,z.")},false);
editorCommand("scene.raycast","Intersect a normalized world ray with render AABBs, sorted nearest first; not triangle/collider tests.",{Param("origin",ValueType::String,true,"World x,y,z."),Param("direction",ValueType::String,true,"Nonzero x,y,z."),Param("max-distance",ValueType::Number,false,"Positive world distance.",10000.0)},false);
editorCommand("asset.list","List project assets, filtered by type.",{Param("type",ValueType::String,false,"all, material or texture.","all")},false);
editorCommand("material.list","List project .nmat assets.",{},false);
editorCommand("material.inspect","Read a project material document.",{Param("path",ValueType::Path,true,"Project-relative assets/*.nmat path.")},false);
auto shapeParameters=std::vector<ParameterDescriptor>{Param("shape",ValueType::String,false,"box, plane, wedge/ramp, stairs, cylinder, cone or pyramid."),
    Param("size",ValueType::String,false,"Positive dimensions x,y,z."),Param("sides",ValueType::Integer,false,"Radial segments 3..64."),
    Param("steps",ValueType::Integer,false,"Stair steps 1..32."),Param("material",ValueType::Path,false,"Project material, or none."),Param("render-type",ValueType::String,false,"Lit or Unlit.")};
auto createParameters=shapeParameters;createParameters.insert(createParameters.begin(),Param("name",ValueType::String,true,"New entity name."));
createParameters.push_back(Param("parent",ValueType::String,false,"Optional parent ID/name."));
editorCommand("promodeler.create","Create editable box/cylinder/cone/pyramid/wedge geometry with Undo.",createParameters);
shapeParameters.insert(shapeParameters.begin(),Param("entity",ValueType::String,true,"Entity ID/name. Regenerating geometry replaces face edits/material overrides."));
editorCommand("promodeler.set","Regenerate an existing ProModeler, or update material/render type without regeneration.",shapeParameters);
editorCommand("promodeler.describe","Describe shape enum names, dimensions and creation parameters.",{},false);
