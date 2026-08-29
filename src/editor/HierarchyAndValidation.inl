bool HasSceneNodeChildren(const LoadedFbxModel& loaded, int nodeIndex)
{
    for (const SceneNode& node : loaded.nodes)
    {
        if (node.parent == nodeIndex) return true;
    }
    return false;
}

bool HasVisibleSceneNodeChildren(const ModelTab& tab, int nodeIndex)
{
    for (int i = 0; i < static_cast<int>(tab.loaded.nodes.size()); ++i)
    {
        if (IsDeletedNode(tab, i)) continue;
        if (tab.loaded.nodes[static_cast<size_t>(i)].parent == nodeIndex) return true;
    }
    return false;
}

bool IsSceneNodeVisible(const LoadedFbxModel& loaded, const std::vector<bool>& collapsed, int nodeIndex)
{
    if (nodeIndex < 0 || nodeIndex >= static_cast<int>(loaded.nodes.size())) return false;
    if (IsSceneRootNode(loaded, nodeIndex)) return false;

    int parent = loaded.nodes[static_cast<size_t>(nodeIndex)].parent;
    while (parent >= 0)
    {
        if (!IsSceneRootNode(loaded, parent) &&
            parent < static_cast<int>(collapsed.size()) &&
            collapsed[static_cast<size_t>(parent)])
        {
            return false;
        }
        parent = loaded.nodes[static_cast<size_t>(parent)].parent;
    }
    return true;
}

bool IsSceneNodeVisible(const ModelTab& tab, const std::vector<bool>& collapsed, int nodeIndex)
{
    if (nodeIndex < 0 || nodeIndex >= static_cast<int>(tab.loaded.nodes.size())) return false;
    if (IsSceneRootNode(tab.loaded, nodeIndex)) return false;
    if (IsDeletedNode(tab, nodeIndex)) return false;

    int parent = tab.loaded.nodes[static_cast<size_t>(nodeIndex)].parent;
    while (parent >= 0)
    {
        if (!IsSceneRootNode(tab.loaded, parent) && IsDeletedNode(tab, parent)) return false;
        if (!IsSceneRootNode(tab.loaded, parent) &&
            parent < static_cast<int>(collapsed.size()) &&
            collapsed[static_cast<size_t>(parent)])
        {
            return false;
        }
        parent = parent < static_cast<int>(tab.loaded.nodes.size()) ? tab.loaded.nodes[static_cast<size_t>(parent)].parent : -1;
    }
    return true;
}

float GetHierarchyPanelHeight()
{
    return static_cast<float>(GetScreenHeight()) - 61.0f - gBottomPanelReservedHeight;
}

float GetHierarchyContentStartY()
{
    return 61.0f + 64.0f;
}

int CountVisibleSceneNodes(const LoadedFbxModel& loaded, const std::vector<bool>& collapsed)
{
    int count = 0;
    for (int i = 0; i < static_cast<int>(loaded.nodes.size()); ++i)
    {
        if (IsSceneNodeVisible(loaded, collapsed, i)) ++count;
    }
    return count;
}

int CountVisibleSceneNodes(const ModelTab& tab, const std::vector<bool>& collapsed)
{
    int count = 0;
    for (int i = 0; i < static_cast<int>(tab.loaded.nodes.size()); ++i)
    {
        if (IsSceneNodeVisible(tab, collapsed, i)) ++count;
    }
    return count;
}

int GetHierarchyDisplayDepth(const LoadedFbxModel& loaded, int nodeIndex)
{
    int depth = 0;
    int parent = nodeIndex >= 0 && nodeIndex < static_cast<int>(loaded.nodes.size()) ? loaded.nodes[static_cast<size_t>(nodeIndex)].parent : -1;
    while (parent >= 0 && parent < static_cast<int>(loaded.nodes.size()))
    {
        if (!IsSceneRootNode(loaded, parent))
        {
            ++depth;
        }
        parent = loaded.nodes[static_cast<size_t>(parent)].parent;
    }
    return depth;
}

int GetVisibleSceneNodeRow(const LoadedFbxModel& loaded, const std::vector<bool>& collapsed, int nodeIndex)
{
    int row = 0;
    for (int i = 0; i < static_cast<int>(loaded.nodes.size()); ++i)
    {
        if (!IsSceneNodeVisible(loaded, collapsed, i)) continue;
        if (i == nodeIndex) return row;
        ++row;
    }
    return -1;
}

int GetVisibleSceneNodeRow(const ModelTab& tab, const std::vector<bool>& collapsed, int nodeIndex)
{
    int row = 0;
    for (int i = 0; i < static_cast<int>(tab.loaded.nodes.size()); ++i)
    {
        if (!IsSceneNodeVisible(tab, collapsed, i)) continue;
        if (i == nodeIndex) return row;
        ++row;
    }
    return -1;
}

void RevealNodeInHierarchy(ModelTab& tab, HierarchyPanelState& panel, int nodeIndex)
{
    if (nodeIndex < 0 || nodeIndex >= static_cast<int>(tab.loaded.nodes.size())) return;
    if (IsDeletedNode(tab, nodeIndex)) return;

    int parent = tab.loaded.nodes[static_cast<size_t>(nodeIndex)].parent;
    while (parent >= 0)
    {
        if (parent < static_cast<int>(tab.collapsedNodes.size()))
        {
            tab.collapsedNodes[static_cast<size_t>(parent)] = false;
        }
        parent = tab.loaded.nodes[static_cast<size_t>(parent)].parent;
    }

    const int row = GetVisibleSceneNodeRow(tab, tab.collapsedNodes, nodeIndex);
    if (row < 0) return;

    constexpr float rowH = 22.0f;
    const float panelH = GetHierarchyPanelHeight();
    const float visibleRows = std::max(1.0f, std::floor((panelH - 64.0f) / rowH));
    const float maxScroll = std::max(0.0f, static_cast<float>(CountVisibleSceneNodes(tab, tab.collapsedNodes)) - visibleRows);

    if (static_cast<float>(row) < panel.scroll)
    {
        panel.scroll = static_cast<float>(row);
    }
    else if (static_cast<float>(row) >= panel.scroll + visibleRows)
    {
        panel.scroll = static_cast<float>(row) - visibleRows + 1.0f;
    }

    panel.scroll = ClampFloat(panel.scroll, 0.0f, maxScroll);
}

void UpdateHierarchyPanelInteraction(HierarchyPanelState& panel, const ModelTab* active)
{
    constexpr float panelX = 0.0f;
    constexpr float panelY = 61.0f;
    constexpr float collapsedW = 28.0f;
    constexpr float rowH = 22.0f;
    const float panelH = GetHierarchyPanelHeight();
    const Vector2 mouse = GetMousePosition();

    if (panel.hidden)
    {
        const Rectangle restoreRect{ panelX, panelY, collapsedW, panelH };
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(mouse, restoreRect))
        {
            panel.hidden = false;
        }
        return;
    }

    panel.width = ClampFloat(panel.width, 220.0f, std::min(620.0f, static_cast<float>(GetScreenWidth()) - 160.0f));

    const Rectangle titleRect{ panelX, panelY, panel.width, 30.0f };
    const Rectangle resizeRect{ panel.width - 5.0f, panelY, 10.0f, panelH };

    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(mouse, resizeRect))
    {
        panel.resizing = true;
    }
    if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT))
    {
        panel.resizing = false;
    }
    if (panel.resizing)
    {
        panel.width = ClampFloat(mouse.x, 220.0f, std::min(620.0f, static_cast<float>(GetScreenWidth()) - 160.0f));
    }
    else if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(mouse, titleRect))
    {
        panel.hidden = true;
        return;
    }

    const Rectangle panelRect{ panelX, panelY, panel.width, panelH };
    if (active && panel.activeTab == LeftPanelTab::Hierarchy && CheckCollisionPointRec(mouse, panelRect))
    {
        const float wheel = GetMouseWheelMove();
        if (std::fabs(wheel) > 0.0f)
        {
            const float visibleRows = std::max(0.0f, std::floor((panelH - 64.0f) / rowH));
            const float maxScroll = std::max(0.0f, static_cast<float>(CountVisibleSceneNodes(*active, active->collapsedNodes)) - visibleRows);
            panel.scroll = ClampFloat(panel.scroll - wheel * 3.0f, 0.0f, maxScroll);
        }
    }
}

float GetHierarchyPanelBlockWidth(const HierarchyPanelState& panel)
{
    return panel.hidden ? 28.0f : panel.width;
}

struct SceneStats
{
    int nodes = 0;
    int meshes = 0;
    int bones = 0;
    int empties = 0;
    int vertices = 0;
    int triangles = 0;
    int materials = 0;
};

SceneStats CalculateSceneStats(const LoadedFbxModel& loaded)
{
    SceneStats stats;
    std::vector<std::string> materialNames;

    for (int nodeIndex = 0; nodeIndex < static_cast<int>(loaded.nodes.size()); ++nodeIndex)
    {
        if (IsSceneRootNode(loaded, nodeIndex)) continue;

        const SceneNode& node = loaded.nodes[static_cast<size_t>(nodeIndex)];
        ++stats.nodes;
        switch (node.type)
        {
        case SceneNodeType::Mesh:
            ++stats.meshes;
            stats.vertices += node.meshVertexCount;
            stats.triangles += node.meshTriangleCount;
            if (!node.materialName.empty() && node.materialName != "None" &&
                std::find(materialNames.begin(), materialNames.end(), node.materialName) == materialNames.end())
            {
                materialNames.push_back(node.materialName);
            }
            break;
        case SceneNodeType::Bone:
            ++stats.bones;
            break;
        case SceneNodeType::Empty:
            ++stats.empties;
            break;
        }
    }

    stats.materials = static_cast<int>(materialNames.size());
    return stats;
}

struct SkeletonEntry
{
    std::string name;
    std::string parentName;
};

int FindNearestBoneParent(const LoadedFbxModel& loaded, int nodeIndex)
{
    int parent = nodeIndex >= 0 && nodeIndex < static_cast<int>(loaded.nodes.size()) ? loaded.nodes[static_cast<size_t>(nodeIndex)].parent : -1;
    while (parent >= 0 && parent < static_cast<int>(loaded.nodes.size()))
    {
        if (loaded.nodes[static_cast<size_t>(parent)].type == SceneNodeType::Bone) return parent;
        parent = loaded.nodes[static_cast<size_t>(parent)].parent;
    }
    return -1;
}

std::vector<SkeletonEntry> BuildSkeletonSignature(const LoadedFbxModel& loaded)
{
    std::vector<SkeletonEntry> entries;
    for (int i = 0; i < static_cast<int>(loaded.nodes.size()); ++i)
    {
        const SceneNode& node = loaded.nodes[static_cast<size_t>(i)];
        if (node.type != SceneNodeType::Bone) continue;

        const int parent = FindNearestBoneParent(loaded, i);
        entries.push_back(SkeletonEntry{
            node.name,
            parent >= 0 ? loaded.nodes[static_cast<size_t>(parent)].name : std::string{}
        });
    }
    return entries;
}

const SkeletonEntry* FindSkeletonEntry(const std::vector<SkeletonEntry>& entries, const std::string& name)
{
    for (const SkeletonEntry& entry : entries)
    {
        if (entry.name == name) return &entry;
    }
    return nullptr;
}

int CountDuplicateBoneNames(const std::vector<SkeletonEntry>& entries)
{
    int duplicates = 0;
    for (size_t i = 0; i < entries.size(); ++i)
    {
        for (size_t j = i + 1; j < entries.size(); ++j)
        {
            if (entries[i].name == entries[j].name)
            {
                ++duplicates;
                break;
            }
        }
    }
    return duplicates;
}

bool CompareSkeletonCompatibility(const LoadedFbxModel& base, const LoadedFbxModel& other, std::string& result)
{
    const std::vector<SkeletonEntry> baseBones = BuildSkeletonSignature(base);
    const std::vector<SkeletonEntry> otherBones = BuildSkeletonSignature(other);
    const int baseDuplicates = CountDuplicateBoneNames(baseBones);
    const int otherDuplicates = CountDuplicateBoneNames(otherBones);
    int missing = 0;
    int extra = 0;
    int parentMismatches = 0;

    for (const SkeletonEntry& baseEntry : baseBones)
    {
        const SkeletonEntry* otherEntry = FindSkeletonEntry(otherBones, baseEntry.name);
        if (!otherEntry)
        {
            ++missing;
            continue;
        }
        if (otherEntry->parentName != baseEntry.parentName)
        {
            ++parentMismatches;
        }
    }

    for (const SkeletonEntry& otherEntry : otherBones)
    {
        if (!FindSkeletonEntry(baseBones, otherEntry.name))
        {
            ++extra;
        }
    }

    const bool compatible = !baseBones.empty() &&
                            baseBones.size() == otherBones.size() &&
                            missing == 0 &&
                            extra == 0 &&
                            parentMismatches == 0 &&
                            baseDuplicates == 0 &&
                            otherDuplicates == 0;

    char line[512] = {};
    std::snprintf(line, sizeof(line),
                  "%s\nBase bones: %zu\nCompare bones: %zu\nMissing: %d\nExtra: %d\nParent mismatches: %d\nDuplicate names: %d / %d",
                  compatible ? "Compatible" : "Not compatible",
                  baseBones.size(),
                  otherBones.size(),
                  missing,
                  extra,
                  parentMismatches,
                  baseDuplicates,
                  otherDuplicates);
    result = line;
    return compatible;
}

std::unordered_map<std::string, int> BuildBoneNodeNameMap(const LoadedFbxModel& loaded)
{
    std::unordered_map<std::string, int> result;
    for (int i = 0; i < static_cast<int>(loaded.nodes.size()); ++i)
    {
        const SceneNode& node = loaded.nodes[static_cast<size_t>(i)];
        if (node.type == SceneNodeType::Bone && !node.name.empty())
        {
            result.emplace(node.name, i);
        }
    }
    return result;
}

std::string MakeUniqueClipName(const LoadedFbxModel& loaded, const std::string& desiredName)
{
    const std::string baseName = desiredName.empty() ? "Imported Animation" : desiredName;
    auto exists = [&](const std::string& name)
    {
        return std::any_of(loaded.animations.begin(), loaded.animations.end(), [&](const AnimationClip& clip) { return clip.name == name; });
    };

    if (!exists(baseName)) return baseName;
    for (int suffix = 2; suffix < 10000; ++suffix)
    {
        const std::string candidate = baseName + "_" + std::to_string(suffix);
        if (!exists(candidate)) return candidate;
    }
    return baseName + "_copy";
}

Matrix MatrixFromPose(const BonePose& pose)
{
    Matrix matrix = MatrixIdentity();
    matrix.m0 = pose.axisX.x * pose.scale.x;
    matrix.m1 = pose.axisX.y * pose.scale.x;
    matrix.m2 = pose.axisX.z * pose.scale.x;
    matrix.m4 = pose.axisY.x * pose.scale.y;
    matrix.m5 = pose.axisY.y * pose.scale.y;
    matrix.m6 = pose.axisY.z * pose.scale.y;
    matrix.m8 = pose.axisZ.x * pose.scale.z;
    matrix.m9 = pose.axisZ.y * pose.scale.z;
    matrix.m10 = pose.axisZ.z * pose.scale.z;
    matrix.m12 = pose.position.x;
    matrix.m13 = pose.position.y;
    matrix.m14 = pose.position.z;
    return matrix;
}

BonePose PoseFromMatrix(Matrix matrix, int nodeIndex)
{
    const Vector3 xColumn{ matrix.m0, matrix.m1, matrix.m2 };
    const Vector3 yColumn{ matrix.m4, matrix.m5, matrix.m6 };
    const Vector3 zColumn{ matrix.m8, matrix.m9, matrix.m10 };

    BonePose pose;
    pose.position = Vector3{ matrix.m12, matrix.m13, matrix.m14 };
    pose.scale = Vector3{
        std::max(0.000001f, Vector3Length(xColumn)),
        std::max(0.000001f, Vector3Length(yColumn)),
        std::max(0.000001f, Vector3Length(zColumn))
    };
    pose.axisX = NormalizeOrFallback(xColumn, Vector3{ 1.0f, 0.0f, 0.0f });
    pose.axisY = NormalizeOrFallback(yColumn, Vector3{ 0.0f, 1.0f, 0.0f });
    pose.axisZ = NormalizeOrFallback(zColumn, Vector3{ 0.0f, 0.0f, 1.0f });

    Matrix rotationMatrix = matrix;
    rotationMatrix.m0 = pose.axisX.x;
    rotationMatrix.m1 = pose.axisX.y;
    rotationMatrix.m2 = pose.axisX.z;
    rotationMatrix.m4 = pose.axisY.x;
    rotationMatrix.m5 = pose.axisY.y;
    rotationMatrix.m6 = pose.axisY.z;
    rotationMatrix.m8 = pose.axisZ.x;
    rotationMatrix.m9 = pose.axisZ.y;
    rotationMatrix.m10 = pose.axisZ.z;
    rotationMatrix.m12 = 0.0f;
    rotationMatrix.m13 = 0.0f;
    rotationMatrix.m14 = 0.0f;
    const Vector3 euler = QuaternionToEuler(QuaternionFromMatrix(rotationMatrix));
    pose.rotation = Vector3Scale(euler, RAD2DEG);
    pose.node = nodeIndex;
    return pose;
}

Matrix GetPoseMatrixByNode(const std::vector<Matrix>& matrices, int nodeIndex)
{
    if (nodeIndex >= 0 && nodeIndex < static_cast<int>(matrices.size())) return matrices[static_cast<size_t>(nodeIndex)];
    return MatrixIdentity();
}

const BonePose* FindFramePoseByNode(const BoneFrame& frame, int nodeIndex)
{
    for (const BonePose& pose : frame.poses)
    {
        if (pose.node == nodeIndex) return &pose;
    }
    return nullptr;
}

std::vector<Matrix> BuildBindPoseMatrices(const LoadedFbxModel& loaded)
{
    std::vector<Matrix> matrices(loaded.nodes.size(), MatrixIdentity());
    for (int i = 0; i < static_cast<int>(loaded.nodes.size()); ++i)
    {
        const SceneNode& node = loaded.nodes[static_cast<size_t>(i)];
        BonePose pose;
        pose.position = node.position;
        pose.axisX = node.axisX;
        pose.axisY = node.axisY;
        pose.axisZ = node.axisZ;
        pose.rotation = node.rotation;
        pose.scale = node.scale;
        pose.node = i;
        matrices[static_cast<size_t>(i)] = MatrixFromPose(pose);
    }

    for (const BonePose& pose : loaded.bonePoses)
    {
        if (pose.node >= 0 && pose.node < static_cast<int>(matrices.size()))
        {
            matrices[static_cast<size_t>(pose.node)] = MatrixFromPose(pose);
        }
    }
    return matrices;
}

std::vector<Matrix> BuildFramePoseMatrices(const LoadedFbxModel& loaded, const BoneFrame& frame)
{
    std::vector<Matrix> matrices = BuildBindPoseMatrices(loaded);
    for (const BonePose& pose : frame.poses)
    {
        if (pose.node >= 0 && pose.node < static_cast<int>(matrices.size()))
        {
            matrices[static_cast<size_t>(pose.node)] = MatrixFromPose(pose);
        }
    }
    return matrices;
}

BoneFrame RemapImportedBoneFrame(const BoneFrame& sourceFrame,
                                 const LoadedFbxModel& source,
                                 const LoadedFbxModel& target,
                                 const std::unordered_map<std::string, int>& sourceBoneNodesByName)
{
    BoneFrame frame;
    frame.time = sourceFrame.time;
    const std::vector<Matrix> sourceFrameMatrices = BuildFramePoseMatrices(source, sourceFrame);
    frame.poses.reserve(target.bonePoses.size());

    for (int targetNodeIndex = 0; targetNodeIndex < static_cast<int>(target.nodes.size()); ++targetNodeIndex)
    {
        const SceneNode& targetNode = target.nodes[static_cast<size_t>(targetNodeIndex)];
        if (targetNode.type != SceneNodeType::Bone) continue;

        const auto sourceNode = sourceBoneNodesByName.find(targetNode.name);
        if (sourceNode == sourceBoneNodesByName.end()) continue;

        const BonePose* sourcePose = FindFramePoseByNode(sourceFrame, sourceNode->second);
        BonePose remappedPose = sourcePose ? *sourcePose : PoseFromMatrix(GetPoseMatrixByNode(sourceFrameMatrices, sourceNode->second), targetNodeIndex);
        remappedPose.node = targetNodeIndex;
        frame.poses.push_back(remappedPose);
    }

    const std::vector<Matrix> targetFrameMatrices = BuildFramePoseMatrices(target, frame);
    frame.bones.reserve(target.bones.size());
    for (int targetNodeIndex = 0; targetNodeIndex < static_cast<int>(target.nodes.size()); ++targetNodeIndex)
    {
        const int parent = FindNearestBoneParent(target, targetNodeIndex);
        if (parent < 0) continue;
        if (target.nodes[static_cast<size_t>(targetNodeIndex)].type != SceneNodeType::Bone) continue;

        const Vector3 parentPosition = Vector3Transform(Vector3Zero(), GetPoseMatrixByNode(targetFrameMatrices, parent));
        const Vector3 nodePosition = Vector3Transform(Vector3Zero(), GetPoseMatrixByNode(targetFrameMatrices, targetNodeIndex));
        frame.bones.push_back(BoneSegment{ parentPosition, nodePosition, parent, targetNodeIndex });
    }

    return frame;
}

Vector3 TransformPosePoint(const BonePose& pose, Vector3 point)
{
    return Vector3Add(pose.position,
                      Vector3Add(Vector3Scale(pose.axisX, point.x * pose.scale.x),
                                 Vector3Add(Vector3Scale(pose.axisY, point.y * pose.scale.y),
                                            Vector3Scale(pose.axisZ, point.z * pose.scale.z))));
}

Vector3 TransformPoseVector(const BonePose& pose, Vector3 vector)
{
    return Vector3Add(Vector3Scale(pose.axisX, vector.x * pose.scale.x),
                      Vector3Add(Vector3Scale(pose.axisY, vector.y * pose.scale.y),
                                 Vector3Scale(pose.axisZ, vector.z * pose.scale.z)));
}

MeshFrame BuildSkinnedMeshFrame(const LoadedFbxModel& target, const BoneFrame& boneFrame)
{
    MeshFrame meshFrame;
    if (target.skinnedVertices.size() != target.bindVertices.size() / 3)
    {
        meshFrame.vertices = target.bindVertices;
        meshFrame.normals = target.bindNormals;
        return meshFrame;
    }

    std::unordered_map<std::string, const BonePose*> posesByName;
    for (const BonePose& pose : boneFrame.poses)
    {
        if (pose.node >= 0 && pose.node < static_cast<int>(target.nodes.size()))
        {
            posesByName[target.nodes[static_cast<size_t>(pose.node)].name] = &pose;
        }
    }

    meshFrame.vertices.reserve(target.bindVertices.size());
    meshFrame.normals.reserve(target.bindNormals.size());
    for (const SkinnedVertex& vertex : target.skinnedVertices)
    {
        Vector3 position{};
        Vector3 normal{};
        float totalWeight = 0.0f;

        for (const SkinnedVertexInfluence& influence : vertex.influences)
        {
            const auto pose = posesByName.find(influence.boneName);
            if (pose == posesByName.end() || !pose->second) continue;
            position = Vector3Add(position, Vector3Scale(TransformPosePoint(*pose->second, influence.bindPositionInBone), influence.weight));
            normal = Vector3Add(normal, Vector3Scale(TransformPoseVector(*pose->second, influence.bindNormalInBone), influence.weight));
            totalWeight += influence.weight;
        }

        if (totalWeight > 0.000001f)
        {
            position = Vector3Scale(position, 1.0f / totalWeight);
            normal = NormalizeOrFallback(Vector3Scale(normal, 1.0f / totalWeight), vertex.bindNormal);
        }
        else
        {
            position = vertex.bindPosition;
            normal = vertex.bindNormal;
        }

        meshFrame.vertices.push_back(position.x);
        meshFrame.vertices.push_back(position.y);
        meshFrame.vertices.push_back(position.z);
        meshFrame.normals.push_back(normal.x);
        meshFrame.normals.push_back(normal.y);
        meshFrame.normals.push_back(normal.z);
    }

    return meshFrame;
}

bool ImportAnimationsFromFbx(ModelTab& targetTab, const std::string& importPath, int& importedCount, std::string& error)
{
    importedCount = 0;
    error.clear();

    LoadedFbxModel source;
    if (!LoadFbxModel(importPath, source, error))
    {
        return false;
    }

    std::string compatibility;
    if (!CompareSkeletonCompatibility(targetTab.loaded, source, compatibility))
    {
        error = "Skeletons are not compatible.\n" + compatibility;
        UnloadFbxModel(source);
        return false;
    }

    const std::unordered_map<std::string, int> sourceBoneNodesByName = BuildBoneNodeNameMap(source);
    for (const AnimationClip& sourceClip : source.animations)
    {
        AnimationClip clip;
        clip.name = MakeUniqueClipName(targetTab.loaded, sourceClip.name);
        clip.duration = sourceClip.duration;
        clip.frames.reserve(sourceClip.frames.size());
        for (const BoneFrame& sourceFrame : sourceClip.frames)
        {
            clip.frames.push_back(RemapImportedBoneFrame(sourceFrame, source, targetTab.loaded, sourceBoneNodesByName));
        }
        clip.meshFrames.reserve(clip.frames.size());
        for (const BoneFrame& frame : clip.frames)
        {
            clip.meshFrames.push_back(BuildSkinnedMeshFrame(targetTab.loaded, frame));
        }

        targetTab.loaded.animations.push_back(std::move(clip));
        ++importedCount;
    }

    UnloadFbxModel(source);
    if (importedCount == 0)
    {
        error = "Compatible FBX loaded, but it contains no animation stacks.";
        return false;
    }

    return true;
}

const char* GetSceneNodeTypeJsonName(SceneNodeType type)
{
    switch (type)
    {
    case SceneNodeType::Mesh: return "mesh";
    case SceneNodeType::Bone: return "bone";
    case SceneNodeType::Empty: return "empty";
    }

    return "unknown";
}

void WriteJsonString(std::ostream& out, const std::string& value)
{
    out << '"';
    for (char c : value)
    {
        switch (c)
        {
        case '\\': out << "\\\\"; break;
        case '"': out << "\\\""; break;
        case '\b': out << "\\b"; break;
        case '\f': out << "\\f"; break;
        case '\n': out << "\\n"; break;
        case '\r': out << "\\r"; break;
        case '\t': out << "\\t"; break;
        default:
            if (static_cast<unsigned char>(c) < 0x20)
            {
                out << ' ';
            }
            else
            {
                out << c;
            }
            break;
        }
    }
    out << '"';
}

void WriteJsonVector3(std::ostream& out, Vector3 value)
{
    out << '[' << value.x << ", " << value.y << ", " << value.z << ']';
}

void WriteJsonBounds(std::ostream& out, const BoundingBox& bounds)
{
    out << "{ \"min\": ";
    WriteJsonVector3(out, bounds.min);
    out << ", \"max\": ";
    WriteJsonVector3(out, bounds.max);
    out << " }";
}

bool ExportFbxJson(const ModelTab& tab, std::string& outputPath, std::string& error)
{
    outputPath = tab.path + ".json";
    error.clear();

    std::ofstream out(outputPath, std::ios::out | std::ios::trunc);
    if (!out)
    {
        error = "Failed to write JSON: " + outputPath;
        return false;
    }

    const LoadedFbxModel& loaded = tab.loaded;
    const SceneStats stats = CalculateSceneStats(loaded);

    out << "{\n";
    out << "  \"source\": ";
    WriteJsonString(out, tab.path);
    out << ",\n";
    out << "  \"title\": ";
    WriteJsonString(out, tab.title);
    out << ",\n";
    out << "  \"bounds\": ";
    WriteJsonBounds(out, loaded.bounds);
    out << ",\n";
    out << "  \"stats\": {\n";
    out << "    \"nodes\": " << stats.nodes << ",\n";
    out << "    \"meshes\": " << stats.meshes << ",\n";
    out << "    \"bones\": " << stats.bones << ",\n";
    out << "    \"empties\": " << stats.empties << ",\n";
    out << "    \"vertices\": " << stats.vertices << ",\n";
    out << "    \"triangles\": " << stats.triangles << ",\n";
    out << "    \"materials\": " << stats.materials << "\n";
    out << "  },\n";

    out << "  \"nodes\": [\n";
    for (size_t i = 0; i < loaded.nodes.size(); ++i)
    {
        const SceneNode& node = loaded.nodes[i];
        out << "    {\n";
        out << "      \"index\": " << i << ",\n";
        out << "      \"name\": ";
        WriteJsonString(out, node.name);
        out << ",\n";
        out << "      \"type\": \"" << GetSceneNodeTypeJsonName(node.type) << "\",\n";
        out << "      \"parent\": " << node.parent << ",\n";
        out << "      \"depth\": " << node.depth << ",\n";
        out << "      \"position\": ";
        WriteJsonVector3(out, node.position);
        out << ",\n";
        out << "      \"rotation\": ";
        WriteJsonVector3(out, node.rotation);
        out << ",\n";
        out << "      \"scale\": ";
        WriteJsonVector3(out, node.scale);
        out << ",\n";
        out << "      \"axisX\": ";
        WriteJsonVector3(out, node.axisX);
        out << ",\n";
        out << "      \"axisY\": ";
        WriteJsonVector3(out, node.axisY);
        out << ",\n";
        out << "      \"axisZ\": ";
        WriteJsonVector3(out, node.axisZ);
        out << ",\n";
        out << "      \"hasBounds\": " << (node.hasBounds ? "true" : "false") << ",\n";
        out << "      \"bounds\": ";
        WriteJsonBounds(out, node.bounds);
        out << ",\n";
        out << "      \"meshVertexStart\": " << node.meshVertexStart << ",\n";
        out << "      \"meshVertexCount\": " << node.meshVertexCount << ",\n";
        out << "      \"meshTriangleCount\": " << node.meshTriangleCount << ",\n";
        out << "      \"material\": ";
        WriteJsonString(out, node.materialName);
        out << "\n";
        out << "    }" << (i + 1 < loaded.nodes.size() ? "," : "") << "\n";
    }
    out << "  ],\n";

    out << "  \"bones\": [\n";
    for (size_t i = 0; i < loaded.bones.size(); ++i)
    {
        const BoneSegment& bone = loaded.bones[i];
        out << "    { \"startNode\": " << bone.startNode << ", \"endNode\": " << bone.endNode << ", \"start\": ";
        WriteJsonVector3(out, bone.start);
        out << ", \"end\": ";
        WriteJsonVector3(out, bone.end);
        out << " }" << (i + 1 < loaded.bones.size() ? "," : "") << "\n";
    }
    out << "  ],\n";

    out << "  \"animations\": [\n";
    for (size_t i = 0; i < loaded.animations.size(); ++i)
    {
        const AnimationClip& clip = loaded.animations[i];
        out << "    {\n";
        out << "      \"name\": ";
        WriteJsonString(out, clip.name);
        out << ",\n";
        out << "      \"duration\": " << clip.duration << ",\n";
        out << "      \"boneFrames\": " << clip.frames.size() << ",\n";
        out << "      \"meshFrames\": " << clip.meshFrames.size() << ",\n";
        out << "      \"totalFrames\": " << std::max(clip.frames.size(), clip.meshFrames.size()) << "\n";
        out << "    }" << (i + 1 < loaded.animations.size() ? "," : "") << "\n";
    }
    out << "  ]\n";
    out << "}\n";

    if (!out)
    {
        error = "Failed to finish writing JSON: " + outputPath;
        return false;
    }

    return true;
}

std::string ToLower(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

std::string CompactName(std::string value)
{
    value = ToLower(std::move(value));
    value.erase(std::remove_if(value.begin(), value.end(), [](unsigned char c)
    {
        return !std::isalnum(c);
    }), value.end());
    return value;
}

bool LowerStemContainsName(const std::string& lowerStem, const std::string& lowerName)
{
    if (lowerName.empty()) return false;
    if (lowerStem.find(lowerName) != std::string::npos) return true;

    const std::string compactStem = CompactName(lowerStem);
    const std::string compactName = CompactName(lowerName);
    return !compactName.empty() && compactStem.find(compactName) != std::string::npos;
}

bool IsTextureExtension(const std::filesystem::path& path)
{
    const std::string extension = ToLower(path.extension().string());
    return extension == ".png" || extension == ".jpg" || extension == ".jpeg" ||
           extension == ".tga" || extension == ".bmp" || extension == ".psd" ||
           extension == ".gif" || extension == ".hdr";
}

bool IsOrmTextureName(const std::string& lowerStem)
{
    const std::string compactStem = CompactName(lowerStem);
    auto hasToken = [&](const char* token)
    {
        const std::string value(token);
        size_t position = lowerStem.find(value);
        while (position != std::string::npos)
        {
            const bool startsOnBoundary = position == 0 || !std::isalnum(static_cast<unsigned char>(lowerStem[position - 1]));
            const size_t end = position + value.size();
            const bool endsOnBoundary = end >= lowerStem.size() || !std::isalnum(static_cast<unsigned char>(lowerStem[end]));
            if (startsOnBoundary && endsOnBoundary) return true;
            position = lowerStem.find(value, position + 1);
        }
        return false;
    };

    return hasToken("orm") ||
           lowerStem.find("occlusionroughnessmetallic") != std::string::npos ||
           lowerStem.find("occlusion_roughness_metallic") != std::string::npos ||
           lowerStem.find("occlusion-roughness-metallic") != std::string::npos ||
           lowerStem.find("ao_roughness_metallic") != std::string::npos ||
           lowerStem.find("ao-roughness-metallic") != std::string::npos ||
           compactStem.find("occlusionroughnessmetallic") != std::string::npos ||
           compactStem.find("aoroughnessmetallic") != std::string::npos;
}

int LoadOrmTexture(ModelTab& tab, int materialIndex, const std::string& path, std::string& error)
{
    int loadedCount = 0;
    error.clear();

    for (PbrTextureSlot slot : { PbrTextureSlot::AmbientOcclusion, PbrTextureSlot::Roughness, PbrTextureSlot::Metallic })
    {
        std::string loadError;
        if (LoadPbrTexture(tab, materialIndex, slot, path, loadError))
        {
            ++loadedCount;
        }
        else if (error.empty())
        {
            error = loadError;
        }
    }

    return loadedCount;
}

int ScoreTextureCandidate(const std::string& lowerStem, const std::string& lowerModelStem, const std::string& lowerMaterialName, PbrTextureSlot slot)
{
    int score = !lowerModelStem.empty() && LowerStemContainsName(lowerStem, lowerModelStem) ? 3 : 0;
    if (LowerStemContainsName(lowerStem, lowerMaterialName)) score += 5;

    auto hasAny = [&](std::initializer_list<const char*> tokens)
    {
        for (const char* token : tokens)
        {
            if (lowerStem.find(token) != std::string::npos) return true;
        }
        return false;
    };

    switch (slot)
    {
    case PbrTextureSlot::Diffuse:
        if (hasAny({ "diffuse", "albedo", "basecolor", "base_color", "_col", "color" })) score += 10;
        if (hasAny({ "normal", "nrm", "rough", "metal", "ao", "occlusion", "opacity", "alpha", "transparency", "mask" })) score -= 8;
        break;
    case PbrTextureSlot::Normal:
        if (hasAny({ "normal", "_nrm", "_nor" })) score += 10;
        break;
    case PbrTextureSlot::Roughness:
        if (hasAny({ "roughness", "_rough", "_rgh" })) score += 10;
        if (IsOrmTextureName(lowerStem)) score += 12;
        break;
    case PbrTextureSlot::Metallic:
        if (hasAny({ "metallic", "metalness", "_metal", "_mtl" })) score += 10;
        if (IsOrmTextureName(lowerStem)) score += 12;
        break;
    case PbrTextureSlot::AmbientOcclusion:
        if (hasAny({ "_ao", "ambientocclusion", "ambient_occlusion", "occlusion" })) score += 10;
        if (IsOrmTextureName(lowerStem)) score += 12;
        break;
    case PbrTextureSlot::Emissive:
        if (hasAny({ "emissive", "emission", "emit", "_ems", "_emiss" })) score += 10;
        break;
    case PbrTextureSlot::Opacity:
        if (hasAny({ "opacity", "alpha", "transparency", "_trans", "_mask", "cutout" })) score += 10;
        break;
    case PbrTextureSlot::Count:
        break;
    }

    return score;
}

bool TextureNameMatchesMaterial(const std::filesystem::path& texturePath, const std::string& materialName)
{
    return LowerStemContainsName(ToLower(texturePath.stem().string()), ToLower(materialName));
}

std::string FindAutoTexturePath(const std::string& modelPath, const std::filesystem::path& directory, const std::string& materialName, PbrTextureSlot slot)
{
    const std::filesystem::path sourcePath(modelPath);
    if (directory.empty() || !std::filesystem::exists(directory)) return {};

    const std::string modelStem = ToLower(sourcePath.stem().string());
    const std::string materialStem = ToLower(materialName);
    int bestScore = 0;
    std::filesystem::path bestPath;

    for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(directory))
    {
        if (!entry.is_regular_file() || !IsTextureExtension(entry.path())) continue;

        const std::string stem = ToLower(entry.path().stem().string());
        const int score = ScoreTextureCandidate(stem, modelStem, materialStem, slot);
        if (score > bestScore)
        {
            bestScore = score;
            bestPath = entry.path();
        }
    }

    return bestScore > 0 ? bestPath.string() : std::string{};
}

int AutoAssignDroppedTextures(ModelTab& tab, const std::vector<std::string>& droppedPaths, std::string& notice, std::string& error)
{
    EnsurePbrMaterialStates(tab);
    if (tab.pbrMaterials.empty()) return 0;

    const std::string modelStem = ToLower(std::filesystem::path(tab.path).stem().string());
    const bool singleMaterial = tab.pbrMaterials.size() == 1;
    int loadedCount = 0;
    std::string firstError;

    for (const std::string& droppedPath : droppedPaths)
    {
        const std::filesystem::path texturePath(droppedPath);
        if (!IsTextureExtension(texturePath)) continue;

        const std::string stem = ToLower(texturePath.stem().string());
        const bool isOrmTexture = IsOrmTextureName(stem);
        int bestMaterial = -1;
        PbrTextureSlot bestSlot = PbrTextureSlot::Diffuse;
        int bestScore = 0;

        for (int materialIndex = 0; materialIndex < static_cast<int>(tab.pbrMaterials.size()); ++materialIndex)
        {
            const std::string materialName = materialIndex < static_cast<int>(tab.loaded.materialNames.size()) ? tab.loaded.materialNames[static_cast<size_t>(materialIndex)] : std::string{};
            if (!singleMaterial && !TextureNameMatchesMaterial(texturePath, materialName)) continue;

            const std::string lowerMaterialName = ToLower(materialName);
            for (int slotIndex = 0; slotIndex < static_cast<int>(PbrTextureSlot::Count); ++slotIndex)
            {
                const PbrTextureSlot slot = static_cast<PbrTextureSlot>(slotIndex);
                const int score = ScoreTextureCandidate(stem, modelStem, lowerMaterialName, slot);
                if (score > bestScore)
                {
                    bestScore = score;
                    bestMaterial = materialIndex;
                    bestSlot = slot;
                }
            }
        }

        if (bestMaterial < 0 || bestScore < 8) continue;

        std::string loadError;
        if (isOrmTexture)
        {
            const int ormLoadedCount = LoadOrmTexture(tab, bestMaterial, droppedPath, loadError);
            if (ormLoadedCount > 0)
            {
                loadedCount += ormLoadedCount;
                tab.selectedMaterial = bestMaterial;
            }
            else if (firstError.empty())
            {
                firstError = loadError;
            }
        }
        else if (LoadPbrTexture(tab, bestMaterial, bestSlot, droppedPath, loadError))
        {
            ++loadedCount;
            tab.selectedMaterial = bestMaterial;
        }
        else if (firstError.empty())
        {
            firstError = loadError;
        }
    }

    if (loadedCount > 0)
    {
        char message[160] = {};
        std::snprintf(message, sizeof(message), "Auto-assigned %d texture slot%s by name.", loadedCount, loadedCount == 1 ? "" : "s");
        notice = message;
        error.clear();
    }
    else if (!firstError.empty())
    {
        error = firstError;
        notice.clear();
    }

    return loadedCount;
}

int LoadPbrTexturesFromFolder(ModelTab& tab, int materialIndex, const std::filesystem::path& directory, std::string& error)
{
    int loadedCount = 0;
    error.clear();
    EnsurePbrMaterialStates(tab);
    const std::string materialName = materialIndex >= 0 && materialIndex < static_cast<int>(tab.loaded.materialNames.size()) ? tab.loaded.materialNames[static_cast<size_t>(materialIndex)] : std::string{};
    for (int i = 0; i < static_cast<int>(PbrTextureSlot::Count); ++i)
    {
        const PbrTextureSlot slot = static_cast<PbrTextureSlot>(i);
        const std::string path = FindAutoTexturePath(tab.path, directory, materialName, slot);
        if (path.empty()) continue;

        std::string loadError;
        if (LoadPbrTexture(tab, materialIndex, slot, path, loadError))
        {
            ++loadedCount;
        }
        else if (error.empty())
        {
            error = loadError;
        }
    }

    return loadedCount;
}

Color GetPackedChannelColor(PackedChannel channel, bool hovered)
{
    switch (channel)
    {
    case PackedChannel::R: return hovered ? Color{ 185, 56, 56, 255 } : Color{ 140, 42, 42, 255 };
    case PackedChannel::G: return hovered ? Color{ 62, 160, 76, 255 } : Color{ 45, 118, 58, 255 };
    case PackedChannel::B: return hovered ? Color{ 66, 106, 200, 255 } : Color{ 50, 78, 152, 255 };
    }

    return hovered ? Color{ 54, 63, 72, 255 } : Color{ 35, 40, 46, 255 };
}

