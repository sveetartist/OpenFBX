void DrawMeshNodeWireframe(const ModelTab& tab, const SceneNode& node, Color color)
{
    const float* vertices = GetCurrentMeshVertices(tab);
    if (!vertices || node.meshVertexStart < 0 || node.meshVertexCount < 3) return;
    const int nodeIndex = static_cast<int>(&node - tab.loaded.nodes.data());

    if (nodeIndex >= 0 && nodeIndex < static_cast<int>(tab.loaded.nodes.size()) && !tab.loaded.meshPolygonEdges.empty() && node.removedTriangleStarts.empty())
    {
        const int vertexCount = static_cast<int>(tab.loaded.bindVertices.size() / 3);
        bool drewSourceEdges = false;
        for (const MeshEdge& edge : tab.loaded.meshPolygonEdges)
        {
            if (edge.node != nodeIndex) continue;
            if (edge.a < 0 || edge.b < 0 || edge.a >= vertexCount || edge.b >= vertexCount) continue;

            const int i0 = edge.a * 3;
            const int i1 = edge.b * 3;
            const Vector3 p0{ vertices[i0], vertices[i0 + 1], vertices[i0 + 2] };
            const Vector3 p1{ vertices[i1], vertices[i1 + 1], vertices[i1 + 2] };
            DrawLine3D(p0, p1, color);
            drewSourceEdges = true;
        }

        if (drewSourceEdges) return;
    }

    const int start = node.meshVertexStart;
    const int end = node.meshVertexStart + node.meshVertexCount;
    for (int vertex = start; vertex + 2 < end; vertex += 3)
    {
        if (IsRemovedTriangle(node, vertex)) continue;
        const int i0 = vertex * 3;
        const int i1 = (vertex + 1) * 3;
        const int i2 = (vertex + 2) * 3;
        const Vector3 p0{ vertices[i0], vertices[i0 + 1], vertices[i0 + 2] };
        const Vector3 p1{ vertices[i1], vertices[i1 + 1], vertices[i1 + 2] };
        const Vector3 p2{ vertices[i2], vertices[i2 + 1], vertices[i2 + 2] };
        DrawLine3D(p0, p1, color);
        DrawLine3D(p1, p2, color);
        DrawLine3D(p2, p0, color);
    }
}

void DrawVisibleMeshWireframe(const ModelTab& tab, Color color)
{
    for (int nodeIndex = 0; nodeIndex < static_cast<int>(tab.loaded.nodes.size()); ++nodeIndex)
    {
        const SceneNode& node = tab.loaded.nodes[static_cast<size_t>(nodeIndex)];
        if (node.type != SceneNodeType::Mesh || !IsViewportNodeVisible(tab, nodeIndex)) continue;
        DrawMeshNodeWireframe(tab, node, color);
    }
}

bool GetRayCollisionMeshNodeTriangles(const ModelTab& tab, const SceneNode& node, Ray ray, RayCollision& outHit)
{
    const float* vertices = GetCurrentMeshVertices(tab);
    if (!vertices || node.meshVertexStart < 0 || node.meshVertexCount < 3) return false;

    bool hitAny = false;
    RayCollision bestHit{};
    bestHit.distance = std::numeric_limits<float>::max();

    const int start = node.meshVertexStart;
    const int end = node.meshVertexStart + node.meshVertexCount;
    for (int vertex = start; vertex + 2 < end; vertex += 3)
    {
        if (IsRemovedTriangle(node, vertex)) continue;
        const int i0 = vertex * 3;
        const int i1 = (vertex + 1) * 3;
        const int i2 = (vertex + 2) * 3;
        const Vector3 p0{ vertices[i0], vertices[i0 + 1], vertices[i0 + 2] };
        const Vector3 p1{ vertices[i1], vertices[i1 + 1], vertices[i1 + 2] };
        const Vector3 p2{ vertices[i2], vertices[i2 + 1], vertices[i2 + 2] };
        const RayCollision hit = GetRayCollisionTriangle(ray, p0, p1, p2);
        if (hit.hit && hit.distance > 0.0f && hit.distance < bestHit.distance)
        {
            bestHit = hit;
            hitAny = true;
        }
    }

    if (hitAny)
    {
        outHit = bestHit;
    }
    return hitAny;
}

float DistancePointToRay(Vector3 point, Ray ray)
{
    const Vector3 toPoint = Vector3Subtract(point, ray.position);
    const Vector3 projected = Vector3Scale(ray.direction, Vector3DotProduct(toPoint, ray.direction));
    return Vector3Length(Vector3Subtract(toPoint, projected));
}

float DistancePointToScreenSegment(Vector2 point, Vector2 a, Vector2 b)
{
    const Vector2 ab = Vector2Subtract(b, a);
    const float lengthSq = Vector2DotProduct(ab, ab);
    if (lengthSq <= 0.0001f)
    {
        return Vector2Distance(point, a);
    }

    const float t = ClampFloat(Vector2DotProduct(Vector2Subtract(point, a), ab) / lengthSq, 0.0f, 1.0f);
    const Vector2 closest = Vector2Add(a, Vector2Scale(ab, t));
    return Vector2Distance(point, closest);
}

float ClosestAlphaOnScreenSegment(Vector2 point, Vector2 a, Vector2 b)
{
    const Vector2 ab = Vector2Subtract(b, a);
    const float lengthSq = Vector2DotProduct(ab, ab);
    if (lengthSq <= 0.0001f)
    {
        return 0.0f;
    }

    return ClampFloat(Vector2DotProduct(Vector2Subtract(point, a), ab) / lengthSq, 0.0f, 1.0f);
}

int PickNodeFromViewport(const ModelTab& tab, Vector2 mouse, const VisibilityState& visibility)
{
    if (!tab.loaded.valid || tab.loaded.nodes.empty()) return -1;

    const Ray ray = GetScreenToWorldRay(mouse, tab.orbit.camera);
    int bestNode = -1;
    float bestDistance = std::numeric_limits<float>::max();
    int bestBoneNode = -1;
    float bestBoneScreenDistance = std::numeric_limits<float>::max();
    float bestBoneDepth = std::numeric_limits<float>::max();
    constexpr float bonePickRadiusPixels = 18.0f;

    if (visibility.bones)
    {
        for (const BoneSegment& bone : GetVisibleBones(tab))
        {
            const float startDepth = Vector3DotProduct(Vector3Subtract(bone.start, ray.position), ray.direction);
            const float endDepth = Vector3DotProduct(Vector3Subtract(bone.end, ray.position), ray.direction);
            if (startDepth <= 0.0f && endDepth <= 0.0f) continue;

            const Vector2 startScreen = GetWorldToScreen(bone.start, tab.orbit.camera);
            const Vector2 endScreen = GetWorldToScreen(bone.end, tab.orbit.camera);
            const float screenDistance = DistancePointToScreenSegment(mouse, startScreen, endScreen);
            const float startDistance = Vector2Distance(mouse, startScreen);
            const float endDistance = Vector2Distance(mouse, endScreen);
            const float jointDistance = std::min(startDistance, endDistance);
            const float pickDistance = std::min(screenDistance, jointDistance);
            if (pickDistance <= bonePickRadiusPixels)
            {
                const float depth = std::max(0.0f, std::min(startDepth, endDepth));
                const bool closerOnScreen = pickDistance < bestBoneScreenDistance - 1.0f;
                const bool sameScreenDistanceButCloser = std::fabs(pickDistance - bestBoneScreenDistance) <= 1.0f && depth < bestBoneDepth;
                if (closerOnScreen || sameScreenDistanceButCloser)
                {
                    const float segmentAlpha = ClosestAlphaOnScreenSegment(mouse, startScreen, endScreen);
                    const bool nearEndJoint = endDistance <= bonePickRadiusPixels && endDistance <= startDistance;
                    const bool nearStartJoint = startDistance <= bonePickRadiusPixels && startDistance < endDistance;
                    const bool canPickStart = IsValidSelectableNode(tab, bone.startNode);
                    const bool canPickEnd = IsValidSelectableNode(tab, bone.endNode);
                    if (!canPickStart && !canPickEnd) continue;

                    bestBoneScreenDistance = pickDistance;
                    bestBoneDepth = depth;
                    if ((nearEndJoint || (!nearStartJoint && segmentAlpha >= 0.5f)) && canPickEnd)
                    {
                        bestBoneNode = bone.endNode;
                    }
                    else
                    {
                        bestBoneNode = canPickStart ? bone.startNode : canPickEnd ? bone.endNode : -1;
                    }
                }
            }
        }
    }

    if (bestBoneNode >= 0)
    {
        return bestBoneNode;
    }

    for (int i = 0; i < static_cast<int>(tab.loaded.nodes.size()); ++i)
    {
        const SceneNode& node = tab.loaded.nodes[static_cast<size_t>(i)];
        if (!IsValidSelectableNode(tab, i)) continue;
        if (!IsViewportNodeVisible(tab, i)) continue;

        if (node.type == SceneNodeType::Mesh)
        {
            if (!visibility.geometry) continue;
            RayCollision hit{};
            if (!GetRayCollisionMeshNodeTriangles(tab, node, ray, hit)) continue;
            if (hit.hit && hit.distance < bestDistance)
            {
                bestDistance = hit.distance;
                bestNode = i;
            }
        }
        else
        {
            if ((node.type == SceneNodeType::Bone && !visibility.bones) ||
                (node.type == SceneNodeType::Empty && !visibility.empties))
            {
                continue;
            }
            const float handleRadius = std::max(0.03f, tab.orbit.distance * 0.015f);
            const float distance = DistancePointToRay(node.position, ray);
            if (distance < handleRadius)
            {
                const float rayDepth = Vector3DotProduct(Vector3Subtract(node.position, ray.position), ray.direction);
                if (rayDepth > 0.0f && rayDepth < bestDistance)
                {
                    bestDistance = rayDepth;
                    bestNode = i;
                }
            }
        }
    }

    if (bestNode >= 0)
    {
        return bestNode;
    }

    return -1;
}

struct MarqueeSelectionState
{
    ModelTab* tab = nullptr;
    Vector2 start{};
    Vector2 end{};
    bool dragging = false;
    bool additive = false;
    bool subtractive = false;
};

Rectangle GetMarqueeRectangle(Vector2 start, Vector2 end)
{
    return Rectangle{ std::min(start.x, end.x), std::min(start.y, end.y),
        std::fabs(end.x - start.x), std::fabs(end.y - start.y) };
}

bool IntersectsMarquee(const Vector3* points, int count, const Camera3D& camera,
                       Rectangle rectangle, int screenWidth, int screenHeight)
{
    // Clip before projection so geometry behind the camera cannot select mirrored objects.
    std::array<Vector3, 8> polygon{};
    std::copy(points, points + count, polygon.begin());
    const Vector3 forward = NormalizeOrFallback(Vector3Subtract(camera.target, camera.position), Vector3{ 0, 0, -1 });
    for (int plane = 0; plane < 2; ++plane)
    {
        std::array<Vector3, 8> clipped{};
        int clippedCount = 0;
        const auto distance = [&](Vector3 point)
        {
            const float depth = Vector3DotProduct(Vector3Subtract(point, camera.position), forward);
            return plane == 0 ? depth - static_cast<float>(kNearClipPlane) : static_cast<float>(kFarClipPlane) - depth;
        };
        for (int i = 0; i < count; ++i)
        {
            const Vector3 a = polygon[static_cast<size_t>(i)];
            const Vector3 b = polygon[static_cast<size_t>((i + 1) % count)];
            const float da = distance(a), db = distance(b);
            if (da >= 0) clipped[static_cast<size_t>(clippedCount++)] = a;
            if ((da >= 0) != (db >= 0))
                clipped[static_cast<size_t>(clippedCount++)] = Vector3Lerp(a, b, da / (da - db));
        }
        count = clippedCount;
        polygon = clipped;
        if (!count) return false;
    }
    std::array<Vector2, 8> projected{};
    for (int i = 0; i < count; ++i)
    {
        projected[static_cast<size_t>(i)] = GetWorldToScreenEx(polygon[static_cast<size_t>(i)], camera, screenWidth, screenHeight);
        if (CheckCollisionPointRec(projected[static_cast<size_t>(i)], rectangle)) return true;
    }
    const Vector2 corners[] = {
        { rectangle.x, rectangle.y }, { rectangle.x + rectangle.width, rectangle.y },
        { rectangle.x + rectangle.width, rectangle.y + rectangle.height }, { rectangle.x, rectangle.y + rectangle.height }
    };
    for (int i = 0; i < count; ++i)
        for (int edge = 0; edge < 4; ++edge)
            if (CheckCollisionLines(projected[static_cast<size_t>(i)], projected[static_cast<size_t>((i + 1) % count)],
                                    corners[edge], corners[(edge + 1) % 4], nullptr)) return true;
    for (int i = 1; i + 1 < count; ++i)
        for (Vector2 corner : corners)
            if (CheckCollisionPointTriangle(corner, projected[0], projected[static_cast<size_t>(i)], projected[static_cast<size_t>(i + 1)])) return true;
    return false;
}

void SelectNodesInMarquee(ModelTab& tab, Rectangle rectangle, const VisibilityState& visibility,
                          bool additive, int screenWidth, int screenHeight, bool subtractive = false)
{
    if (!additive && !subtractive) ClearNodeSelection(tab);
    const auto applyNode = [&](int nodeIndex)
    {
        if (!IsValidSelectableNode(tab, nodeIndex) || !IsViewportNodeVisible(tab, nodeIndex)) return;
        if (subtractive)
        {
            tab.selectedNodes.erase(std::remove(tab.selectedNodes.begin(), tab.selectedNodes.end(), nodeIndex), tab.selectedNodes.end());
            if (tab.selectedNode == nodeIndex)
                tab.selectedNode = tab.selectedNodes.empty() ? -1 : tab.selectedNodes.back();
            return;
        }
        if (!IsNodeSelected(tab, nodeIndex)) tab.selectedNodes.push_back(nodeIndex);
        tab.selectedNode = nodeIndex;
    };
    const auto intersects = [&](const Vector3* points, int count)
    {
        return IntersectsMarquee(points, count, tab.orbit.camera, rectangle, screenWidth, screenHeight);
    };
    const float* vertices = GetCurrentMeshVertices(tab);
    const int vertexCount = static_cast<int>(tab.loaded.bindVertices.size() / 3);
    for (int index = 0; index < static_cast<int>(tab.loaded.nodes.size()); ++index)
    {
        if (!IsValidSelectableNode(tab, index) || !IsViewportNodeVisible(tab, index)) continue;
        const auto& node = tab.loaded.nodes[static_cast<size_t>(index)];
        if (node.type == SceneNodeType::Mesh && visibility.geometry && vertices && node.meshVertexStart >= 0)
        {
            const int end = std::min(vertexCount, node.meshVertexStart + node.meshVertexCount);
            for (int vertex = node.meshVertexStart; vertex + 2 < end; vertex += 3)
            {
                if (IsRemovedTriangle(node, vertex)) continue;
                Vector3 triangle[3];
                for (int corner = 0; corner < 3; ++corner)
                {
                    const int offset = (vertex + corner) * 3;
                    triangle[corner] = Vector3{ vertices[offset], vertices[offset + 1], vertices[offset + 2] };
                }
                if (intersects(triangle, 3)) { applyNode(index); break; }
            }
        }
        else if (node.type == SceneNodeType::Empty && visibility.empties && intersects(&node.position, 1))
            applyNode(index);
    }
    if (visibility.bones)
    {
        for (const auto& pose : GetVisibleBonePoses(tab))
            if (intersects(&pose.position, 1)) applyNode(pose.node);
        for (const auto& bone : GetVisibleBones(tab))
        {
            const Vector3 segment[] = { bone.start, bone.end };
            if (intersects(segment, 2)) applyNode(bone.startNode);
        }
    }
}

bool SelectNodeFromViewport(ModelTab& tab, Vector2 mouse, const VisibilityState& visibility, bool additive = false)
{
    const int pickedNode = PickNodeFromViewport(tab, mouse, visibility);
    if (pickedNode < 0) return false;

    SelectNode(tab, pickedNode, additive);
    return true;
}

bool PickMeshNodeFromViewport(const ModelTab& tab, Vector2 mouse, const VisibilityState& visibility, int& outNode)
{
    outNode = -1;
    if (!tab.loaded.valid || !tab.loaded.hasMesh || tab.loaded.nodes.empty()) return false;
    if (!visibility.geometry) return false;

    const Ray ray = GetScreenToWorldRay(mouse, tab.orbit.camera);
    float bestDistance = std::numeric_limits<float>::max();
    for (int nodeIndex = 0; nodeIndex < static_cast<int>(tab.loaded.nodes.size()); ++nodeIndex)
    {
        const SceneNode& node = tab.loaded.nodes[static_cast<size_t>(nodeIndex)];
        if (node.type != SceneNodeType::Mesh || !IsViewportNodeVisible(tab, nodeIndex)) continue;

        RayCollision hit{};
        if (!GetRayCollisionMeshNodeTriangles(tab, node, ray, hit)) continue;
        if (hit.hit && hit.distance < bestDistance)
        {
            bestDistance = hit.distance;
            outNode = nodeIndex;
        }
    }
    return outNode >= 0;
}

bool PaintSkinWeightsAtMouse(ModelTab& tab,
                             Vector2 mouse,
                             const VisibilityState& visibility,
                             const WeightBrushSettings& brush,
                             WeightBrushMode mode,
                             std::string& notice,
                             std::string& error)
{
    if (!HasCpuSkinnedMesh(tab.loaded))
    {
        error = "No CPU skin weights available to paint.";
        notice.clear();
        return false;
    }

    if (tab.selectedNode < 0 ||
        tab.selectedNode >= static_cast<int>(tab.loaded.nodes.size()) ||
        tab.loaded.nodes[static_cast<size_t>(tab.selectedNode)].type != SceneNodeType::Bone ||
        IsDeletedNode(tab, tab.selectedNode))
    {
        error = "Select a bone to paint weights.";
        notice.clear();
        return false;
    }

    int meshNodeIndex = -1;
    if (!PickMeshNodeFromViewport(tab, mouse, visibility, meshNodeIndex))
    {
        return false;
    }

    const BonePose* bonePose = FindBonePoseByNode(tab.originalBindBonePoses, tab.selectedNode);
    if (!bonePose)
    {
        bonePose = FindBonePoseByNode(tab.loaded.bonePoses, tab.selectedNode);
    }
    if (!bonePose)
    {
        error = "Selected bone has no bind pose.";
        notice.clear();
        return false;
    }

    const SceneNode& meshNode = tab.loaded.nodes[static_cast<size_t>(meshNodeIndex)];
    const int start = std::max(0, meshNode.meshVertexStart);
    const int end = std::min(meshNode.meshVertexStart + meshNode.meshVertexCount, static_cast<int>(tab.loaded.skinnedVertices.size()));
    if (start >= end) return false;

    const float* displayedVertices = GetCurrentMeshVertices(tab);
    if (!displayedVertices) return false;

    const std::string& boneName = tab.loaded.nodes[static_cast<size_t>(tab.selectedNode)].name;
    const float radius = ClampFloat(brush.sizePixels, 4.0f, 220.0f);
    const float radiusSqr = radius * radius;
    const float amountScale = ClampFloat(brush.strength, 0.0f, 1.0f) * std::max(0.0f, GetFrameTime()) * 2.0f;
    if (amountScale <= 0.000001f) return false;

    struct BrushVertex
    {
        int index = -1;
        float falloff = 0.0f;
        float oldWeight = 0.0f;
        Vector3 bindPoint{};
    };

    std::vector<BrushVertex> brushVertices;
    brushVertices.reserve(static_cast<size_t>(end - start));
    float smoothWeightSum = 0.0f;
    float smoothFalloffSum = 0.0f;
    int changedVertices = 0;
    for (int vertexIndex = start; vertexIndex < end; ++vertexIndex)
    {
        const size_t base = static_cast<size_t>(vertexIndex) * 3;
        if (base + 2 >= tab.loaded.bindVertices.size()) continue;

        const Vector3 displayedPoint{
            displayedVertices[base],
            displayedVertices[base + 1],
            displayedVertices[base + 2]
        };
        const Vector2 screen = GetWorldToScreen(displayedPoint, tab.orbit.camera);
        const float distanceSqr = Vector2DistanceSqr(mouse, screen);
        if (distanceSqr > radiusSqr) continue;

        const Vector3 bindPoint{
            tab.loaded.bindVertices[base],
            tab.loaded.bindVertices[base + 1],
            tab.loaded.bindVertices[base + 2]
        };
        const float falloff = 1.0f - std::sqrt(distanceSqr) / radius;
        const float clampedFalloff = ClampFloat(falloff, 0.0f, 1.0f);
        const float oldWeight = GetBoneWeightOnVertex(tab.loaded.skinnedVertices[static_cast<size_t>(vertexIndex)], boneName);
        brushVertices.push_back(BrushVertex{ vertexIndex, clampedFalloff, oldWeight, bindPoint });
        smoothWeightSum += oldWeight * clampedFalloff;
        smoothFalloffSum += clampedFalloff;
    }

    if (brushVertices.empty()) return false;
    tab.skinningGeometry.reset();

    const float smoothTargetWeight = smoothFalloffSum > 0.000001f ? smoothWeightSum / smoothFalloffSum : 0.0f;
    for (const BrushVertex& brushVertex : brushVertices)
    {
        const float amount = amountScale * brushVertex.falloff;
        SkinnedVertex& vertex = tab.loaded.skinnedVertices[static_cast<size_t>(brushVertex.index)];
        vertex.bindPosition = brushVertex.bindPoint;

        bool changed = false;
        if (mode == WeightBrushMode::Subtract)
        {
            changed = SubtractBoneWeightFromVertex(vertex, boneName, *bonePose, amount, brush.autoNormalize);
        }
        else if (mode == WeightBrushMode::Smooth)
        {
            const float targetWeight = brushVertex.oldWeight + (smoothTargetWeight - brushVertex.oldWeight) * ClampFloat(amount, 0.0f, 1.0f);
            changed = SetBoneWeightOnVertex(vertex, boneName, *bonePose, targetWeight, brush.autoNormalize, true);
        }
        else
        {
            changed = AddBoneWeightToVertex(vertex, boneName, *bonePose, amount, brush.autoNormalize);
        }

        if (changed)
        {
            ++changedVertices;
        }
    }

    if (changedVertices <= 0) return false;

    RebuildSkinnedAnimationMeshFrames(tab);
    if (tab.animation.clipIndex < 0 && !RebuildCurrentSkinnedMeshFromBones(tab))
    {
        RefreshDisplayedMesh(tab);
    }
    if (mode == WeightBrushMode::Subtract)
    {
        notice = "Subtracted weights: " + std::to_string(changedVertices) + " vertices.";
    }
    else if (mode == WeightBrushMode::Smooth)
    {
        notice = "Smoothed weights: " + std::to_string(changedVertices) + " vertices.";
    }
    else
    {
        notice = "Painted weights: " + std::to_string(changedVertices) + " vertices.";
    }
    error.clear();
    return true;
}

bool GetSelectedNodePosition(const ModelTab& tab, Vector3& outPosition)
{
    if (tab.selectedNode < 0 || tab.selectedNode >= static_cast<int>(tab.loaded.nodes.size())) return false;

    const SceneNode& node = tab.loaded.nodes[static_cast<size_t>(tab.selectedNode)];
    if (node.type == SceneNodeType::Bone)
    {
        for (const BoneSegment& bone : GetVisibleBones(tab))
        {
            if (bone.endNode == tab.selectedNode)
            {
                outPosition = bone.end;
                return true;
            }
            if (bone.startNode == tab.selectedNode)
            {
                outPosition = bone.start;
                return true;
            }
        }
    }

    outPosition = node.position;
    return true;
}

bool FocusCameraOnSelection(ModelTab& tab)
{
    if (tab.selectedNode < 0 || tab.selectedNode >= static_cast<int>(tab.loaded.nodes.size())) return false;

    const SceneNode& node = tab.loaded.nodes[static_cast<size_t>(tab.selectedNode)];
    if (!IsViewportNodeVisible(tab, tab.selectedNode)) return false;
    if (node.type == SceneNodeType::Mesh && node.hasBounds)
    {
        FocusCameraOnBounds(tab.orbit, node.bounds);
        return true;
    }

    Vector3 position{};
    if (!GetSelectedNodePosition(tab, position)) return false;

    FocusCameraOnPoint(tab.orbit, position, GetBoundsDiagonal(tab.loaded.bounds));
    return true;
}

void DrawSelectedMeshOverlay(const ModelTab& tab, const VisibilityState& visibility)
{
    if (!visibility.geometry) return;

    for (int nodeIndex : tab.selectedNodes)
    {
        if (nodeIndex < 0 || nodeIndex >= static_cast<int>(tab.loaded.nodes.size())) continue;
        if (!IsViewportNodeVisible(tab, nodeIndex)) continue;

        const SceneNode& node = tab.loaded.nodes[static_cast<size_t>(nodeIndex)];
        if (node.type == SceneNodeType::Mesh && node.hasBounds)
        {
            DrawMeshNodeWireframe(tab, node, kSelectionColor);
        }
    }
}

void DrawSelectedNodeOverlay(const ModelTab& tab, const VisibilityState& visibility, TransformTool tool)
{
    for (int nodeIndex : tab.selectedNodes)
    {
        if (nodeIndex < 0 || nodeIndex >= static_cast<int>(tab.loaded.nodes.size())) continue;
        if (!IsViewportNodeVisible(tab, nodeIndex)) continue;

        const SceneNode& node = tab.loaded.nodes[static_cast<size_t>(nodeIndex)];
        if (node.type == SceneNodeType::Mesh && node.hasBounds)
        {
            DrawMeshOrigin(node, tab.orbit.camera);
        }
        else if (node.type == SceneNodeType::Bone && visibility.bones && tool == TransformTool::Select)
        {
            DrawBoneOrigin(node, FindBonePoseByNodeLinear(GetVisibleBonePoses(tab), nodeIndex), tab.orbit.camera);
        }
        else if (node.type == SceneNodeType::Empty && visibility.empties)
        {
            const float length = ClampFloat(GetBoundsDiagonal(tab.loaded.bounds) * 0.055f, 0.08f, 0.8f);
            DrawEmptyCross(node, length, kSelectionColor);
        }
    }
}

