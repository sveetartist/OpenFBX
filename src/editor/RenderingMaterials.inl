void TrimEditHistory(std::vector<EditSnapshot>& history)
{
    constexpr size_t kMaxEditHistory = 64;
    if (history.size() > kMaxEditHistory)
    {
        history.erase(history.begin(), history.begin() + static_cast<std::ptrdiff_t>(history.size() - kMaxEditHistory));
    }
}

void PushUndoSnapshot(ModelTab& tab, EditSnapshot snapshot)
{
    tab.undoStack.push_back(std::move(snapshot));
    TrimEditHistory(tab.undoStack);
    tab.redoStack.clear();
}

void PushUndoSnapshot(ModelTab& tab)
{
    PushUndoSnapshot(tab, CaptureEditSnapshot(tab));
}

void RestoreEditSnapshot(ModelTab& tab, const EditSnapshot& snapshot)
{
    tab.loaded.uvSetNames = snapshot.uvSetNames;
    tab.loaded.uvSets = snapshot.uvSets;
    tab.loaded.uvSetPresence = snapshot.uvSetPresence;
    tab.loaded.uvSetsEdited = snapshot.uvSetsEdited;
    tab.selectedUvSet = snapshot.selectedUvSet;
    tab.uvContextSet = -1;
    tab.viewportUvIslandCache = ViewportUvIslandCache{};
    tab.selectedUvIslands.clear();
    RefreshUvMeshBuffers(tab);
    tab.skinningGeometry.reset();
    tab.loaded.bounds = snapshot.bounds;
    tab.loaded.nodes = snapshot.nodes;
    tab.loaded.bones = snapshot.bones;
    tab.loaded.bonePoses = snapshot.bonePoses;
    tab.loaded.bindVertices = snapshot.bindVertices;
    tab.loaded.bindNormals = snapshot.bindNormals;
    tab.currentVertices = snapshot.currentVertices;
    tab.currentNormals = snapshot.currentNormals;
    tab.loaded.skinnedVertices = snapshot.skinnedVertices;
    tab.visibleBones = snapshot.visibleBones;
    tab.visibleBonePoses = snapshot.visibleBonePoses;
    tab.originalBindBonePoses = snapshot.originalBindBonePoses;
    tab.loaded.animations = snapshot.animations;
    tab.deletedNodes = snapshot.deletedNodes;
    tab.selectedNode = snapshot.selectedNode;
    tab.selectedNodes = snapshot.selectedNodes;
    PruneSelectedNodes(tab);
    tab.selectedMaterial = snapshot.selectedMaterial;
    tab.animation.clipIndex = snapshot.animationClipIndex;
    tab.animation.clipScroll = snapshot.animationClipScroll;
    tab.animation.time = snapshot.animationTime;
    tab.manualSkinnedMeshPose = snapshot.manualSkinnedMeshPose;
    tab.animation.playing = false;
    tab.animation.scrubbing = false;
    tab.animation.contextMenuOpen = false;
    tab.animation.contextClipIndex = -1;

    UnloadPbrTextures(tab);
    tab.pbrMaterials.clear();
    tab.pbrMaterials.resize(snapshot.pbrMaterials.size());
    for (int materialIndex = 0; materialIndex < static_cast<int>(snapshot.pbrMaterials.size()); ++materialIndex)
    {
        const PbrMaterialSnapshot& materialSnapshot = snapshot.pbrMaterials[static_cast<size_t>(materialIndex)];
        PbrMaterialState& material = tab.pbrMaterials[static_cast<size_t>(materialIndex)];
        material.normalDirectX = materialSnapshot.normalDirectX;
        material.roughnessChannel = materialSnapshot.roughnessChannel;
        material.metallicChannel = materialSnapshot.metallicChannel;
        material.aoChannel = materialSnapshot.aoChannel;
        material.opacityChannel = materialSnapshot.opacityChannel;

        for (int slotIndex = 0; slotIndex < static_cast<int>(PbrTextureSlot::Count); ++slotIndex)
        {
            const PbrTextureSnapshot& textureSnapshot = materialSnapshot.textures[static_cast<size_t>(slotIndex)];
            if (!textureSnapshot.loaded || textureSnapshot.path.empty()) continue;

            std::string loadError;
            LoadPbrTexture(tab, materialIndex, static_cast<PbrTextureSlot>(slotIndex), textureSnapshot.path, loadError);
        }
    }

    EnsurePbrMaterialStates(tab);
    tab.selectedMaterial = std::max(0, std::min(tab.selectedMaterial, static_cast<int>(tab.pbrMaterials.size()) - 1));
    InvalidateDisplayedAnimationCaches(tab);
    RefreshDisplayedMesh(tab);
}

bool UndoEdit(ModelTab& tab)
{
    if (tab.undoStack.empty()) return false;
    tab.redoStack.push_back(CaptureEditSnapshot(tab));
    EditSnapshot snapshot = std::move(tab.undoStack.back());
    tab.undoStack.pop_back();
    RestoreEditSnapshot(tab, snapshot);
    return true;
}

bool RedoEdit(ModelTab& tab)
{
    if (tab.redoStack.empty()) return false;
    tab.undoStack.push_back(CaptureEditSnapshot(tab));
    TrimEditHistory(tab.undoStack);
    EditSnapshot snapshot = std::move(tab.redoStack.back());
    tab.redoStack.pop_back();
    RestoreEditSnapshot(tab, snapshot);
    return true;
}

void UpdateMaterialShader(const LitShader& lit, const PbrMaterialState* pbr, MaterialPreviewMode previewMode, bool texturesVisible)
{
    if (!lit.valid) return;

    const int texturesEnabled = texturesVisible ? 1 : 0;
    const int hasDiffuse = texturesVisible && pbr && GetPbrTexture(*pbr, PbrTextureSlot::Diffuse).loaded ? 1 : 0;
    const int hasNormal = texturesVisible && pbr && GetPbrTexture(*pbr, PbrTextureSlot::Normal).loaded ? 1 : 0;
    const int hasRoughness = texturesVisible && pbr && GetPbrTexture(*pbr, PbrTextureSlot::Roughness).loaded ? 1 : 0;
    const int hasMetallic = texturesVisible && pbr && GetPbrTexture(*pbr, PbrTextureSlot::Metallic).loaded ? 1 : 0;
    const int hasAo = texturesVisible && pbr && GetPbrTexture(*pbr, PbrTextureSlot::AmbientOcclusion).loaded ? 1 : 0;
    const int hasEmissive = texturesVisible && pbr && GetPbrTexture(*pbr, PbrTextureSlot::Emissive).loaded ? 1 : 0;
    const int hasOpacity = texturesVisible && pbr && GetPbrTexture(*pbr, PbrTextureSlot::Opacity).loaded ? 1 : 0;
    const int normalDirectX = pbr && pbr->normalDirectX ? 1 : 0;
    const int roughnessChannel = pbr ? ToInt(pbr->roughnessChannel) : 0;
    const int metallicChannel = pbr ? ToInt(pbr->metallicChannel) : 0;
    const int aoChannel = pbr ? ToInt(pbr->aoChannel) : 0;
    const int opacityChannel = pbr ? ToInt(pbr->opacityChannel) : ToInt(OpacityChannel::RGB);
    const int materialPreviewMode = ToInt(previewMode);

    SetShaderValue(lit.shader, lit.hasDiffuseMapLoc, &hasDiffuse, SHADER_UNIFORM_INT);
    SetShaderValue(lit.shader, lit.hasNormalMapLoc, &hasNormal, SHADER_UNIFORM_INT);
    SetShaderValue(lit.shader, lit.hasRoughnessMapLoc, &hasRoughness, SHADER_UNIFORM_INT);
    SetShaderValue(lit.shader, lit.hasMetallicMapLoc, &hasMetallic, SHADER_UNIFORM_INT);
    SetShaderValue(lit.shader, lit.hasAoMapLoc, &hasAo, SHADER_UNIFORM_INT);
    SetShaderValue(lit.shader, lit.hasEmissiveMapLoc, &hasEmissive, SHADER_UNIFORM_INT);
    SetShaderValue(lit.shader, lit.hasOpacityMapLoc, &hasOpacity, SHADER_UNIFORM_INT);
    SetShaderValue(lit.shader, lit.normalDirectXLoc, &normalDirectX, SHADER_UNIFORM_INT);
    SetShaderValue(lit.shader, lit.roughnessChannelLoc, &roughnessChannel, SHADER_UNIFORM_INT);
    SetShaderValue(lit.shader, lit.metallicChannelLoc, &metallicChannel, SHADER_UNIFORM_INT);
    SetShaderValue(lit.shader, lit.aoChannelLoc, &aoChannel, SHADER_UNIFORM_INT);
    SetShaderValue(lit.shader, lit.opacityChannelLoc, &opacityChannel, SHADER_UNIFORM_INT);
    SetShaderValue(lit.shader, lit.materialPreviewModeLoc, &materialPreviewMode, SHADER_UNIFORM_INT);
    SetShaderValue(lit.shader, lit.texturesVisibleLoc, &texturesEnabled, SHADER_UNIFORM_INT);
}

void DrawMaterialModel(ModelTab& tab, const LitShader& lit, MaterialPreviewMode previewMode, bool texturesVisible)
{
    EnsurePbrMaterialStates(tab);
    const Matrix transform = MatrixIdentity();

    auto drawMeshes = [&](bool transparentPass)
    {
        for (int meshIndex = 0; meshIndex < tab.loaded.model.meshCount; ++meshIndex)
        {
            int materialIndex = tab.loaded.model.meshMaterial ? tab.loaded.model.meshMaterial[meshIndex] : 0;
            materialIndex = std::max(0, std::min(materialIndex, static_cast<int>(tab.pbrMaterials.size()) - 1));

            const PbrMaterialState& pbr = tab.pbrMaterials[static_cast<size_t>(materialIndex)];
            const bool transparentMaterial = texturesVisible &&
                                             previewMode == MaterialPreviewMode::Shaded &&
                                             GetPbrTexture(pbr, PbrTextureSlot::Opacity).loaded;
            if (transparentMaterial != transparentPass) continue;

            UpdateMaterialShader(lit, &pbr, previewMode, texturesVisible);
            DrawMesh(tab.loaded.model.meshes[meshIndex], tab.loaded.model.materials[materialIndex], transform);
        }
    };

    drawMeshes(false);

    rlDrawRenderBatchActive();
    rlDisableDepthMask();
    drawMeshes(true);
    rlDrawRenderBatchActive();
    rlEnableDepthMask();
}

void DrawMaterialColorModel(ModelTab& tab, const LitShader& lit)
{
    EnsurePbrMaterialStates(tab);
    if (!tab.loaded.hasMesh || tab.loaded.model.meshCount <= 0) return;

    UpdateMaterialShader(lit, nullptr, MaterialPreviewMode::Shaded, false);
    const Matrix transform = MatrixIdentity();
    for (int meshIndex = 0; meshIndex < tab.loaded.model.meshCount; ++meshIndex)
    {
        int materialIndex = tab.loaded.model.meshMaterial ? tab.loaded.model.meshMaterial[meshIndex] : meshIndex;
        materialIndex = ClampInt(materialIndex, 0, std::max(0, tab.loaded.model.materialCount - 1));

        Material material = tab.loaded.model.materials[materialIndex];
        MaterialMap maps[MATERIAL_MAP_BRDF + 1]{};
        for (int mapIndex = 0; mapIndex <= MATERIAL_MAP_BRDF; ++mapIndex)
        {
            maps[mapIndex] = tab.loaded.model.materials[materialIndex].maps[mapIndex];
        }
        material.maps = maps;
        material.maps[MATERIAL_MAP_DIFFUSE].color = GetDebugIndexColor(materialIndex);
        DrawMesh(tab.loaded.model.meshes[meshIndex], material, transform);
    }
}

void DrawMeterGridLine(Vector3 start, Vector3 end, bool major, bool floorLine)
{
    const Color color = floorLine ? Color{ 230, 236, 242, 235 } :
                        major ? Color{ 88, 98, 108, 170 } :
                                Color{ 76, 84, 92, 140 };
    rlSetLineWidth(floorLine ? 2.5f : major ? 1.6f : 1.0f);
    DrawLine3D(start, end, color);
    rlSetLineWidth(1.0f);
}

void DrawMeterGrid(const OrbitCamera& orbit)
{
    constexpr int halfCells = 20;
    constexpr float extent = static_cast<float>(halfCells) * kMetersPerGridCell;

    if (!orbit.snappedView || orbit.camera.projection != CAMERA_ORTHOGRAPHIC)
    {
        for (int i = -halfCells; i <= halfCells; ++i)
        {
            const float offset = static_cast<float>(i) * kMetersPerGridCell;
            const bool major = i == 0 || i % 5 == 0;
            DrawMeterGridLine(Vector3{ -extent, 0.0f, offset }, Vector3{ extent, 0.0f, offset }, major, i == 0);
            DrawMeterGridLine(Vector3{ offset, 0.0f, -extent }, Vector3{ offset, 0.0f, extent }, major, i == 0);
        }
        return;
    }

    const Vector3 forward = Vector3Normalize(Vector3Subtract(orbit.camera.target, orbit.camera.position));
    const float absX = std::fabs(forward.x);
    const float absY = std::fabs(forward.y);
    const float absZ = std::fabs(forward.z);

    for (int i = -halfCells; i <= halfCells; ++i)
    {
        const float offset = static_cast<float>(i) * kMetersPerGridCell;
        const bool major = i == 0 || i % 5 == 0;
        if (absY >= absX && absY >= absZ)
        {
            DrawMeterGridLine(Vector3{ -extent, 0.0f, offset }, Vector3{ extent, 0.0f, offset }, major, i == 0);
            DrawMeterGridLine(Vector3{ offset, 0.0f, -extent }, Vector3{ offset, 0.0f, extent }, major, i == 0);
        }
        else if (absZ >= absX)
        {
            const float z = orbit.target.z;
            DrawMeterGridLine(Vector3{ -extent, offset, z }, Vector3{ extent, offset, z }, major, i == 0);
            DrawMeterGridLine(Vector3{ offset, -extent, z }, Vector3{ offset, extent, z }, major, false);
        }
        else
        {
            const float x = orbit.target.x;
            DrawMeterGridLine(Vector3{ x, offset, -extent }, Vector3{ x, offset, extent }, major, i == 0);
            DrawMeterGridLine(Vector3{ x, -extent, offset }, Vector3{ x, extent, offset }, major, false);
        }
    }
}

void UpdateLitShader(const LitShader& lit, const OrbitCamera& orbit)
{
    if (!lit.valid) return;

    const float viewPosition[3] = { orbit.camera.position.x, orbit.camera.position.y, orbit.camera.position.z };
    Vector3 light = Vector3Normalize(Vector3Subtract(orbit.target, orbit.camera.position));
    light = Vector3Normalize(Vector3Add(light, Vector3{ 0.0f, -0.35f, 0.0f }));
    const float lightDirection[3] = { light.x, light.y, light.z };
    const float lightColor[4] = { 1.12f, 1.08f, 1.0f, 1.0f };
    const float ambient[4] = { 0.46f, 0.48f, 0.50f, 1.0f };

    SetShaderValue(lit.shader, lit.viewPositionLoc, viewPosition, SHADER_UNIFORM_VEC3);
    SetShaderValue(lit.shader, lit.lightDirectionLoc, lightDirection, SHADER_UNIFORM_VEC3);
    SetShaderValue(lit.shader, lit.lightColorLoc, lightColor, SHADER_UNIFORM_VEC4);
    SetShaderValue(lit.shader, lit.ambientLoc, ambient, SHADER_UNIFORM_VEC4);
}

bool IsViewportNodeVisible(const ModelTab& tab, int nodeIndex);

const std::vector<BoneSegment>& GetVisibleBones(const ModelTab& tab)
{
    const std::vector<BoneSegment>& source = tab.visibleBones.empty() ? tab.loaded.bones : tab.visibleBones;
    if (tab.deletedNodes.empty() && tab.hiddenNodes.empty() && tab.isolatedNode < 0) return source;

    static std::vector<BoneSegment> filteredBones;
    filteredBones.clear();
    for (const BoneSegment& bone : source)
    {
        if (!IsViewportNodeVisible(tab, bone.startNode) || !IsViewportNodeVisible(tab, bone.endNode)) continue;
        filteredBones.push_back(bone);
    }
    return filteredBones;
}

const std::vector<BonePose>& GetVisibleBonePoses(const ModelTab& tab)
{
    const std::vector<BonePose>& source = tab.visibleBonePoses.empty() ? tab.loaded.bonePoses : tab.visibleBonePoses;
    if (tab.deletedNodes.empty() && tab.hiddenNodes.empty() && tab.isolatedNode < 0) return source;

    static std::vector<BonePose> filteredPoses;
    filteredPoses.clear();
    for (const BonePose& pose : source)
    {
        if (!IsViewportNodeVisible(tab, pose.node)) continue;
        filteredPoses.push_back(pose);
    }
    return filteredPoses;
}

int GetAnimationFrameIndex(const LoadedFbxModel& loaded, const AnimationState& animation)
{
    if (animation.clipIndex < 0 || animation.clipIndex >= static_cast<int>(loaded.animations.size()))
    {
        return -1;
    }

    const AnimationClip& clip = loaded.animations[static_cast<size_t>(animation.clipIndex)];
    if (clip.duration <= 0.0f || clip.frames.empty())
    {
        return clip.frames.empty() ? -1 : 0;
    }

    const float normalizedTime = ClampFloat(animation.time, 0.0f, clip.duration);
    const float frameAlpha = normalizedTime / clip.duration * static_cast<float>(clip.frames.size() - 1);
    return static_cast<int>(ClampFloat(std::round(frameAlpha), 0.0f, static_cast<float>(clip.frames.size() - 1)));
}

struct MeshFrameSample
{
    int first = -1;
    int second = -1;
    float alpha = 0.0f;
};

MeshFrameSample GetFrameSample(float duration, size_t frameCount, float time)
{
    if (duration <= 0.0f || frameCount == 0)
    {
        return MeshFrameSample{ frameCount == 0 ? -1 : 0, frameCount == 0 ? -1 : 0, 0.0f };
    }

    const float normalizedTime = ClampFloat(time, 0.0f, duration);
    const float framePosition = normalizedTime / duration * static_cast<float>(frameCount - 1);
    const int first = static_cast<int>(ClampFloat(std::floor(framePosition), 0.0f, static_cast<float>(frameCount - 1)));
    const int second = std::min(first + 1, static_cast<int>(frameCount) - 1);
    return MeshFrameSample{ first, second, framePosition - static_cast<float>(first) };
}

MeshFrameSample GetMeshFrameSample(const AnimationClip& clip, float time)
{
    return GetFrameSample(clip.duration, clip.meshFrames.size(), time);
}

bool IsDescendantNode(const LoadedFbxModel& loaded, int possibleDescendant, int ancestor);

bool IsNodeVisibleInIsolation(const ModelTab& tab, int nodeIndex)
{
    if (tab.isolatedNode < 0) return true;
    if (nodeIndex < 0 || nodeIndex >= static_cast<int>(tab.loaded.nodes.size())) return false;
    if (tab.isolatedNode >= static_cast<int>(tab.loaded.nodes.size())) return true;
    return nodeIndex == tab.isolatedNode || IsDescendantNode(tab.loaded, nodeIndex, tab.isolatedNode);
}

bool IsViewportNodeVisible(const ModelTab& tab, int nodeIndex)
{
    return !(nodeIndex >= 0 && nodeIndex < static_cast<int>(tab.hiddenNodes.size()) &&
             tab.hiddenNodes[static_cast<size_t>(nodeIndex)]) &&
           !IsDeletedNode(tab, nodeIndex) && IsNodeVisibleInIsolation(tab, nodeIndex);
}

std::vector<int> BuildHiddenMeshNodes(const ModelTab& tab, size_t vertexCount)
{
    // Resolve ownership once, preserving the first matching mesh for overlapping ranges.
    std::vector<int> hiddenMeshNodes;
    if (!tab.deletedNodes.empty() || !tab.hiddenNodes.empty() || tab.isolatedNode >= 0)
    {
        hiddenMeshNodes.assign(vertexCount, -2);
        for (int nodeIndex = 0; nodeIndex < static_cast<int>(tab.loaded.nodes.size()); ++nodeIndex)
        {
            const SceneNode& node = tab.loaded.nodes[static_cast<size_t>(nodeIndex)];
            if (node.type != SceneNodeType::Mesh || node.meshVertexStart < 0 || node.meshVertexCount <= 0) continue;
            const size_t start = static_cast<size_t>(node.meshVertexStart);
            const size_t end = std::min(start + static_cast<size_t>(node.meshVertexCount), hiddenMeshNodes.size());
            const int hiddenNode = IsViewportNodeVisible(tab, nodeIndex) ? -1 : nodeIndex;
            for (size_t vertex = start; vertex < end; ++vertex)
            {
                if (hiddenMeshNodes[vertex] == -2) hiddenMeshNodes[vertex] = hiddenNode;
            }
        }
    }
    return hiddenMeshNodes;
}

void UploadGlobalMeshFrame(ModelTab& tab, const float* vertices, const float* normals, size_t expectedFloats)
{
    if (!vertices || !normals || !tab.loaded.hasMesh) return;
    if (vertices != tab.currentVertices.data()) tab.currentVertices.assign(vertices, vertices + expectedFloats);
    if (normals != tab.currentNormals.data()) tab.currentNormals.assign(normals, normals + expectedFloats);
    const std::vector<int> hiddenMeshNodes = BuildHiddenMeshNodes(tab, expectedFloats / 3);

    for (int meshIndex = 0; meshIndex < tab.loaded.model.meshCount; ++meshIndex)
    {
        if (meshIndex >= static_cast<int>(tab.loaded.meshGlobalVertexIndices.size())) continue;

        Mesh& mesh = tab.loaded.model.meshes[meshIndex];
        const std::vector<int>& globalIndices = tab.loaded.meshGlobalVertexIndices[static_cast<size_t>(meshIndex)];
        if (globalIndices.size() != static_cast<size_t>(mesh.vertexCount)) continue;

        tab.blendedVertices.resize(globalIndices.size() * 3);
        tab.blendedNormals.resize(globalIndices.size() * 3);

        for (size_t localVertex = 0; localVertex < globalIndices.size(); ++localVertex)
        {
            const size_t globalBase = static_cast<size_t>(globalIndices[localVertex]) * 3;
            const size_t localBase = localVertex * 3;
            if (globalBase + 2 >= expectedFloats) continue;

            const int hiddenMeshNode = hiddenMeshNodes.empty() ? -1 : hiddenMeshNodes[globalBase / 3];

            if (hiddenMeshNode >= 0)
            {
                const Vector3 collapsed = tab.loaded.nodes[static_cast<size_t>(hiddenMeshNode)].position;
                tab.blendedVertices[localBase] = collapsed.x;
                tab.blendedVertices[localBase + 1] = collapsed.y;
                tab.blendedVertices[localBase + 2] = collapsed.z;
                tab.blendedNormals[localBase] = 0.0f;
                tab.blendedNormals[localBase + 1] = 1.0f;
                tab.blendedNormals[localBase + 2] = 0.0f;
            }
            else
            {
                tab.blendedVertices[localBase] = vertices[globalBase];
                tab.blendedVertices[localBase + 1] = vertices[globalBase + 1];
                tab.blendedVertices[localBase + 2] = vertices[globalBase + 2];
                tab.blendedNormals[localBase] = normals[globalBase];
                tab.blendedNormals[localBase + 1] = normals[globalBase + 1];
                tab.blendedNormals[localBase + 2] = normals[globalBase + 2];
            }
        }

        BuildMeshTangents(tab.blendedVertices, tab.blendedNormals, mesh.texcoords, tab.blendedTangents);
        const size_t vertexBytes = globalIndices.size() * 3 * sizeof(float);
        const size_t tangentBytes = globalIndices.size() * 4 * sizeof(float);
        std::memcpy(mesh.vertices, tab.blendedVertices.data(), vertexBytes);
        std::memcpy(mesh.normals, tab.blendedNormals.data(), vertexBytes);
        if (mesh.tangents && tab.blendedTangents.size() >= globalIndices.size() * 4)
        {
            std::memcpy(mesh.tangents, tab.blendedTangents.data(), tangentBytes);
        }
        UpdateMeshBuffer(mesh, 0, tab.blendedVertices.data(), static_cast<int>(vertexBytes), 0);
        UpdateMeshBuffer(mesh, 2, tab.blendedNormals.data(), static_cast<int>(vertexBytes), 0);
        if (mesh.tangents && tab.blendedTangents.size() >= globalIndices.size() * 4)
        {
            UpdateMeshBuffer(mesh, 4, tab.blendedTangents.data(), static_cast<int>(tangentBytes), 0);
        }
    }
}

