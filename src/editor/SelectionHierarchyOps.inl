bool IsDeletedNode(const ModelTab& tab, int nodeIndex)
{
    return nodeIndex >= 0 &&
           nodeIndex < static_cast<int>(tab.deletedNodes.size()) &&
           tab.deletedNodes[static_cast<size_t>(nodeIndex)];
}

bool IsSceneRootNode(const LoadedFbxModel& loaded, int nodeIndex)
{
    return nodeIndex >= 0 &&
           nodeIndex < static_cast<int>(loaded.nodes.size()) &&
           loaded.nodes[static_cast<size_t>(nodeIndex)].parent < 0;
}

bool IsValidSelectableNode(const ModelTab& tab, int nodeIndex)
{
    return nodeIndex >= 0 &&
           nodeIndex < static_cast<int>(tab.loaded.nodes.size()) &&
           !IsSceneRootNode(tab.loaded, nodeIndex) &&
           !IsDeletedNode(tab, nodeIndex);
}

bool IsNodeSelected(const ModelTab& tab, int nodeIndex)
{
    return std::find(tab.selectedNodes.begin(), tab.selectedNodes.end(), nodeIndex) != tab.selectedNodes.end();
}

void PruneSelectedNodes(ModelTab& tab)
{
    tab.selectedNodes.erase(std::remove_if(tab.selectedNodes.begin(), tab.selectedNodes.end(), [&](int nodeIndex)
    {
        return !IsValidSelectableNode(tab, nodeIndex);
    }), tab.selectedNodes.end());

    if (tab.selectedNode >= 0 && !IsNodeSelected(tab, tab.selectedNode))
    {
        tab.selectedNode = tab.selectedNodes.empty() ? -1 : tab.selectedNodes.back();
    }
    if (tab.selectedNode < 0 && !tab.selectedNodes.empty())
    {
        tab.selectedNode = tab.selectedNodes.back();
    }
}

void ClearNodeSelection(ModelTab& tab)
{
    tab.selectedNode = -1;
    tab.selectedNodes.clear();
}

void SetSingleSelectedNode(ModelTab& tab, int nodeIndex)
{
    if (!IsValidSelectableNode(tab, nodeIndex))
    {
        ClearNodeSelection(tab);
        return;
    }

    tab.selectedNode = nodeIndex;
    tab.selectedNodes.assign(1, nodeIndex);
}

void ToggleSelectedNode(ModelTab& tab, int nodeIndex)
{
    if (!IsValidSelectableNode(tab, nodeIndex)) return;

    const auto found = std::find(tab.selectedNodes.begin(), tab.selectedNodes.end(), nodeIndex);
    if (found != tab.selectedNodes.end())
    {
        tab.selectedNodes.erase(found);
        if (tab.selectedNode == nodeIndex)
        {
            tab.selectedNode = tab.selectedNodes.empty() ? -1 : tab.selectedNodes.back();
        }
        return;
    }

    tab.selectedNodes.push_back(nodeIndex);
    tab.selectedNode = nodeIndex;
}

void SelectNode(ModelTab& tab, int nodeIndex, bool additive)
{
    if (additive)
    {
        ToggleSelectedNode(tab, nodeIndex);
    }
    else
    {
        SetSingleSelectedNode(tab, nodeIndex);
    }
}

void SetVisibleNodeRangeSelection(ModelTab& tab,
                                  const std::vector<bool>& collapsed,
                                  int anchorNode,
                                  int targetNode,
                                  const std::vector<int>& baseSelection)
{
    if (!IsValidSelectableNode(tab, anchorNode) || !IsValidSelectableNode(tab, targetNode)) return;

    int anchorRow = -1;
    int targetRow = -1;
    const std::vector<int> order = BuildVisibleHierarchyOrder(tab, collapsed);
    for (int visibleRow = 0; visibleRow < static_cast<int>(order.size()); ++visibleRow)
    {
        const int i = order[static_cast<size_t>(visibleRow)];
        if (i == anchorNode) anchorRow = visibleRow;
        if (i == targetNode) targetRow = visibleRow;
    }

    if (anchorRow < 0 || targetRow < 0) return;

    const int firstRow = std::min(anchorRow, targetRow);
    const int lastRow = std::max(anchorRow, targetRow);
    tab.selectedNodes.clear();
    for (const int selectedNode : baseSelection)
    {
        if (IsValidSelectableNode(tab, selectedNode) &&
            std::find(tab.selectedNodes.begin(), tab.selectedNodes.end(), selectedNode) == tab.selectedNodes.end())
        {
            tab.selectedNodes.push_back(selectedNode);
        }
    }
    for (int visibleRow = 0; visibleRow < static_cast<int>(order.size()); ++visibleRow)
    {
        const int i = order[static_cast<size_t>(visibleRow)];
        if (visibleRow >= firstRow &&
            visibleRow <= lastRow &&
            IsValidSelectableNode(tab, i) &&
            std::find(tab.selectedNodes.begin(), tab.selectedNodes.end(), i) == tab.selectedNodes.end())
        {
            tab.selectedNodes.push_back(i);
        }
    }
    tab.selectedNode = targetNode;
}

void SetVisibleNodeRangeSelection(ModelTab& tab, const std::vector<bool>& collapsed, int anchorNode, int targetNode)
{
    SetVisibleNodeRangeSelection(tab, collapsed, anchorNode, targetNode, {});
}

std::string MakeUniqueSceneNodeName(const LoadedFbxModel& loaded, const char* baseName)
{
    const std::string base = baseName && baseName[0] ? baseName : "Merged Geometry";
    auto nameExists = [&](const std::string& name)
    {
        for (const SceneNode& node : loaded.nodes)
        {
            if (node.name == name) return true;
        }
        return false;
    };

    if (!nameExists(base)) return base;

    for (int suffix = 2; suffix < 10000; ++suffix)
    {
        const std::string candidate = base + " " + std::to_string(suffix);
        if (!nameExists(candidate)) return candidate;
    }
    return base + " Copy";
}

std::vector<int> BuildGlobalVertexMaterialMap(const LoadedFbxModel& loaded)
{
    const int vertexCount = static_cast<int>(loaded.bindVertices.size() / 3);
    std::vector<int> materialForVertex(static_cast<size_t>(vertexCount), 0);
    for (int meshIndex = 0; meshIndex < static_cast<int>(loaded.meshGlobalVertexIndices.size()); ++meshIndex)
    {
        int materialIndex = loaded.model.meshMaterial && meshIndex < loaded.model.meshCount ? loaded.model.meshMaterial[meshIndex] : meshIndex;
        materialIndex = ClampInt(materialIndex, 0, std::max(0, static_cast<int>(loaded.materialNames.size()) - 1));
        for (int globalVertex : loaded.meshGlobalVertexIndices[static_cast<size_t>(meshIndex)])
        {
            if (globalVertex >= 0 && globalVertex < vertexCount)
            {
                materialForVertex[static_cast<size_t>(globalVertex)] = materialIndex;
            }
        }
    }
    return materialForVertex;
}

template <typename T>
T* CopyEditorVectorToRaylibBuffer(const std::vector<T>& values)
{
    if (values.empty()) return nullptr;

    const size_t byteCount = values.size() * sizeof(T);
    void* memory = MemAlloc(static_cast<unsigned int>(byteCount));
    if (!memory) return nullptr;

    std::memcpy(memory, values.data(), byteCount);
    return static_cast<T*>(memory);
}

void FreeUnuploadedMeshBuffers(Mesh& mesh)
{
    if (mesh.vertices) MemFree(mesh.vertices);
    if (mesh.normals) MemFree(mesh.normals);
    if (mesh.texcoords) MemFree(mesh.texcoords);
    if (mesh.tangents) MemFree(mesh.tangents);
    mesh = Mesh{};
}

bool RebuildRaylibModelFromGlobalGeometry(ModelTab& tab, std::string& error)
{
    LoadedFbxModel& loaded = tab.loaded;
    if (!loaded.hasMesh || loaded.model.meshCount <= 0 || !loaded.model.meshes) return true;

    const int materialCount = std::max(1, static_cast<int>(loaded.materialNames.size()));
    std::vector<std::vector<int>> verticesByMaterial(static_cast<size_t>(materialCount));
    for (int meshIndex = 0; meshIndex < static_cast<int>(loaded.meshGlobalVertexIndices.size()); ++meshIndex)
    {
        int materialIndex = loaded.model.meshMaterial && meshIndex < loaded.model.meshCount ? loaded.model.meshMaterial[meshIndex] : meshIndex;
        materialIndex = ClampInt(materialIndex, 0, materialCount - 1);
        std::vector<int>& target = verticesByMaterial[static_cast<size_t>(materialIndex)];
        const std::vector<int>& source = loaded.meshGlobalVertexIndices[static_cast<size_t>(meshIndex)];
        target.insert(target.end(), source.begin(), source.end());
    }

    const std::vector<float>& verticesSource = tab.currentVertices.size() == loaded.bindVertices.size() ? tab.currentVertices : loaded.bindVertices;
    const std::vector<float>& normalsSource = tab.currentNormals.size() == loaded.bindNormals.size() ? tab.currentNormals : loaded.bindNormals;
    const std::vector<float>* uvSource = loaded.uvSets.empty() ? nullptr : &loaded.uvSets.front();

    std::vector<Mesh> meshes;
    std::vector<int> meshMaterials;
    std::vector<std::vector<int>> meshGlobalIndices;
    for (int materialIndex = 0; materialIndex < materialCount; ++materialIndex)
    {
        const std::vector<int>& globalIndices = verticesByMaterial[static_cast<size_t>(materialIndex)];
        if (globalIndices.empty()) continue;

        std::vector<float> vertices;
        std::vector<float> normals;
        std::vector<float> texcoords;
        vertices.reserve(globalIndices.size() * 3);
        normals.reserve(globalIndices.size() * 3);
        texcoords.reserve(globalIndices.size() * 2);

        for (int globalVertex : globalIndices)
        {
            const size_t vertexBase = static_cast<size_t>(globalVertex) * 3;
            const size_t uvBase = static_cast<size_t>(globalVertex) * 2;
            if (vertexBase + 2 >= verticesSource.size() || vertexBase + 2 >= normalsSource.size()) continue;

            vertices.push_back(verticesSource[vertexBase]);
            vertices.push_back(verticesSource[vertexBase + 1]);
            vertices.push_back(verticesSource[vertexBase + 2]);
            normals.push_back(normalsSource[vertexBase]);
            normals.push_back(normalsSource[vertexBase + 1]);
            normals.push_back(normalsSource[vertexBase + 2]);
            if (uvSource && uvBase + 1 < uvSource->size())
            {
                texcoords.push_back((*uvSource)[uvBase]);
                texcoords.push_back((*uvSource)[uvBase + 1]);
            }
            else
            {
                texcoords.push_back(0.0f);
                texcoords.push_back(0.0f);
            }
        }

        if (vertices.empty()) continue;

        std::vector<float> tangents;
        BuildMeshTangents(vertices, normals, texcoords.data(), tangents);

        Mesh mesh{};
        mesh.vertexCount = static_cast<int>(vertices.size() / 3);
        mesh.triangleCount = mesh.vertexCount / 3;
        mesh.vertices = CopyEditorVectorToRaylibBuffer(vertices);
        mesh.normals = CopyEditorVectorToRaylibBuffer(normals);
        mesh.texcoords = CopyEditorVectorToRaylibBuffer(texcoords);
        mesh.tangents = CopyEditorVectorToRaylibBuffer(tangents);
        if (!mesh.vertices || !mesh.normals || !mesh.texcoords || !mesh.tangents)
        {
            FreeUnuploadedMeshBuffers(mesh);
            for (Mesh& created : meshes) UnloadMesh(created);
            error = "Failed to allocate merged mesh buffers.";
            return false;
        }

        UploadMesh(&mesh, true);
        meshes.push_back(mesh);
        meshMaterials.push_back(materialIndex);
        meshGlobalIndices.push_back(globalIndices);
    }

    if (meshes.empty())
    {
        error = "Merged geometry produced no renderable triangles.";
        return false;
    }

    Model rebuilt{};
    rebuilt.meshCount = static_cast<int>(meshes.size());
    rebuilt.meshes = CopyEditorVectorToRaylibBuffer(meshes);
    rebuilt.meshMaterial = CopyEditorVectorToRaylibBuffer(meshMaterials);
    if (!rebuilt.meshes || !rebuilt.meshMaterial)
    {
        if (rebuilt.meshes) MemFree(rebuilt.meshes);
        if (rebuilt.meshMaterial) MemFree(rebuilt.meshMaterial);
        for (Mesh& mesh : meshes) UnloadMesh(mesh);
        error = "Failed to allocate merged model.";
        return false;
    }

    // Only geometry changes: retain material maps, the lighting shader and model transform.
    for (int meshIndex = 0; meshIndex < loaded.model.meshCount; ++meshIndex)
    {
        UnloadMesh(loaded.model.meshes[meshIndex]);
    }
    MemFree(loaded.model.meshes);
    MemFree(loaded.model.meshMaterial);
    loaded.model.meshCount = rebuilt.meshCount;
    loaded.model.meshes = rebuilt.meshes;
    loaded.model.meshMaterial = rebuilt.meshMaterial;
    loaded.meshGlobalVertexIndices = std::move(meshGlobalIndices);
    return true;
}

std::vector<int> GetMergeableMeshNodes(const ModelTab& tab, const std::vector<int>& contextNodes)
{
    std::vector<int> meshNodes;
    for (int nodeIndex : contextNodes)
    {
        if (!IsValidSelectableNode(tab, nodeIndex)) continue;
        const SceneNode& node = tab.loaded.nodes[static_cast<size_t>(nodeIndex)];
        if (node.type != SceneNodeType::Mesh || node.meshVertexStart < 0 || node.meshVertexCount < 3) continue;
        if (std::find(meshNodes.begin(), meshNodes.end(), nodeIndex) == meshNodes.end())
        {
            meshNodes.push_back(nodeIndex);
        }
    }
    return meshNodes;
}

int MergeSelectedGeometry(ModelTab& tab, const std::vector<int>& contextNodes, std::string& error)
{
    error.clear();
    const std::vector<int> meshNodes = GetMergeableMeshNodes(tab, contextNodes);
    if (meshNodes.size() < 2)
    {
        error = "Select at least two mesh objects to merge.";
        return 0;
    }
    if (tab.loaded.bindVertices.size() != tab.loaded.bindNormals.size())
    {
        error = "Cannot merge geometry with inconsistent vertex and normal buffers.";
        return 0;
    }

    const int oldVertexCount = static_cast<int>(tab.loaded.bindVertices.size() / 3);
    const bool hadCurrentMesh = tab.currentVertices.size() == tab.loaded.bindVertices.size() &&
                                tab.currentNormals.size() == tab.loaded.bindNormals.size();
    const std::vector<int> materialForVertex = BuildGlobalVertexMaterialMap(tab.loaded);
    std::vector<int> newVerticesByMaterial;
    newVerticesByMaterial.reserve(static_cast<size_t>(oldVertexCount));

    SceneNode merged{};
    merged.name = MakeUniqueSceneNodeName(tab.loaded, "Merged Geometry");
    merged.type = SceneNodeType::Mesh;
    merged.parent = tab.loaded.nodes[static_cast<size_t>(meshNodes.front())].parent;
    merged.meshVertexStart = oldVertexCount;
    merged.meshPolygonVertexStart = static_cast<int>(tab.loaded.meshPolygonVertexGlobalIndices.size());
    merged.meshHadNormals = true;
    merged.meshHadUvs = !tab.loaded.uvSets.empty();

    int missingSkinWeights = 0;
    int badSkinWeights = 0;
    int degenerateTriangles = 0;
    int sourcePolygonCount = 0;
    for (int nodeIndex : meshNodes)
    {
        const SceneNode& sourceNode = tab.loaded.nodes[static_cast<size_t>(nodeIndex)];
        merged.meshHasSkin = merged.meshHasSkin || sourceNode.meshHasSkin;
        merged.hasSkinBindPose = merged.hasSkinBindPose || sourceNode.hasSkinBindPose;
        missingSkinWeights += sourceNode.missingSkinWeightCount;
        badSkinWeights += sourceNode.badSkinWeightCount;
        degenerateTriangles += sourceNode.degenerateTriangleCount;
        sourcePolygonCount += sourceNode.meshPolygonCount > 0 ? sourceNode.meshPolygonCount : sourceNode.meshTriangleCount;

        const int start = std::max(0, sourceNode.meshVertexStart);
        const int end = std::min(sourceNode.meshVertexStart + sourceNode.meshVertexCount, oldVertexCount);
        for (int sourceVertex = start; sourceVertex < end; ++sourceVertex)
        {
            if (IsRemovedTriangle(sourceNode, sourceVertex)) continue;
            const int newVertex = static_cast<int>(tab.loaded.bindVertices.size() / 3);
            const size_t sourceBase = static_cast<size_t>(sourceVertex) * 3;
            const float bindX = tab.loaded.bindVertices[sourceBase];
            const float bindY = tab.loaded.bindVertices[sourceBase + 1];
            const float bindZ = tab.loaded.bindVertices[sourceBase + 2];
            const float normalX = tab.loaded.bindNormals[sourceBase];
            const float normalY = tab.loaded.bindNormals[sourceBase + 1];
            const float normalZ = tab.loaded.bindNormals[sourceBase + 2];
            tab.loaded.bindVertices.insert(tab.loaded.bindVertices.end(), { bindX, bindY, bindZ });
            tab.loaded.bindNormals.insert(tab.loaded.bindNormals.end(), { normalX, normalY, normalZ });
            if (hadCurrentMesh)
            {
                const float currentX = tab.currentVertices[sourceBase];
                const float currentY = tab.currentVertices[sourceBase + 1];
                const float currentZ = tab.currentVertices[sourceBase + 2];
                const float currentNormalX = tab.currentNormals[sourceBase];
                const float currentNormalY = tab.currentNormals[sourceBase + 1];
                const float currentNormalZ = tab.currentNormals[sourceBase + 2];
                tab.currentVertices.insert(tab.currentVertices.end(), { currentX, currentY, currentZ });
                tab.currentNormals.insert(tab.currentNormals.end(), { currentNormalX, currentNormalY, currentNormalZ });
            }
            if (sourceVertex < static_cast<int>(tab.loaded.skinnedVertices.size()))
            {
                tab.loaded.skinnedVertices.push_back(tab.loaded.skinnedVertices[static_cast<size_t>(sourceVertex)]);
            }
            else
            {
                SkinnedVertex skinned;
                skinned.bindPosition = Vector3{ bindX, bindY, bindZ };
                skinned.bindNormal = Vector3{ normalX, normalY, normalZ };
                tab.loaded.skinnedVertices.push_back(skinned);
            }
            tab.loaded.meshControlPointIndices.push_back(newVertex - merged.meshVertexStart);
            tab.loaded.meshPolygonVertexGlobalIndices.push_back(newVertex);
            newVerticesByMaterial.push_back(sourceVertex < static_cast<int>(materialForVertex.size()) ? materialForVertex[static_cast<size_t>(sourceVertex)] : 0);

            const size_t uvBase = static_cast<size_t>(sourceVertex) * 2;
            for (auto& presence : tab.loaded.uvSetPresence)
                presence.push_back(sourceVertex < static_cast<int>(presence.size()) ? presence[sourceVertex] : 0);
            for (std::vector<float>& uvSet : tab.loaded.uvSets)
            {
                if (uvBase + 1 < uvSet.size())
                {
                    uvSet.push_back(uvSet[uvBase]);
                    uvSet.push_back(uvSet[uvBase + 1]);
                }
                else
                {
                    uvSet.push_back(0.0f);
                    uvSet.push_back(0.0f);
                }
            }
        }
    }

    merged.meshVertexCount = static_cast<int>(tab.loaded.bindVertices.size() / 3) - merged.meshVertexStart;
    merged.meshPolygonVertexCount = merged.meshVertexCount;
    merged.meshTriangleCount = merged.meshVertexCount / 3;
    merged.meshPolygonCount = sourcePolygonCount > 0 ? sourcePolygonCount : merged.meshTriangleCount;
    merged.missingSkinWeightCount = missingSkinWeights;
    merged.badSkinWeightCount = badSkinWeights;
    merged.degenerateTriangleCount = degenerateTriangles;
    const int mergedNodeIndex = static_cast<int>(tab.loaded.nodes.size());

    for (int vertex = merged.meshVertexStart; vertex + 2 < merged.meshVertexStart + merged.meshVertexCount; vertex += 3)
    {
        tab.loaded.meshPolygonEdges.push_back(MeshEdge{ vertex, vertex + 1, mergedNodeIndex });
        tab.loaded.meshPolygonEdges.push_back(MeshEdge{ vertex + 1, vertex + 2, mergedNodeIndex });
        tab.loaded.meshPolygonEdges.push_back(MeshEdge{ vertex + 2, vertex, mergedNodeIndex });
    }

    tab.loaded.nodes.push_back(merged);
    tab.deletedNodes.resize(tab.loaded.nodes.size(), false);
    tab.collapsedNodes.resize(tab.loaded.nodes.size(), false);

    SceneNode& mergedNode = tab.loaded.nodes.back();
    RecomputeMeshNodeBounds(tab, mergedNode);
    if (mergedNode.hasBounds)
    {
        mergedNode.position = Vector3{
            (mergedNode.bounds.min.x + mergedNode.bounds.max.x) * 0.5f,
            (mergedNode.bounds.min.y + mergedNode.bounds.max.y) * 0.5f,
            (mergedNode.bounds.min.z + mergedNode.bounds.max.z) * 0.5f
        };
    }

    for (size_t offset = 0; offset < newVerticesByMaterial.size(); ++offset)
    {
        const int newVertex = merged.meshVertexStart + static_cast<int>(offset);
        const int materialIndex = ClampInt(newVerticesByMaterial[offset], 0, std::max(0, static_cast<int>(tab.loaded.materialNames.size()) - 1));
        bool added = false;
        for (int meshIndex = 0; meshIndex < static_cast<int>(tab.loaded.meshGlobalVertexIndices.size()); ++meshIndex)
        {
            const int meshMaterial = tab.loaded.model.meshMaterial && meshIndex < tab.loaded.model.meshCount ? tab.loaded.model.meshMaterial[meshIndex] : meshIndex;
            if (meshMaterial == materialIndex)
            {
                tab.loaded.meshGlobalVertexIndices[static_cast<size_t>(meshIndex)].push_back(newVertex);
                added = true;
                break;
            }
        }
        if (!added)
        {
            tab.loaded.meshGlobalVertexIndices.push_back({ newVertex });
        }
    }

    for (int nodeIndex : meshNodes)
    {
        tab.deletedNodes[static_cast<size_t>(nodeIndex)] = true;
    }
    tab.selectedNode = mergedNodeIndex;
    tab.selectedNodes.assign(1, mergedNodeIndex);
    tab.isolatedNode = -1;
    tab.skinningGeometry.reset();
    tab.viewportUvIslandCache = ViewportUvIslandCache{};
    RecomputeSceneBounds(tab);

    if (!RebuildRaylibModelFromGlobalGeometry(tab, error))
    {
        return 0;
    }

    if (HasCpuSkinnedMesh(tab.loaded))
    {
        RebuildSkinnedAnimationMeshFrames(tab);
        if (tab.animation.clipIndex < 0)
        {
            RebuildCurrentSkinnedMeshFromBones(tab);
        }
        else
        {
            RefreshDisplayedMesh(tab);
        }
    }
    else
    {
        RefreshDisplayedMesh(tab);
    }
    return static_cast<int>(meshNodes.size());
}

void RefreshDisplayedMesh(ModelTab& tab)
{
    tab.validationCache.dirty = true;
    if (!tab.loaded.hasMesh || tab.loaded.bindVertices.size() != tab.loaded.bindNormals.size()) return;

    if (tab.currentVertices.size() == tab.loaded.bindVertices.size() &&
        tab.currentNormals.size() == tab.loaded.bindNormals.size())
    {
        UploadGlobalMeshFrame(tab, tab.currentVertices.data(), tab.currentNormals.data(), tab.currentVertices.size());
        return;
    }

    UploadGlobalMeshFrame(tab, tab.loaded.bindVertices.data(), tab.loaded.bindNormals.data(), tab.loaded.bindVertices.size());
}

bool IsDescendantNode(const LoadedFbxModel& loaded, int possibleDescendant, int ancestor)
{
    int parent = possibleDescendant >= 0 && possibleDescendant < static_cast<int>(loaded.nodes.size()) ? loaded.nodes[static_cast<size_t>(possibleDescendant)].parent : -1;
    while (parent >= 0)
    {
        if (parent == ancestor) return true;
        parent = parent < static_cast<int>(loaded.nodes.size()) ? loaded.nodes[static_cast<size_t>(parent)].parent : -1;
    }
    return false;
}

int MarkNodeSubtreeDeleted(ModelTab& tab, int nodeIndex)
{
    if (nodeIndex < 0 || nodeIndex >= static_cast<int>(tab.loaded.nodes.size())) return 0;
    if (IsSceneRootNode(tab.loaded, nodeIndex)) return 0;
    if (tab.deletedNodes.size() != tab.loaded.nodes.size())
    {
        tab.deletedNodes.resize(tab.loaded.nodes.size(), false);
    }

    int deletedCount = 0;
    for (int i = 0; i < static_cast<int>(tab.loaded.nodes.size()); ++i)
    {
        if (i != nodeIndex && !IsDescendantNode(tab.loaded, i, nodeIndex)) continue;
        if (tab.deletedNodes[static_cast<size_t>(i)]) continue;
        tab.deletedNodes[static_cast<size_t>(i)] = true;
        ++deletedCount;
    }

    PruneSelectedNodes(tab);
    if (IsDeletedNode(tab, tab.isolatedNode))
    {
        tab.isolatedNode = -1;
    }
    RefreshDisplayedMesh(tab);
    return deletedCount;
}

std::vector<int> GetContextActionNodes(const ModelTab& tab, int contextNodeIndex)
{
    std::vector<int> nodes;
    if (!IsValidSelectableNode(tab, contextNodeIndex)) return nodes;

    if (!IsNodeSelected(tab, contextNodeIndex))
    {
        nodes.push_back(contextNodeIndex);
        return nodes;
    }

    for (int nodeIndex : tab.selectedNodes)
    {
        if (IsValidSelectableNode(tab, nodeIndex))
        {
            nodes.push_back(nodeIndex);
        }
    }
    return nodes;
}

std::vector<int> GetValidContextActionNodes(const ModelTab& tab, const std::vector<int>& contextNodeIndices)
{
    std::vector<int> nodes;
    for (int nodeIndex : contextNodeIndices)
    {
        if (IsValidSelectableNode(tab, nodeIndex) && std::find(nodes.begin(), nodes.end(), nodeIndex) == nodes.end())
        {
            nodes.push_back(nodeIndex);
        }
    }
    return nodes;
}

int GetContextAnchorNode(const ModelTab& tab, const std::vector<int>& contextNodeIndices)
{
    if (IsValidSelectableNode(tab, tab.selectedNode) &&
        std::find(contextNodeIndices.begin(), contextNodeIndices.end(), tab.selectedNode) != contextNodeIndices.end())
    {
        return tab.selectedNode;
    }
    return contextNodeIndices.empty() ? -1 : contextNodeIndices.front();
}

std::vector<int> GetContextActionNodes(const ModelTab& tab, int contextNodeIndex, const std::vector<int>& contextNodeIndices)
{
    const std::vector<int> frozenNodes = GetValidContextActionNodes(tab, contextNodeIndices);
    if (!frozenNodes.empty()) return frozenNodes;
    return GetContextActionNodes(tab, contextNodeIndex);
}

std::vector<int> GetContextActionRoots(const ModelTab& tab, const std::vector<int>& actionNodes)
{
    std::vector<int> roots;
    for (int nodeIndex : actionNodes)
    {
        if (!IsValidSelectableNode(tab, nodeIndex)) continue;

        bool coveredByContextAncestor = false;
        for (int otherNode : actionNodes)
        {
            if (otherNode == nodeIndex || !IsValidSelectableNode(tab, otherNode)) continue;
            if (IsDescendantNode(tab.loaded, nodeIndex, otherNode))
            {
                coveredByContextAncestor = true;
                break;
            }
        }
        if (!coveredByContextAncestor)
        {
            roots.push_back(nodeIndex);
        }
    }
    return roots;
}

std::vector<int> GetContextActionRoots(const ModelTab& tab, int contextNodeIndex, const std::vector<int>& contextNodeIndices)
{
    return GetContextActionRoots(tab, GetContextActionNodes(tab, contextNodeIndex, contextNodeIndices));
}

int FindReparentNearestBoneParent(const LoadedFbxModel& loaded, int nodeIndex)
{
    int parent = nodeIndex >= 0 && nodeIndex < static_cast<int>(loaded.nodes.size()) ? loaded.nodes[static_cast<size_t>(nodeIndex)].parent : -1;
    while (parent >= 0 && parent < static_cast<int>(loaded.nodes.size()))
    {
        if (loaded.nodes[static_cast<size_t>(parent)].type == SceneNodeType::Bone) return parent;
        parent = loaded.nodes[static_cast<size_t>(parent)].parent;
    }
    return -1;
}

Vector3 GetBonePosePositionOrNode(const LoadedFbxModel& loaded, const std::vector<BonePose>& poses, int nodeIndex)
{
    if (const BonePose* pose = FindBonePoseByNode(poses, nodeIndex))
    {
        return pose->position;
    }

    if (nodeIndex >= 0 && nodeIndex < static_cast<int>(loaded.nodes.size()))
    {
        return loaded.nodes[static_cast<size_t>(nodeIndex)].position;
    }

    return Vector3Zero();
}

std::vector<BoneSegment> BuildBoneSegmentsFromHierarchy(const LoadedFbxModel& loaded, const std::vector<BonePose>& poses)
{
    std::vector<BoneSegment> bones;
    for (int nodeIndex = 0; nodeIndex < static_cast<int>(loaded.nodes.size()); ++nodeIndex)
    {
        const SceneNode& node = loaded.nodes[static_cast<size_t>(nodeIndex)];
        if (node.type != SceneNodeType::Bone) continue;

        const int parent = FindReparentNearestBoneParent(loaded, nodeIndex);
        if (parent < 0) continue;

        bones.push_back(BoneSegment{
            GetBonePosePositionOrNode(loaded, poses, parent),
            GetBonePosePositionOrNode(loaded, poses, nodeIndex),
            parent,
            nodeIndex
        });
    }
    return bones;
}

void RebuildBoneSegmentsAfterReparent(ModelTab& tab)
{
    tab.loaded.bones = BuildBoneSegmentsFromHierarchy(tab.loaded, tab.loaded.bonePoses);
    for (AnimationClip& clip : tab.loaded.animations)
    {
        for (BoneFrame& frame : clip.frames)
        {
            frame.bones = BuildBoneSegmentsFromHierarchy(tab.loaded, frame.poses);
        }
    }

    if (tab.animation.clipIndex < 0)
    {
        tab.visibleBones = tab.loaded.bones;
        tab.visibleBonePoses = tab.loaded.bonePoses;
    }
    InvalidateDisplayedAnimationCaches(tab);
}

void RecomputeSceneNodeDepths(LoadedFbxModel& loaded)
{
    for (int nodeIndex = 0; nodeIndex < static_cast<int>(loaded.nodes.size()); ++nodeIndex)
    {
        int depth = 0;
        int parent = loaded.nodes[static_cast<size_t>(nodeIndex)].parent;
        std::vector<bool> visited(loaded.nodes.size(), false);
        while (parent >= 0 && parent < static_cast<int>(loaded.nodes.size()) && !visited[static_cast<size_t>(parent)])
        {
            visited[static_cast<size_t>(parent)] = true;
            ++depth;
            parent = loaded.nodes[static_cast<size_t>(parent)].parent;
        }
        loaded.nodes[static_cast<size_t>(nodeIndex)].depth = depth;
    }
}

std::vector<int> GetReparentDragRoots(const ModelTab& tab, int sourceNode)
{
    if (!IsValidSelectableNode(tab, sourceNode)) return {};
    if (IsNodeSelected(tab, sourceNode))
    {
        std::vector<int> selected = GetValidContextActionNodes(tab, tab.selectedNodes);
        std::vector<int> roots = GetContextActionRoots(tab, selected);
        if (!roots.empty()) return roots;
    }
    return { sourceNode };
}

int GetSceneRootNodeIndex(const LoadedFbxModel& loaded)
{
    for (int nodeIndex = 0; nodeIndex < static_cast<int>(loaded.nodes.size()); ++nodeIndex)
    {
        if (IsSceneRootNode(loaded, nodeIndex))
        {
            return nodeIndex;
        }
    }
    return -1;
}

bool CanReparentNodeRoots(const ModelTab& tab, const std::vector<int>& roots, int newParent)
{
    if (!IsValidSelectableNode(tab, newParent)) return false;
    if (roots.empty()) return false;

    bool changesParent = false;
    for (int root : roots)
    {
        if (!IsValidSelectableNode(tab, root)) return false;
        if (root == newParent) return false;
        if (IsDescendantNode(tab.loaded, newParent, root)) return false;
        changesParent = changesParent || tab.loaded.nodes[static_cast<size_t>(root)].parent != newParent;
    }
    return changesParent;
}

int ReparentNodeRoots(ModelTab& tab, const std::vector<int>& roots, int newParent)
{
    if (!CanReparentNodeRoots(tab, roots, newParent)) return 0;

    int changedCount = 0;
    for (int root : roots)
    {
        SceneNode& node = tab.loaded.nodes[static_cast<size_t>(root)];
        if (node.parent == newParent) continue;
        node.parent = newParent;
        ++changedCount;
    }

    if (changedCount <= 0) return 0;

    RecomputeSceneNodeDepths(tab.loaded);
    RebuildBoneSegmentsAfterReparent(tab);
    RefreshDisplayedMesh(tab);
    return changedCount;
}

int UnparentNodeRoots(ModelTab& tab, const std::vector<int>& roots)
{
    const int sceneRoot = GetSceneRootNodeIndex(tab.loaded);
    if (sceneRoot < 0 || roots.empty()) return 0;

    int changedCount = 0;
    for (int root : roots)
    {
        if (!IsValidSelectableNode(tab, root)) continue;
        SceneNode& node = tab.loaded.nodes[static_cast<size_t>(root)];
        if (node.parent == sceneRoot) continue;
        node.parent = sceneRoot;
        ++changedCount;
    }

    if (changedCount <= 0) return 0;

    RecomputeSceneNodeDepths(tab.loaded);
    RebuildBoneSegmentsAfterReparent(tab);
    RefreshDisplayedMesh(tab);
    return changedCount;
}

bool HasBoneNode(const ModelTab& tab, const std::vector<int>& nodeIndices)
{
    for (int nodeIndex : nodeIndices)
    {
        if (!IsValidSelectableNode(tab, nodeIndex)) continue;
        if (tab.loaded.nodes[static_cast<size_t>(nodeIndex)].type == SceneNodeType::Bone)
        {
            return true;
        }
    }
    return false;
}

bool HasMeshNode(const ModelTab& tab, const std::vector<int>& nodeIndices)
{
    for (int nodeIndex : nodeIndices)
    {
        if (!IsValidSelectableNode(tab, nodeIndex)) continue;
        if (tab.loaded.nodes[static_cast<size_t>(nodeIndex)].type == SceneNodeType::Mesh)
        {
            return true;
        }
    }
    return false;
}

float GetNodeContextMenuHeight(const ModelTab& tab, const std::vector<int>& contextNodes)
{
    float height = kNodeContextMenuBaseH;
    if (HasMeshNode(tab, contextNodes))
    {
        height += 90.0f;
        if (GetMergeableMeshNodes(tab, contextNodes).size() >= 2)
        {
            height += 30.0f;
        }
    }
    if (HasBoneNode(tab, contextNodes))
    {
        height += 30.0f;
    }
    return height;
}

float GetNodeContextMenuHeight(const ModelTab& tab, int contextNodeIndex, const std::vector<int>& contextNodeIndices)
{
    return GetNodeContextMenuHeight(tab, GetContextActionNodes(tab, contextNodeIndex, contextNodeIndices));
}

int DeleteContextNodeSubtrees(ModelTab& tab, const std::vector<int>& contextRoots)
{
    int deletedCount = 0;
    for (int nodeIndex : contextRoots)
    {
        deletedCount += MarkNodeSubtreeDeleted(tab, nodeIndex);
    }
    return deletedCount;
}

int ApplyScaleToContextNodes(ModelTab& tab, const std::vector<int>& contextNodes)
{
    int changedCount = 0;
    for (int nodeIndex : contextNodes)
    {
        if (ApplyScaleToNode(tab, nodeIndex))
        {
            ++changedCount;
        }
    }
    return changedCount;
}

int SetMeshPivotsToBoundsCenterForContextNodes(ModelTab& tab, const std::vector<int>& contextNodes)
{
    int changedCount = 0;
    for (int nodeIndex : contextNodes)
    {
        if (SetMeshPivotToBoundsCenter(tab, nodeIndex))
        {
            ++changedCount;
        }
    }
    return changedCount;
}

int SetMeshPivotsToBoundsBottomForContextNodes(ModelTab& tab, const std::vector<int>& contextNodes)
{
    int changedCount = 0;
    for (int nodeIndex : contextNodes)
    {
        if (SetMeshPivotToBoundsBottom(tab, nodeIndex))
        {
            ++changedCount;
        }
    }
    return changedCount;
}

int FlipMeshNormalsForContextNodes(ModelTab& tab, const std::vector<int>& contextNodes)
{
    int changedCount = 0;
    for (int nodeIndex : contextNodes)
    {
        if (FlipMeshNormals(tab, nodeIndex))
        {
            ++changedCount;
        }
    }
    return changedCount;
}

int ResetContextBoneSubtreesToOriginalBindPose(ModelTab& tab, const std::vector<int>& actionNodes)
{
    int changedCount = 0;
    for (int nodeIndex : actionNodes)
    {
        if (!IsValidSelectableNode(tab, nodeIndex)) continue;
        if (tab.loaded.nodes[static_cast<size_t>(nodeIndex)].type != SceneNodeType::Bone) continue;

        bool coveredBySelectedBoneAncestor = false;
        for (int otherNode : actionNodes)
        {
            if (otherNode == nodeIndex || !IsValidSelectableNode(tab, otherNode)) continue;
            if (tab.loaded.nodes[static_cast<size_t>(otherNode)].type != SceneNodeType::Bone) continue;
            if (IsDescendantNode(tab.loaded, nodeIndex, otherNode))
            {
                coveredBySelectedBoneAncestor = true;
                break;
            }
        }
        if (coveredBySelectedBoneAncestor) continue;

        if (ResetBoneSubtreeToOriginalBindPose(tab, nodeIndex))
        {
            ++changedCount;
        }
    }
    return changedCount;
}

void ToggleSelectedNodeVisibility(ModelTab& tab)
{
    tab.hiddenNodes.resize(tab.loaded.nodes.size(), false);
    std::vector<int> nodes = tab.selectedNodes;
    if (nodes.empty() && IsValidSelectableNode(tab, tab.selectedNode)) nodes.push_back(tab.selectedNode);
    for (int nodeIndex : nodes)
    {
        if (!IsValidSelectableNode(tab, nodeIndex)) continue;
        const size_t index = static_cast<size_t>(nodeIndex);
        tab.hiddenNodes[index] = !tab.hiddenNodes[index];
    }
    RefreshDisplayedMesh(tab);
}

void ShowAllNodes(ModelTab& tab)
{
    tab.hiddenNodes.clear();
    tab.isolatedNode = -1;
    RefreshDisplayedMesh(tab);
}

int FixDegenerateTriangles(ModelTab& tab, int nodeIndex)
{
    if (!IsValidSelectableNode(tab, nodeIndex)) return 0;
    SceneNode& node = tab.loaded.nodes[static_cast<size_t>(nodeIndex)];
    if (node.type != SceneNodeType::Mesh) return 0;
    std::vector<int> removed;
    const int end = std::min(node.meshVertexStart + node.meshVertexCount, static_cast<int>(tab.loaded.bindVertices.size() / 3));
    int vertex = std::max(0, node.meshVertexStart);
    std::vector<int> polygonCounts = node.sourcePolygonTriangleCounts;
    if (polygonCounts.empty()) polygonCounts.assign(static_cast<size_t>(std::max(0, end - vertex) / 3), 1);
    int removedFaces = 0;
    for (int count : polygonCounts)
    {
        int surviving = 0;
        int newlyRemoved = 0;
        int previouslyRemoved = 0;
        for (int triangle = 0; triangle < count && vertex + triangle * 3 + 2 < end; ++triangle)
        {
            const int triangleStart = vertex + triangle * 3;
            if (IsRemovedTriangle(node, triangleStart)) { ++previouslyRemoved; continue; }
            const float* data = tab.loaded.bindVertices.data() + triangleStart * 3;
            const Vector3 a{ data[0], data[1], data[2] }, b{ data[3], data[4], data[5] }, c{ data[6], data[7], data[8] };
            if (Vector3LengthSqr(Vector3CrossProduct(Vector3Subtract(b, a), Vector3Subtract(c, a))) <= 0.000000000001f)
            {
                removed.push_back(triangleStart);
                ++newlyRemoved;
            }
            else ++surviving;
        }
        // A partially repaired polygon exports as its surviving fan triangles.
        if (newlyRemoved > 0) removedFaces += previouslyRemoved > 0 ? newlyRemoved : 1 - surviving;
        vertex += count * 3;
    }
    if (removed.empty()) return 0;
    PushUndoSnapshot(tab);
    node.removedTriangleStarts.insert(node.removedTriangleStarts.end(), removed.begin(), removed.end());
    std::sort(node.removedTriangleStarts.begin(), node.removedTriangleStarts.end());
    node.degenerateTriangleCount = std::max(0, node.degenerateTriangleCount - static_cast<int>(removed.size()));
    node.meshPolygonCount = std::max(0, node.meshPolygonCount - removedFaces);
    node.meshTriangleCount = std::max(0, node.meshVertexCount / 3 - static_cast<int>(node.removedTriangleStarts.size()));
    // Preserve source vertex indices, UVs and skin weights for animation and undo.
    tab.viewportUvIslandCache = ViewportUvIslandCache{};
    RefreshDisplayedMesh(tab);
    return static_cast<int>(removed.size());
}

bool ToggleSelectedNodeIsolation(ModelTab& tab, std::string& notice, std::string& error)
{
    if (tab.isolatedNode >= 0 && tab.isolatedNode == tab.selectedNode)
    {
        tab.isolatedNode = -1;
        RefreshDisplayedMesh(tab);
        notice = "Isolation off.";
        error.clear();
        return true;
    }

    if (tab.selectedNode < 0 ||
        tab.selectedNode >= static_cast<int>(tab.loaded.nodes.size()) ||
        IsDeletedNode(tab, tab.selectedNode))
    {
        if (tab.isolatedNode >= 0)
        {
            tab.isolatedNode = -1;
            RefreshDisplayedMesh(tab);
            notice = "Isolation off.";
            error.clear();
            return true;
        }

        error = "Select an object to isolate.";
        notice.clear();
        return false;
    }

    tab.isolatedNode = tab.selectedNode;
    RefreshDisplayedMesh(tab);
    notice = "Isolated: " + tab.loaded.nodes[static_cast<size_t>(tab.isolatedNode)].name;
    error.clear();
    return true;
}

void RenameSceneNode(ModelTab& tab, int nodeIndex, const std::string& newName)
{
    tab.validationCache.dirty = true;
    if (nodeIndex < 0 || nodeIndex >= static_cast<int>(tab.loaded.nodes.size()) || newName.empty()) return;
    if (IsSceneRootNode(tab.loaded, nodeIndex)) return;

    SceneNode& node = tab.loaded.nodes[static_cast<size_t>(nodeIndex)];
    const std::string oldName = node.name;
    if (oldName == newName) return;
    node.name = newName;
    tab.skinningGeometry.reset();

    if (node.type == SceneNodeType::Bone)
    {
        for (SkinnedVertex& vertex : tab.loaded.skinnedVertices)
        {
            for (SkinnedVertexInfluence& influence : vertex.influences)
            {
                if (influence.boneName == oldName)
                {
                    influence.boneName = newName;
                }
            }
        }
    }
}

void StartRenameNode(RenameEditor& editor, const LoadedFbxModel& loaded, int nodeIndex)
{
    if (nodeIndex < 0 || nodeIndex >= static_cast<int>(loaded.nodes.size())) return;
    if (IsSceneRootNode(loaded, nodeIndex)) return;
    editor = RenameEditor{};
    editor.target = RenameTarget::SceneNode;
    editor.nodeIndex = nodeIndex;
    editor.active = true;
    editor.justOpened = true;
    std::snprintf(editor.text, sizeof(editor.text), "%s", loaded.nodes[static_cast<size_t>(nodeIndex)].name.c_str());
    editor.cursor = static_cast<int>(std::strlen(editor.text));
    editor.textSelected = editor.cursor > 0;
}

void StartRenameAnimation(RenameEditor& editor, const LoadedFbxModel& loaded, int clipIndex)
{
    if (clipIndex < 0 || clipIndex >= static_cast<int>(loaded.animations.size())) return;
    editor = RenameEditor{};
    editor.target = RenameTarget::AnimationClip;
    editor.clipIndex = clipIndex;
    editor.active = true;
    editor.justOpened = true;
    std::snprintf(editor.text, sizeof(editor.text), "%s", loaded.animations[static_cast<size_t>(clipIndex)].name.c_str());
    editor.cursor = static_cast<int>(std::strlen(editor.text));
    editor.textSelected = editor.cursor > 0;
}

void ClearRenameSelection(RenameEditor& editor)
{
    if (!editor.textSelected) return;
    editor.text[0] = '\0';
    editor.cursor = 0;
    editor.textSelected = false;
}

void InsertRenameText(RenameEditor& editor, const char* text)
{
    if (!text || text[0] == '\0') return;
    ClearRenameSelection(editor);

    const int length = static_cast<int>(std::strlen(editor.text));
    editor.cursor = std::clamp(editor.cursor, 0, length);

    for (const char* read = text; *read; ++read)
    {
        const unsigned char value = static_cast<unsigned char>(*read);
        if (value < 32 || value >= 127) continue;

        const int currentLength = static_cast<int>(std::strlen(editor.text));
        if (currentLength + 1 >= static_cast<int>(sizeof(editor.text))) break;

        std::memmove(editor.text + editor.cursor + 1,
                     editor.text + editor.cursor,
                     static_cast<size_t>(currentLength - editor.cursor + 1));
        editor.text[editor.cursor] = static_cast<char>(value);
        editor.cursor++;
    }
}

void DeleteRenameSelectionOrPreviousChar(RenameEditor& editor)
{
    if (editor.textSelected)
    {
        ClearRenameSelection(editor);
        return;
    }

    const int length = static_cast<int>(std::strlen(editor.text));
    editor.cursor = std::clamp(editor.cursor, 0, length);
    if (editor.cursor <= 0) return;

    std::memmove(editor.text + editor.cursor - 1,
                 editor.text + editor.cursor,
                 static_cast<size_t>(length - editor.cursor + 1));
    editor.cursor--;
}

void DeleteRenameSelectionOrNextChar(RenameEditor& editor)
{
    if (editor.textSelected)
    {
        ClearRenameSelection(editor);
        return;
    }

    const int length = static_cast<int>(std::strlen(editor.text));
    editor.cursor = std::clamp(editor.cursor, 0, length);
    if (editor.cursor >= length) return;

    std::memmove(editor.text + editor.cursor,
                 editor.text + editor.cursor + 1,
                 static_cast<size_t>(length - editor.cursor));
}

void DrawRenameEditor(Font font, ModelTab* active, RenameEditor& editor, std::string& notice, std::string& error)
{
    if (!editor.active || !active) return;

    const bool controlDown = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL);
    int key = GetCharPressed();
    while (key > 0)
    {
        if (!controlDown && key >= 32 && key < 127)
        {
            char value[2]{ static_cast<char>(key), '\0' };
            InsertRenameText(editor, value);
        }
        key = GetCharPressed();
    }

    if (IsKeyPressed(KEY_BACKSPACE))
    {
        DeleteRenameSelectionOrPreviousChar(editor);
    }
    if (IsKeyPressed(KEY_DELETE))
    {
        DeleteRenameSelectionOrNextChar(editor);
    }
    if (controlDown && IsKeyPressed(KEY_A))
    {
        editor.textSelected = std::strlen(editor.text) > 0;
        editor.cursor = static_cast<int>(std::strlen(editor.text));
    }
    if (controlDown && IsKeyPressed(KEY_V))
    {
        InsertRenameText(editor, GetClipboardText());
    }
    if (IsKeyPressed(KEY_LEFT))
    {
        const int length = static_cast<int>(std::strlen(editor.text));
        if (editor.textSelected)
        {
            editor.cursor = 0;
            editor.textSelected = false;
        }
        else
        {
            editor.cursor = std::max(0, std::clamp(editor.cursor, 0, length) - 1);
        }
    }
    if (IsKeyPressed(KEY_RIGHT))
    {
        const int length = static_cast<int>(std::strlen(editor.text));
        if (editor.textSelected)
        {
            editor.cursor = length;
            editor.textSelected = false;
        }
        else
        {
            editor.cursor = std::min(length, std::clamp(editor.cursor, 0, length) + 1);
        }
    }
    if (IsKeyPressed(KEY_HOME))
    {
        editor.cursor = 0;
        editor.textSelected = false;
    }
    if (IsKeyPressed(KEY_END))
    {
        editor.cursor = static_cast<int>(std::strlen(editor.text));
        editor.textSelected = false;
    }
    if (IsKeyPressed(KEY_ESCAPE))
    {
        editor = RenameEditor{};
        return;
    }

    const bool submit = IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER);
    const bool cancelClick = openfbx::UiMouseButtonPressed(MOUSE_BUTTON_LEFT);
    const float dialogW = 360.0f;
    const float dialogH = 128.0f;
    const Rectangle dialog{ (static_cast<float>(GetScreenWidth()) - dialogW) * 0.5f,
                            (static_cast<float>(GetScreenHeight()) - dialogH) * 0.5f,
                            dialogW,
                            dialogH };
    const Rectangle input{ dialog.x + 16.0f, dialog.y + 48.0f, dialog.width - 32.0f, 28.0f };
    const Rectangle okButton{ dialog.x + dialog.width - 166.0f, dialog.y + dialog.height - 38.0f, 70.0f, 24.0f };
    const Rectangle cancelButton{ dialog.x + dialog.width - 86.0f, dialog.y + dialog.height - 38.0f, 70.0f, 24.0f };

    DrawRectangleRec(Rectangle{ 0.0f, 0.0f, static_cast<float>(GetScreenWidth()), static_cast<float>(GetScreenHeight()) }, Color{ 0, 0, 0, 80 });
    DrawRectangleRec(dialog, Color{ 24, 27, 31, 250 });
    DrawRectangleLinesEx(dialog, 1.0f, Color{ 86, 96, 108, 255 });
    DrawUiText(font, editor.target == RenameTarget::UvSet ? "Rename UV Set" : editor.target == RenameTarget::AnimationClip ? "Rename Animation" : "Rename Object", dialog.x + 16.0f, dialog.y + 16.0f, 16.0f, Color{ 205, 213, 220, 255 });
    DrawRectangleRec(input, Color{ 14, 16, 19, 255 });
    DrawRectangleLinesEx(input, 1.0f, Color{ 94, 156, 214, 255 });
    if (editor.textSelected && editor.text[0] != '\0')
    {
        const float selectionW = std::min(input.width - 16.0f, MeasureTextEx(font, editor.text, 15.0f, 1.0f).x + 2.0f);
        DrawRectangleRec(Rectangle{ input.x + 7.0f, input.y + 5.0f, selectionW, input.height - 10.0f }, Color{ 52, 106, 158, 210 });
    }
    DrawUiTextClipped(font, editor.text, input.x + 8.0f, input.y + 6.0f, 15.0f, input.width - 16.0f, RAYWHITE);
    if (!editor.textSelected)
    {
        const int cursor = std::clamp(editor.cursor, 0, static_cast<int>(std::strlen(editor.text)));
        const std::string beforeCursor(editor.text, editor.text + cursor);
        const float caretX = std::min(input.x + input.width - 8.0f, input.x + 8.0f + MeasureTextEx(font, beforeCursor.c_str(), 15.0f, 1.0f).x);
        DrawRectangleRec(Rectangle{ caretX, input.y + 6.0f, 1.0f, input.height - 12.0f }, Color{ 230, 236, 242, 255 });
    }

    if (editor.justOpened)
    {
        editor.justOpened = false;
        return;
    }

    if (DrawPanelButton(font, okButton, "OK") || submit)
    {
        const std::string newName = editor.text;
        if (newName.empty())
        {
            error = "Name cannot be empty.";
            notice.clear();
        }
        else if (editor.target == RenameTarget::UvSet && editor.uvSetIndex >= 0 &&
                 editor.uvSetIndex < static_cast<int>(active->loaded.uvSetNames.size()))
        {
            auto& names = active->loaded.uvSetNames;
            const auto existing = std::find(names.begin(), names.end(), newName);
            if (newName.find_first_not_of(" \t\r\n") == std::string::npos ||
                (existing != names.end() && existing - names.begin() != editor.uvSetIndex))
            {
                error = "UV set names must be nonblank and unique.";
                notice.clear();
            }
            else
            {
                if (names[editor.uvSetIndex] != newName)
                {
                    PushUndoSnapshot(*active);
                    names[editor.uvSetIndex] = newName;
                    active->loaded.uvSetsEdited = true;
                }
                notice = "Renamed UV set.";
                error.clear();
                editor = RenameEditor{};
            }
        }
        else if (editor.target == RenameTarget::SceneNode)
        {
            const bool changed = editor.nodeIndex >= 0 &&
                                 editor.nodeIndex < static_cast<int>(active->loaded.nodes.size()) &&
                                 active->loaded.nodes[static_cast<size_t>(editor.nodeIndex)].name != newName;
            if (changed)
            {
                PushUndoSnapshot(*active);
            }
            RenameSceneNode(*active, editor.nodeIndex, newName);
            notice = "Renamed object.";
            error.clear();
            editor = RenameEditor{};
        }
        else if (editor.target == RenameTarget::AnimationClip &&
                 editor.clipIndex >= 0 &&
                 editor.clipIndex < static_cast<int>(active->loaded.animations.size()))
        {
            AnimationClip& clip = active->loaded.animations[static_cast<size_t>(editor.clipIndex)];
            if (clip.name != newName)
            {
                PushUndoSnapshot(*active);
                clip.name = newName;
            }
            notice = "Renamed animation.";
            error.clear();
            editor = RenameEditor{};
        }
    }
    if (DrawPanelButton(font, cancelButton, "Cancel") ||
        (cancelClick && !CheckCollisionPointRec(GetMousePosition(), dialog)))
    {
        editor = RenameEditor{};
    }
}

