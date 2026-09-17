void DrawJointCircle(Vector3 position, Vector3 axisA, Vector3 axisB, float radius, Color color)
{
    constexpr int kSegments = 28;
    Vector3 previous = Vector3Add(position, Vector3Scale(axisA, radius));
    for (int i = 1; i <= kSegments; ++i)
    {
        const float angle = static_cast<float>(i) / static_cast<float>(kSegments) * 2.0f * PI;
        const Vector3 offset = Vector3Add(Vector3Scale(axisA, std::cos(angle) * radius),
                                          Vector3Scale(axisB, std::sin(angle) * radius));
        const Vector3 current = Vector3Add(position, offset);
        DrawLine3D(previous, current, color);
        previous = current;
    }
}

void DrawJointSphere(Vector3 position, Vector3 axisX, Vector3 axisY, Vector3 axisZ, float radius, Color color)
{
    axisX = NormalizeOrFallback(axisX, Vector3{ 1.0f, 0.0f, 0.0f });
    axisY = NormalizeOrFallback(axisY, Vector3{ 0.0f, 1.0f, 0.0f });
    axisZ = NormalizeOrFallback(axisZ, Vector3{ 0.0f, 0.0f, 1.0f });
    DrawJointCircle(position, axisX, axisY, radius, color);
    DrawJointCircle(position, axisX, axisZ, radius, color);
    DrawJointCircle(position, axisY, axisZ, radius, color);
}

void DrawMayaBone(Vector3 start, Vector3 end, float radius, Color color)
{
    const Vector3 axis = Vector3Subtract(end, start);
    const float length = Vector3Length(axis);
    if (length < 0.0001f) return;

    const Vector3 forward = Vector3Scale(axis, 1.0f / length);
    Vector3 side = Vector3CrossProduct(forward, Vector3{ 0.0f, 1.0f, 0.0f });
    if (Vector3Length(side) < 0.0001f)
    {
        side = Vector3CrossProduct(forward, Vector3{ 1.0f, 0.0f, 0.0f });
    }
    side = Vector3Normalize(side);
    const Vector3 up = Vector3Normalize(Vector3CrossProduct(side, forward));
    const Vector3 base = Vector3Add(start, Vector3Scale(forward, std::min(length * 0.28f, radius * 5.0f)));
    const Vector3 points[] = {
        Vector3Add(base, Vector3Scale(side, radius)),
        Vector3Subtract(base, Vector3Scale(side, radius)),
        Vector3Add(base, Vector3Scale(up, radius)),
        Vector3Subtract(base, Vector3Scale(up, radius))
    };

    for (const Vector3& point : points)
    {
        DrawLine3D(start, point, color);
        DrawLine3D(point, end, color);
    }

    DrawLine3D(points[0], points[2], color);
    DrawLine3D(points[2], points[1], color);
    DrawLine3D(points[1], points[3], color);
    DrawLine3D(points[3], points[0], color);
}

bool IsNodeInSelectionList(const std::vector<int>& selectedNodes, int nodeIndex)
{
    return std::find(selectedNodes.begin(), selectedNodes.end(), nodeIndex) != selectedNodes.end();
}

const BonePose* FindBonePoseByNodeLinear(const std::vector<BonePose>& poses, int nodeIndex)
{
    for (const BonePose& pose : poses)
    {
        if (pose.node == nodeIndex) return &pose;
    }
    return nullptr;
}

void DrawJointSphereForNode(const std::vector<BonePose>& poses, int nodeIndex, Vector3 position, float radius, Color color)
{
    const BonePose* pose = FindBonePoseByNodeLinear(poses, nodeIndex);
    const Vector3 axisX = pose ? pose->axisX : Vector3{ 1.0f, 0.0f, 0.0f };
    const Vector3 axisY = pose ? pose->axisY : Vector3{ 0.0f, 1.0f, 0.0f };
    const Vector3 axisZ = pose ? pose->axisZ : Vector3{ 0.0f, 0.0f, 1.0f };
    DrawJointSphere(position, axisX, axisY, axisZ, radius, color);
}

void DrawBones(const std::vector<BoneSegment>& bones, const std::vector<BonePose>& poses, int selectedNode, const std::vector<int>& selectedNodes)
{
    float radius = 0.035f;
    if (!bones.empty())
    {
        float totalLength = 0.0f;
        for (const BoneSegment& bone : bones)
        {
            totalLength += Vector3Length(Vector3Subtract(bone.end, bone.start));
        }
        radius = ClampFloat(totalLength / static_cast<float>(bones.size()) * 0.06f, 0.015f, 0.12f);
    }

    for (const BoneSegment& bone : bones)
    {
        const bool selected = bone.startNode == selectedNode || IsNodeInSelectionList(selectedNodes, bone.startNode);
        DrawMayaBone(bone.start, bone.end, radius, selected ? kSelectionColor : Color{ 100, 185, 255, 255 });
    }

    for (const BoneSegment& bone : bones)
    {
        const bool startSelected = bone.startNode == selectedNode || IsNodeInSelectionList(selectedNodes, bone.startNode);
        const bool endSelected = bone.endNode == selectedNode || IsNodeInSelectionList(selectedNodes, bone.endNode);
        DrawJointSphereForNode(poses, bone.startNode, bone.start, startSelected ? radius : radius * 0.85f, startSelected ? kSelectionColor : Color{ 142, 210, 255, 255 });
        DrawJointSphereForNode(poses, bone.endNode, bone.end, endSelected ? radius : radius * 0.85f, endSelected ? kSelectionColor : Color{ 142, 210, 255, 255 });
    }
}

void DrawBoneRotations(const std::vector<BonePose>& poses, float sceneDiagonal, int selectedNode, const Camera3D& camera)
{
    const float axisLength = ClampFloat(sceneDiagonal * 0.028f, 0.04f, 0.32f) * gBoneOrientationScale;
    const float screenHeight = static_cast<float>(std::max(1, GetScreenHeight()));
    const Vector3 forward = NormalizeOrFallback(Vector3Subtract(camera.target, camera.position), Vector3{ 0.0f, 0.0f, -1.0f });
    for (const BonePose& pose : poses)
    {
        const unsigned char alpha = pose.node == selectedNode ? 255 : 190;
        const auto drawAxis = [&](Vector3 axis, Color color)
        {
            const Vector3 end = Vector3Add(pose.position, Vector3Scale(axis, axisLength));
            const Vector3 midpoint = Vector3Scale(Vector3Add(pose.position, end), 0.5f);
            const float depth = std::max(0.0001f, Vector3DotProduct(Vector3Subtract(midpoint, camera.position), forward));
            const float worldPerPixel = camera.projection == CAMERA_ORTHOGRAPHIC
                ? camera.fovy / screenHeight
                : 2.0f * depth * std::tan(camera.fovy * DEG2RAD * 0.5f) / screenHeight;
            const float radius = std::max(0.000001f, worldPerPixel * gBoneOrientationLineWidth * 0.5f);
            DrawCylinderEx(pose.position, end, radius, radius, 8, color);
        };
        drawAxis(pose.axisX, Color{ 235, 74, 74, alpha });
        drawAxis(pose.axisY, Color{ 92, 210, 94, alpha });
        drawAxis(pose.axisZ, Color{ 86, 142, 255, alpha });
    }
}

float GetBoundsDiagonal(const BoundingBox& bounds)
{
    return Vector3Length(Vector3Subtract(bounds.max, bounds.min));
}

void DrawAxisLine(Vector3 origin, Vector3 axis, float length, Color color)
{
    const Vector3 end = Vector3Add(origin, Vector3Scale(axis, length));
    DrawLine3D(origin, end, color);
    DrawSphere(end, length * 0.045f, color);
}

void DrawNodeAxes(const SceneNode& node, float length, unsigned char alpha)
{
    DrawAxisLine(node.position, node.axisX, length, Color{ 235, 74, 74, alpha });
    DrawAxisLine(node.position, node.axisY, length, Color{ 92, 210, 94, alpha });
    DrawAxisLine(node.position, node.axisZ, length, Color{ 86, 142, 255, alpha });
}

void DrawEmptyCross(const SceneNode& node, float length, Color color)
{
    DrawLine3D(Vector3Subtract(node.position, Vector3Scale(node.axisX, length)), Vector3Add(node.position, Vector3Scale(node.axisX, length)), color);
    DrawLine3D(Vector3Subtract(node.position, Vector3Scale(node.axisY, length)), Vector3Add(node.position, Vector3Scale(node.axisY, length)), color);
    DrawLine3D(Vector3Subtract(node.position, Vector3Scale(node.axisZ, length)), Vector3Add(node.position, Vector3Scale(node.axisZ, length)), color);
}

void DrawMeshOriginAxis(Vector3 origin, Vector3 axis, float length, Color color)
{
    DrawLine3D(origin, Vector3Add(origin, Vector3Scale(axis, length)), color);
}

float GetViewScaledAxisLength(const Camera3D& camera, Vector3 origin)
{
    const float screenHeight = std::max(1.0f, static_cast<float>(GetScreenHeight()));
    float worldPerPixel = 0.001f;
    if (camera.projection == CAMERA_ORTHOGRAPHIC)
    {
        worldPerPixel = std::max(0.000001f, camera.fovy / screenHeight);
    }
    else
    {
        const Vector3 forward = Vector3Normalize(Vector3Subtract(camera.target, camera.position));
        const float depth = std::max(0.0001f, Vector3DotProduct(Vector3Subtract(origin, camera.position), forward));
        worldPerPixel = std::max(0.000001f, (2.0f * depth * std::tan(camera.fovy * DEG2RAD * 0.5f)) / screenHeight);
    }

    return ClampFloat(worldPerPixel * 38.0f, 0.02f, 0.35f);
}

void DrawMeshOrigin(const SceneNode& node, const Camera3D& camera)
{
    const float axisLength = GetViewScaledAxisLength(camera, node.position);
    DrawMeshOriginAxis(node.position, node.axisX, axisLength, Color{ 235, 74, 74, 255 });
    DrawMeshOriginAxis(node.position, node.axisY, axisLength, Color{ 92, 210, 94, 255 });
    DrawMeshOriginAxis(node.position, node.axisZ, axisLength, Color{ 86, 142, 255, 255 });
}

void DrawBoneOrigin(const SceneNode& node, const BonePose* pose, const Camera3D& camera)
{
    const Vector3 position = pose ? pose->position : node.position;
    const Vector3 axisX = pose ? pose->axisX : node.axisX;
    const Vector3 axisY = pose ? pose->axisY : node.axisY;
    const Vector3 axisZ = pose ? pose->axisZ : node.axisZ;
    const float axisLength = GetViewScaledAxisLength(camera, position);
    DrawMeshOriginAxis(position, axisX, axisLength, Color{ 235, 74, 74, 255 });
    DrawMeshOriginAxis(position, axisY, axisLength, Color{ 92, 210, 94, 255 });
    DrawMeshOriginAxis(position, axisZ, axisLength, Color{ 86, 142, 255, 255 });
}

void DrawEmptyCrosses(const ModelTab& tab)
{
    const float length = ClampFloat(GetBoundsDiagonal(tab.loaded.bounds) * 0.035f, 0.06f, 0.6f);
    for (int i = 0; i < static_cast<int>(tab.loaded.nodes.size()); ++i)
    {
        const SceneNode& node = tab.loaded.nodes[static_cast<size_t>(i)];
        if (node.type != SceneNodeType::Empty || node.parent < 0) continue;
        if (!IsViewportNodeVisible(tab, i)) continue;
        DrawEmptyCross(node, length, i == tab.selectedNode ? kSelectionColor : Color{ 100, 185, 255, 230 });
    }
}

const float* GetCurrentMeshVertices(const ModelTab& tab)
{
    if (!tab.loaded.hasMesh) return nullptr;
    if (tab.currentVertices.size() == tab.loaded.bindVertices.size()) return tab.currentVertices.data();
    return tab.loaded.bindVertices.empty() ? nullptr : tab.loaded.bindVertices.data();
}

const float* GetCurrentMeshNormals(const ModelTab& tab)
{
    if (!tab.loaded.hasMesh) return nullptr;
    if (tab.currentNormals.size() == tab.loaded.bindNormals.size()) return tab.currentNormals.data();
    return tab.loaded.bindNormals.empty() ? nullptr : tab.loaded.bindNormals.data();
}

float GetBoneInfluenceWeight(const SkinnedVertex& vertex, const std::string& boneName)
{
    float weight = 0.0f;
    for (const SkinnedVertexInfluence& influence : vertex.influences)
    {
        if (influence.boneName == boneName)
        {
            weight += influence.weight;
        }
    }
    return weight;
}

Color LerpColor(Color a, Color b, float t)
{
    t = ClampFloat(t, 0.0f, 1.0f);
    return Color{
        static_cast<unsigned char>(static_cast<float>(a.r) + (static_cast<float>(b.r) - static_cast<float>(a.r)) * t),
        static_cast<unsigned char>(static_cast<float>(a.g) + (static_cast<float>(b.g) - static_cast<float>(a.g)) * t),
        static_cast<unsigned char>(static_cast<float>(a.b) + (static_cast<float>(b.b) - static_cast<float>(a.b)) * t),
        static_cast<unsigned char>(static_cast<float>(a.a) + (static_cast<float>(b.a) - static_cast<float>(a.a)) * t)
    };
}

Color GetSkinWeightHeatColor(float weight)
{
    weight = ClampFloat(weight, 0.0f, 1.0f);
    if (weight < 0.25f) return LerpColor(Color{ 18, 34, 92, 120 }, Color{ 28, 170, 215, 180 }, weight / 0.25f);
    if (weight < 0.50f) return LerpColor(Color{ 28, 170, 215, 180 }, Color{ 54, 205, 92, 205 }, (weight - 0.25f) / 0.25f);
    if (weight < 0.75f) return LerpColor(Color{ 54, 205, 92, 205 }, Color{ 245, 220, 76, 230 }, (weight - 0.50f) / 0.25f);
    return LerpColor(Color{ 245, 220, 76, 230 }, Color{ 238, 54, 46, 245 }, (weight - 0.75f) / 0.25f);
}

int QuantizeViewportCoord(float value)
{
    constexpr float scale = 100000.0f;
    return static_cast<int>(std::round(value * scale));
}

std::string MakeViewportUvPointKey(Vector2 value)
{
    return std::to_string(QuantizeViewportCoord(value.x)) + "," +
           std::to_string(QuantizeViewportCoord(value.y));
}

Vector3 GetBindVertexPosition(const LoadedFbxModel& loaded, int vertexIndex)
{
    const size_t base = static_cast<size_t>(vertexIndex) * 3;
    if (base + 2 >= loaded.bindVertices.size()) return Vector3{};
    return Vector3{ loaded.bindVertices[base], loaded.bindVertices[base + 1], loaded.bindVertices[base + 2] };
}

std::string MakeViewportPositionPointKey(const LoadedFbxModel& loaded, int vertexIndex)
{
    const Vector3 value = GetBindVertexPosition(loaded, vertexIndex);
    return std::to_string(QuantizeViewportCoord(value.x)) + "," +
           std::to_string(QuantizeViewportCoord(value.y)) + "," +
           std::to_string(QuantizeViewportCoord(value.z));
}

std::string MakeOrderedViewportEdgeKey(const std::string& a, const std::string& b)
{
    return a <= b ? a + "|" + b : b + "|" + a;
}

Vector2 GetViewportVertexUv(const std::vector<float>& uvs, int vertexIndex)
{
    const size_t base = static_cast<size_t>(vertexIndex) * 2;
    if (base + 1 >= uvs.size()) return Vector2{};
    return Vector2{ uvs[base], uvs[base + 1] };
}

std::string MakeViewportUvAdjacencyKey(const LoadedFbxModel& loaded,
                                       const std::vector<float>& uvs,
                                       const ViewportUvIslandTriangle& triangle,
                                       int edge)
{
    const int next = (edge + 1) % 3;
    const std::string positionEdge = MakeOrderedViewportEdgeKey(MakeViewportPositionPointKey(loaded, triangle.vertex[edge]),
                                                                MakeViewportPositionPointKey(loaded, triangle.vertex[next]));
    const std::string uvEdge = MakeOrderedViewportEdgeKey(MakeViewportUvPointKey(GetViewportVertexUv(uvs, triangle.vertex[edge])),
                                                          MakeViewportUvPointKey(GetViewportVertexUv(uvs, triangle.vertex[next])));
    return positionEdge + "#" + uvEdge;
}

int FindViewportIslandParent(std::vector<int>& parents, int value)
{
    if (parents[static_cast<size_t>(value)] == value) return value;
    parents[static_cast<size_t>(value)] = FindViewportIslandParent(parents, parents[static_cast<size_t>(value)]);
    return parents[static_cast<size_t>(value)];
}

void UnionViewportIslandParents(std::vector<int>& parents, int a, int b)
{
    const int rootA = FindViewportIslandParent(parents, a);
    const int rootB = FindViewportIslandParent(parents, b);
    if (rootA != rootB)
    {
        parents[static_cast<size_t>(rootB)] = rootA;
    }
}

void BuildViewportUvIslandCache(ModelTab& tab, int uvSetIndex)
{
    ViewportUvIslandCache cache;
    cache.uvSetIndex = uvSetIndex;
    cache.nodeCount = tab.loaded.nodes.size();
    cache.vertexValueCount = tab.loaded.bindVertices.size();
    if (uvSetIndex < 0 || uvSetIndex >= static_cast<int>(tab.loaded.uvSets.size()))
    {
        tab.viewportUvIslandCache = std::move(cache);
        return;
    }

    const std::vector<float>& uvs = tab.loaded.uvSets[static_cast<size_t>(uvSetIndex)];
    cache.uvValueCount = uvs.size();
    const int vertexFloatCount = static_cast<int>(tab.loaded.bindVertices.size());
    for (int nodeIndex = 0; nodeIndex < static_cast<int>(tab.loaded.nodes.size()); ++nodeIndex)
    {
        const SceneNode& node = tab.loaded.nodes[static_cast<size_t>(nodeIndex)];
        if (node.type != SceneNodeType::Mesh || node.meshVertexStart < 0 || node.meshVertexCount < 3) continue;
        if (uvs.size() < static_cast<size_t>(node.meshVertexStart + node.meshVertexCount) * 2) continue;

        const int start = node.meshVertexStart;
        const int end = node.meshVertexStart + node.meshVertexCount;
        for (int vertex = start; vertex + 2 < end; vertex += 3)
        {
            if (IsRemovedTriangle(node, vertex)) continue;
            if ((vertex + 2) * 3 + 2 >= vertexFloatCount) continue;

            ViewportUvIslandTriangle triangle;
            triangle.vertex[0] = vertex;
            triangle.vertex[1] = vertex + 1;
            triangle.vertex[2] = vertex + 2;
            triangle.nodeIndex = nodeIndex;
            cache.triangles.push_back(triangle);
        }
    }

    if (cache.triangles.empty())
    {
        tab.viewportUvIslandCache = std::move(cache);
        return;
    }

    std::vector<int> parents(cache.triangles.size());
    for (int i = 0; i < static_cast<int>(parents.size()); ++i)
    {
        parents[static_cast<size_t>(i)] = i;
    }

    std::unordered_map<std::string, int> edgeToTriangle;
    for (int triangleIndex = 0; triangleIndex < static_cast<int>(cache.triangles.size()); ++triangleIndex)
    {
        const ViewportUvIslandTriangle& triangle = cache.triangles[static_cast<size_t>(triangleIndex)];
        for (int edge = 0; edge < 3; ++edge)
        {
            const std::string edgeKey = MakeViewportUvAdjacencyKey(tab.loaded, uvs, triangle, edge);
            const auto found = edgeToTriangle.find(edgeKey);
            if (found == edgeToTriangle.end())
            {
                edgeToTriangle[edgeKey] = triangleIndex;
            }
            else
            {
                UnionViewportIslandParents(parents, triangleIndex, found->second);
            }
        }
    }

    std::unordered_map<int, int> rootToIsland;
    for (int triangleIndex = 0; triangleIndex < static_cast<int>(cache.triangles.size()); ++triangleIndex)
    {
        const int root = FindViewportIslandParent(parents, triangleIndex);
        auto found = rootToIsland.find(root);
        if (found == rootToIsland.end())
        {
            const int islandIndex = static_cast<int>(rootToIsland.size());
            found = rootToIsland.emplace(root, islandIndex).first;
        }
        cache.triangles[static_cast<size_t>(triangleIndex)].islandIndex = found->second;
    }

    tab.viewportUvIslandCache = std::move(cache);
}

ViewportUvIslandCache& GetViewportUvIslandCache(ModelTab& tab, int uvSetIndex)
{
    const size_t uvValueCount = uvSetIndex >= 0 && uvSetIndex < static_cast<int>(tab.loaded.uvSets.size()) ?
                                tab.loaded.uvSets[static_cast<size_t>(uvSetIndex)].size() :
                                0;
    if (tab.viewportUvIslandCache.uvSetIndex != uvSetIndex ||
        tab.viewportUvIslandCache.uvValueCount != uvValueCount ||
        tab.viewportUvIslandCache.vertexValueCount != tab.loaded.bindVertices.size() ||
        tab.viewportUvIslandCache.nodeCount != tab.loaded.nodes.size())
    {
        BuildViewportUvIslandCache(tab, uvSetIndex);
    }
    return tab.viewportUvIslandCache;
}

Vector3 OffsetViewportPoint(Vector3 point, Vector3 normal, float offset)
{
    return Vector3Add(point, Vector3Scale(normal, offset));
}

void DrawUvIslandColorOverlay(ModelTab& tab)
{
    if (!tab.loaded.hasMesh || tab.loaded.uvSets.empty()) return;

    const int uvSetIndex = ClampInt(tab.selectedUvSet, 0, static_cast<int>(tab.loaded.uvSets.size()) - 1);
    ViewportUvIslandCache& cache = GetViewportUvIslandCache(tab, uvSetIndex);
    if (cache.triangles.empty()) return;

    const float* vertices = GetCurrentMeshVertices(tab);
    const float* normals = GetCurrentMeshNormals(tab);
    if (!vertices) return;

    const int vertexFloatCount = static_cast<int>(tab.loaded.bindVertices.size());
    const int normalFloatCount = static_cast<int>(tab.loaded.bindNormals.size());
    const float offset = ClampFloat(GetBoundsDiagonal(tab.loaded.bounds) * 0.00035f, 0.0001f, 0.006f);

    rlBegin(RL_TRIANGLES);
    for (const ViewportUvIslandTriangle& triangle : cache.triangles)
    {
        if (!IsViewportNodeVisible(tab, triangle.nodeIndex)) continue;

        Vector3 points[3]{};
        Vector3 normalValues[3]{};
        bool validTriangle = true;
        bool hasCornerNormals = normals != nullptr;
        for (int corner = 0; corner < 3; ++corner)
        {
            const int positionBase = triangle.vertex[corner] * 3;
            if (positionBase + 2 >= vertexFloatCount)
            {
                validTriangle = false;
                break;
            }

            points[corner] = Vector3{ vertices[positionBase], vertices[positionBase + 1], vertices[positionBase + 2] };
            if (normals && positionBase + 2 < normalFloatCount)
            {
                normalValues[corner] = NormalizeOrFallback(Vector3{ normals[positionBase], normals[positionBase + 1], normals[positionBase + 2] },
                                                           Vector3{ 0.0f, 1.0f, 0.0f });
            }
            else
            {
                hasCornerNormals = false;
            }
        }
        if (!validTriangle) continue;

        if (!hasCornerNormals)
        {
            const Vector3 fallbackNormal = NormalizeOrFallback(Vector3CrossProduct(Vector3Subtract(points[1], points[0]),
                                                                                   Vector3Subtract(points[2], points[0])),
                                                               Vector3{ 0.0f, 1.0f, 0.0f });
            normalValues[0] = fallbackNormal;
            normalValues[1] = fallbackNormal;
            normalValues[2] = fallbackNormal;
        }

        const Color color = GetDebugIndexColor(triangle.islandIndex);
        rlColor4ub(color.r, color.g, color.b, color.a);
        for (int corner = 0; corner < 3; ++corner)
        {
            const Vector3 point = OffsetViewportPoint(points[corner], normalValues[corner], offset);
            rlVertex3f(point.x, point.y, point.z);
        }
    }
    rlEnd();
}

bool GetSelectedBoneName(const ModelTab& tab, std::string& boneName)
{
    if (tab.selectedNode < 0 || tab.selectedNode >= static_cast<int>(tab.loaded.nodes.size())) return false;
    const SceneNode& node = tab.loaded.nodes[static_cast<size_t>(tab.selectedNode)];
    if (node.type != SceneNodeType::Bone || IsDeletedNode(tab, tab.selectedNode)) return false;
    boneName = node.name;
    return !boneName.empty();
}

void DrawSkinWeightHeatMap(const ModelTab& tab)
{
    std::string boneName;
    if (!GetSelectedBoneName(tab, boneName)) return;
    if (tab.loaded.skinnedVertices.empty()) return;

    const float* vertices = GetCurrentMeshVertices(tab);
    const float* normals = GetCurrentMeshNormals(tab);
    if (!vertices) return;

    const float offset = ClampFloat(GetBoundsDiagonal(tab.loaded.bounds) * 0.0008f, 0.0002f, 0.01f);
    rlDisableBackfaceCulling();
    rlDisableDepthMask();
    BeginBlendMode(BLEND_ALPHA);
    rlBegin(RL_TRIANGLES);
    for (int nodeIndex = 0; nodeIndex < static_cast<int>(tab.loaded.nodes.size()); ++nodeIndex)
    {
        const SceneNode& node = tab.loaded.nodes[static_cast<size_t>(nodeIndex)];
        if (node.type != SceneNodeType::Mesh || node.meshVertexStart < 0 || node.meshVertexCount < 3) continue;
        if (!IsViewportNodeVisible(tab, nodeIndex)) continue;

        const int start = std::max(0, node.meshVertexStart);
        const int end = std::min(node.meshVertexStart + node.meshVertexCount, static_cast<int>(tab.loaded.skinnedVertices.size()));
        for (int vertex = start; vertex + 2 < end; vertex += 3)
        {
            if (IsRemovedTriangle(node, vertex)) continue;
            const int vertexIndices[3] = { vertex, vertex + 1, vertex + 2 };
            Vector3 points[3]{};
            Vector3 fallbackNormal{};
            for (int corner = 0; corner < 3; ++corner)
            {
                const int base = vertexIndices[corner] * 3;
                points[corner] = Vector3{ vertices[base], vertices[base + 1], vertices[base + 2] };
            }
            fallbackNormal = NormalizeOrFallback(Vector3CrossProduct(Vector3Subtract(points[1], points[0]), Vector3Subtract(points[2], points[0])), Vector3{ 0.0f, 1.0f, 0.0f });

            for (int corner = 0; corner < 3; ++corner)
            {
                const int globalVertex = vertexIndices[corner];
                const int base = globalVertex * 3;
                Vector3 normal = fallbackNormal;
                if (normals && base + 2 < static_cast<int>(tab.loaded.bindNormals.size()))
                {
                    normal = NormalizeOrFallback(Vector3{ normals[base], normals[base + 1], normals[base + 2] }, fallbackNormal);
                }
                const float weight = GetBoneInfluenceWeight(tab.loaded.skinnedVertices[static_cast<size_t>(globalVertex)], boneName);
                const Color color = GetSkinWeightHeatColor(weight);
                const Vector3 point = Vector3Add(points[corner], Vector3Scale(normal, offset));
                rlColor4ub(color.r, color.g, color.b, color.a);
                rlVertex3f(point.x, point.y, point.z);
            }
        }
    }
    rlEnd();
    EndBlendMode();
    rlEnableDepthMask();
}

Vector3 GetTransformAxisVector(TransformAxis axis)
{
    switch (axis)
    {
    case TransformAxis::X: return Vector3{ 1.0f, 0.0f, 0.0f };
    case TransformAxis::Y: return Vector3{ 0.0f, 1.0f, 0.0f };
    case TransformAxis::Z: return Vector3{ 0.0f, 0.0f, 1.0f };
    case TransformAxis::Center:
    case TransformAxis::None: break;
    }
    return Vector3Zero();
}

Vector3 GetCameraForward(const Camera3D& camera)
{
    return NormalizeOrFallback(Vector3Subtract(camera.target, camera.position), Vector3{ 0.0f, 0.0f, -1.0f });
}

Vector3 GetCameraRight(const Camera3D& camera)
{
    const Vector3 forward = GetCameraForward(camera);
    return NormalizeOrFallback(Vector3CrossProduct(forward, camera.up), Vector3{ 1.0f, 0.0f, 0.0f });
}

Vector3 GetCameraUpVector(const Camera3D& camera)
{
    const Vector3 forward = GetCameraForward(camera);
    const Vector3 right = GetCameraRight(camera);
    return NormalizeOrFallback(Vector3CrossProduct(right, forward), Vector3{ 0.0f, 1.0f, 0.0f });
}

