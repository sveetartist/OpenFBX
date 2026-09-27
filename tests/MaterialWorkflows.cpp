#include <fbxsdk.h>
#include <stdexcept>
#include "editor/OpenFbxApp.cpp"

static void Check(bool condition, const std::string& message)
{
    if (!condition) throw std::runtime_error(message);
}

static void CreateFixture(const std::string& path)
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
    node->AddMaterial(FbxSurfacePhong::Create(scene, "Surface"));
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

int main()
{
    try
    {
        SetTraceLogLevel(LOG_WARNING);
        SetConfigFlags(FLAG_WINDOW_HIDDEN);
        InitWindow(640, 800, "Material workflow regression");
        const auto folder = std::filesystem::temp_directory_path() / "openfbx-material-regression";
        std::filesystem::create_directories(folder);
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
        Font font = LoadTechnicalFont();
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
