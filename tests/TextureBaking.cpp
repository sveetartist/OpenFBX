#include <fbxsdk.h>
#include <stdexcept>
#include "editor/OpenFbxApp.cpp"

static void Check(bool value, const std::string& message)
{
    if (!value) throw std::runtime_error(message);
}

static void CreateBakeFixture(const std::filesystem::path& path)
{
    auto* manager = FbxManager::Create();
    manager->SetIOSettings(FbxIOSettings::Create(manager, IOSROOT));
    auto* scene = FbxScene::Create(manager, "bake");
    scene->GetGlobalSettings().SetSystemUnit(FbxSystemUnit::m);
    scene->GetGlobalSettings().SetAxisSystem(FbxAxisSystem::OpenGL);
    auto* node = FbxNode::Create(scene, "ThreeQuads");
    auto* mesh = FbxMesh::Create(scene, "ThreeQuads");
    mesh->InitControlPoints(12);
    for (int quad = 0; quad < 3; ++quad)
    {
        const double x = quad*2;
        mesh->GetControlPoints()[quad*4] = FbxVector4(x,0,0);
        mesh->GetControlPoints()[quad*4+1] = FbxVector4(x+1,0,0);
        mesh->GetControlPoints()[quad*4+2] = FbxVector4(x+1,1,0);
        mesh->GetControlPoints()[quad*4+3] = FbxVector4(x,1,0);
    }
    for (int m = 0; m < 2; ++m)
        node->AddMaterial(FbxSurfacePhong::Create(scene, m == 0 ? "First" : "Second"));
    auto* materials = mesh->CreateElementMaterial();
    materials->SetMappingMode(FbxLayerElement::eByPolygon);
    materials->SetReferenceMode(FbxLayerElement::eIndexToDirect);
    auto* normals = mesh->CreateElementNormal();
    normals->SetMappingMode(FbxLayerElement::eByControlPoint);
    normals->SetReferenceMode(FbxLayerElement::eDirect);
    for (int i = 0; i < 12; ++i) normals->GetDirectArray().Add(FbxVector4(0,0,1));
    for (int set = 0; set < 2; ++set)
    {
        auto* uv = mesh->CreateElementUV(set == 0 ? "Original" : "Detail");
        uv->SetMappingMode(FbxLayerElement::eByControlPoint);
        uv->SetReferenceMode(FbxLayerElement::eDirect);
        const double x = set ? .15 : -.15, y = set ? .1 : -.2;
        for (int quad = 0; quad < 3; ++quad)
        {
            const double left = set && quad == 2 ? x+.65 : x, right = set && quad == 2 ? x : x+.65;
            uv->GetDirectArray().Add(FbxVector2(left,y));
            uv->GetDirectArray().Add(FbxVector2(right,y));
            uv->GetDirectArray().Add(FbxVector2(right,y+.65));
            uv->GetDirectArray().Add(FbxVector2(left,y+.65));
        }
    }
    for (int quad = 0; quad < 3; ++quad)
    {
        const int m = quad == 2 ? 1 : 0;
        for (const auto corners : {std::array<int,3>{0,1,2}, std::array<int,3>{0,2,3}})
        {
            mesh->BeginPolygon(m);
            for (int c : corners) mesh->AddPolygon(quad*4+c);
            mesh->EndPolygon();
        }
    }
    node->SetNodeAttribute(mesh); scene->GetRootNode()->AddChild(node);
    // The same material on another mesh whose native channel order is reversed.
    auto* extraNode = FbxNode::Create(scene,"ReversedUvOrder");
    auto* extraMesh = FbxMesh::Create(scene,"ReversedUvOrder");
    extraMesh->InitControlPoints(3);
    extraMesh->GetControlPoints()[0] = FbxVector4(6,0,0);
    extraMesh->GetControlPoints()[1] = FbxVector4(7,0,0);
    extraMesh->GetControlPoints()[2] = FbxVector4(6,1,0);
    extraNode->AddMaterial(node->GetMaterial(1));
    auto* extraMaterials = extraMesh->CreateElementMaterial();
    extraMaterials->SetMappingMode(FbxLayerElement::eAllSame); extraMaterials->SetReferenceMode(FbxLayerElement::eIndexToDirect);
    extraMaterials->GetIndexArray().Add(0);
    auto* extraNormals = extraMesh->CreateElementNormal();
    extraNormals->SetMappingMode(FbxLayerElement::eByControlPoint); extraNormals->SetReferenceMode(FbxLayerElement::eDirect);
    for (int c = 0; c < 3; ++c) extraNormals->GetDirectArray().Add(FbxVector4(0,0,1));
    for (int set = 0; set < 2; ++set)
    {
        auto* uv = extraMesh->CreateElementUV(set == 0 ? "Detail" : "Original");
        uv->SetMappingMode(FbxLayerElement::eByControlPoint); uv->SetReferenceMode(FbxLayerElement::eDirect);
        const double offset = set == 0 ? .1 : .3;
        uv->GetDirectArray().Add(FbxVector2(offset,offset));
        uv->GetDirectArray().Add(FbxVector2(offset+.3,offset));
        uv->GetDirectArray().Add(FbxVector2(offset,offset+.3));
    }
    extraMesh->BeginPolygon(0); for (int c = 0; c < 3; ++c) extraMesh->AddPolygon(c); extraMesh->EndPolygon();
    extraNode->SetNodeAttribute(extraMesh); scene->GetRootNode()->AddChild(extraNode);
    auto* exporter = FbxExporter::Create(manager, "export");
    Check(exporter->Initialize(path.string().c_str(), -1, manager->GetIOSettings()) && exporter->Export(scene), "Cannot create bake fixture");
    manager->Destroy();
}

static void CheckBinding(const ModelTab& tab)
{
    for (int mesh = 0; mesh < tab.loaded.model.meshCount; ++mesh)
    {
        for (size_t local = 0; local < tab.loaded.meshGlobalVertexIndices[mesh].size(); ++local)
        {
            const size_t global = tab.loaded.meshGlobalVertexIndices[mesh][local];
            const int uv = GetMaterialVertexUvSetIndex(tab, tab.loaded.model.meshMaterial[mesh], static_cast<int>(global));
            Check(std::abs(tab.loaded.model.meshes[mesh].texcoords[local*2]-tab.loaded.uvSets[uv][global*2]) < 1e-6f &&
                  std::abs(tab.loaded.model.meshes[mesh].texcoords[local*2+1]-tab.loaded.uvSets[uv][global*2+1]) < 1e-6f,
                  "Viewport did not use the material's UV channel");
        }
    }
}

static void CheckStandardFbxBindings(const std::string& path, const std::string& uvName)
{
    auto* manager = FbxManager::Create(); manager->SetIOSettings(FbxIOSettings::Create(manager, IOSROOT));
    auto* scene = FbxScene::Create(manager, "inspect"); auto* importer = FbxImporter::Create(manager, "import");
    Check(importer->Initialize(path.c_str(), -1, manager->GetIOSettings()) && importer->Import(scene), "Cannot inspect baked FBX");
    Check(scene->GetSrcObjectCount<FbxFileTexture>() >= 9, "Baked maps missing from standard FBX texture objects");
    for (int i = 0; i < scene->GetSrcObjectCount<FbxFileTexture>(); ++i)
        Check(scene->GetSrcObject<FbxFileTexture>(i)->UVSet.Get() == FbxString(uvName.c_str()), "Standard FBX texture UVSet lost");
    bool found = false;
    for (int i = 0; i < scene->GetSrcObjectCount<FbxMesh>(); ++i)
    {
        auto* mesh = scene->GetSrcObject<FbxMesh>(i);
        std::unordered_set<std::string> names;
        for (int set = 0; set < mesh->GetElementUVCount(); ++set) names.insert(mesh->GetElementUV(set)->GetName());
        Check(names.count("Original") && names.count("Detail") && names.count(uvName), "FBX mesh did not retain the three named UV channels");
        found = true;
    }
    Check(found, "Export contains no meshes"); manager->Destroy();
}

static void CheckConservativeBakeCoverage()
{
    constexpr int resolution = 64;
    BakeMaterialInput material;
    BakeSourceImage source; source.width = source.height = 8;
    for (int y = 0; y < 8; ++y) for (int x = 0; x < 8; ++x)
        source.pixels.push_back(Color{static_cast<unsigned char>(20+x*20), static_cast<unsigned char>(30+y*20), 91,
                                     static_cast<unsigned char>(100+x*5)});
    material.images.push_back(source);
    UvTriangleSample triangle{};
    triangle.uv[0] = {.501f,.501f}; triangle.uv[1] = {.50101f,.501f}; triangle.uv[2] = {.501f,.50101f};
    material.target.push_back(triangle);
    material.transforms.push_back(BakeUvTransform{.1,.2,.00002,{.501f,.501f}});
    std::string error;
    std::vector<std::vector<Color>> images;
    Check(RasterBakeMaterial(material,resolution,2,images,error),"Subpixel triangle failed: " + error);
    const auto checkSample = [&](int x, int y) {
        std::array<double,2> sample{};
        Check(BakeTriangleTexelSample(material.target[0],x,y,resolution,sample),"Expected a nonempty triangle/texel intersection");
        const auto& transform = material.transforms[0];
        const auto expected = SampleBakeImage(source,transform.minU+(sample[0]-transform.origin.x)/transform.scale,
                                                    transform.minV+(sample[1]-transform.origin.y)/transform.scale);
        const auto actual = images[0][size_t(y)*resolution+x];
        Check(std::abs(actual.r-std::lround(expected[0])) <= 1 && std::abs(actual.g-std::lround(expected[1])) <= 1 &&
              actual.b == 91 && std::abs(actual.a-std::lround(expected[3])) <= 1,
              "Conservative coverage extrapolated source UVs or introduced black/alpha artifacts");
    };
    checkSample(32,32);
    const auto seed = images[0][32*resolution+32], gutter = images[0][32*resolution+31];
    Check(seed.r == gutter.r && seed.g == gutter.g && seed.b == gutter.b && seed.a == gutter.a,
          "Subpixel triangle did not receive matching gutter padding");
    Check(images[0][0].a == 0,"Conservative coverage filled unrelated empty texels");
    Check(images[0][30*resolution+30].r == seed.r && images[0][30*resolution+30].a == seed.a &&
          images[0][32*resolution+29].a == 0,"Two-pixel dilation must cover diagonals and stop at its selected radius");
    Check(RasterBakeMaterial(material,resolution,0,images,error),error);
    Check(images[0][32*resolution+31].a == 0 && images[0][31*resolution+31].a == 0 &&
          images[0][32*resolution+32].r == seed.r && images[0][32*resolution+32].a == seed.a,
          "Disabled dilation must leave empty texels transparent and preserve surface pixels");
    Check(RasterBakeMaterial(material,resolution,1,images,error),error);
    Check(images[0][31*resolution+31].b == seed.b && images[0][31*resolution+31].a == seed.a &&
          images[0][32*resolution+30].a == 0,"One-pixel dilation must preserve RGBA and stop after one pixel");
    // A long strip misses every regular y sample but still needs coverage along
    // its entire length, rather than a single seed stretched by the gutter pass.
    material.target[0].uv[0] = {.1f,.505f}; material.target[0].uv[1] = {.9f,.505f}; material.target[0].uv[2] = {.1f,.506f};
    material.transforms[0] = BakeUvTransform{0,0,1,{0,0}};
    Check(RasterBakeMaterial(material,resolution,2,images,error),"Thin strip failed: " + error);
    for (int x = 7; x <= 56; ++x) { checkSample(x,32); Check(images[0][31*resolution+x].a > 0,"Thin-strip gutter contains a hole"); }
    const auto forward = images;
    std::swap(material.target[0].uv[1],material.target[0].uv[2]);
    Check(RasterBakeMaterial(material,resolution,2,images,error),"Clockwise thin strip failed: " + error);
    for (size_t p = 0; p < images[0].size(); ++p)
        Check(images[0][p].r == forward[0][p].r && images[0][p].g == forward[0][p].g &&
              images[0][p].b == forward[0][p].b && images[0][p].a == forward[0][p].a,"Conservative coverage depends on triangle winding");
    triangle.uv[0] = {.5f,.5f}; triangle.uv[1] = {.75f,.5f}; triangle.uv[2] = {.5f,.75f};
    std::array<double,2> sample{};
    Check(!BakeTriangleTexelSample(triangle,31,32,resolution,sample) &&
          !BakeTriangleTexelSample(triangle,32,31,resolution,sample) &&
          BakeTriangleTexelSample(triangle,32,32,resolution,sample),"Pixel-boundary contact was mistaken for surface coverage");
}

static void CheckSharedTransferCoverage()
{
    ModelTab tab; tab.loaded.bindNormals = {0,0,1, 0,0,1, 0,0,1, 0,0,1, 0,0,1, 0,0,1};
    BakeMaterialInput material; material.directTransfer = true;
    UvTriangleSample a{}; a.vertexStart = 0;
    a.position[0] = {0,0,0}; a.position[1] = {1,0,0}; a.position[2] = {0,1,0};
    a.uv[0] = {.1f,.1f}; a.uv[1] = {.9f,.1f}; a.uv[2] = {.1f,.9f};
    auto b = a; b.vertexStart = 3; b.position[2] = {.7f,.7f,.1f}; b.uv[2] = {.66f,.66f};
    material.source = {a,b}; material.layout = material.source;
    for (auto& triangle : material.layout) for (auto& uv : triangle.uv) uv = {.1f+uv.x*.6f,.15f+uv.y*.6f};
    BakeSourceImage color; color.width = color.height = 32;
    for (int y = 0; y < 32; ++y) for (int x = 0; x < 32; ++x)
        color.pixels.push_back({static_cast<unsigned char>(20+x*5),static_cast<unsigned char>(30+y*4),91,static_cast<unsigned char>(100+x*3)});
    auto normal = color; normal.slot = int(PbrTextureSlot::Normal); normal.pixels.assign(1024,Color{78,162,239,117});
    material.images = {color,normal};
    std::string error;
    Check(PrepareTextureTransfer(tab,material,error),"Safe folded island was rejected: " + error);
    std::vector<std::vector<Color>> first, second;
    Check(RasterBakeMaterial(material,128,4,first,error),error);
    std::reverse(material.source.begin(),material.source.end()); std::reverse(material.layout.begin(),material.layout.end());
    Check(PrepareTextureTransfer(tab,material,error),error); Check(RasterBakeMaterial(material,128,4,second,error),error);
    for (size_t p = 0; p < first[0].size(); ++p)
    {
        const auto a = first[0][p], b = second[0][p];
        // Conservative edge samples may choose different points inside the same
        // texel. Interior overlap samples and constant normal data stay exact.
        Check(std::abs(int(a.r)-b.r) <= 3 && std::abs(int(a.g)-b.g) <= 3 && a.b == b.b && std::abs(int(a.a)-b.a) <= 3,
              "Shared coverage produced inconsistent source pixels");
        Check(std::memcmp(&first[1][p],&second[1][p],sizeof(Color)) == 0,"Shared coverage changed normal data or alpha");
        const Vector2 uv{(float(p%128)+.5f)/128,(float(p/128)+.5f)/128};
        const auto wa = BakeBarycentric(material.target[0],uv), wb = BakeBarycentric(material.target[1],uv);
        if (*std::min_element(wa.begin(),wa.end()) > .02 && *std::min_element(wb.begin(),wb.end()) > .02)
            Check(std::memcmp(&a,&b,sizeof(Color)) == 0,"Interior shared UV sampling depends on triangle order");
    }
    // An overlapping face pointing at different source pixels must still fail.
    for (auto& uv : material.source[0].uv) uv.x += .1f;
    material.images.resize(1);
    Check(!PrepareTextureTransfer(tab,material,error) && error.find("conflicting") != std::string::npos,
          "Conflicting overlap was allowed after admitting shared source mappings");
}

static void CheckTransferNormalOrientation()
{
    ModelTab tab; tab.loaded.bindNormals = {0,0,1, 0,0,1, 0,0,1};
    UvTriangleSample triangle{}; triangle.vertexStart = 0;
    triangle.position[0] = {0,0,0}; triangle.position[1] = {1,0,0}; triangle.position[2] = {0,1,0};
    triangle.uv[0] = {.1f,.1f}; triangle.uv[1] = {.7f,.1f}; triangle.uv[2] = {.1f,.7f};
    const Color original{178,208,239,117};
    for (int mode = 0; mode < 4; ++mode) for (bool directX : {false,true})
    {
        BakeMaterialInput material; material.directTransfer = true; material.normalDirectX = directX;
        material.source = {triangle}; material.layout = {triangle};
        for (auto& uv : material.layout[0].uv)
        {
            const float u = uv.x, v = uv.y;
            if (mode == 1) uv = {1-v,u}; // rotation
            if (mode == 2) uv = {1-u,v}; // mirrored tangent basis
            if (mode == 3) uv = {.1f+u*.55f+v*.25f,.1f+v*.8f}; // shear and nonuniform scale
        }
        BakeSourceImage image; image.slot = int(PbrTextureSlot::Normal); image.width = image.height = 8;
        image.pixels.assign(64,original); material.images = {image};
        std::string error; Check(PrepareTextureTransfer(tab,material,error),error);
        std::vector<std::vector<Color>> output; Check(RasterBakeMaterial(material,128,0,output,error),error);
        double u = 0, v = 0;
        for (const auto uv : material.target[0].uv) { u += uv.x/3.0; v += uv.y/3.0; }
        const auto actual = output[0][size_t(int(v*128))*128+int(u*128)];
        const auto decode = [&](Color c, Vector3 t, Vector3 b) {
            return Vector3Add(Vector3Add(Vector3Scale(t,c.r/127.5f-1),Vector3Scale(b,(c.g/127.5f-1)*(directX ? -1 : 1))),
                              Vector3{0,0,c.b/127.5f-1}); };
        // Derive target tangents independently from geometry and UV derivatives.
        const auto a = material.target[0].uv[0], b = material.target[0].uv[1], c = material.target[0].uv[2];
        const float determinant = (b.x-a.x)*(c.y-a.y)-(c.x-a.x)*(b.y-a.y);
        const Vector3 t = Vector3Normalize({(c.y-a.y)/determinant,-(b.y-a.y)/determinant,0});
        const Vector3 targetB = Vector3Scale(Vector3CrossProduct({0,0,1},t),determinant < 0 ? -1.0f : 1.0f);
        const auto expectedWorld = decode(original,{1,0,0},{0,1,0});
        Check(Vector3Distance(expectedWorld,decode(actual,t,targetB)) < .012f && actual.b == original.b && actual.a == original.a,
              "Normal transfer changed world direction, convention, Z, or alpha for rotation/mirroring/shear");
    }
}

int main(int argc, char** argv)
{
    try
    {
        CheckConservativeBakeCoverage();
        CheckTransferNormalOrientation();
        CheckSharedTransferCoverage();
        SetConfigFlags(FLAG_WINDOW_HIDDEN); InitWindow(800,600,"Texture baking regression"); SetTraceLogLevel(LOG_WARNING);
        const auto folder = std::filesystem::absolute("baking_test_outputs");
        std::filesystem::create_directories(folder);
        ModelTab tab; tab.path = (folder / "source.fbx").string(); CreateBakeFixture(tab.path);
        std::string error; Check(LoadFbxModel(tab.path, tab.loaded, error), error); EnsurePbrMaterialStates(tab);
        Check(tab.loaded.uvSets.size() == 2 && tab.pbrMaterials.size() == 2, "Fixture channel/material layout incorrect");
        Check(GetMaterialVertexUvSetIndex(tab,1,static_cast<int>(tab.loaded.bindVertices.size()/3)-1) == 1,
              "Auto UVs must follow native mesh channel order, rather than global channel order");
        tab.pbrMaterials[0].uvSetName = "Original"; tab.pbrMaterials[1].uvSetName = "Detail";
        tab.pbrMaterials[0].normalDirectX = true; tab.pbrMaterials[0].roughnessChannel = PackedChannel::A;
        tab.pbrMaterials[0].metallicChannel = PackedChannel::R; tab.pbrMaterials[0].aoChannel = PackedChannel::B;
        tab.pbrMaterials[0].opacityChannel = OpacityChannel::A;
        tab.pbrMaterials[1].workflow = MaterialWorkflow::SpecularGlossiness;
        for (int s = 0; s < static_cast<int>(PbrTextureSlot::Count); ++s)
        {
            const auto path = folder / ("source_map_"+std::to_string(s)+".png");
            Image image = GenImageColor(32,32,Color{78,162,239,117});
            if (s == static_cast<int>(PbrTextureSlot::Opacity)) ImageClearBackground(&image,WHITE);
            if (s == static_cast<int>(PbrTextureSlot::Diffuse))
                for (int y = 0; y < 32; ++y) for (int x = 0; x < 32; ++x)
                    ImageDrawPixel(&image,x,y,Color{static_cast<unsigned char>(40+x*4),static_cast<unsigned char>(20+y*5),91,static_cast<unsigned char>(160+x*2)});
            Check(ExportImage(image,path.string().c_str()), "Cannot export source map"); UnloadImage(image);
            for (int m = 0; m < 2; ++m) Check(LoadPbrTexture(tab,m,static_cast<PbrTextureSlot>(s),path.string(),error),error);
        }
        RefreshUvMeshBuffers(tab); RefreshDisplayedMesh(tab); CheckBinding(tab);
        // Save/reopen Auto without collapsing or reordering the original channels.
        tab.pbrMaterials[0].uvSetName.clear(); tab.pbrMaterials[1].uvSetName.clear(); tab.loaded.uvSetsEdited = true;
        RefreshUvMeshBuffers(tab); RefreshDisplayedMesh(tab); CheckBinding(tab);
        const auto autoPath = (folder / "auto.fbx").string(); Check(SaveEditedFbx(tab,autoPath,error),error);
        ModelTab autoReload; autoReload.path = autoPath; Check(LoadFbxModel(autoPath,autoReload.loaded,error),error); EnsurePbrMaterialStates(autoReload);
        Check(LoadImportedPbrTextures(autoReload,error) == 18,error);
        Check(autoReload.pbrMaterials[0].uvSetName.empty() && autoReload.pbrMaterials[1].uvSetName.empty() && autoReload.loaded.uvSets.size() == 2 &&
              GetMaterialVertexUvSetIndex(autoReload,1,static_cast<int>(autoReload.loaded.bindVertices.size()/3)-1) == 1,
              "Auto material binding or native UV order was lost on export"); CheckBinding(autoReload);
        UnloadPbrTextures(autoReload); UnloadFbxModel(autoReload.loaded);
        tab.pbrMaterials[0].uvSetName = "Original"; tab.pbrMaterials[1].uvSetName = "Detail";
        RefreshUvMeshBuffers(tab); RefreshDisplayedMesh(tab);
        const LitShader lit = LoadBasicLitShader(); Check(lit.valid,"Bake preview shader failed"); ApplyLitShader(tab.loaded,lit);
        const Vector3 view{3.5f,.5f,7}, light{0,0,-1}; const Vector4 white{1,1,1,1}, ambient{.2f,.2f,.2f,1};
        SetShaderValue(lit.shader,lit.viewPositionLoc,&view,SHADER_UNIFORM_VEC3);
        SetShaderValue(lit.shader,lit.lightDirectionLoc,&light,SHADER_UNIFORM_VEC3);
        SetShaderValue(lit.shader,lit.lightColorLoc,&white,SHADER_UNIFORM_VEC4);
        SetShaderValue(lit.shader,lit.ambientLoc,&ambient,SHADER_UNIFORM_VEC4);
        const Camera3D camera{view,Vector3{3.5f,.5f,0},Vector3{0,1,0},2,CAMERA_ORTHOGRAPHIC};
        const auto preview = LoadRenderTexture(800,200);
        auto render = [&](MaterialPreviewMode mode)
        {
            BeginTextureMode(preview); ClearBackground(MAGENTA); BeginMode3D(camera);
            DrawMaterialModel(tab,lit,mode,true); EndMode3D(); EndTextureMode();
            return LoadImageFromTexture(preview.texture);
        };
        std::array<Image,3> originals{render(MaterialPreviewMode::Diffuse),render(MaterialPreviewMode::Normal),render(MaterialPreviewMode::Shaded)};
        const auto originalUvs = tab.loaded.uvSets;
        const auto originalDiffuse = tab.pbrMaterials[0].textures[0].resource;
        std::vector<std::vector<float>> oldTangents;
        for (int m = 0; m < tab.loaded.model.meshCount; ++m)
        {
            const auto& mesh = tab.loaded.model.meshes[m];
            oldTangents.emplace_back(mesh.tangents,mesh.tangents+mesh.vertexCount*4);
        }
        // Deletion must update GPU sampling and survive FBX export, including an
        // empty UV list. Undo restores live textures and their original bindings.
        {
            Check(DeleteUvSet(tab,1) && tab.pbrMaterials[1].uvSetName.empty(),"Deleting bound channel did not reset material UVs");
            Check(tab.loaded.uvSets == std::vector<std::vector<float>>{originalUvs[0]} &&
                  tab.pbrMaterials[0].textures[0].resource == originalDiffuse,"UV deletion changed source UVs or texture resources");
            CheckBinding(tab);
            const auto deletedPath = (folder / "deleted_channel.fbx").string(); Check(SaveEditedFbx(tab,deletedPath,error),error);
            ModelTab deletedReload; deletedReload.path = deletedPath;
            Check(LoadFbxModel(deletedPath,deletedReload.loaded,error),error); EnsurePbrMaterialStates(deletedReload);
            Check(LoadImportedPbrTextures(deletedReload,error) == 18,error);
            Check(deletedReload.loaded.uvSetNames == std::vector<std::string>{"Original"} &&
                  deletedReload.pbrMaterials[1].uvSetName.empty(),"Deleted UV channel or stale material binding returned on reload");
            CheckBinding(deletedReload); UnloadPbrTextures(deletedReload); UnloadFbxModel(deletedReload.loaded);
            Check(DeleteUvSet(tab,0) && tab.loaded.uvSets.empty(),"Could not delete last UV channel");
            for (int m = 0; m < tab.loaded.model.meshCount; ++m)
                for (int v = 0; v < tab.loaded.model.meshes[m].vertexCount*2; ++v)
                    Check(tab.loaded.model.meshes[m].texcoords[v] == 0,"Last UV deletion left stale GPU texture coordinates");
            const auto emptyPath = (folder / "no_channels.fbx").string(); Check(SaveEditedFbx(tab,emptyPath,error),error);
            ModelTab emptyReload; emptyReload.path = emptyPath;
            Check(LoadFbxModel(emptyPath,emptyReload.loaded,error),error);
            Check(emptyReload.loaded.uvSets.empty(),"Last UV deletion did not survive FBX export"); UnloadFbxModel(emptyReload.loaded);
            Check(UndoEdit(tab) && UndoEdit(tab) && tab.loaded.uvSets == originalUvs &&
                  tab.pbrMaterials[0].uvSetName == "Original" && tab.pbrMaterials[1].uvSetName == "Detail" &&
                  tab.pbrMaterials[0].textures[0].resource == originalDiffuse,"Undo did not restore deleted UV channels and resources");
            CheckBinding(tab);
            Check(RedoEdit(tab) && tab.loaded.uvSets.size() == 1 && UndoEdit(tab),"Deletion redo did not restore render UVs"); CheckBinding(tab);
        }
        // Transfer into a rotated existing target, preserving every UV channel.
        // The viewport comparison also checks DirectX and mirrored tangent normals.
        {
            UvRepackReport report; Check(RepackUvSetIslands(tab,0,&report),report.error);
            const int target = report.uvSet;
            auto& targetUvs = tab.loaded.uvSets[target];
            for (size_t v = 0; v < targetUvs.size(); v += 2)
            { const float u = targetUvs[v], y = targetUvs[v+1]; targetUvs[v] = 1-y; targetUvs[v+1] = u; }
            const auto allUvs = tab.loaded.uvSets;
            const auto allPresence = tab.loaded.uvSetPresence;
            const auto defaults = tab.loaded.defaultUvSetIndices; const auto undoCount = tab.undoStack.size();
            TextureBakeOptions transfer; transfer.resolution = 512; transfer.dilation = 4;
            transfer.targetUvSet = target; transfer.parentDirectory = folder;
            TextureBakeInput transferInput;
            Check(BuildTextureBakeInput(tab,transfer,transferInput,error),error);
            Check(transferInput.targetUvs == allUvs[target] && transferInput.targetName == report.uvSetName &&
                  transferInput.materials[0].sourceUv == "Original" && transferInput.materials[1].sourceUv == "Detail",
                  "Transfer changed target coordinates or ignored material source bindings");
            const auto transferPlan = transferInput;
            transfer.sourceUvSet = 1; Check(BuildTextureBakeInput(tab,transfer,transferInput,error),error);
            Check(transferInput.materials[0].sourceUv == "Detail" &&
                  transferInput.materials[0].source[0].uv[0].x == originalUvs[1][0],"Explicit source UV selection was ignored");
            transfer.sourceUvSet = -1;
            auto invalid = transfer; invalid.targetUvSet = -1;
            Check(!BuildTextureBakeInput(tab,invalid,transferInput,error),"Transfer accepted an absent target selection");
            invalid = transfer; invalid.sourceUvSet = 99;
            Check(!BuildTextureBakeInput(tab,invalid,transferInput,error),"Transfer accepted an invalid source selection");
            const auto validTarget = targetUvs;
            tab.loaded.uvSetPresence[target][0] = 0;
            Check(!BuildTextureBakeInput(tab,transfer,transferInput,error),"Transfer silently filled missing target UVs");
            tab.loaded.uvSetPresence[target][0] = 1;
            targetUvs[0] = -0.1f;
            Check(!BuildTextureBakeInput(tab,transfer,transferInput,error),"Transfer silently clipped out-of-tile target UVs"); targetUvs = validTarget;
            targetUvs[2] = targetUvs[0]; targetUvs[3] = targetUvs[1];
            Check(!BuildTextureBakeInput(tab,transfer,transferInput,error),"Transfer accepted a collapsed target triangle"); targetUvs = validTarget;
            for (int c = 0; c < 6; ++c) targetUvs[12+c] = targetUvs[c];
            Check(!BuildTextureBakeInput(tab,transfer,transferInput,error) && error.find("overlap") != std::string::npos,
                  "Transfer accepted overlapping target triangles"); targetUvs = validTarget;
            Check(tab.undoStack.size() == undoCount && tab.loaded.uvSets == allUvs,"Validation edited UVs or undo history");
            auto transferred = RunTextureBake(transferPlan); Check(transferred.error.empty(),transferred.error);
            Check(transferred.maps.size() == 18,"Transfer omitted inactive maps or packed channels");
            for (const auto& material : transferPlan.materials)
            {
                const auto map = std::find_if(transferred.maps.begin(),transferred.maps.end(),[&](const BakeOutputMap& m) {
                    return m.material == material.material && m.slot == int(PbrTextureSlot::Diffuse); });
                Image image = LoadImage(map->path.c_str()); Color* colors = LoadImageColors(image);
                BakeSourceImage baked{0,image.width,image.height,{colors,colors+size_t(image.width)*image.height}};
                UnloadImageColors(colors); UnloadImage(image);
                for (size_t t = 0; t < material.target.size(); ++t)
                    for (const auto weights : {std::array<double,3>{.3,.3,.4},std::array<double,3>{.6,.2,.2}})
                    {
                        double u = 0,v = 0,tu = 0,tv = 0;
                        for (int c = 0; c < 3; ++c)
                        { u += weights[c]*material.source[t].uv[c].x; v += weights[c]*material.source[t].uv[c].y;
                          tu += weights[c]*material.target[t].uv[c].x; tv += weights[c]*material.target[t].uv[c].y; }
                        const auto expected = SampleBakeImage(material.images[0],u,v), actual = SampleBakeImage(baked,tu,tv);
                        for (int c = 0; c < 4; ++c) Check(std::abs(expected[c]-actual[c]) <= 3,"Texture transfer changed sampled color or alpha");
                    }
            }
            Check(ApplyTextureBake(tab,transferred,error),error);
            Check(tab.loaded.uvSets == allUvs && tab.loaded.uvSetPresence == allPresence && tab.loaded.defaultUvSetIndices == defaults &&
                  tab.selectedUvSet == target && tab.pbrMaterials[0].uvSetName == report.uvSetName && tab.pbrMaterials[1].uvSetName == report.uvSetName,
                  "Transfer edited UV channels or bound an incorrect target"); CheckBinding(tab);
            for (const auto mode : {MaterialPreviewMode::Diffuse,MaterialPreviewMode::Shaded})
            {
                Image after = render(mode); Color* a = LoadImageColors(originals[mode == MaterialPreviewMode::Diffuse ? 0 : 2]);
                Color* b = LoadImageColors(after); double difference = 0; int compared = 0;
                for (int p = 0; p < after.width*after.height; ++p)
                {
                    if (a[p].r == 255 && a[p].g == 0 && a[p].b == 255) continue;
                    difference += std::abs(int(a[p].r)-b[p].r)+std::abs(int(a[p].g)-b[p].g)+std::abs(int(a[p].b)-b[p].b); ++compared;
                }
                std::cout << "Transfer preview " << int(mode) << ": mean RGB difference " << difference/std::max(1,compared*3) << '\n';
                Check(compared > 10000 && difference/(compared*3) <= 4,"Rotated/mirrored UV transfer changed viewport color or tangent normals");
                UnloadImageColors(a); UnloadImageColors(b); UnloadImage(after);
            }
            const auto transferPath = (folder / "transferred.fbx").string(); Check(SaveEditedFbx(tab,transferPath,error),error);
            CheckStandardFbxBindings(transferPath,report.uvSetName);
            ModelTab reload; reload.path = transferPath; Check(LoadFbxModel(transferPath,reload.loaded,error),error); EnsurePbrMaterialStates(reload);
            Check(LoadImportedPbrTextures(reload,error) == 18,error);
            Check(reload.loaded.uvSetNames == tab.loaded.uvSetNames && reload.pbrMaterials[0].uvSetName == report.uvSetName &&
                  reload.pbrMaterials[1].uvSetName == report.uvSetName,"Transfer export lost target channel or texture bindings");
            for (size_t set = 0; set < allUvs.size(); ++set) for (size_t v = 0; v < allUvs[set].size(); ++v)
                Check(std::abs(allUvs[set][v]-reload.loaded.uvSets[set][v]) < 1e-5f,"Transfer export altered existing UV coordinates");
            CheckBinding(reload); UnloadPbrTextures(reload); UnloadFbxModel(reload.loaded);
            const auto transferredResource = tab.pbrMaterials[0].textures[0].resource;
            Check(UndoEdit(tab) && tab.loaded.uvSets == allUvs && tab.pbrMaterials[0].textures[0].resource == originalDiffuse &&
                  tab.pbrMaterials[0].uvSetName == "Original" && tab.pbrMaterials[1].uvSetName == "Detail","Transfer undo changed UVs or lost original maps");
            Check(RedoEdit(tab) && tab.loaded.uvSets == allUvs && tab.pbrMaterials[0].textures[0].resource == transferredResource,
                  "Transfer redo changed UVs or lost baked resources"); CheckBinding(tab);
            Check(UndoEdit(tab) && UndoEdit(tab) && tab.loaded.uvSets == originalUvs,"Could not undo transfer and preparation separately"); CheckBinding(tab);
        }
        // UV preparation creates separate channels and keeps the live material
        // sampling the original textures until its new maps have been baked.
        {
            UvRepackReport preparation;
            const auto originalPresence = tab.loaded.uvSetPresence;
            const auto originalDefaultSets = tab.loaded.defaultUvSetIndices;
            Check(AverageUvSetIslands(tab,0,&preparation),preparation.error);
            const int averaged = preparation.uvSet;
            Check(RepackUvSetIslands(tab,averaged,&preparation),preparation.error);
            const int prepared = preparation.uvSet;
            Check(tab.loaded.uvSets.size() == 4 && tab.loaded.uvSets[0] == originalUvs[0] && tab.loaded.uvSets[1] == originalUvs[1] &&
                  tab.loaded.uvSetPresence[prepared] == originalPresence[0] && tab.loaded.defaultUvSetIndices == originalDefaultSets &&
                  tab.pbrMaterials[0].uvSetName == "Original" && tab.pbrMaterials[1].uvSetName == "Detail" &&
                  tab.pbrMaterials[0].textures[0].resource == originalDiffuse,"UV preparation changed original UVs, material bindings, or textures");
            CheckBinding(tab);
            Image preparedView = render(MaterialPreviewMode::Diffuse);
            Color* originalColors = LoadImageColors(originals[0]); Color* preparedColors = LoadImageColors(preparedView);
            Check(std::memcmp(originalColors,preparedColors,size_t(preparedView.width)*preparedView.height*sizeof(Color)) == 0,
                  "Viewport texture appearance changed before rebaking");
            UnloadImageColors(originalColors); UnloadImageColors(preparedColors); UnloadImage(preparedView);
            const auto preparedPath = (folder / "prepared_channels.fbx").string(); Check(SaveEditedFbx(tab,preparedPath,error),error);
            ModelTab preparedReload; preparedReload.path = preparedPath;
            Check(LoadFbxModel(preparedPath,preparedReload.loaded,error),error); EnsurePbrMaterialStates(preparedReload);
            Check(LoadImportedPbrTextures(preparedReload,error) == 18,error);
            Check(preparedReload.loaded.uvSetNames == tab.loaded.uvSetNames && preparedReload.pbrMaterials[0].uvSetName == "Original" &&
                  preparedReload.pbrMaterials[1].uvSetName == "Detail","Prepared UV channels or original material bindings were lost on FBX reload");
            for (size_t set = 0; set < 2; ++set) for (size_t v = 0; v < originalUvs[set].size(); ++v)
                Check(std::abs(preparedReload.loaded.uvSets[set][v]-originalUvs[set][v]) < 1e-5f,"Prepared UV export changed an original channel");
            CheckBinding(preparedReload); UnloadPbrTextures(preparedReload); UnloadFbxModel(preparedReload.loaded);
            TextureBakeOptions preparedOptions; preparedOptions.repack = true; preparedOptions.resolution = 128; preparedOptions.padding = preparedOptions.dilation = 4;
            preparedOptions.material = 0; preparedOptions.layoutUvSet = prepared; preparedOptions.parentDirectory = folder;
            TextureBakeInput preparedInput; Check(BuildTextureBakeInput(tab,preparedOptions,preparedInput,error),error);
            Check(preparedInput.materials[0].sourceUv == "Original" && preparedInput.layoutName == tab.loaded.uvSetNames[prepared],
                  "Prepared bake did not keep source texture UVs separate from its layout UVs");
            const auto preparedPlan = preparedInput;
            auto preparedResult = RunTextureBake(std::move(preparedInput)); Check(preparedResult.error.empty(),preparedResult.error);
            const auto diffuseMap = std::find_if(preparedResult.maps.begin(),preparedResult.maps.end(),[](const BakeOutputMap& m) { return m.slot == 0; });
            Image bakedImage = LoadImage(diffuseMap->path.c_str()); Color* bakedColors = LoadImageColors(bakedImage);
            BakeSourceImage bakedSource{0,bakedImage.width,bakedImage.height,{bakedColors,bakedColors+size_t(bakedImage.width)*bakedImage.height}};
            UnloadImageColors(bakedColors); UnloadImage(bakedImage);
            const auto& material = preparedPlan.materials[0];
            for (size_t t = 0; t < material.source.size(); ++t)
            {
                double u = 0,v = 0,nu = 0,nv = 0; const double weights[3] = {.3,.3,.4};
                for (int c = 0; c < 3; ++c)
                { u += weights[c]*material.source[t].uv[c].x; v += weights[c]*material.source[t].uv[c].y;
                  nu += weights[c]*material.target[t].uv[c].x; nv += weights[c]*material.target[t].uv[c].y; }
                const auto expected = SampleBakeImage(material.images[0],u,v), actual = SampleBakeImage(bakedSource,nu,nv);
                for (int c = 0; c < 4; ++c) Check(std::abs(expected[c]-actual[c]) <= 3,"Prepared-layout bake sampled modified UVs instead of original texture UVs");
            }
            Check(ApplyTextureBake(tab,preparedResult,error),error);
            Check(tab.loaded.uvSets.size() == 5 && tab.loaded.uvSets[0] == originalUvs[0] && tab.pbrMaterials[1].uvSetName == "Detail",
                  "Prepared rebake changed original UVs or an unbaked material");
            Check(UndoEdit(tab) && tab.loaded.uvSets.size() == 4 && tab.pbrMaterials[0].uvSetName == "Original" &&
                  tab.pbrMaterials[0].textures[0].resource == originalDiffuse,"Prepared rebake undo did not restore source material bindings/maps");
            Check(UndoEdit(tab) && tab.loaded.uvSets.size() == 3 && UndoEdit(tab) && tab.loaded.uvSets == originalUvs,
                  "UV preparation did not undo as separate channel creations"); CheckBinding(tab);
        }
        TextureBakeOptions options; options.repack = true; options.resolution = 128; options.padding = 4; options.dilation = 4; options.parentDirectory = folder;
        TextureBakeInput input;
        options.dilation = -1; Check(!BuildTextureBakeInput(tab,options,input,error),"Negative dilation accepted");
        options.dilation = 65; Check(!BuildTextureBakeInput(tab,options,input,error),"Excessive dilation accepted");
        options.dilation = 16; Check(BuildTextureBakeInput(tab,options,input,error),error);
        for (const auto& material : input.materials) for (const auto& triangle : material.target) for (const auto uv : triangle.uv)
            Check(uv.x >= 18.0f/128-1e-6f && uv.y >= 18.0f/128-1e-6f &&
                  uv.x <= 1-18.0f/128+1e-6f && uv.y <= 1-18.0f/128+1e-6f,"Packing did not reserve space for dilation larger than island padding");
        options.dilation = 4;
        Check(BuildTextureBakeInput(tab,options,input,error),error);
        Check(input.materials.size() == 2 && input.materials[0].source.size() == 4 && input.materials[1].source.size() == 3,
              "One mesh's per-face materials were not grouped correctly");
        const auto plan = input;
        auto result = RunTextureBake(std::move(input)); Check(result.error.empty(),result.error);
        std::ifstream manifest(result.directory / "bake.txt");
        const std::string manifestText((std::istreambuf_iterator<char>(manifest)), std::istreambuf_iterator<char>());
        Check(manifestText.find("Dilation: 4\n") != std::string::npos,"Bake manifest omitted the chosen dilation");
        Check(result.maps.size() == 18, "Bake must retain inactive workflow maps and packed RGBA slots");
        // Compare barycentric points sampled through the actual exported image.
        for (const auto& material : plan.materials)
        {
            const auto& source = material.images[static_cast<int>(PbrTextureSlot::Diffuse)];
            const auto map = std::find_if(result.maps.begin(),result.maps.end(),[&](const BakeOutputMap& m) { return m.material == material.material && m.slot == source.slot; });
            Image image = LoadImage(map->path.c_str()); Color* colors = LoadImageColors(image);
            BakeSourceImage baked{source.slot,image.width,image.height,{colors,colors+size_t(image.width)*image.height}};
            UnloadImageColors(colors); UnloadImage(image);
            for (size_t t = 0; t < material.source.size(); ++t)
            for (const auto weights : {std::array<double,3>{.3,.3,.4},std::array<double,3>{.6,.2,.2}})
            {
                double u = 0,v = 0,nu = 0,nv = 0;
                for (int c = 0; c < 3; ++c)
                {
                    u += weights[c]*material.source[t].uv[c].x; v += weights[c]*material.source[t].uv[c].y;
                    nu += weights[c]*material.target[t].uv[c].x; nv += weights[c]*material.target[t].uv[c].y;
                }
                const auto expected = SampleBakeImage(source,u,v), actual = SampleBakeImage(baked,nu,nv);
                for (int c = 0; c < 4; ++c) Check(std::abs(expected[c]-actual[c]) <= 3, "Barycentric reprojection changed texture orientation, alpha, or sample coordinates");
            }
            const auto normalMap = std::find_if(result.maps.begin(),result.maps.end(),[&](const BakeOutputMap& m) { return m.material == material.material && m.slot == static_cast<int>(PbrTextureSlot::Normal); });
            Image normal = LoadImage(normalMap->path.c_str());
            for (const auto& triangle : material.target)
            {
                const auto uv = triangle.uv[0];
                const int x = int(uv.x*normal.width)-1, y = int(uv.y*normal.height)-1;
                const Color color = GetImageColor(normal,x,y);
                Check(color.r == 78 && color.g == 162 && color.b == 239 && color.a == 117,
                      "Padding must preserve normal data and alpha without black/transparent seams");
            }
            UnloadImage(normal);
        }
        Check(ApplyTextureBake(tab,result,error),error);
        Check(tab.loaded.uvSets.size() == 3 && tab.loaded.uvSets[0] == originalUvs[0] && tab.loaded.uvSets[1] == originalUvs[1],
              "Baking overwrote a source channel"); CheckBinding(tab);
        const MaterialPreviewMode modes[]{MaterialPreviewMode::Diffuse,MaterialPreviewMode::Normal,MaterialPreviewMode::Shaded};
        for (int mode = 0; mode < 3; ++mode)
        {
            Image baked = render(modes[mode]); Color* before = LoadImageColors(originals[mode]); Color* after = LoadImageColors(baked);
            double difference = 0; int compared = 0;
            for (int p = 0; p < baked.width*baked.height; ++p)
            {
                if (before[p].r == 255 && before[p].g == 0 && before[p].b == 255) continue;
                difference += std::abs(int(before[p].r)-after[p].r) + std::abs(int(before[p].g)-after[p].g) + std::abs(int(before[p].b)-after[p].b);
                ++compared;
            }
            std::cout << "Preview " << mode << ": " << compared << " pixels, mean RGB difference " << difference/std::max(1,compared*3) << '\n';
            if (mode == 0)
            {
                ImageFlipVertical(&originals[mode]); ImageFlipVertical(&baked);
                Check(ExportImage(originals[mode],(folder / "viewport-before.png").string().c_str()),"Cannot export source viewport");
                Check(ExportImage(baked,(folder / "viewport-after.png").string().c_str()),"Cannot export baked viewport");
            }
            Check(compared > 10000 && difference/(compared*3) <= 4, "Viewport rendering changed after UV bake, beyond finite-resolution resampling");
            UnloadImageColors(before); UnloadImageColors(after); UnloadImage(originals[mode]); UnloadImage(baked);
        }
        for (int m = 0; m < tab.loaded.model.meshCount; ++m)
        for (size_t i = 0; i < oldTangents[m].size(); ++i)
            Check(std::abs(oldTangents[m][i]-tab.loaded.model.meshes[m].tangents[i]) < 1e-4f,
                  "Packing changed tangent direction or handedness");
        const auto bakedResource = tab.pbrMaterials[0].textures[0].resource;
        Check(UndoEdit(tab) && tab.loaded.uvSets == originalUvs && tab.pbrMaterials[0].textures[0].resource == originalDiffuse &&
              tab.pbrMaterials[1].uvSetName == "Detail", "Bake undo did not restore UVs, material bindings, and maps"); CheckBinding(tab);
        Check(RedoEdit(tab) && tab.pbrMaterials[0].textures[0].resource == bakedResource && tab.loaded.uvSets.size() == 3,
              "Bake redo did not restore GPU resources and channel"); CheckBinding(tab);
        Check(BuildTextureBakeInput(tab,options,input,error) && input.targetName == "BakedUV_2", "A subsequent bake must use a fresh UV channel name");
        const auto saved = (folder / "baked.fbx").string(); Check(SaveEditedFbx(tab,saved,error),error);
        CheckStandardFbxBindings(saved,result.input.targetName);
        ModelTab reopened; reopened.path = saved; Check(LoadFbxModel(saved,reopened.loaded,error),error);
        EnsurePbrMaterialStates(reopened); Check(LoadImportedPbrTextures(reopened,error) >= 18,error);
        Check(reopened.loaded.uvSets.size() == 3 && reopened.pbrMaterials[0].uvSetName == result.input.targetName &&
              reopened.pbrMaterials[1].uvSetName == result.input.targetName, "Reload lost baked UV/material binding: channels=" + std::to_string(reopened.loaded.uvSets.size()) + " bindings=" + reopened.pbrMaterials[0].uvSetName + "," + reopened.pbrMaterials[1].uvSetName); CheckBinding(reopened);
        Check(reopened.pbrMaterials[0].normalDirectX && reopened.pbrMaterials[0].roughnessChannel == PackedChannel::A &&
              reopened.pbrMaterials[0].metallicChannel == PackedChannel::R && reopened.pbrMaterials[0].aoChannel == PackedChannel::B &&
              reopened.pbrMaterials[0].opacityChannel == OpacityChannel::A && reopened.pbrMaterials[1].workflow == MaterialWorkflow::SpecularGlossiness,
              "Reload lost normal orientation, packed channels, alpha, or workflow");
        for (size_t set = 0; set < 2; ++set) for (size_t i = 0; i < originalUvs[set].size(); ++i)
            Check(std::abs(originalUvs[set][i]-reopened.loaded.uvSets[set][i]) < 1e-5f,"Export changed original UVs");
        // Fresh directory collision and source protection.
        auto second = RunTextureBake(plan); Check(second.error.empty() && second.directory != result.directory, "Bake overwrote an existing output folder");
        Check(std::filesystem::exists(result.maps[0].path), "Subsequent bake deleted previous output");
        // A failed GPU staging step must not modify the model or undo history.
        const auto beforeUvs = tab.loaded.uvSets; const auto history = tab.undoStack.size();
        auto collapsed = plan;
        for (auto& uv : collapsed.materials[0].target[0].uv) uv = {.5f,.5f};
        auto collapsedResult = RunTextureBake(std::move(collapsed));
        Check(!collapsedResult.error.empty() && collapsedResult.directory.empty() && tab.loaded.uvSets == beforeUvs && tab.undoStack.size() == history,
              "Collapsed coverage failure must remove its own output and leave scene/history intact");
        std::filesystem::remove(second.maps[0].path);
        Check(!ApplyTextureBake(tab,second,error) && tab.loaded.uvSets == beforeUvs && tab.undoStack.size() == history,
              "Failed bake changed the scene or history");
        // Validate source failures without creating folders or editing data.
        Check(UndoEdit(tab), "Cannot return to original scene for validation");
        auto& sourceUv = tab.loaded.uvSets[0]; const auto validUv = sourceUv;
        sourceUv[0] = std::numeric_limits<float>::quiet_NaN();
        Check(!BuildTextureBakeInput(tab,options,input,error), "Nonfinite source UV accepted"); sourceUv = validUv;
        sourceUv[2] = sourceUv[0]; sourceUv[3] = sourceUv[1];
        Check(!BuildTextureBakeInput(tab,options,input,error), "Collapsed source UV accepted"); sourceUv = validUv;
        tab.loaded.uvSetPresence[0][0] = 0;
        Check(!BuildTextureBakeInput(tab,options,input,error), "Missing source UV accepted"); tab.loaded.uvSetPresence[0][0] = 1;
        // Second triangle folds over its connected neighbor along their shared edge.
        sourceUv[6] = sourceUv[0]; sourceUv[7] = sourceUv[1];
        sourceUv[8] = sourceUv[4]; sourceUv[9] = sourceUv[5];
        sourceUv[10] = sourceUv[2]; sourceUv[11] = sourceUv[3];
        Check(BuildTextureBakeInput(tab,options,input,error), "Shared source UV overlaps must be safe under the same island transform");
        const auto& transforms = input.materials[0].transforms;
        Check(transforms[0].origin.x == transforms[1].origin.x && transforms[0].origin.y == transforms[1].origin.y &&
              transforms[0].scale == transforms[1].scale && transforms[0].minU == transforms[1].minU && transforms[0].minV == transforms[1].minV,
              "Overlapping source faces require identical inverse mapping"); sourceUv = validUv;
        tab.pbrMaterials[0].uvSetName = "Missing";
        Check(!BuildTextureBakeInput(tab,options,input,error), "Missing material UV binding silently fell back"); tab.pbrMaterials[0].uvSetName = "Original";
        FbxTextureReference unsupported; unsupported.materialName = tab.loaded.materialNames[0]; unsupported.filePath = tab.pbrMaterials[0].textures[0].path; unsupported.unsupportedTransform = true;
        tab.loaded.textureReferences.push_back(unsupported);
        Check(!BuildTextureBakeInput(tab,options,input,error), "Unsupported texture transform accepted"); tab.loaded.textureReferences.pop_back();
        options.material = 1;
        Check(BuildTextureBakeInput(tab,options,input,error) && input.materials.size() == 1 && input.materials[0].material == 1,error);
        auto single = RunTextureBake(std::move(input)); Check(single.error.empty(),single.error);
        Check(ApplyTextureBake(tab,single,error) && tab.pbrMaterials[0].uvSetName == "Original" && tab.pbrMaterials[1].uvSetName == single.input.targetName,
              "Selected-material bake changed other material bindings"); CheckBinding(tab);
        const auto ui = LoadRenderTexture(800,600);
        RenameEditor uvRename;
        tab.uvContextSet = 0; tab.uvContextPosition = {500,160}; tab.uvContextJustOpened = true;
        std::string uvNotice;
        BeginTextureMode(ui); ClearBackground(Color{15,17,20,255}); DrawUvSetContextMenu(GetFontDefault(),tab,uvRename,uvNotice,error); EndTextureMode();
        Image menuCapture = LoadImageFromTexture(ui.texture); ImageFlipVertical(&menuCapture);
        Check(ExportImage(menuCapture,(folder / "uv-set-menu.png").string().c_str()),"Cannot export UV set popup preview");
        UnloadImage(menuCapture); tab.uvContextSet = -1;
        OpenTextureBakeDialog(tab);
        BeginTextureMode(ui); ClearBackground(Color{15,17,20,255}); DrawTextureBakeDialog(GetFontDefault(),error,error); EndTextureMode();
        Image capture = LoadImageFromTexture(ui.texture); ImageFlipVertical(&capture);
        Check(ExportImage(capture,(folder / "bake-dialog.png").string().c_str()), "Cannot export bake UI preview");
        UnloadImage(capture); UnloadRenderTexture(ui); gTextureBakeDialog.open = false; gTextureBakeDialog.tab = nullptr;
        if (argc > 1)
        {
            ModelTab asset; asset.path = argv[1]; Check(LoadFbxModel(asset.path,asset.loaded,error),error); EnsurePbrMaterialStates(asset);
            LoadImportedPbrTextures(asset,error); AutoLoadModelTextures(asset);
            if (argc > 2) Check(LoadPbrTexturesFromFolder(asset,0,argv[2],error) > 0,error);
            const bool transferAsset = argc > 4 && std::string(argv[4]) == "--transfer";
            if (transferAsset)
                for (int node = 0; node < static_cast<int>(asset.loaded.nodes.size()); ++node) FixDegenerateTriangles(asset,node,false);
            TextureBakeOptions realOptions; realOptions.repack = true; realOptions.resolution = argc > 3 ? std::stoi(argv[3]) : 2048; realOptions.parentDirectory = folder;
            UvRepackReport preparedAsset;
            Check(AverageUvSetIslands(asset,0,&preparedAsset),preparedAsset.error);
            Check(RepackUvSetIslands(asset,preparedAsset.uvSet,&preparedAsset),preparedAsset.error);
            realOptions.layoutUvSet = preparedAsset.uvSet;
            if (transferAsset)
            { realOptions.repack = false; realOptions.targetUvSet = preparedAsset.uvSet; realOptions.material = 0; }
            for (size_t set = 0; set < asset.loaded.uvSetNames.size(); ++set)
                std::cout << "Asset UV " << asset.loaded.uvSetNames[set] << ": " << std::count(asset.loaded.uvSetPresence[set].begin(),asset.loaded.uvSetPresence[set].end(),1)
                          << "/" << asset.loaded.bindVertices.size()/3 << " present\n";
            for (int m = 0; m < static_cast<int>(asset.pbrMaterials.size()); ++m)
                std::cout << "Asset material " << asset.loaded.materialNames[m] << ": " << GetMaterialUvSetIndex(asset,m) << '\n';
            TextureBakeInput realInput;
            const bool valid = BuildTextureBakeInput(asset,realOptions,realInput,error);
            Check(valid,"Real asset validation: " + error);
            std::cout << "Real asset bake: " << asset.loaded.bindVertices.size()/9 << " triangles, " << realInput.materials.size() << " materials\n";
            auto realResult = RunTextureBake(std::move(realInput)); Check(realResult.error.empty(),"Real asset bake: " + realResult.error);
            Check(ApplyTextureBake(asset,realResult,error),error);
            const auto realPath = (realResult.directory / "baked.fbx").string(); Check(SaveEditedFbx(asset,realPath,error),error);
            ModelTab realReload; realReload.path = realPath; Check(LoadFbxModel(realPath,realReload.loaded,error),error); EnsurePbrMaterialStates(realReload);
            Check(LoadImportedPbrTextures(realReload,error) == static_cast<int>(realResult.maps.size()),error);
            Check(realReload.loaded.animations.size() == asset.loaded.animations.size() && realReload.loaded.bones.size() == asset.loaded.bones.size(),
                  "Rigged bake lost skeleton or animation data"); CheckBinding(realReload);
            UnloadPbrTextures(realReload); UnloadFbxModel(realReload.loaded);
            asset.undoStack.clear(); UnloadPbrTextures(asset); UnloadFbxModel(asset.loaded);
        }
        UnloadPbrTextures(reopened); UnloadFbxModel(reopened.loaded);
        UnloadRenderTexture(preview);
        tab.undoStack.clear(); tab.redoStack.clear(); UnloadPbrTextures(tab);
        for (int m = 0; m < tab.loaded.model.materialCount; ++m) tab.loaded.model.materials[m].shader = Shader{rlGetShaderIdDefault(),rlGetShaderLocsDefault()};
        UnloadFbxModel(tab.loaded); UnloadShader(lit.shader);
        CloseWindow();
        std::cout << "Texture baking: correspondence, padding, tangent preservation, per-material UVs, failure rollback, undo/redo, and FBX round trip passed.\n";
        return 0;
    }
    catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
