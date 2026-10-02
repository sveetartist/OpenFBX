#include <fbxsdk.h>
#include <stdexcept>
#include "editor/OpenFbxApp.cpp"

static void Check(bool condition, const std::string& message)
{
    if (!condition) throw std::runtime_error(message);
}

static void CreateFixture(const std::string& path, const std::string& texturePath = {})
{
    auto* manager = FbxManager::Create();
    manager->SetIOSettings(FbxIOSettings::Create(manager, IOSROOT));
    auto* scene = FbxScene::Create(manager, "workflow");
    scene->GetGlobalSettings().SetSystemUnit(FbxSystemUnit::m);
    scene->GetGlobalSettings().SetAxisSystem(FbxAxisSystem::OpenGL);
    auto* node = FbxNode::Create(scene, "Triangle");
    auto* mesh = FbxMesh::Create(scene, "Mesh");
    mesh->InitControlPoints(3);
    mesh->SetControlPointAt(FbxVector4(-1, -1, 0), 0);
    mesh->SetControlPointAt(FbxVector4(1, -1, 0), 1);
    mesh->SetControlPointAt(FbxVector4(0, 1, 0), 2);
    auto* surface = FbxSurfacePhong::Create(scene, "Surface");
    node->AddMaterial(surface);
    if (!texturePath.empty())
    {
        auto* texture = FbxFileTexture::Create(scene, "Diffuse");
        texture->SetFileName(texturePath.c_str());
        surface->Diffuse.ConnectSrcObject(texture);
    }
    auto* materials = mesh->CreateElementMaterial();
    materials->SetMappingMode(FbxLayerElement::eAllSame);
    materials->SetReferenceMode(FbxLayerElement::eIndexToDirect);
    materials->GetIndexArray().Add(0);
    auto* normals = mesh->CreateElementNormal();
    normals->SetMappingMode(FbxLayerElement::eByControlPoint);
    normals->SetReferenceMode(FbxLayerElement::eDirect);
    for (int i = 0; i < 3; ++i) normals->GetDirectArray().Add(FbxVector4(0, 0, 1));
    auto* uv = mesh->CreateElementUV("UVSet");
    uv->SetMappingMode(FbxLayerElement::eByControlPoint);
    uv->SetReferenceMode(FbxLayerElement::eDirect);
    uv->GetDirectArray().Add(FbxVector2(0, 0));
    uv->GetDirectArray().Add(FbxVector2(1, 0));
    uv->GetDirectArray().Add(FbxVector2(0.5, 1));
    mesh->BeginPolygon(0);
    for (int i = 0; i < 3; ++i) mesh->AddPolygon(i);
    mesh->EndPolygon();
    node->SetNodeAttribute(mesh);
    scene->GetRootNode()->AddChild(node);
    auto* exporter = FbxExporter::Create(manager, "export");
    Check(exporter->Initialize(path.c_str(), -1, manager->GetIOSettings()) && exporter->Export(scene), "Fixture export failed");
    manager->Destroy();
}

static void TestValidationTextureRepair(const std::filesystem::path& folder)
{
    ModelTab tab;
    const auto modelPath = folder / "texture_repair.fbx";
    CreateFixture(modelPath.string());
    std::string error;
    Check(LoadFbxModel(modelPath.string(), tab.loaded, error), error);
    // Two materials expose incorrect repairs to the selected/first material.
    Material* materials = static_cast<Material*>(MemAlloc(sizeof(Material) * 2));
    materials[0] = tab.loaded.model.materials[0];
    materials[1] = LoadMaterialDefault();
    MemFree(tab.loaded.model.materials);
    tab.loaded.model.materials = materials;
    tab.loaded.model.materialCount = 2;
    tab.loaded.materialNames.push_back("Second Surface");
    EnsurePbrMaterialStates(tab);
    auto findIssue = [&](const char* category, PbrTextureSlot slot)
    {
        std::vector<ValidatorIssue> issues;
        ValidateTextures(tab, issues);
        for (const auto& issue : issues)
            if (issue.category == category && issue.materialIndex == 1 && issue.textureSlot == slot) return issue;
        throw std::runtime_error("Texture validator did not identify the affected material and slot");
    };
    const auto missing = findIssue("Missing texture", PbrTextureSlot::Normal);
    const auto texturePath = folder / "texture_repair.png";
    Image image = GenImageColor(4, 4, BLUE);
    Check(ExportImage(image, texturePath.string().c_str()), "Repair texture export failed");
    UnloadImage(image);
    Check(LoadValidationTextureFile(tab, missing.materialIndex, missing.textureSlot, texturePath.string(), error), error);
    Check(GetPbrTexture(tab.pbrMaterials[1], PbrTextureSlot::Normal).loaded &&
          !GetPbrTexture(tab.pbrMaterials[0], PbrTextureSlot::Normal).loaded &&
          !GetPbrTexture(tab.pbrMaterials[1], PbrTextureSlot::Diffuse).loaded,
          "Missing texture repair must affect only the warning's material and slot");
    Check(UndoEdit(tab) && !GetPbrTexture(tab.pbrMaterials[1], PbrTextureSlot::Normal).loaded,
          "Texture repair must be undoable");
    Check(RedoEdit(tab) && GetPbrTexture(tab.pbrMaterials[1], PbrTextureSlot::Normal).loaded,
          "Texture repair must support redo");
    const auto originalId = GetPbrTexture(tab.pbrMaterials[1], PbrTextureSlot::Normal).texture.id;
    const auto historySize = tab.undoStack.size();
    Check(!LoadValidationTextureFile(tab, 1, PbrTextureSlot::Normal, (folder / "unreadable_repair.png").string(), error) &&
          tab.undoStack.size() == historySize && GetPbrTexture(tab.pbrMaterials[1], PbrTextureSlot::Normal).texture.id == originalId,
          "Failed texture repairs must preserve the existing texture and history");
    std::filesystem::remove(texturePath);
    const auto broken = findIssue("Broken texture", PbrTextureSlot::Normal);
    image = GenImageColor(4, 4, RED);
    Check(ExportImage(image, texturePath.string().c_str()), "Replacement texture export failed");
    UnloadImage(image);
    Check(LoadValidationTextureFile(tab, broken.materialIndex, broken.textureSlot, texturePath.string(), error), error);
    Check(GetPbrTexture(tab.pbrMaterials[1], PbrTextureSlot::Normal).texture.id != originalId &&
          !GetPbrTexture(tab.pbrMaterials[0], PbrTextureSlot::Normal).loaded,
          "Broken texture repair must replace only the affected slot");
    tab.undoStack.clear();
    tab.redoStack.clear();
    UnloadPbrTextures(tab);
    UnloadFbxModel(tab.loaded);
}

static void TestTextureUndo(const std::filesystem::path& folder)
{
    const auto modelPath = folder / "texture_undo.fbx";
    const auto texturePath = folder / "texture_undo.png";
    CreateFixture(modelPath.string());
    ModelTab tab;
    std::string error;
    Check(LoadFbxModel(modelPath.string(), tab.loaded, error), error);
    tab.currentVertices = tab.loaded.bindVertices;
    tab.currentNormals = tab.loaded.bindNormals;
    for (int i = 0; i < static_cast<int>(tab.loaded.nodes.size()); ++i)
        if (tab.loaded.nodes[i].type == SceneNodeType::Mesh) SetSingleSelectedNode(tab, i);
    auto loadColor = [&](Color color)
    {
        Image image = GenImageColor(4, 4, color);
        Check(ExportImage(image, texturePath.string().c_str()), "Undo texture export failed");
        UnloadImage(image);
        Check(LoadPbrTexture(tab, 0, PbrTextureSlot::Diffuse, texturePath.string(), error), error);
    };
    auto checkColor = [&](Color expected)
    {
        const auto& texture = GetPbrTexture(tab.pbrMaterials[0], PbrTextureSlot::Diffuse);
        Check(texture.loaded, "Undo/redo lost a loaded texture when its source file was missing");
        Check(tab.loaded.model.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture.id == texture.texture.id,
              "Undo/redo did not bind the restored texture to the model");
        Image image = LoadImageFromTexture(texture.texture);
        Color actual = GetImageColor(image, 0, 0);
        UnloadImage(image);
        Check(actual.r == expected.r && actual.g == expected.g && actual.b == expected.b,
              "Undo/redo changed the saved texture pixels");
    };
    loadColor(RED);
    PushUndoSnapshot(tab);
    MoveSelectedSubtree(tab, Vector3{1, 0, 0});
    std::filesystem::remove(texturePath);
    Check(UndoEdit(tab), "Transform undo failed");
    checkColor(RED);
    Check(RedoEdit(tab), "Transform redo failed");
    checkColor(RED);

    // Replacing the contents of the same file must preserve both texture versions.
    PushUndoSnapshot(tab);
    loadColor(BLUE);
    std::filesystem::remove(texturePath);
    Check(UndoEdit(tab), "Texture replacement undo failed");
    checkColor(RED);
    Check(RedoEdit(tab), "Texture replacement redo failed");
    checkColor(BLUE);
    PushUndoSnapshot(tab);
    UnloadPbrTexture(tab, 0, PbrTextureSlot::Diffuse);
    Check(UndoEdit(tab), "Texture removal undo failed");
    checkColor(BLUE);
    Check(RedoEdit(tab) && !GetPbrTexture(tab.pbrMaterials[0], PbrTextureSlot::Diffuse).loaded,
          "Texture removal redo must leave the slot empty");
    tab.undoStack.clear();
    tab.redoStack.clear();
    UnloadPbrTextures(tab);
    UnloadFbxModel(tab.loaded);
}

int main()
{
    try
    {
        SetTraceLogLevel(LOG_WARNING);
        SetConfigFlags(FLAG_WINDOW_HIDDEN);
        InitWindow(640, 800, "Material workflow regression");
        const auto folder = std::filesystem::temp_directory_path() / "openfbx-material-regression";
        std::filesystem::create_directories(folder);
        TestValidationTextureRepair(folder);
        TestTextureUndo(folder);
        const auto source = folder / "source.fbx";
        CreateFixture(source.string());
        ModelTab tab;
        tab.path = source.string();
        std::string error;
        Check(LoadFbxModel(tab.path, tab.loaded, error), error);
        tab.currentVertices = tab.loaded.bindVertices;
        tab.currentNormals = tab.loaded.bindNormals;
        EnsurePbrMaterialStates(tab);
        auto texture = [&](const char* name, Color color, PbrTextureSlot slot)
        {
            const auto path = (folder / name).string();
            Image image = GenImageColor(4, 4, color);
            Check(ExportImage(image, path.c_str()), "Texture fixture export failed");
            UnloadImage(image);
            Check(LoadPbrTexture(tab, 0, slot, path, error), error);
            return path;
        };
        texture("surface_diffuse.png", BLACK, PbrTextureSlot::Diffuse);
        texture("surface_roughness.png", Color{0, 128, 0, 255}, PbrTextureSlot::Roughness);
        texture("surface_metallic.png", Color{0, 0, 255, 255}, PbrTextureSlot::Metallic);
        const auto specPath = texture("surface_specgloss.png", Color{180, 40, 20, 64}, PbrTextureSlot::Specular);
        Check(LoadPbrTexture(tab, 0, PbrTextureSlot::Glossiness, specPath, error), error);
        tab.pbrMaterials[0].glossinessChannel = PackedChannel::A;
        auto& maps = tab.loaded.model.materials[0].maps;
        const auto metallicId = maps[MATERIAL_MAP_METALNESS].texture.id;
        const auto roughnessId = maps[MATERIAL_MAP_ROUGHNESS].texture.id;
        Check(metallicId == GetPbrTexture(tab.pbrMaterials[0], PbrTextureSlot::Metallic).texture.id,
              "Loading an inactive specular map must not replace metallic");
        PushUndoSnapshot(tab);
        SetMaterialWorkflow(tab, 0, MaterialWorkflow::SpecularGlossiness);
        Check(maps[MATERIAL_MAP_METALNESS].texture.id == GetPbrTexture(tab.pbrMaterials[0], PbrTextureSlot::Specular).texture.id,
              "Workflow must bind specular texture");
        SetMaterialWorkflow(tab, 0, MaterialWorkflow::MetallicRoughness);
        Check(maps[MATERIAL_MAP_METALNESS].texture.id == metallicId && maps[MATERIAL_MAP_ROUGHNESS].texture.id == roughnessId,
              "Switching back must preserve original maps");
        SetMaterialWorkflow(tab, 0, MaterialWorkflow::SpecularGlossiness);
        Check(UndoEdit(tab) && tab.pbrMaterials[0].workflow == MaterialWorkflow::MetallicRoughness, "Undo workflow failed");
        Check(RedoEdit(tab) && tab.pbrMaterials[0].workflow == MaterialWorkflow::SpecularGlossiness, "Redo workflow failed");
        Check(tab.pbrMaterials[0].glossinessChannel == PackedChannel::A, "Undo must preserve alpha channel");

        const LitShader lit = LoadBasicLitShader();
        Check(lit.valid && lit.materialWorkflowLoc >= 0, "Workflow shader must compile");
        ApplyLitShader(tab.loaded, lit);
        const Vector3 view{0, 0, 3}, light{0, 0, -1};
        const Vector4 white{1, 1, 1, 1}, ambient{0.2f, 0.2f, 0.2f, 1};
        SetShaderValue(lit.shader, lit.viewPositionLoc, &view, SHADER_UNIFORM_VEC3);
        SetShaderValue(lit.shader, lit.lightDirectionLoc, &light, SHADER_UNIFORM_VEC3);
        SetShaderValue(lit.shader, lit.lightColorLoc, &white, SHADER_UNIFORM_VEC4);
        SetShaderValue(lit.shader, lit.ambientLoc, &ambient, SHADER_UNIFORM_VEC4);
        const Camera3D camera{view, Vector3{}, Vector3{0, 1, 0}, 45, CAMERA_PERSPECTIVE};
        const auto target = LoadRenderTexture(128, 128);
        auto render = [&](MaterialPreviewMode mode)
        {
            BeginTextureMode(target);
            ClearBackground(MAGENTA);
            BeginMode3D(camera);
            DrawMaterialModel(tab, lit, mode, true);
            EndMode3D();
            EndTextureMode();
            Image image = LoadImageFromTexture(target.texture);
            Color result = GetImageColor(image, 64, 64);
            UnloadImage(image);
            return result;
        };
        Color specular = render(MaterialPreviewMode::Metallic);
        Check(std::abs(int(specular.r) - 180) < 3 && std::abs(int(specular.g) - 40) < 3, "Specular preview must use RGB");
        Color gloss = render(MaterialPreviewMode::Roughness);
        Check(std::abs(int(gloss.r) - 64) < 3 && gloss.r == gloss.g, "Gloss preview must read alpha");
        Color shaded = render(MaterialPreviewMode::Shaded);
        Check(shaded.r > shaded.g + 20, "Specular color must tint shaded highlights");
        tab.pbrMaterials[0].glossinessChannel = PackedChannel::R;
        Check(render(MaterialPreviewMode::Shaded).r > shaded.r, "Higher glossiness must change the highlight");
        tab.pbrMaterials[0].glossinessChannel = PackedChannel::A;
        SetMaterialWorkflow(tab, 0, MaterialWorkflow::MetallicRoughness);
        Check(std::abs(int(render(MaterialPreviewMode::Roughness).r) - 128) < 3, "MR roughness preview changed");
        Check(render(MaterialPreviewMode::Metallic).r > 250, "MR metallic preview changed");
        SetMaterialWorkflow(tab, 0, MaterialWorkflow::SpecularGlossiness);

        const auto saved = (folder / "saved.fbx").string();
        Check(SaveEditedFbx(tab, saved, error), error);
        ModelTab reopened;
        reopened.path = saved;
        Check(LoadFbxModel(saved, reopened.loaded, error), error);
        const int imported = LoadImportedPbrTextures(reopened, error);
        Check(imported == 5, "Save/reopen must preserve both workflows' maps (loaded " + std::to_string(imported) + "): " + error);
        Check(reopened.pbrMaterials[0].workflow == MaterialWorkflow::SpecularGlossiness &&
              reopened.pbrMaterials[0].glossinessChannel == PackedChannel::A, "Workflow and alpha must survive save/reopen");
        Check(GetPbrTexture(reopened.pbrMaterials[0], PbrTextureSlot::Specular).loaded, "Specular texture missing on reopen");
        Check(ScoreTextureCandidate("surface_specgloss", "", "", PbrTextureSlot::Specular) >= 10 &&
              ScoreTextureCandidate("surface_specgloss", "", "", PbrTextureSlot::Glossiness) >= 10,
              "Folder discovery must recognize combined maps");

        // A package owns its textures even while conflicting source files exist.
        const auto packageFolder = folder / "packaged";
        std::filesystem::create_directories(packageFolder);
        ModelTab packageSource;
        packageSource.path = (packageFolder / "asset.fbx").string();
        CreateFixture(packageSource.path);
        Check(LoadFbxModel(packageSource.path, packageSource.loaded, error), error);
        const auto packageDiffuse = packageFolder / "surface_diffuse.png";
        auto writePackageTexture = [&](const std::filesystem::path& path, Color color)
        {
            Image fixture = GenImageColor(4, 4, color);
            Check(ExportImage(fixture, path.string().c_str()), "Package texture export failed");
            UnloadImage(fixture);
        };
        writePackageTexture(packageDiffuse, WHITE);
        Check(LoadPbrTexture(packageSource, 0, PbrTextureSlot::Diffuse, packageDiffuse.string(), error), error);
        std::filesystem::create_directories(packageFolder / "other");
        const auto packageEmissive = packageFolder / "other/surface_diffuse.png";
        writePackageTexture(packageEmissive, RED);
        Check(LoadPbrTexture(packageSource, 0, PbrTextureSlot::Emissive, packageEmissive.string(), error), error);
        const auto packagedPath = MakePackagedFbxPath(packageSource.path);
        std::string packageNotice;
        Check(PackageFbxWithTextures(packageSource, packageNotice, error), error);
        UnloadPbrTextures(packageSource);
        UnloadFbxModel(packageSource.loaded);
        writePackageTexture(packageDiffuse, BLACK);
        writePackageTexture(packageEmissive, BLACK);
        writePackageTexture(packageFolder / "surface_normal.png", WHITE);
        const auto resavedPackage = packageFolder / "resaved.fbx";
        auto checkPackage = [&](const std::filesystem::path& path, bool saveCopy = false)
        {
            ModelTab packaged;
            packaged.path = path.string();
            Check(LoadFbxModel(packaged.path, packaged.loaded, error), error);
            Check(packaged.loaded.sourceHasEmbeddedMedia, "Package must report embedded media");
            Check(LoadImportedPbrTextures(packaged, error) == 2, "Package textures missing: " + error);
            const auto& diffuse = GetPbrTexture(packaged.pbrMaterials[0], PbrTextureSlot::Diffuse);
            Check(AbsoluteNormalizedPath(diffuse.path).parent_path() ==
                  AbsoluteNormalizedPath(packaged.loaded.embeddedMediaExtractionDirectory),
                  "Package must use its extracted texture instead of the source file: " + diffuse.path +
                  " (extraction: " + packaged.loaded.embeddedMediaExtractionDirectory + ")");
            Image image = LoadImageFromTexture(diffuse.texture);
            const Color pixel = GetImageColor(image, 0, 0);
            UnloadImage(image);
            Check(pixel.r == 255 && pixel.g == 255 && pixel.b == 255,
                  "Changed directory texture replaced packaged pixels");
            const auto& emissive = GetPbrTexture(packaged.pbrMaterials[0], PbrTextureSlot::Emissive);
            image = LoadImageFromTexture(emissive.texture);
            const Color emissivePixel = GetImageColor(image, 0, 0);
            UnloadImage(image);
            Check(emissive.path != diffuse.path && emissivePixel.r == RED.r && emissivePixel.g == RED.g,
                  "Same-name embedded textures must remain distinct");
            Check(AutoLoadModelTextures(packaged) == 0, "Package must skip automatic directory discovery");
            Check(!GetPbrTexture(packaged.pbrMaterials[0], PbrTextureSlot::Normal).loaded,
                  "Unassigned package maps must stay empty despite nearby textures");
            if (saveCopy)
                Check(SaveFbxModelAnimations(packaged.path, resavedPackage.string(), packaged.loaded,
                                            packaged.deletedNodes, error), error);
            packaged.currentVertices = packaged.loaded.bindVertices;
            packaged.currentNormals = packaged.loaded.bindNormals;
            for (int i = 0; i < static_cast<int>(packaged.loaded.nodes.size()); ++i)
                if (packaged.loaded.nodes[i].type == SceneNodeType::Mesh) SetSingleSelectedNode(packaged, i);
            const auto diffuseId = diffuse.texture.id;
            const auto emissiveId = emissive.texture.id;
            if (!saveCopy)
            {
                std::filesystem::remove(diffuse.path);
                std::filesystem::remove(emissive.path);
            }
            ApplyLitShader(packaged.loaded, lit);
            auto checkUndoTextures = [&]
            {
                const auto& restoredDiffuse = GetPbrTexture(packaged.pbrMaterials[0], PbrTextureSlot::Diffuse);
                const auto& restoredEmissive = GetPbrTexture(packaged.pbrMaterials[0], PbrTextureSlot::Emissive);
                Check(restoredDiffuse.loaded && restoredDiffuse.texture.id == diffuseId &&
                      restoredEmissive.loaded && restoredEmissive.texture.id == emissiveId,
                      "Repeated package undo/redo must retain the original loaded textures");
                Check(packaged.loaded.model.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture.id == diffuseId &&
                      packaged.loaded.model.materials[0].maps[MATERIAL_MAP_EMISSION].texture.id == emissiveId,
                      "Repeated package undo/redo must preserve viewport texture bindings");
                BeginTextureMode(target);
                ClearBackground(MAGENTA);
                BeginMode3D(camera);
                DrawMaterialModel(packaged, lit, MaterialPreviewMode::Emissive, true);
                EndMode3D();
                EndTextureMode();
                Image capture = LoadImageFromTexture(target.texture);
                const Color rendered = GetImageColor(capture, 64, 64);
                UnloadImage(capture);
                Check(std::abs(int(rendered.r) - RED.r) < 3 && std::abs(int(rendered.g) - RED.g) < 3 &&
                      std::abs(int(rendered.b) - RED.b) < 3,
                      "Repeated package undo/redo must keep textures visible in the viewport");
            };
            checkUndoTextures();
            for (int step = 0; step < 8; ++step)
            {
                PushUndoSnapshot(packaged);
                Vector3 pivot{};
                Check(GetGizmoPivot(packaged, pivot), "Package fixture has no transform pivot");
                RotateSelectedSubtree(packaged, pivot, TransformAxis::Z, Vector3{0, 0, 1}, 0.1f);
                Check(UndoEdit(packaged), "Package rotation undo failed");
                checkUndoTextures();
                Check(RedoEdit(packaged), "Package rotation redo failed");
                checkUndoTextures();
            }
            packaged.undoStack.clear();
            packaged.redoStack.clear();
            UnloadPbrTextures(packaged);
            for (int i = 0; i < packaged.loaded.model.materialCount; ++i)
                packaged.loaded.model.materials[i].shader = Shader{rlGetShaderIdDefault(), rlGetShaderLocsDefault()};
            UnloadFbxModel(packaged.loaded);
        };
        checkPackage(packagedPath, true);
        checkPackage(resavedPackage);
        std::filesystem::remove(packageDiffuse);
        std::filesystem::remove(packageEmissive);
        checkPackage(packagedPath);
        Font font = LoadTechnicalFont();
        // Opening discovers named maps, preserves existing maps, and skips ties.
        const auto discoveryFolder = folder / "discovery";
        std::filesystem::create_directories(discoveryFolder / "textures");
        const auto discoverySource = discoveryFolder / "asset.fbx";
        CreateFixture(discoverySource.string());
        auto writeTexture = [&](const std::filesystem::path& path)
        {
            Image fixture = GenImageColor(4, 4, WHITE);
            Check(ExportImage(fixture, path.string().c_str()), "Discovery texture export failed");
            UnloadImage(fixture);
        };
        writeTexture(discoveryFolder / "textures/surface_albedo.png");
        writeTexture(discoveryFolder / "textures/surface_orm.png");
        writeTexture(discoveryFolder / "surface_normal.png");
        writeTexture(discoveryFolder / "textures/surface_normal.png");
        ModelTab discovered;
        discovered.path = discoverySource.string();
        Check(LoadFbxModel(discovered.path, discovered.loaded, error), error);
        Check(AutoLoadModelTextures(discovered) == 4, "Discovery must load albedo and ORM, skipping ambiguous normals");
        Check(discovered.pbrMaterials[0].roughnessChannel == PackedChannel::G &&
              discovered.pbrMaterials[0].metallicChannel == PackedChannel::B, "Discovery must configure ORM channels");
        Check(AutoLoadModelTextures(discovered) == 0, "Discovery must preserve loaded maps");
        UnloadPbrTextures(discovered);
        UnloadFbxModel(discovered.loaded);
        const auto relocated = discoveryFolder / "textures/relocated.PNG";
        writeTexture(relocated);
        CreateFixture(discoverySource.string(), (discoveryFolder / "missing/RELOCATED.png").string());
        Check(LoadFbxModel(discovered.path, discovered.loaded, error), error);
        Check(LoadImportedPbrTextures(discovered, error) == 1,
              "Missing absolute references must resolve by filename in model subfolders: " + error);
        UnloadPbrTextures(discovered);
        UnloadFbxModel(discovered.loaded);
        TextureClipboard clipboard;
        bool dropped = false;
        std::string notice;
        const auto ui = LoadRenderTexture(640, 800);
        BeginTextureMode(ui);
        ClearBackground(Color{24, 28, 33, 255});
        DrawMaterialsPanel(font, reopened, 0, 40, 360, {}, dropped, clipboard, notice, error, false);
        EndTextureMode();
        Image image = LoadImageFromTexture(ui.texture);
        ImageFlipVertical(&image);
        ExportImage(image, (folder / "materials.png").string().c_str());
        UnloadImage(image);
        // Many materials and narrow panels must not paint over the timeline or tabs.
        ModelTab many;
        many.loaded.model.materialCount = 40;
        many.loaded.materialNames.assign(40, "Very long material name that must stay inside its row");
        EnsurePbrMaterialStates(many);
        const float savedReservedHeight = gBottomPanelReservedHeight;
        for (float reserved : {180.0f, 340.0f})
        for (float panelWidth : {220.0f, 360.0f})
        for (float requestedScroll : {0.0f, 100000.0f})
        {
            gBottomPanelReservedHeight = reserved;
            many.materialsScroll = requestedScroll;
            BeginTextureMode(ui);
            ClearBackground(MAGENTA);
            DrawMaterialsPanel(font, many, 0, 135, panelWidth, {}, dropped, clipboard, notice, error, false);
            EndTextureMode();
            Image capture = LoadImageFromTexture(ui.texture);
            ImageFlipVertical(&capture);
            const int bottom = 800 - static_cast<int>(reserved);
            for (int py = 0; py < capture.height; ++py)
            for (int px = 0; px < capture.width; ++px)
            {
                if (py >= 135 && py < bottom && px < panelWidth) continue;
                const Color pixel = GetImageColor(capture, px, py);
                Check(pixel.r == 255 && pixel.g == 0 && pixel.b == 255,
                      "Material panel painted outside its viewport");
            }
            if (requestedScroll > 0)
            {
                Check(many.materialsScroll > 0 && many.materialsScroll < requestedScroll,
                      "Material scroll must clamp to the reachable last row");
                ExportImage(capture, (folder / ("materials-bottom-" + std::to_string(static_cast<int>(panelWidth)) + ".png")).string().c_str());
            }
            UnloadImage(capture);
            many.statsScroll = requestedScroll;
            BeginTextureMode(ui);
            ClearBackground(MAGENTA);
            DrawSceneStatsPanel(font, many, 0, 135, panelWidth);
            EndTextureMode();
            capture = LoadImageFromTexture(ui.texture);
            ImageFlipVertical(&capture);
            for (int py = 0; py < capture.height; ++py)
            for (int px = 0; px < capture.width; ++px)
            {
                if (py >= 135 && py < bottom && px < panelWidth) continue;
                const Color pixel = GetImageColor(capture, px, py);
                Check(pixel.r == 255 && pixel.g == 0 && pixel.b == 255,
                      "Scene stats painted outside its viewport");
            }
            UnloadImage(capture);
        }
        many.loaded.model.materialCount = 1;
        gBottomPanelReservedHeight = 0;
        BeginTextureMode(ui);
        DrawMaterialsPanel(font, many, 0, 40, 220, {}, dropped, clipboard, notice, error, false);
        EndTextureMode();
        Check(many.materialsScroll == 0, "Removing materials must reset an obsolete scroll offset");
        gBottomPanelReservedHeight = savedReservedHeight;
        UnloadRenderTexture(ui);
        if (font.texture.id != GetFontDefault().texture.id) UnloadFont(font);
        UnloadRenderTexture(target);
        UnloadPbrTextures(reopened);
        UnloadFbxModel(reopened.loaded);
        UnloadPbrTextures(tab);
        // Model materials share the shader, which is owned here.
        for (int i = 0; i < tab.loaded.model.materialCount; ++i) tab.loaded.model.materials[i].shader = Shader{rlGetShaderIdDefault(), rlGetShaderLocsDefault()};
        UnloadFbxModel(tab.loaded);
        UnloadShader(lit.shader);
        CloseWindow();
        std::cout << "Material workflows: shader, undo, map preservation, and FBX round trip passed.\n";
        return 0;
    }
    catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
