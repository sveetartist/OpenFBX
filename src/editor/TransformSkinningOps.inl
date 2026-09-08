bool GetSelectedNodeLocalAxis(const ModelTab& tab, TransformAxis axis, Vector3& outAxis)
{
    if (tab.selectedNode < 0 || tab.selectedNode >= static_cast<int>(tab.loaded.nodes.size())) return false;
    const SceneNode& node = tab.loaded.nodes[static_cast<size_t>(tab.selectedNode)];
    if (axis == TransformAxis::X)
    {
        outAxis = NormalizeOrFallback(node.axisX, Vector3{ 1.0f, 0.0f, 0.0f });
        return true;
    }
    if (axis == TransformAxis::Y)
    {
        outAxis = NormalizeOrFallback(node.axisY, Vector3{ 0.0f, 1.0f, 0.0f });
        return true;
    }
    if (axis == TransformAxis::Z)
    {
        outAxis = NormalizeOrFallback(node.axisZ, Vector3{ 0.0f, 0.0f, 1.0f });
        return true;
    }
    return false;
}

Vector3 GetTransformAxisVector(const ModelTab& tab, TransformAxis axis, GizmoOrientation orientation)
{
    if (orientation == GizmoOrientation::Local)
    {
        Vector3 localAxis{};
        if (GetSelectedNodeLocalAxis(tab, axis, localAxis)) return localAxis;
    }
    return GetTransformAxisVector(axis);
}

bool GetTransformPlaneBasis(const ModelTab& tab,
                            TransformAxis axis,
                            GizmoOrientation orientation,
                            Vector3& axisA,
                            Vector3& axisB)
{
    if (orientation == GizmoOrientation::Local && tab.selectedNode >= 0 && tab.selectedNode < static_cast<int>(tab.loaded.nodes.size()))
    {
        const SceneNode& node = tab.loaded.nodes[static_cast<size_t>(tab.selectedNode)];
        if (axis == TransformAxis::X)
        {
            axisA = NormalizeOrFallback(node.axisY, Vector3{ 0.0f, 1.0f, 0.0f });
            axisB = NormalizeOrFallback(node.axisZ, Vector3{ 0.0f, 0.0f, 1.0f });
            return true;
        }
        if (axis == TransformAxis::Y)
        {
            axisA = NormalizeOrFallback(node.axisX, Vector3{ 1.0f, 0.0f, 0.0f });
            axisB = NormalizeOrFallback(node.axisZ, Vector3{ 0.0f, 0.0f, 1.0f });
            return true;
        }
        if (axis == TransformAxis::Z)
        {
            axisA = NormalizeOrFallback(node.axisX, Vector3{ 1.0f, 0.0f, 0.0f });
            axisB = NormalizeOrFallback(node.axisY, Vector3{ 0.0f, 1.0f, 0.0f });
            return true;
        }
    }

    axisA = Vector3{ 1.0f, 0.0f, 0.0f };
    axisB = Vector3{ 0.0f, 1.0f, 0.0f };
    if (axis == TransformAxis::X)
    {
        axisA = Vector3{ 0.0f, 1.0f, 0.0f };
        axisB = Vector3{ 0.0f, 0.0f, 1.0f };
        return true;
    }
    if (axis == TransformAxis::Y)
    {
        axisA = Vector3{ 1.0f, 0.0f, 0.0f };
        axisB = Vector3{ 0.0f, 0.0f, 1.0f };
        return true;
    }
    if (axis == TransformAxis::Z)
    {
        axisA = Vector3{ 1.0f, 0.0f, 0.0f };
        axisB = Vector3{ 0.0f, 1.0f, 0.0f };
        return true;
    }
    return false;
}

Color GetTransformAxisColor(TransformAxis axis, bool active = false)
{
    const unsigned char alpha = active ? 255 : 220;
    auto activeColor = [active, alpha](Color color)
    {
        if (!active) return Color{ color.r, color.g, color.b, alpha };
        constexpr float kActiveLighten = 0.28f;
        return Color{
            static_cast<unsigned char>(static_cast<float>(color.r) + (255.0f - static_cast<float>(color.r)) * kActiveLighten),
            static_cast<unsigned char>(static_cast<float>(color.g) + (255.0f - static_cast<float>(color.g)) * kActiveLighten),
            static_cast<unsigned char>(static_cast<float>(color.b) + (255.0f - static_cast<float>(color.b)) * kActiveLighten),
            alpha
        };
    };
    switch (axis)
    {
    case TransformAxis::X: return activeColor(Color{ 235, 74, 74, 255 });
    case TransformAxis::Y: return activeColor(Color{ 92, 210, 94, 255 });
    case TransformAxis::Z: return activeColor(Color{ 86, 142, 255, 255 });
    case TransformAxis::Center:
    case TransformAxis::None: break;
    }
    return activeColor(Color{ 210, 218, 226, 255 });
}

bool IsNodeInTransformScope(const LoadedFbxModel& loaded, int nodeIndex, int rootNode)
{
    if (nodeIndex < 0 || rootNode < 0) return false;
    return nodeIndex == rootNode || IsDescendantNode(loaded, nodeIndex, rootNode);
}

Vector3 RotatePointAroundAxis(Vector3 point, Vector3 pivot, Vector3 axis, float radians)
{
    return Vector3Add(pivot, Vector3RotateByAxisAngle(Vector3Subtract(point, pivot), axis, radians));
}

Vector3 ScalePointAlongAxis(Vector3 point, Vector3 pivot, Vector3 axis, float factor)
{
    const Vector3 offset = Vector3Subtract(point, pivot);
    const float along = Vector3DotProduct(offset, axis);
    const Vector3 parallel = Vector3Scale(axis, along);
    const Vector3 perpendicular = Vector3Subtract(offset, parallel);
    return Vector3Add(pivot, Vector3Add(perpendicular, Vector3Scale(parallel, factor)));
}

Vector3 ScalePointUniform(Vector3 point, Vector3 pivot, float factor)
{
    return Vector3Add(pivot, Vector3Scale(Vector3Subtract(point, pivot), factor));
}

Vector3 ScaleNormalAlongAxis(Vector3 normal, Vector3 axis, float factor)
{
    const float safeFactor = std::max(0.001f, std::fabs(factor));
    const float along = Vector3DotProduct(normal, axis);
    const Vector3 parallel = Vector3Scale(axis, along / safeFactor);
    const Vector3 perpendicular = Vector3Subtract(normal, Vector3Scale(axis, along));
    return NormalizeOrFallback(Vector3Add(perpendicular, parallel), normal);
}

Vector3 NegateVector3(Vector3 value)
{
    return Vector3{ -value.x, -value.y, -value.z };
}

Vector3 EulerDegreesFromAxes(Vector3 axisX, Vector3 axisY, Vector3 axisZ)
{
    Matrix rotationMatrix = MatrixIdentity();
    axisX = NormalizeOrFallback(axisX, Vector3{ 1.0f, 0.0f, 0.0f });
    axisY = NormalizeOrFallback(axisY, Vector3{ 0.0f, 1.0f, 0.0f });
    axisZ = NormalizeOrFallback(axisZ, Vector3{ 0.0f, 0.0f, 1.0f });
    rotationMatrix.m0 = axisX.x;
    rotationMatrix.m1 = axisX.y;
    rotationMatrix.m2 = axisX.z;
    rotationMatrix.m4 = axisY.x;
    rotationMatrix.m5 = axisY.y;
    rotationMatrix.m6 = axisY.z;
    rotationMatrix.m8 = axisZ.x;
    rotationMatrix.m9 = axisZ.y;
    rotationMatrix.m10 = axisZ.z;
    return Vector3Scale(QuaternionToEuler(QuaternionFromMatrix(rotationMatrix)), RAD2DEG);
}

void AxesFromEulerDegrees(Vector3 rotation, Vector3& axisX, Vector3& axisY, Vector3& axisZ)
{
    const Matrix matrix = MatrixRotateXYZ(Vector3Scale(rotation, DEG2RAD));
    axisX = NormalizeOrFallback(Vector3{ matrix.m0, matrix.m1, matrix.m2 }, Vector3{ 1.0f, 0.0f, 0.0f });
    axisY = NormalizeOrFallback(Vector3{ matrix.m4, matrix.m5, matrix.m6 }, Vector3{ 0.0f, 1.0f, 0.0f });
    axisZ = NormalizeOrFallback(Vector3{ matrix.m8, matrix.m9, matrix.m10 }, Vector3{ 0.0f, 0.0f, 1.0f });
}

void ApplyTransformValueUpdates(SceneNode& node, TransformTool tool, TransformAxis axis, float amount)
{
    if (tool == TransformTool::Rotate)
    {
        node.rotation = EulerDegreesFromAxes(node.axisX, node.axisY, node.axisZ);
    }
    else if (tool == TransformTool::Scale)
    {
        if (axis == TransformAxis::X) node.scale.x *= amount;
        else if (axis == TransformAxis::Y) node.scale.y *= amount;
        else if (axis == TransformAxis::Z) node.scale.z *= amount;
        else if (axis == TransformAxis::Center)
        {
            node.scale.x *= amount;
            node.scale.y *= amount;
            node.scale.z *= amount;
        }
    }
}

void ApplyTransformValueUpdates(BonePose& pose, TransformTool tool, TransformAxis axis, float amount)
{
    if (tool == TransformTool::Rotate)
    {
        pose.rotation = EulerDegreesFromAxes(pose.axisX, pose.axisY, pose.axisZ);
    }
    else if (tool == TransformTool::Scale)
    {
        if (axis == TransformAxis::X) pose.scale.x *= amount;
        else if (axis == TransformAxis::Y) pose.scale.y *= amount;
        else if (axis == TransformAxis::Z) pose.scale.z *= amount;
        else if (axis == TransformAxis::Center)
        {
            pose.scale.x *= amount;
            pose.scale.y *= amount;
            pose.scale.z *= amount;
        }
    }
}

void RecomputeMeshNodeBounds(ModelTab& tab, SceneNode& node)
{
    if (node.type != SceneNodeType::Mesh || node.meshVertexStart < 0 || node.meshVertexCount <= 0 ||
        tab.loaded.bindVertices.empty())
    {
        return;
    }

    const std::vector<float>& vertices = tab.currentVertices.size() == tab.loaded.bindVertices.size() ? tab.currentVertices : tab.loaded.bindVertices;
    const int start = std::max(0, node.meshVertexStart);
    const int end = std::min(node.meshVertexStart + node.meshVertexCount, static_cast<int>(vertices.size() / 3));
    if (start >= end) return;

    const int firstBase = start * 3;
    BoundingBox bounds{
        Vector3{ vertices[static_cast<size_t>(firstBase)], vertices[static_cast<size_t>(firstBase + 1)], vertices[static_cast<size_t>(firstBase + 2)] },
        Vector3{ vertices[static_cast<size_t>(firstBase)], vertices[static_cast<size_t>(firstBase + 1)], vertices[static_cast<size_t>(firstBase + 2)] }
    };
    for (int vertex = start + 1; vertex < end; ++vertex)
    {
        const int base = vertex * 3;
        const Vector3 point{ vertices[static_cast<size_t>(base)], vertices[static_cast<size_t>(base + 1)], vertices[static_cast<size_t>(base + 2)] };
        bounds.min.x = std::min(bounds.min.x, point.x);
        bounds.min.y = std::min(bounds.min.y, point.y);
        bounds.min.z = std::min(bounds.min.z, point.z);
        bounds.max.x = std::max(bounds.max.x, point.x);
        bounds.max.y = std::max(bounds.max.y, point.y);
        bounds.max.z = std::max(bounds.max.z, point.z);
    }

    node.bounds = bounds;
    node.hasBounds = true;
}

void RecomputeSceneBounds(ModelTab& tab)
{
    bool found = false;
    BoundingBox bounds{};
    for (const SceneNode& node : tab.loaded.nodes)
    {
        if (!node.hasBounds) continue;
        if (!found)
        {
            bounds = node.bounds;
            found = true;
            continue;
        }
        bounds.min.x = std::min(bounds.min.x, node.bounds.min.x);
        bounds.min.y = std::min(bounds.min.y, node.bounds.min.y);
        bounds.min.z = std::min(bounds.min.z, node.bounds.min.z);
        bounds.max.x = std::max(bounds.max.x, node.bounds.max.x);
        bounds.max.y = std::max(bounds.max.y, node.bounds.max.y);
        bounds.max.z = std::max(bounds.max.z, node.bounds.max.z);
    }
    if (found)
    {
        tab.loaded.bounds = bounds;
    }
}

template <typename PointTransform>
void TransformPositionBuffer(std::vector<float>& values, int startVertex, int vertexCount, PointTransform transform)
{
    if (values.empty() || startVertex < 0 || vertexCount <= 0) return;
    const int start = std::max(0, startVertex);
    const int end = std::min(startVertex + vertexCount, static_cast<int>(values.size() / 3));
    for (int vertex = start; vertex < end; ++vertex)
    {
        const int base = vertex * 3;
        const Vector3 value{ values[static_cast<size_t>(base)], values[static_cast<size_t>(base + 1)], values[static_cast<size_t>(base + 2)] };
        const Vector3 transformed = transform(value);
        values[static_cast<size_t>(base)] = transformed.x;
        values[static_cast<size_t>(base + 1)] = transformed.y;
        values[static_cast<size_t>(base + 2)] = transformed.z;
    }
}

template <typename DirectionTransform>
void TransformDirectionBuffer(std::vector<float>& values, int startVertex, int vertexCount, DirectionTransform transform)
{
    if (values.empty() || startVertex < 0 || vertexCount <= 0) return;
    const int start = std::max(0, startVertex);
    const int end = std::min(startVertex + vertexCount, static_cast<int>(values.size() / 3));
    for (int vertex = start; vertex < end; ++vertex)
    {
        const int base = vertex * 3;
        const Vector3 value{ values[static_cast<size_t>(base)], values[static_cast<size_t>(base + 1)], values[static_cast<size_t>(base + 2)] };
        const Vector3 transformed = transform(value);
        values[static_cast<size_t>(base)] = transformed.x;
        values[static_cast<size_t>(base + 1)] = transformed.y;
        values[static_cast<size_t>(base + 2)] = transformed.z;
    }
}

void FlipNormalBufferRange(std::vector<float>& values, int startVertex, int vertexCount)
{
    if (values.empty() || startVertex < 0 || vertexCount <= 0) return;
    const int start = std::max(0, startVertex);
    const int end = std::min(startVertex + vertexCount, static_cast<int>(values.size() / 3));
    for (int vertex = start; vertex < end; ++vertex)
    {
        const size_t base = static_cast<size_t>(vertex) * 3;
        values[base] = -values[base];
        values[base + 1] = -values[base + 1];
        values[base + 2] = -values[base + 2];
    }
}

bool FlipMeshNormals(ModelTab& tab, int nodeIndex)
{
    if (nodeIndex < 0 ||
        nodeIndex >= static_cast<int>(tab.loaded.nodes.size()) ||
        IsDeletedNode(tab, nodeIndex))
    {
        return false;
    }

    const SceneNode& node = tab.loaded.nodes[static_cast<size_t>(nodeIndex)];
    if (node.type != SceneNodeType::Mesh ||
        node.meshVertexStart < 0 ||
        node.meshVertexCount <= 0 ||
        tab.loaded.bindNormals.empty())
    {
        return false;
    }

    FlipNormalBufferRange(tab.loaded.bindNormals, node.meshVertexStart, node.meshVertexCount);
    tab.skinningGeometry.reset();
    FlipNormalBufferRange(tab.currentNormals, node.meshVertexStart, node.meshVertexCount);
    for (AnimationClip& clip : tab.loaded.animations)
    {
        for (MeshFrame& frame : clip.meshFrames)
        {
            ResolveMeshFrame(frame);
            FlipNormalBufferRange(frame.normals, node.meshVertexStart, node.meshVertexCount);
        }
    }

    const int start = std::max(0, node.meshVertexStart);
    const int end = std::min(node.meshVertexStart + node.meshVertexCount, static_cast<int>(tab.loaded.skinnedVertices.size()));
    for (int vertex = start; vertex < end; ++vertex)
    {
        SkinnedVertex& skinned = tab.loaded.skinnedVertices[static_cast<size_t>(vertex)];
        skinned.bindNormal = NegateVector3(skinned.bindNormal);
        for (SkinnedVertexInfluence& influence : skinned.influences)
        {
            influence.bindNormalInBone = NegateVector3(influence.bindNormalInBone);
        }
    }

    InvalidateDisplayedAnimationCaches(tab);
    RefreshDisplayedMesh(tab);
    return true;
}

bool SetMeshPivotToBoundsPoint(ModelTab& tab, int nodeIndex, bool bottom)
{
    if (nodeIndex < 0 ||
        nodeIndex >= static_cast<int>(tab.loaded.nodes.size()) ||
        IsDeletedNode(tab, nodeIndex))
    {
        return false;
    }

    SceneNode& node = tab.loaded.nodes[static_cast<size_t>(nodeIndex)];
    if (node.type != SceneNodeType::Mesh ||
        node.meshVertexStart < 0 ||
        node.meshVertexCount <= 0)
    {
        return false;
    }

    RecomputeMeshNodeBounds(tab, node);
    if (!node.hasBounds) return false;

    Vector3 pivot{
        (node.bounds.min.x + node.bounds.max.x) * 0.5f,
        (node.bounds.min.y + node.bounds.max.y) * 0.5f,
        (node.bounds.min.z + node.bounds.max.z) * 0.5f
    };
    if (bottom)
    {
        pivot.y = node.bounds.min.y;
    }

    if (Vector3Distance(node.position, pivot) <= 0.000001f) return false;

    node.position = pivot;
    RefreshDisplayedMesh(tab);
    return true;
}

bool SetMeshPivotToBoundsCenter(ModelTab& tab, int nodeIndex)
{
    return SetMeshPivotToBoundsPoint(tab, nodeIndex, false);
}

bool SetMeshPivotToBoundsBottom(ModelTab& tab, int nodeIndex)
{
    return SetMeshPivotToBoundsPoint(tab, nodeIndex, true);
}

template <typename PointTransform, typename DirectionTransform>
void TransformMeshNodeRange(ModelTab& tab, SceneNode& node, PointTransform transformPoint, DirectionTransform transformDirection)
{
    tab.skinningGeometry.reset();
    TransformPositionBuffer(tab.loaded.bindVertices, node.meshVertexStart, node.meshVertexCount, transformPoint);
    TransformDirectionBuffer(tab.loaded.bindNormals, node.meshVertexStart, node.meshVertexCount, transformDirection);
    TransformPositionBuffer(tab.currentVertices, node.meshVertexStart, node.meshVertexCount, transformPoint);
    TransformDirectionBuffer(tab.currentNormals, node.meshVertexStart, node.meshVertexCount, transformDirection);

    const int start = std::max(0, node.meshVertexStart);
    const int end = std::min(node.meshVertexStart + node.meshVertexCount, static_cast<int>(tab.loaded.skinnedVertices.size()));
    for (int vertex = start; vertex < end; ++vertex)
    {
        SkinnedVertex& skinned = tab.loaded.skinnedVertices[static_cast<size_t>(vertex)];
        skinned.bindPosition = transformPoint(skinned.bindPosition);
        skinned.bindNormal = transformDirection(skinned.bindNormal);
    }

    for (AnimationClip& clip : tab.loaded.animations)
    {
        for (MeshFrame& frame : clip.meshFrames)
        {
            ResolveMeshFrame(frame);
            TransformPositionBuffer(frame.vertices, node.meshVertexStart, node.meshVertexCount, transformPoint);
            TransformDirectionBuffer(frame.normals, node.meshVertexStart, node.meshVertexCount, transformDirection);
        }
    }

    RecomputeMeshNodeBounds(tab, node);
}

template <typename PointTransform, typename DirectionTransform>
void TransformBoneData(ModelTab& tab,
                       int rootNode,
                       PointTransform transformPoint,
                       DirectionTransform transformDirection,
                       TransformTool tool,
                       TransformAxis axis,
                       float amount)
{
    std::vector<unsigned char> inScope(tab.loaded.nodes.size());
    for (int node = 0; node < static_cast<int>(inScope.size()); ++node)
    {
        inScope[static_cast<size_t>(node)] = IsNodeInTransformScope(tab.loaded, node, rootNode);
    }
    auto containsNode = [&](int node)
    {
        return node >= 0 && node < static_cast<int>(inScope.size()) && inScope[static_cast<size_t>(node)] != 0;
    };
    auto transformBoneSegment = [&](BoneSegment& bone)
    {
        if (containsNode(bone.startNode))
        {
            bone.start = transformPoint(bone.start);
        }
        if (containsNode(bone.endNode))
        {
            bone.end = transformPoint(bone.end);
        }
    };
    auto transformBonePose = [&](BonePose& pose)
    {
        if (!containsNode(pose.node)) return;
        pose.position = transformPoint(pose.position);
        pose.axisX = NormalizeOrFallback(transformDirection(pose.axisX), pose.axisX);
        pose.axisY = NormalizeOrFallback(transformDirection(pose.axisY), pose.axisY);
        pose.axisZ = NormalizeOrFallback(transformDirection(pose.axisZ), pose.axisZ);
        ApplyTransformValueUpdates(pose, tool, axis, amount);
    };

    for (BoneSegment& bone : tab.loaded.bones) transformBoneSegment(bone);
    for (BoneSegment& bone : tab.visibleBones) transformBoneSegment(bone);
    for (BonePose& pose : tab.loaded.bonePoses) transformBonePose(pose);
    for (BonePose& pose : tab.visibleBonePoses) transformBonePose(pose);
    for (AnimationClip& clip : tab.loaded.animations)
    {
        for (BoneFrame& frame : clip.frames)
        {
            for (BoneSegment& bone : frame.bones) transformBoneSegment(bone);
            for (BonePose& pose : frame.poses) transformBonePose(pose);
        }
    }
}

void InvalidateDisplayedAnimationCaches(ModelTab& tab)
{
    tab.appliedClipIndex = -2;
    tab.appliedMeshFrameIndex = -1;
    tab.appliedNextMeshFrameIndex = -1;
    tab.appliedMeshFrameAlpha = -1.0f;
    tab.appliedBoneClipIndex = -2;
    tab.appliedBoneFrameIndex = -1;
    tab.appliedNextBoneFrameIndex = -1;
    tab.appliedBoneFrameAlpha = -1.0f;
}

bool HasCpuSkinnedMesh(const LoadedFbxModel& loaded)
{
    return loaded.hasMesh &&
           !loaded.skinnedVertices.empty() &&
           loaded.skinnedVertices.size() == loaded.bindVertices.size() / 3 &&
           loaded.bindVertices.size() == loaded.bindNormals.size();
}

BoneFrame BuildBindBoneFrame(const LoadedFbxModel& loaded)
{
    BoneFrame frame;
    frame.bones = loaded.bones;
    frame.poses = loaded.bonePoses;
    return frame;
}

std::shared_ptr<const SkinningGeometry> GetSkinningGeometry(ModelTab& tab)
{
    if (!tab.skinningGeometry) tab.skinningGeometry = CaptureSkinningGeometry(tab.loaded);
    return tab.skinningGeometry;
}

MeshFrame BuildCurrentBindSkin(ModelTab& tab)
{
    MeshFrame frame;
    frame.deferred = CaptureSkinningFrame(GetSkinningGeometry(tab), BuildBindBoneFrame(tab.loaded));
    ResolveMeshFrame(frame);
    return frame;
}

void RecomputeAllMeshNodeBounds(ModelTab& tab)
{
    for (SceneNode& node : tab.loaded.nodes)
    {
        if (node.type == SceneNodeType::Mesh)
        {
            RecomputeMeshNodeBounds(tab, node);
        }
    }
    RecomputeSceneBounds(tab);
}

bool StoreMeshFrameAsBindMesh(ModelTab& tab, const MeshFrame& meshFrame)
{
    if (!HasCpuSkinnedMesh(tab.loaded) ||
        meshFrame.vertices.size() != tab.loaded.bindVertices.size() ||
        meshFrame.normals.size() != tab.loaded.bindNormals.size())
    {
        return false;
    }

    tab.loaded.bindVertices = meshFrame.vertices;
    tab.loaded.bindNormals = meshFrame.normals;

    const int vertexCount = std::min(static_cast<int>(tab.loaded.skinnedVertices.size()),
                                     static_cast<int>(tab.loaded.bindVertices.size() / 3));
    for (int vertex = 0; vertex < vertexCount; ++vertex)
    {
        const size_t base = static_cast<size_t>(vertex) * 3;
        SkinnedVertex& skinned = tab.loaded.skinnedVertices[static_cast<size_t>(vertex)];
        skinned.bindPosition = Vector3{
            tab.loaded.bindVertices[base],
            tab.loaded.bindVertices[base + 1],
            tab.loaded.bindVertices[base + 2]
        };
        skinned.bindNormal = NormalizeOrFallback(Vector3{
            tab.loaded.bindNormals[base],
            tab.loaded.bindNormals[base + 1],
            tab.loaded.bindNormals[base + 2]
        }, skinned.bindNormal);
    }

    if (tab.skinningGeometry)
    {
        auto refreshed = std::make_shared<SkinningGeometry>(*tab.skinningGeometry);
        for (size_t vertex = 0; vertex < refreshed->vertices.size(); ++vertex)
        {
            refreshed->vertices[vertex].position = tab.loaded.skinnedVertices[vertex].bindPosition;
            refreshed->vertices[vertex].normal = tab.loaded.skinnedVertices[vertex].bindNormal;
        }
        tab.skinningGeometry = std::move(refreshed);
    }
    return true;
}

bool BakeSkinnedBindMeshFromBones(ModelTab& tab, bool updateDisplayedMesh)
{
    if (!HasCpuSkinnedMesh(tab.loaded) || tab.loaded.bonePoses.empty()) return false;

    // The FBX writer rebuilds control points from bindVertices, so keep that
    // buffer aligned with the CPU-skinned bind pose after bone transform edits.
    const MeshFrame meshFrame = BuildCurrentBindSkin(tab);
    if (!StoreMeshFrameAsBindMesh(tab, meshFrame)) return false;

    InvalidateDisplayedAnimationCaches(tab);
    if (updateDisplayedMesh)
    {
        tab.currentVertices = meshFrame.vertices;
        tab.currentNormals = meshFrame.normals;
        tab.manualSkinnedMeshPose = true;
        RefreshDisplayedMesh(tab);
        if (tab.animation.clipIndex < 0) tab.appliedClipIndex = -1;
    }

    RecomputeAllMeshNodeBounds(tab);
    return true;
}

bool RebuildCurrentSkinnedMeshFromBones(ModelTab& tab)
{
    if (!HasCpuSkinnedMesh(tab.loaded) || tab.loaded.bonePoses.empty()) return false;

    const MeshFrame meshFrame = BuildCurrentBindSkin(tab);
    if (meshFrame.vertices.size() != tab.loaded.bindVertices.size() ||
        meshFrame.normals.size() != tab.loaded.bindNormals.size())
    {
        return false;
    }

    tab.currentVertices = meshFrame.vertices;
    tab.currentNormals = meshFrame.normals;
    tab.manualSkinnedMeshPose = true;
    RecomputeAllMeshNodeBounds(tab);
    InvalidateDisplayedAnimationCaches(tab);
    RefreshDisplayedMesh(tab);
    if (tab.animation.clipIndex < 0) tab.appliedClipIndex = -1;
    return true;
}

bool RebuildSkinnedAnimationMeshFrames(ModelTab& tab)
{
    if (!HasCpuSkinnedMesh(tab.loaded)) return false;

    bool rebuilt = false;
    std::shared_ptr<const SkinningGeometry> geometry;
    for (AnimationClip& clip : tab.loaded.animations)
    {
        if (clip.frames.empty()) continue;
        if (!geometry) geometry = GetSkinningGeometry(tab);
        clip.meshFrames.resize(clip.frames.size());
        for (size_t index = 0; index < clip.frames.size(); ++index)
        {
            MeshFrame pending;
            pending.deferred = CaptureSkinningFrame(geometry, clip.frames[index]);
            clip.meshFrames[index] = std::move(pending);
        }
        rebuilt = true;
    }
    if (rebuilt)
    {
        const bool bindMeshDisplayed = tab.animation.clipIndex < 0 && tab.appliedClipIndex == -1;
        InvalidateDisplayedAnimationCaches(tab);
        if (bindMeshDisplayed) tab.appliedClipIndex = -1;
    }
    return rebuilt;
}

bool IsScaleApproximatelyApplied(Vector3 scale)
{
    constexpr float epsilon = 0.000001f;
    return std::fabs(scale.x - 1.0f) <= epsilon &&
           std::fabs(scale.y - 1.0f) <= epsilon &&
           std::fabs(scale.z - 1.0f) <= epsilon;
}

bool SetScaleToApplied(Vector3& scale)
{
    if (IsScaleApproximatelyApplied(scale)) return false;
    scale = Vector3{ 1.0f, 1.0f, 1.0f };
    return true;
}

float GetAxesHandedness(Vector3 axisX, Vector3 axisY, Vector3 axisZ)
{
    return Vector3DotProduct(Vector3CrossProduct(axisX, axisY), axisZ);
}

bool NormalizeAppliedScaleAxes(SceneNode& node, Vector3 appliedScale)
{
    Vector3 axisX = NormalizeOrFallback(node.axisX, Vector3{ 1.0f, 0.0f, 0.0f });
    Vector3 axisY = NormalizeOrFallback(node.axisY, Vector3{ 0.0f, 1.0f, 0.0f });
    Vector3 axisZ = NormalizeOrFallback(node.axisZ, Vector3{ 0.0f, 0.0f, 1.0f });

    if (appliedScale.x < 0.0f) axisX = NegateVector3(axisX);
    if (appliedScale.y < 0.0f) axisY = NegateVector3(axisY);
    if (appliedScale.z < 0.0f) axisZ = NegateVector3(axisZ);

    if (GetAxesHandedness(axisX, axisY, axisZ) < 0.0f)
    {
        Vector3 rotationAxisX{};
        Vector3 rotationAxisY{};
        Vector3 rotationAxisZ{};
        AxesFromEulerDegrees(node.rotation, rotationAxisX, rotationAxisY, rotationAxisZ);
        axisX = rotationAxisX;
        axisY = rotationAxisY;
        axisZ = rotationAxisZ;
    }

    if (GetAxesHandedness(axisX, axisY, axisZ) < 0.0f)
    {
        axisX = NegateVector3(axisX);
    }

    const bool changed = Vector3Distance(node.axisX, axisX) > 0.000001f ||
                         Vector3Distance(node.axisY, axisY) > 0.000001f ||
                         Vector3Distance(node.axisZ, axisZ) > 0.000001f;
    if (changed)
    {
        node.axisX = axisX;
        node.axisY = axisY;
        node.axisZ = axisZ;
        node.rotation = EulerDegreesFromAxes(node.axisX, node.axisY, node.axisZ);
    }
    return changed;
}

void SyncMeshRangeToSkinnedBindFallbacks(LoadedFbxModel& loaded, const SceneNode& node)
{
    if (node.type != SceneNodeType::Mesh ||
        node.meshVertexStart < 0 ||
        node.meshVertexCount <= 0 ||
        loaded.skinnedVertices.empty())
    {
        return;
    }

    const int start = std::max(0, node.meshVertexStart);
    const int end = std::min(node.meshVertexStart + node.meshVertexCount, static_cast<int>(loaded.skinnedVertices.size()));
    for (int vertex = start; vertex < end; ++vertex)
    {
        const size_t base = static_cast<size_t>(vertex) * 3;
        if (base + 2 >= loaded.bindVertices.size() || base + 2 >= loaded.bindNormals.size()) continue;

        SkinnedVertex& skinned = loaded.skinnedVertices[static_cast<size_t>(vertex)];
        skinned.bindPosition = Vector3{
            loaded.bindVertices[base],
            loaded.bindVertices[base + 1],
            loaded.bindVertices[base + 2]
        };
        skinned.bindNormal = NormalizeOrFallback(Vector3{
            loaded.bindNormals[base],
            loaded.bindNormals[base + 1],
            loaded.bindNormals[base + 2]
        }, skinned.bindNormal);
    }
}

void PreserveAppliedMeshScaleGeometry(ModelTab& tab, SceneNode& node)
{
    if (node.type != SceneNodeType::Mesh ||
        node.meshVertexStart < 0 ||
        node.meshVertexCount <= 0 ||
        tab.loaded.bindVertices.size() != tab.loaded.bindNormals.size())
    {
        return;
    }

    if (tab.animation.clipIndex < 0 &&
        tab.currentVertices.size() == tab.loaded.bindVertices.size() &&
        tab.currentNormals.size() == tab.loaded.bindNormals.size() &&
        (!node.meshHasSkin || !HasCpuSkinnedMesh(tab.loaded)))
    {
        const int start = std::max(0, node.meshVertexStart);
        const int end = std::min(node.meshVertexStart + node.meshVertexCount, static_cast<int>(tab.loaded.bindVertices.size() / 3));
        for (int vertex = start; vertex < end; ++vertex)
        {
            const size_t base = static_cast<size_t>(vertex) * 3;
            tab.loaded.bindVertices[base] = tab.currentVertices[base];
            tab.loaded.bindVertices[base + 1] = tab.currentVertices[base + 1];
            tab.loaded.bindVertices[base + 2] = tab.currentVertices[base + 2];
            tab.loaded.bindNormals[base] = tab.currentNormals[base];
            tab.loaded.bindNormals[base + 1] = tab.currentNormals[base + 1];
            tab.loaded.bindNormals[base + 2] = tab.currentNormals[base + 2];
        }
    }

    SyncMeshRangeToSkinnedBindFallbacks(tab.loaded, node);
    tab.skinningGeometry.reset();
    RecomputeMeshNodeBounds(tab, node);
}

Vector3 MultiplyComponents(Vector3 a, Vector3 b)
{
    return Vector3{ a.x * b.x, a.y * b.y, a.z * b.z };
}

void BakeBoneScaleIntoSkinBindData(ModelTab& tab, const SceneNode& node, Vector3 scale)
{
    if (!HasCpuSkinnedMesh(tab.loaded) || IsScaleApproximatelyApplied(scale)) return;
    tab.skinningGeometry.reset();

    for (SkinnedVertex& vertex : tab.loaded.skinnedVertices)
    {
        for (SkinnedVertexInfluence& influence : vertex.influences)
        {
            const bool nameMatches = influence.boneName == node.name ||
                                     (!node.sourceName.empty() && influence.boneName == node.sourceName);
            if (!nameMatches) continue;

            influence.bindPositionInBone = MultiplyComponents(influence.bindPositionInBone, scale);
            influence.bindNormalInBone = NormalizeOrFallback(MultiplyComponents(influence.bindNormalInBone, scale), influence.bindNormalInBone);
        }
    }
}

bool ApplyScaleToNode(ModelTab& tab, int nodeIndex)
{
    if (nodeIndex < 0 ||
        nodeIndex >= static_cast<int>(tab.loaded.nodes.size()) ||
        IsSceneRootNode(tab.loaded, nodeIndex) ||
        IsDeletedNode(tab, nodeIndex))
    {
        return false;
    }

    SceneNode& node = tab.loaded.nodes[static_cast<size_t>(nodeIndex)];
    bool changed = false;
    Vector3 boneScaleToBake = node.scale;
    const Vector3 appliedScale = node.scale;

    if (node.type == SceneNodeType::Bone)
    {
        for (const BonePose& pose : tab.loaded.bonePoses)
        {
            if (pose.node == nodeIndex && !IsScaleApproximatelyApplied(pose.scale))
            {
                boneScaleToBake = pose.scale;
                break;
            }
        }
        BakeBoneScaleIntoSkinBindData(tab, node, boneScaleToBake);
    }
    else if (node.type == SceneNodeType::Mesh)
    {
        PreserveAppliedMeshScaleGeometry(tab, node);
        changed = NormalizeAppliedScaleAxes(node, appliedScale) || changed;
    }

    changed = SetScaleToApplied(node.scale) || changed;

    auto applyPoseScale = [&](BonePose& pose)
    {
        if (pose.node == nodeIndex)
        {
            changed = SetScaleToApplied(pose.scale) || changed;
        }
    };

    if (node.type == SceneNodeType::Bone)
    {
        for (BonePose& pose : tab.loaded.bonePoses) applyPoseScale(pose);
        for (BonePose& pose : tab.visibleBonePoses) applyPoseScale(pose);
        for (AnimationClip& clip : tab.loaded.animations)
        {
            for (BoneFrame& frame : clip.frames)
            {
                for (BonePose& pose : frame.poses) applyPoseScale(pose);
            }
        }
    }

    if (!changed) return false;

    if (node.type == SceneNodeType::Bone)
    {
        const bool bakedBindMesh = BakeSkinnedBindMeshFromBones(tab, tab.animation.clipIndex < 0);
        RebuildSkinnedAnimationMeshFrames(tab);
        if (bakedBindMesh && tab.animation.clipIndex < 0)
        {
            return true;
        }
        InvalidateDisplayedAnimationCaches(tab);
    }

    RecomputeSceneBounds(tab);
    RefreshDisplayedMesh(tab);
    return true;
}

const BonePose* FindBonePoseByNode(const std::vector<BonePose>& poses, int nodeIndex)
{
    for (const BonePose& pose : poses)
    {
        if (pose.node == nodeIndex) return &pose;
    }
    return nullptr;
}

bool AssignBonePose(BonePose& target, const BonePose& source)
{
    const bool changed = Vector3Distance(target.position, source.position) > 0.000001f ||
                         Vector3Distance(target.axisX, source.axisX) > 0.000001f ||
                         Vector3Distance(target.axisY, source.axisY) > 0.000001f ||
                         Vector3Distance(target.axisZ, source.axisZ) > 0.000001f ||
                         Vector3Distance(target.rotation, source.rotation) > 0.000001f ||
                         Vector3Distance(target.scale, source.scale) > 0.000001f;
    if (changed)
    {
        const int node = target.node;
        target = source;
        target.node = node;
    }
    return changed;
}

bool AssignSceneNodePose(SceneNode& node, const BonePose& pose)
{
    const bool changed = Vector3Distance(node.position, pose.position) > 0.000001f ||
                         Vector3Distance(node.axisX, pose.axisX) > 0.000001f ||
                         Vector3Distance(node.axisY, pose.axisY) > 0.000001f ||
                         Vector3Distance(node.axisZ, pose.axisZ) > 0.000001f ||
                         Vector3Distance(node.rotation, pose.rotation) > 0.000001f ||
                         Vector3Distance(node.scale, pose.scale) > 0.000001f;
    if (changed)
    {
        node.position = pose.position;
        node.axisX = pose.axisX;
        node.axisY = pose.axisY;
        node.axisZ = pose.axisZ;
        node.rotation = pose.rotation;
        node.scale = pose.scale;
    }
    return changed;
}

void UpdateBoneSegmentsForNode(std::vector<BoneSegment>& bones, int nodeIndex, Vector3 position)
{
    for (BoneSegment& bone : bones)
    {
        if (bone.startNode == nodeIndex)
        {
            bone.start = position;
        }
        if (bone.endNode == nodeIndex)
        {
            bone.end = position;
        }
    }
}

bool ResetSingleBoneToOriginalBindPose(ModelTab& tab, int nodeIndex)
{
    if (nodeIndex < 0 ||
        nodeIndex >= static_cast<int>(tab.loaded.nodes.size()) ||
        IsDeletedNode(tab, nodeIndex) ||
        tab.loaded.nodes[static_cast<size_t>(nodeIndex)].type != SceneNodeType::Bone)
    {
        return false;
    }

    const BonePose* bindPose = FindBonePoseByNode(tab.originalBindBonePoses, nodeIndex);
    if (!bindPose)
    {
        bindPose = FindBonePoseByNode(tab.loaded.bonePoses, nodeIndex);
    }
    if (!bindPose)
    {
        return false;
    }

    bool changed = AssignSceneNodePose(tab.loaded.nodes[static_cast<size_t>(nodeIndex)], *bindPose);
    bool foundLoadedPose = false;
    for (BonePose& pose : tab.loaded.bonePoses)
    {
        if (pose.node != nodeIndex) continue;
        changed = AssignBonePose(pose, *bindPose) || changed;
        foundLoadedPose = true;
    }
    if (!foundLoadedPose)
    {
        tab.loaded.bonePoses.push_back(*bindPose);
        changed = true;
    }

    for (BonePose& pose : tab.visibleBonePoses)
    {
        if (pose.node == nodeIndex)
        {
            changed = AssignBonePose(pose, *bindPose) || changed;
        }
    }
    UpdateBoneSegmentsForNode(tab.loaded.bones, nodeIndex, bindPose->position);
    UpdateBoneSegmentsForNode(tab.visibleBones, nodeIndex, bindPose->position);
    return changed;
}

bool ResetBoneSubtreeToOriginalBindPose(ModelTab& tab, int rootNodeIndex)
{
    if (rootNodeIndex < 0 ||
        rootNodeIndex >= static_cast<int>(tab.loaded.nodes.size()) ||
        IsDeletedNode(tab, rootNodeIndex) ||
        tab.loaded.nodes[static_cast<size_t>(rootNodeIndex)].type != SceneNodeType::Bone)
    {
        return false;
    }

    bool changed = false;
    for (int nodeIndex = 0; nodeIndex < static_cast<int>(tab.loaded.nodes.size()); ++nodeIndex)
    {
        if (!IsNodeInTransformScope(tab.loaded, nodeIndex, rootNodeIndex)) continue;
        if (tab.loaded.nodes[static_cast<size_t>(nodeIndex)].type != SceneNodeType::Bone) continue;
        changed = ResetSingleBoneToOriginalBindPose(tab, nodeIndex) || changed;
    }
    if (!changed) return false;

    if (tab.animation.clipIndex < 0)
    {
        tab.visibleBones = tab.loaded.bones;
        tab.visibleBonePoses = tab.loaded.bonePoses;
    }
    const bool bakedBindMesh = BakeSkinnedBindMeshFromBones(tab, tab.animation.clipIndex < 0);
    RebuildSkinnedAnimationMeshFrames(tab);
    if (!bakedBindMesh && tab.animation.clipIndex < 0 && !RebuildCurrentSkinnedMeshFromBones(tab))
    {
        RecomputeSceneBounds(tab);
        RefreshDisplayedMesh(tab);
    }
    return true;
}

std::vector<int> GetSelectedTransformRoots(const ModelTab& tab)
{
    std::vector<int> roots;
    if (tab.selectedNodes.empty())
    {
        if (IsValidSelectableNode(tab, tab.selectedNode))
        {
            roots.push_back(tab.selectedNode);
        }
        return roots;
    }

    for (int nodeIndex : tab.selectedNodes)
    {
        if (!IsValidSelectableNode(tab, nodeIndex)) continue;

        bool coveredBySelectedAncestor = false;
        for (int otherNode : tab.selectedNodes)
        {
            if (otherNode == nodeIndex || !IsValidSelectableNode(tab, otherNode)) continue;
            if (IsDescendantNode(tab.loaded, nodeIndex, otherNode))
            {
                coveredBySelectedAncestor = true;
                break;
            }
        }
        if (!coveredBySelectedAncestor)
        {
            roots.push_back(nodeIndex);
        }
    }
    return roots;
}

template <typename PointTransform, typename DirectionTransform>
void ApplyTransformToSelectedSubtree(ModelTab& tab,
                                     PointTransform transformPoint,
                                     DirectionTransform transformDirection,
                                     TransformTool tool,
                                     TransformAxis axis,
                                     float amount)
{
    const std::vector<int> rootNodes = GetSelectedTransformRoots(tab);
    if (rootNodes.empty()) return;
    const bool canCpuSkin = HasCpuSkinnedMesh(tab.loaded);
    bool touchedBoneRoot = false;

    for (int rootNode : rootNodes)
    {
        if (!IsValidSelectableNode(tab, rootNode)) continue;
        const bool rootIsBone = tab.loaded.nodes[static_cast<size_t>(rootNode)].type == SceneNodeType::Bone;
        touchedBoneRoot = touchedBoneRoot || rootIsBone;

        for (int nodeIndex = 0; nodeIndex < static_cast<int>(tab.loaded.nodes.size()); ++nodeIndex)
        {
            if (!IsNodeInTransformScope(tab.loaded, nodeIndex, rootNode)) continue;
            SceneNode& node = tab.loaded.nodes[static_cast<size_t>(nodeIndex)];
            node.position = transformPoint(node.position);
            node.axisX = NormalizeOrFallback(transformDirection(node.axisX), node.axisX);
            node.axisY = NormalizeOrFallback(transformDirection(node.axisY), node.axisY);
            node.axisZ = NormalizeOrFallback(transformDirection(node.axisZ), node.axisZ);
            ApplyTransformValueUpdates(node, tool, axis, amount);

            const bool skinnedMeshHandledByBones = rootIsBone && canCpuSkin && node.type == SceneNodeType::Mesh && node.meshHasSkin;
            if (node.type == SceneNodeType::Mesh && !skinnedMeshHandledByBones)
            {
                TransformMeshNodeRange(tab, node, transformPoint, transformDirection);
            }
        }

        TransformBoneData(tab, rootNode, transformPoint, transformDirection, tool, axis, amount);
    }

    if (touchedBoneRoot && canCpuSkin)
    {
        const bool bakedBindMesh = BakeSkinnedBindMeshFromBones(tab, tab.animation.clipIndex < 0);
        RebuildSkinnedAnimationMeshFrames(tab);
        if (!bakedBindMesh && !RebuildCurrentSkinnedMeshFromBones(tab))
        {
            RecomputeSceneBounds(tab);
            RefreshDisplayedMesh(tab);
        }
    }
    else
    {
        RecomputeSceneBounds(tab);
        RefreshDisplayedMesh(tab);
    }
}

void MoveSelectedSubtree(ModelTab& tab, Vector3 delta)
{
    auto transformPoint = [&](Vector3 point) { return Vector3Add(point, delta); };
    auto transformDirection = [](Vector3 direction) { return direction; };
    ApplyTransformToSelectedSubtree(tab, transformPoint, transformDirection, TransformTool::Move, TransformAxis::None, 0.0f);
}

bool IsValidPivotNode(const ModelTab& tab, int nodeIndex)
{
    return IsValidSelectableNode(tab, nodeIndex) &&
           nodeIndex < static_cast<int>(tab.loaded.nodes.size());
}

bool IsValidJointPivotNode(const ModelTab& tab, int nodeIndex)
{
    return IsValidPivotNode(tab, nodeIndex) &&
           tab.loaded.nodes[static_cast<size_t>(nodeIndex)].type == SceneNodeType::Bone;
}

BonePose MakeBonePoseFromSceneNode(const SceneNode& node, int nodeIndex)
{
    BonePose pose;
    pose.position = node.position;
    pose.axisX = node.axisX;
    pose.axisY = node.axisY;
    pose.axisZ = node.axisZ;
    pose.rotation = node.rotation;
    pose.scale = node.scale;
    pose.node = nodeIndex;
    return pose;
}

void SetBonePosePosition(std::vector<BonePose>& poses, const SceneNode& node, int nodeIndex, Vector3 position, bool createIfMissing)
{
    for (BonePose& pose : poses)
    {
        if (pose.node != nodeIndex) continue;
        pose.position = position;
        return;
    }

    if (createIfMissing)
    {
        BonePose pose = MakeBonePoseFromSceneNode(node, nodeIndex);
        pose.position = position;
        poses.push_back(pose);
    }
}

void SetBonePoseRotation(std::vector<BonePose>& poses, const SceneNode& node, int nodeIndex, Vector3 rotation, bool createIfMissing)
{
    for (BonePose& pose : poses)
    {
        if (pose.node != nodeIndex) continue;
        pose.axisX = node.axisX;
        pose.axisY = node.axisY;
        pose.axisZ = node.axisZ;
        pose.rotation = rotation;
        return;
    }

    if (createIfMissing)
    {
        BonePose pose = MakeBonePoseFromSceneNode(node, nodeIndex);
        pose.rotation = rotation;
        poses.push_back(pose);
    }
}

void SetBonePoseScale(std::vector<BonePose>& poses, const SceneNode& node, int nodeIndex, Vector3 scale, bool createIfMissing)
{
    for (BonePose& pose : poses)
    {
        if (pose.node != nodeIndex) continue;
        pose.scale = scale;
        return;
    }

    if (createIfMissing)
    {
        BonePose pose = MakeBonePoseFromSceneNode(node, nodeIndex);
        pose.scale = scale;
        poses.push_back(pose);
    }
}

void OffsetBoneFrameJointPosition(BoneFrame& frame, int nodeIndex, Vector3 delta)
{
    for (BoneSegment& bone : frame.bones)
    {
        if (bone.startNode == nodeIndex)
        {
            bone.start = Vector3Add(bone.start, delta);
        }
        if (bone.endNode == nodeIndex)
        {
            bone.end = Vector3Add(bone.end, delta);
        }
    }
    for (BonePose& pose : frame.poses)
    {
        if (pose.node == nodeIndex)
        {
            pose.position = Vector3Add(pose.position, delta);
        }
    }
}

void RotateBonePoseAxes(BonePose& pose, Vector3 axisVector, float radians)
{
    pose.axisX = NormalizeOrFallback(Vector3RotateByAxisAngle(pose.axisX, axisVector, radians), pose.axisX);
    pose.axisY = NormalizeOrFallback(Vector3RotateByAxisAngle(pose.axisY, axisVector, radians), pose.axisY);
    pose.axisZ = NormalizeOrFallback(Vector3RotateByAxisAngle(pose.axisZ, axisVector, radians), pose.axisZ);
    pose.rotation = EulerDegreesFromAxes(pose.axisX, pose.axisY, pose.axisZ);
}

void RotateBoneFrameJointAxes(BoneFrame& frame, int nodeIndex, Vector3 axisVector, float radians)
{
    for (BonePose& pose : frame.poses)
    {
        if (pose.node == nodeIndex)
        {
            RotateBonePoseAxes(pose, axisVector, radians);
        }
    }
}

void MoveSelectedJointPivot(ModelTab& tab, Vector3 delta)
{
    if (Vector3Length(delta) <= 0.000001f || !IsValidJointPivotNode(tab, tab.selectedNode)) return;

    const int nodeIndex = tab.selectedNode;
    SceneNode& joint = tab.loaded.nodes[static_cast<size_t>(nodeIndex)];
    joint.position = Vector3Add(joint.position, delta);

    // Editor node transforms are global; unchanged child globals become compensated child locals when saving.
    SetBonePosePosition(tab.loaded.bonePoses, joint, nodeIndex, joint.position, true);
    SetBonePosePosition(tab.visibleBonePoses, joint, nodeIndex, joint.position, false);
    UpdateBoneSegmentsForNode(tab.loaded.bones, nodeIndex, joint.position);
    UpdateBoneSegmentsForNode(tab.visibleBones, nodeIndex, joint.position);

    for (AnimationClip& clip : tab.loaded.animations)
    {
        for (BoneFrame& frame : clip.frames)
        {
            OffsetBoneFrameJointPosition(frame, nodeIndex, delta);
        }
    }
    InvalidateDisplayedAnimationCaches(tab);
}

void MoveSelectedPivot(ModelTab& tab, Vector3 delta)
{
    if (Vector3Length(delta) <= 0.000001f || !IsValidPivotNode(tab, tab.selectedNode)) return;
    if (IsValidJointPivotNode(tab, tab.selectedNode))
    {
        MoveSelectedJointPivot(tab, delta);
        return;
    }

    SceneNode& node = tab.loaded.nodes[static_cast<size_t>(tab.selectedNode)];
    node.position = Vector3Add(node.position, delta);
}

void RotateSelectedJointPivot(ModelTab& tab, Vector3 axisVector, float radians)
{
    if (std::fabs(radians) <= 0.000001f || !IsValidJointPivotNode(tab, tab.selectedNode)) return;

    const int nodeIndex = tab.selectedNode;
    SceneNode& joint = tab.loaded.nodes[static_cast<size_t>(nodeIndex)];
    axisVector = NormalizeOrFallback(axisVector, Vector3{ 0.0f, 1.0f, 0.0f });
    joint.axisX = NormalizeOrFallback(Vector3RotateByAxisAngle(joint.axisX, axisVector, radians), joint.axisX);
    joint.axisY = NormalizeOrFallback(Vector3RotateByAxisAngle(joint.axisY, axisVector, radians), joint.axisY);
    joint.axisZ = NormalizeOrFallback(Vector3RotateByAxisAngle(joint.axisZ, axisVector, radians), joint.axisZ);
    joint.rotation = EulerDegreesFromAxes(joint.axisX, joint.axisY, joint.axisZ);

    for (BonePose& pose : tab.loaded.bonePoses)
    {
        if (pose.node == nodeIndex)
        {
            pose.axisX = joint.axisX;
            pose.axisY = joint.axisY;
            pose.axisZ = joint.axisZ;
            pose.rotation = joint.rotation;
        }
    }
    for (BonePose& pose : tab.visibleBonePoses)
    {
        if (pose.node == nodeIndex)
        {
            RotateBonePoseAxes(pose, axisVector, radians);
        }
    }
    for (AnimationClip& clip : tab.loaded.animations)
    {
        for (BoneFrame& frame : clip.frames)
        {
            RotateBoneFrameJointAxes(frame, nodeIndex, axisVector, radians);
        }
    }
    InvalidateDisplayedAnimationCaches(tab);
}

void RotateSelectedPivot(ModelTab& tab, Vector3 axisVector, float radians)
{
    if (std::fabs(radians) <= 0.000001f || !IsValidPivotNode(tab, tab.selectedNode)) return;
    if (IsValidJointPivotNode(tab, tab.selectedNode))
    {
        RotateSelectedJointPivot(tab, axisVector, radians);
        return;
    }

    SceneNode& node = tab.loaded.nodes[static_cast<size_t>(tab.selectedNode)];
    axisVector = NormalizeOrFallback(axisVector, Vector3{ 0.0f, 1.0f, 0.0f });
    node.axisX = NormalizeOrFallback(Vector3RotateByAxisAngle(node.axisX, axisVector, radians), node.axisX);
    node.axisY = NormalizeOrFallback(Vector3RotateByAxisAngle(node.axisY, axisVector, radians), node.axisY);
    node.axisZ = NormalizeOrFallback(Vector3RotateByAxisAngle(node.axisZ, axisVector, radians), node.axisZ);
    node.rotation = EulerDegreesFromAxes(node.axisX, node.axisY, node.axisZ);
}

void RotateSelectedSubtree(ModelTab& tab, Vector3 pivot, TransformAxis axis, Vector3 axisVector, float radians)
{
    axisVector = NormalizeOrFallback(axisVector, GetTransformAxisVector(axis));
    auto transformPoint = [&](Vector3 point) { return RotatePointAroundAxis(point, pivot, axisVector, radians); };
    auto transformDirection = [&](Vector3 direction) { return NormalizeOrFallback(Vector3RotateByAxisAngle(direction, axisVector, radians), direction); };
    ApplyTransformToSelectedSubtree(tab, transformPoint, transformDirection, TransformTool::Rotate, axis, radians);
}

void RotateSelectedSubtreeArcball(ModelTab& tab, Vector3 pivot, Vector3 rightAxis, float rightRadians, Vector3 upAxis, float upRadians)
{
    rightAxis = NormalizeOrFallback(rightAxis, Vector3{ 1.0f, 0.0f, 0.0f });
    upAxis = NormalizeOrFallback(upAxis, Vector3{ 0.0f, 1.0f, 0.0f });
    auto rotatePoint = [&](Vector3 point)
    {
        point = RotatePointAroundAxis(point, pivot, rightAxis, rightRadians);
        return RotatePointAroundAxis(point, pivot, upAxis, upRadians);
    };
    auto rotateDirection = [&](Vector3 direction)
    {
        Vector3 rotated = Vector3RotateByAxisAngle(direction, rightAxis, rightRadians);
        rotated = Vector3RotateByAxisAngle(rotated, upAxis, upRadians);
        return NormalizeOrFallback(rotated, direction);
    };
    ApplyTransformToSelectedSubtree(tab, rotatePoint, rotateDirection, TransformTool::Rotate, TransformAxis::Center, 0.0f);
}

void ScaleSelectedSubtree(ModelTab& tab, Vector3 pivot, TransformAxis axis, Vector3 axisVector, float factor)
{
    factor = ClampFloat(factor, 0.05f, 20.0f);
    axisVector = NormalizeOrFallback(axisVector, GetTransformAxisVector(axis));
    auto transformPoint = [&](Vector3 point) { return ScalePointAlongAxis(point, pivot, axisVector, factor); };
    auto transformDirection = [&](Vector3 direction) { return ScaleNormalAlongAxis(direction, axisVector, factor); };
    ApplyTransformToSelectedSubtree(tab, transformPoint, transformDirection, TransformTool::Scale, axis, factor);
}

void ScaleSelectedSubtreeUniform(ModelTab& tab, Vector3 pivot, float factor)
{
    factor = ClampFloat(factor, 0.05f, 20.0f);
    auto transformPoint = [&](Vector3 point) { return ScalePointUniform(point, pivot, factor); };
    auto transformDirection = [](Vector3 direction) { return direction; };
    ApplyTransformToSelectedSubtree(tab, transformPoint, transformDirection, TransformTool::Scale, TransformAxis::Center, factor);
}

template <typename TransformFn>
void ApplyTransformToSingleSelectedNode(ModelTab& tab, int nodeIndex, TransformFn transform)
{
    const int previousSelectedNode = tab.selectedNode;
    const std::vector<int> previousSelectedNodes = tab.selectedNodes;
    SetSingleSelectedNode(tab, nodeIndex);
    transform();
    tab.selectedNode = previousSelectedNode;
    tab.selectedNodes = previousSelectedNodes;
    PruneSelectedNodes(tab);
}

Vector3 TransformDirectionBetweenAxes(Vector3 direction,
                                      Vector3 oldAxisX,
                                      Vector3 oldAxisY,
                                      Vector3 oldAxisZ,
                                      Vector3 newAxisX,
                                      Vector3 newAxisY,
                                      Vector3 newAxisZ)
{
    oldAxisX = NormalizeOrFallback(oldAxisX, Vector3{ 1.0f, 0.0f, 0.0f });
    oldAxisY = NormalizeOrFallback(oldAxisY, Vector3{ 0.0f, 1.0f, 0.0f });
    oldAxisZ = NormalizeOrFallback(oldAxisZ, Vector3{ 0.0f, 0.0f, 1.0f });
    newAxisX = NormalizeOrFallback(newAxisX, oldAxisX);
    newAxisY = NormalizeOrFallback(newAxisY, oldAxisY);
    newAxisZ = NormalizeOrFallback(newAxisZ, oldAxisZ);

    return Vector3Add(Vector3Scale(newAxisX, Vector3DotProduct(direction, oldAxisX)),
                      Vector3Add(Vector3Scale(newAxisY, Vector3DotProduct(direction, oldAxisY)),
                                 Vector3Scale(newAxisZ, Vector3DotProduct(direction, oldAxisZ))));
}

void SetSelectedJointPivotRotation(ModelTab& tab, Vector3 rotation)
{
    if (!IsValidJointPivotNode(tab, tab.selectedNode)) return;

    const int nodeIndex = tab.selectedNode;
    SceneNode& joint = tab.loaded.nodes[static_cast<size_t>(nodeIndex)];
    AxesFromEulerDegrees(rotation, joint.axisX, joint.axisY, joint.axisZ);
    joint.rotation = rotation;

    auto setPose = [&](BonePose& pose)
    {
        if (pose.node != nodeIndex) return;
        pose.axisX = joint.axisX;
        pose.axisY = joint.axisY;
        pose.axisZ = joint.axisZ;
        pose.rotation = rotation;
    };

    bool foundLoadedPose = false;
    for (BonePose& pose : tab.loaded.bonePoses)
    {
        if (pose.node == nodeIndex)
        {
            setPose(pose);
            foundLoadedPose = true;
        }
    }
    if (!foundLoadedPose)
    {
        tab.loaded.bonePoses.push_back(MakeBonePoseFromSceneNode(joint, nodeIndex));
    }
    for (BonePose& pose : tab.visibleBonePoses) setPose(pose);
    for (AnimationClip& clip : tab.loaded.animations)
    {
        for (BoneFrame& frame : clip.frames)
        {
            for (BonePose& pose : frame.poses) setPose(pose);
        }
    }
    InvalidateDisplayedAnimationCaches(tab);
}

void SetSelectedPivotRotation(ModelTab& tab, Vector3 rotation)
{
    if (!IsValidPivotNode(tab, tab.selectedNode)) return;
    if (IsValidJointPivotNode(tab, tab.selectedNode))
    {
        SetSelectedJointPivotRotation(tab, rotation);
        return;
    }

    SceneNode& node = tab.loaded.nodes[static_cast<size_t>(tab.selectedNode)];
    AxesFromEulerDegrees(rotation, node.axisX, node.axisY, node.axisZ);
    node.rotation = rotation;
}

void SetSelectedNodeRotation(ModelTab& tab, int nodeIndex, Vector3 rotation)
{
    if (!IsValidSelectableNode(tab, nodeIndex)) return;

    SceneNode& node = tab.loaded.nodes[static_cast<size_t>(nodeIndex)];
    Vector3 newAxisX{};
    Vector3 newAxisY{};
    Vector3 newAxisZ{};
    AxesFromEulerDegrees(rotation, newAxisX, newAxisY, newAxisZ);

    const Vector3 pivot = node.position;
    const Vector3 oldAxisX = node.axisX;
    const Vector3 oldAxisY = node.axisY;
    const Vector3 oldAxisZ = node.axisZ;
    auto transformDirection = [&](Vector3 direction)
    {
        return TransformDirectionBetweenAxes(direction, oldAxisX, oldAxisY, oldAxisZ, newAxisX, newAxisY, newAxisZ);
    };
    auto transformPoint = [&](Vector3 point)
    {
        return Vector3Add(pivot, transformDirection(Vector3Subtract(point, pivot)));
    };

    ApplyTransformToSingleSelectedNode(tab, nodeIndex, [&]()
    {
        ApplyTransformToSelectedSubtree(tab, transformPoint, transformDirection, TransformTool::Rotate, TransformAxis::Center, 0.0f);
    });

    if (IsValidSelectableNode(tab, nodeIndex))
    {
        SceneNode& updatedNode = tab.loaded.nodes[static_cast<size_t>(nodeIndex)];
        updatedNode.axisX = newAxisX;
        updatedNode.axisY = newAxisY;
        updatedNode.axisZ = newAxisZ;
        updatedNode.rotation = rotation;
        if (updatedNode.type == SceneNodeType::Bone)
        {
            SetBonePoseRotation(tab.loaded.bonePoses, updatedNode, nodeIndex, rotation, true);
            SetBonePoseRotation(tab.visibleBonePoses, updatedNode, nodeIndex, rotation, false);
        }
    }
}

void SetSelectedNodeScale(ModelTab& tab, int nodeIndex, Vector3 scale)
{
    if (!IsValidSelectableNode(tab, nodeIndex)) return;

    const SceneNode& node = tab.loaded.nodes[static_cast<size_t>(nodeIndex)];
    const Vector3 current = node.scale;
    const Vector3 pivot = node.position;
    const Vector3 axes[] = {
        NormalizeOrFallback(node.axisX, Vector3{ 1.0f, 0.0f, 0.0f }),
        NormalizeOrFallback(node.axisY, Vector3{ 0.0f, 1.0f, 0.0f }),
        NormalizeOrFallback(node.axisZ, Vector3{ 0.0f, 0.0f, 1.0f })
    };
    const float factors[] = {
        scale.x / std::max(0.000001f, std::fabs(current.x)),
        scale.y / std::max(0.000001f, std::fabs(current.y)),
        scale.z / std::max(0.000001f, std::fabs(current.z))
    };
    const TransformAxis transformAxes[] = { TransformAxis::X, TransformAxis::Y, TransformAxis::Z };

    ApplyTransformToSingleSelectedNode(tab, nodeIndex, [&]()
    {
        for (int i = 0; i < 3; ++i)
        {
            if (std::fabs(factors[i] - 1.0f) <= 0.000001f) continue;
            ApplyTransformToSelectedSubtree(tab,
                                           [&](Vector3 point) { return ScalePointAlongAxis(point, pivot, axes[i], factors[i]); },
                                           [&](Vector3 direction) { return ScaleNormalAlongAxis(direction, axes[i], factors[i]); },
                                           TransformTool::Scale,
                                           transformAxes[i],
                                           factors[i]);
        }
    });

    if (IsValidSelectableNode(tab, nodeIndex))
    {
        SceneNode& updatedNode = tab.loaded.nodes[static_cast<size_t>(nodeIndex)];
        updatedNode.scale = scale;
        if (updatedNode.type == SceneNodeType::Bone)
        {
            SetBonePoseScale(tab.loaded.bonePoses, updatedNode, nodeIndex, scale, true);
            SetBonePoseScale(tab.visibleBonePoses, updatedNode, nodeIndex, scale, false);
        }
    }
}

