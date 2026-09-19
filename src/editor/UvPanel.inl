struct UvDensityStats
{
    float surfaceArea = 0.0f;
    float uvArea = 0.0f;
    float density = 0.0f;
    float minU = 0.0f;
    float maxU = 0.0f;
    float minV = 0.0f;
    float maxV = 0.0f;
    int textureWidth = 0;
    int textureHeight = 0;
    int triangles = 0;
};

struct UvTriangleSample
{
    Vector2 uv[3]{};
    Vector3 position[3]{};
    int nodeIndex = -1;
    int vertexStart = -1;
};

struct UvIslandStats
{
    std::vector<int> triangles;
    float surfaceArea = 0.0f;
    float uvArea = 0.0f;
    float density = 0.0f;
    float minU = 0.0f;
    float maxU = 0.0f;
    float minV = 0.0f;
    float maxV = 0.0f;
    int nodeIndex = -1;
};

float TriangleArea3D(Vector3 a, Vector3 b, Vector3 c)
{
    return Vector3Length(Vector3CrossProduct(Vector3Subtract(b, a), Vector3Subtract(c, a))) * 0.5f;
}

float TriangleAreaUv(Vector2 a, Vector2 b, Vector2 c)
{
    return std::fabs((b.x - a.x) * (c.y - a.y) - (c.x - a.x) * (b.y - a.y)) * 0.5f;
}

Vector3 GetModelVertexPosition(const LoadedFbxModel& loaded, int vertexIndex)
{
    const size_t base = static_cast<size_t>(vertexIndex) * 3;
    if (base + 2 >= loaded.bindVertices.size()) return Vector3{};
    return Vector3{ loaded.bindVertices[base], loaded.bindVertices[base + 1], loaded.bindVertices[base + 2] };
}

Vector2 GetModelVertexUv(const std::vector<float>& uvs, int vertexIndex)
{
    const size_t base = static_cast<size_t>(vertexIndex) * 2;
    if (base + 1 >= uvs.size()) return Vector2{};
    return Vector2{ uvs[base], uvs[base + 1] };
}

std::string GetPrimaryMaterialName(const SceneNode& node)
{
    const std::string multiMaterialSuffix = " (+";
    const size_t suffix = node.materialName.find(multiMaterialSuffix);
    if (suffix != std::string::npos)
    {
        return node.materialName.substr(0, suffix);
    }
    return node.materialName;
}

int GetMaterialIndexForNode(const ModelTab& tab, const SceneNode& node)
{
    const std::string primaryMaterial = GetPrimaryMaterialName(node);
    for (int i = 0; i < static_cast<int>(tab.loaded.materialNames.size()); ++i)
    {
        if (tab.loaded.materialNames[static_cast<size_t>(i)] == primaryMaterial)
        {
            return i;
        }
    }

    return ClampInt(tab.selectedMaterial, 0, std::max(0, static_cast<int>(tab.pbrMaterials.size()) - 1));
}

bool NodeUsesSamePrimaryMaterial(const SceneNode& node, const std::string& materialName)
{
    return node.type == SceneNodeType::Mesh && GetPrimaryMaterialName(node) == materialName;
}

void GetUvDensityTextureSize(const ModelTab& tab, const SceneNode& node, int& textureWidth, int& textureHeight, bool& usingTexture)
{
    textureWidth = std::max(1, tab.uvDensityTileSize);
    textureHeight = textureWidth;
    usingTexture = false;

    const int materialIndex = GetMaterialIndexForNode(tab, node);
    if (materialIndex < 0 || materialIndex >= static_cast<int>(tab.pbrMaterials.size())) return;

    const PbrTexture& diffuse = GetPbrTexture(tab.pbrMaterials[static_cast<size_t>(materialIndex)], PbrTextureSlot::Diffuse);
    if (!diffuse.loaded || diffuse.texture.width <= 0 || diffuse.texture.height <= 0) return;

    textureWidth = diffuse.texture.width;
    textureHeight = diffuse.texture.height;
    usingTexture = true;
}

UvDensityStats CalculateUvDensityStats(const LoadedFbxModel& loaded,
                                       const SceneNode& node,
                                       const std::vector<float>& uvs,
                                       int textureWidth,
                                       int textureHeight)
{
    UvDensityStats stats;
    stats.textureWidth = textureWidth;
    stats.textureHeight = textureHeight;
    if (node.meshVertexStart < 0 || node.meshVertexCount <= 0) return stats;

    bool hasUv = false;
    const int start = node.meshVertexStart;
    const int end = node.meshVertexStart + node.meshVertexCount;
    for (int vertex = start; vertex < end; ++vertex)
    {
        const Vector2 uv = GetModelVertexUv(uvs, vertex);
        if (!hasUv)
        {
            stats.minU = uv.x;
            stats.maxU = uv.x;
            stats.minV = uv.y;
            stats.maxV = uv.y;
            hasUv = true;
        }
        else
        {
            stats.minU = std::min(stats.minU, uv.x);
            stats.maxU = std::max(stats.maxU, uv.x);
            stats.minV = std::min(stats.minV, uv.y);
            stats.maxV = std::max(stats.maxV, uv.y);
        }
    }

    if (!hasUv) return stats;

    for (int vertex = start; vertex + 2 < end; vertex += 3)
    {
        if (IsRemovedTriangle(node, vertex)) continue;
        const Vector3 p0 = GetModelVertexPosition(loaded, vertex);
        const Vector3 p1 = GetModelVertexPosition(loaded, vertex + 1);
        const Vector3 p2 = GetModelVertexPosition(loaded, vertex + 2);
        const Vector2 uv0 = GetModelVertexUv(uvs, vertex);
        const Vector2 uv1 = GetModelVertexUv(uvs, vertex + 1);
        const Vector2 uv2 = GetModelVertexUv(uvs, vertex + 2);
        stats.surfaceArea += TriangleArea3D(p0, p1, p2);
        stats.uvArea += TriangleAreaUv(uv0, uv1, uv2);
        ++stats.triangles;
    }

    if (stats.surfaceArea > 0.0000001f && stats.uvArea > 0.0000001f && textureWidth > 0 && textureHeight > 0)
    {
        stats.density = std::sqrt((stats.uvArea * static_cast<float>(textureWidth) * static_cast<float>(textureHeight)) / stats.surfaceArea);
    }
    return stats;
}

int QuantizeUvCoord(float value)
{
    constexpr float scale = 100000.0f;
    return static_cast<int>(std::round(value * scale));
}

std::string MakeUvEdgeKey(Vector2 a, Vector2 b)
{
    const int au = QuantizeUvCoord(a.x);
    const int av = QuantizeUvCoord(a.y);
    const int bu = QuantizeUvCoord(b.x);
    const int bv = QuantizeUvCoord(b.y);
    if (au < bu || (au == bu && av <= bv))
    {
        return std::to_string(au) + "," + std::to_string(av) + "|" + std::to_string(bu) + "," + std::to_string(bv);
    }
    return std::to_string(bu) + "," + std::to_string(bv) + "|" + std::to_string(au) + "," + std::to_string(av);
}

std::string MakeUvTopologyEdgeKey(const UvTriangleSample& triangle, int a, int b)
{
    auto endpoint = [&](int i)
    {
        const Vector3 p = triangle.position[i];
        return std::to_string(p.x) + "," + std::to_string(p.y) + "," + std::to_string(p.z) + ":" +
               std::to_string(QuantizeUvCoord(triangle.uv[i].x)) + "," + std::to_string(QuantizeUvCoord(triangle.uv[i].y));
    };
    std::string first = endpoint(a), second = endpoint(b);
    if (second < first) std::swap(first, second);
    return std::to_string(triangle.nodeIndex) + ":" + first + "|" + second;
}

int FindIslandParent(std::vector<int>& parents, int value)
{
    if (parents[static_cast<size_t>(value)] == value) return value;
    parents[static_cast<size_t>(value)] = FindIslandParent(parents, parents[static_cast<size_t>(value)]);
    return parents[static_cast<size_t>(value)];
}

void UnionIslandParents(std::vector<int>& parents, int a, int b)
{
    const int rootA = FindIslandParent(parents, a);
    const int rootB = FindIslandParent(parents, b);
    if (rootA != rootB)
    {
        parents[static_cast<size_t>(rootB)] = rootA;
    }
}

void AppendNodeUvTriangles(const LoadedFbxModel& loaded,
                           const std::vector<float>& uvs,
                           int nodeIndex,
                           std::vector<UvTriangleSample>& triangles)
{
    if (nodeIndex < 0 || nodeIndex >= static_cast<int>(loaded.nodes.size())) return;

    const SceneNode& node = loaded.nodes[static_cast<size_t>(nodeIndex)];
    if (node.type != SceneNodeType::Mesh || node.meshVertexStart < 0 || node.meshVertexCount <= 0) return;
    if (uvs.size() < static_cast<size_t>(node.meshVertexStart + node.meshVertexCount) * 2) return;

    const int start = node.meshVertexStart;
    const int end = node.meshVertexStart + node.meshVertexCount;
    for (int vertex = start; vertex + 2 < end; vertex += 3)
    {
        if (IsRemovedTriangle(node, vertex)) continue;
        UvTriangleSample triangle;
        triangle.nodeIndex = nodeIndex;
        triangle.vertexStart = vertex;
        for (int i = 0; i < 3; ++i)
        {
            triangle.uv[i] = GetModelVertexUv(uvs, vertex + i);
            triangle.position[i] = GetModelVertexPosition(loaded, vertex + i);
        }
        triangles.push_back(triangle);
    }
}

std::vector<int> GetUvScopeNodeIndices(const ModelTab& tab, const SceneNode& selectedNode, int selectedNodeIndex)
{
    std::vector<int> nodeIndices;
    if (tab.showUvSameMaterialMeshes)
    {
        const std::string materialName = GetPrimaryMaterialName(selectedNode);
        for (int i = 0; i < static_cast<int>(tab.loaded.nodes.size()); ++i)
        {
            const SceneNode& node = tab.loaded.nodes[static_cast<size_t>(i)];
            if (NodeUsesSamePrimaryMaterial(node, materialName))
            {
                nodeIndices.push_back(i);
            }
        }
    }

    if (nodeIndices.empty() && selectedNodeIndex >= 0)
    {
        nodeIndices.push_back(selectedNodeIndex);
    }
    return nodeIndices;
}

std::vector<UvTriangleSample> BuildUvScopeTriangles(const ModelTab& tab,
                                                    const std::vector<float>& uvs,
                                                    const std::vector<int>& nodeIndices)
{
    std::vector<UvTriangleSample> triangles;
    for (int nodeIndex : nodeIndices)
    {
        AppendNodeUvTriangles(tab.loaded, uvs, nodeIndex, triangles);
    }
    return triangles;
}

std::vector<UvIslandStats> CalculateUvIslandStats(const std::vector<UvTriangleSample>& triangles,
                                                  int textureWidth,
                                                  int textureHeight)
{
    std::vector<UvIslandStats> islands;
    if (triangles.empty()) return islands;

    std::vector<int> parents(triangles.size());
    for (int i = 0; i < static_cast<int>(parents.size()); ++i)
    {
        parents[static_cast<size_t>(i)] = i;
    }

    std::unordered_map<std::string, int> edgeToTriangle;
    for (int triangleIndex = 0; triangleIndex < static_cast<int>(triangles.size()); ++triangleIndex)
    {
        const UvTriangleSample& triangle = triangles[static_cast<size_t>(triangleIndex)];
        const std::string edgeKeys[] = {
            MakeUvTopologyEdgeKey(triangle, 0, 1),
            MakeUvTopologyEdgeKey(triangle, 1, 2),
            MakeUvTopologyEdgeKey(triangle, 2, 0)
        };

        for (const std::string& edgeKey : edgeKeys)
        {
            const auto found = edgeToTriangle.find(edgeKey);
            if (found == edgeToTriangle.end())
            {
                edgeToTriangle[edgeKey] = triangleIndex;
            }
            else
            {
                UnionIslandParents(parents, triangleIndex, found->second);
            }
        }
    }

    std::unordered_map<int, int> rootToIsland;
    for (int triangleIndex = 0; triangleIndex < static_cast<int>(triangles.size()); ++triangleIndex)
    {
        const int root = FindIslandParent(parents, triangleIndex);
        auto found = rootToIsland.find(root);
        if (found == rootToIsland.end())
        {
            const int islandIndex = static_cast<int>(islands.size());
            rootToIsland[root] = islandIndex;
            islands.push_back(UvIslandStats{});
            found = rootToIsland.find(root);
        }

        UvIslandStats& island = islands[static_cast<size_t>(found->second)];
        const UvTriangleSample& triangle = triangles[static_cast<size_t>(triangleIndex)];
        island.triangles.push_back(triangleIndex);
        if (island.triangles.size() == 1)
        {
            island.minU = island.maxU = triangle.uv[0].x;
            island.minV = island.maxV = triangle.uv[0].y;
            island.nodeIndex = triangle.nodeIndex;
        }
        for (int i = 0; i < 3; ++i)
        {
            island.minU = std::min(island.minU, triangle.uv[i].x);
            island.maxU = std::max(island.maxU, triangle.uv[i].x);
            island.minV = std::min(island.minV, triangle.uv[i].y);
            island.maxV = std::max(island.maxV, triangle.uv[i].y);
        }
        island.surfaceArea += TriangleArea3D(triangle.position[0], triangle.position[1], triangle.position[2]);
        island.uvArea += TriangleAreaUv(triangle.uv[0], triangle.uv[1], triangle.uv[2]);
        if (island.nodeIndex != triangle.nodeIndex)
        {
            island.nodeIndex = -1;
        }
    }

    for (UvIslandStats& island : islands)
    {
        if (island.surfaceArea > 0.0000001f && island.uvArea > 0.0000001f && textureWidth > 0 && textureHeight > 0)
        {
            island.density = std::sqrt((island.uvArea * static_cast<float>(textureWidth) * static_cast<float>(textureHeight)) / island.surfaceArea);
        }
    }

    std::stable_sort(islands.begin(), islands.end(), [](const UvIslandStats& a, const UvIslandStats& b)
    {
        return a.uvArea > b.uvArea;
    });
    return islands;
}

float CrossVector2(Vector2 a, Vector2 b)
{
    return a.x * b.y - a.y * b.x;
}

float PolygonAreaUv(const std::vector<Vector2>& polygon)
{
    if (polygon.size() < 3) return 0.0f;

    float area = 0.0f;
    for (int i = 0; i < static_cast<int>(polygon.size()); ++i)
    {
        const Vector2 a = polygon[static_cast<size_t>(i)];
        const Vector2 b = polygon[static_cast<size_t>((i + 1) % static_cast<int>(polygon.size()))];
        area += a.x * b.y - b.x * a.y;
    }
    return std::fabs(area) * 0.5f;
}

bool IsInsideUvClipEdge(Vector2 point, Vector2 edgeStart, Vector2 edgeEnd, float clipSign)
{
    constexpr float epsilon = 0.0000001f;
    const float cross = CrossVector2(Vector2Subtract(edgeEnd, edgeStart), Vector2Subtract(point, edgeStart));
    return clipSign >= 0.0f ? cross >= -epsilon : cross <= epsilon;
}

Vector2 IntersectUvLines(Vector2 a0, Vector2 a1, Vector2 b0, Vector2 b1)
{
    const Vector2 aDirection = Vector2Subtract(a1, a0);
    const Vector2 bDirection = Vector2Subtract(b1, b0);
    const float denominator = CrossVector2(aDirection, bDirection);
    if (std::fabs(denominator) <= 0.0000001f)
    {
        return a1;
    }

    const float t = CrossVector2(Vector2Subtract(b0, a0), bDirection) / denominator;
    return Vector2Add(a0, Vector2Scale(aDirection, t));
}

std::vector<Vector2> ClipUvPolygonAgainstEdge(const std::vector<Vector2>& subject,
                                              Vector2 edgeStart,
                                              Vector2 edgeEnd,
                                              float clipSign)
{
    std::vector<Vector2> output;
    if (subject.empty()) return output;

    Vector2 previous = subject.back();
    bool previousInside = IsInsideUvClipEdge(previous, edgeStart, edgeEnd, clipSign);
    for (Vector2 current : subject)
    {
        const bool currentInside = IsInsideUvClipEdge(current, edgeStart, edgeEnd, clipSign);
        if (currentInside != previousInside)
        {
            output.push_back(IntersectUvLines(previous, current, edgeStart, edgeEnd));
        }
        if (currentInside)
        {
            output.push_back(current);
        }
        previous = current;
        previousInside = currentInside;
    }
    return output;
}

float TriangleUvOverlapArea(const UvTriangleSample& a, const UvTriangleSample& b)
{
    const float clipArea = CrossVector2(Vector2Subtract(b.uv[1], b.uv[0]), Vector2Subtract(b.uv[2], b.uv[0]));
    if (std::fabs(clipArea) <= 0.0000001f) return 0.0f;

    std::vector<Vector2> clipped{ a.uv[0], a.uv[1], a.uv[2] };
    const float clipSign = clipArea >= 0.0f ? 1.0f : -1.0f;
    for (int i = 0; i < 3 && !clipped.empty(); ++i)
    {
        clipped = ClipUvPolygonAgainstEdge(clipped, b.uv[i], b.uv[(i + 1) % 3], clipSign);
    }
    return PolygonAreaUv(clipped);
}

bool UvIslandBoundsOverlap(const UvIslandStats& a, const UvIslandStats& b)
{
    constexpr float epsilon = 0.0000001f;
    return std::min(a.maxU, b.maxU) - std::max(a.minU, b.minU) > epsilon &&
           std::min(a.maxV, b.maxV) - std::max(a.minV, b.minV) > epsilon;
}

struct UvOverlapTree
{
    struct Node
    {
        UvIslandStats bounds;
        int begin = 0;
        int end = 0;
        int left = -1;
        int right = -1;
    };
    std::vector<int> triangles;
    std::vector<Node> nodes;
};

UvIslandStats GetUvTriangleBounds(const UvTriangleSample& triangle)
{
    UvIslandStats bounds;
    bounds.minU = bounds.maxU = triangle.uv[0].x;
    bounds.minV = bounds.maxV = triangle.uv[0].y;
    for (int i = 1; i < 3; ++i)
    {
        bounds.minU = std::min(bounds.minU, triangle.uv[i].x);
        bounds.maxU = std::max(bounds.maxU, triangle.uv[i].x);
        bounds.minV = std::min(bounds.minV, triangle.uv[i].y);
        bounds.maxV = std::max(bounds.maxV, triangle.uv[i].y);
    }
    return bounds;
}

int BuildUvOverlapTreeNode(UvOverlapTree& tree, const std::vector<UvTriangleSample>& triangles, int begin, int end)
{
    UvOverlapTree::Node node;
    node.begin = begin;
    node.end = end;
    node.bounds = GetUvTriangleBounds(triangles[static_cast<size_t>(tree.triangles[static_cast<size_t>(begin)])]);
    for (int i = begin + 1; i < end; ++i)
    {
        const auto bounds = GetUvTriangleBounds(triangles[static_cast<size_t>(tree.triangles[static_cast<size_t>(i)])]);
        node.bounds.minU = std::min(node.bounds.minU, bounds.minU);
        node.bounds.maxU = std::max(node.bounds.maxU, bounds.maxU);
        node.bounds.minV = std::min(node.bounds.minV, bounds.minV);
        node.bounds.maxV = std::max(node.bounds.maxV, bounds.maxV);
    }
    const int index = static_cast<int>(tree.nodes.size());
    tree.nodes.push_back(node);
    if (end - begin > 8)
    {
        const bool splitU = node.bounds.maxU - node.bounds.minU >= node.bounds.maxV - node.bounds.minV;
        const int middle = begin + (end - begin) / 2;
        std::nth_element(tree.triangles.begin() + begin, tree.triangles.begin() + middle, tree.triangles.begin() + end,
                         [&](int a, int b)
        {
            const auto& ta = triangles[static_cast<size_t>(a)];
            const auto& tb = triangles[static_cast<size_t>(b)];
            return splitU ? ta.uv[0].x + ta.uv[1].x + ta.uv[2].x < tb.uv[0].x + tb.uv[1].x + tb.uv[2].x :
                            ta.uv[0].y + ta.uv[1].y + ta.uv[2].y < tb.uv[0].y + tb.uv[1].y + tb.uv[2].y;
        });
        const int left = BuildUvOverlapTreeNode(tree, triangles, begin, middle);
        const int right = BuildUvOverlapTreeNode(tree, triangles, middle, end);
        tree.nodes[static_cast<size_t>(index)].left = left;
        tree.nodes[static_cast<size_t>(index)].right = right;
    }
    return index;
}

UvOverlapTree BuildUvOverlapTree(const UvIslandStats& island, const std::vector<UvTriangleSample>& triangles)
{
    UvOverlapTree tree;
    for (int index : island.triangles)
        if (index >= 0 && index < static_cast<int>(triangles.size())) tree.triangles.push_back(index);
    if (!tree.triangles.empty()) BuildUvOverlapTreeNode(tree, triangles, 0, static_cast<int>(tree.triangles.size()));
    return tree;
}

bool UvOverlapTreeNodesIntersect(const UvOverlapTree& a, int ai, const UvOverlapTree& b, int bi,
                                 const std::vector<UvTriangleSample>& triangles)
{
    const auto& an = a.nodes[static_cast<size_t>(ai)];
    const auto& bn = b.nodes[static_cast<size_t>(bi)];
    // Use inclusive bounds here: the exact area test below retains the original tolerance.
    if (an.bounds.maxU < bn.bounds.minU || bn.bounds.maxU < an.bounds.minU ||
        an.bounds.maxV < bn.bounds.minV || bn.bounds.maxV < an.bounds.minV) return false;
    if (an.left >= 0 && (bn.left < 0 || an.end - an.begin >= bn.end - bn.begin))
        return UvOverlapTreeNodesIntersect(a, an.left, b, bi, triangles) || UvOverlapTreeNodesIntersect(a, an.right, b, bi, triangles);
    if (bn.left >= 0)
        return UvOverlapTreeNodesIntersect(a, ai, b, bn.left, triangles) || UvOverlapTreeNodesIntersect(a, ai, b, bn.right, triangles);
    for (int i = an.begin; i < an.end; ++i)
        for (int j = bn.begin; j < bn.end; ++j)
            if (TriangleUvOverlapArea(triangles[static_cast<size_t>(a.triangles[static_cast<size_t>(i)])],
                                      triangles[static_cast<size_t>(b.triangles[static_cast<size_t>(j)])]) > 0.0000001f) return true;
    return false;
}

bool UvIslandsOverlap(const UvIslandStats& a, const UvIslandStats& b, const std::vector<UvTriangleSample>& triangles)
{
    if (!UvIslandBoundsOverlap(a, b)) return false;
    const auto ta = BuildUvOverlapTree(a, triangles);
    const auto tb = BuildUvOverlapTree(b, triangles);
    return !ta.nodes.empty() && !tb.nodes.empty() && UvOverlapTreeNodesIntersect(ta, 0, tb, 0, triangles);
}

bool IsPointInUvTriangle(Vector2 point, Vector2 a, Vector2 b, Vector2 c)
{
    constexpr float epsilon = 0.000001f;
    const Vector2 ab = Vector2Subtract(b, a);
    const Vector2 bc = Vector2Subtract(c, b);
    const Vector2 ca = Vector2Subtract(a, c);
    const float c0 = CrossVector2(ab, Vector2Subtract(point, a));
    const float c1 = CrossVector2(bc, Vector2Subtract(point, b));
    const float c2 = CrossVector2(ca, Vector2Subtract(point, c));
    const bool hasNegative = c0 < -epsilon || c1 < -epsilon || c2 < -epsilon;
    const bool hasPositive = c0 > epsilon || c1 > epsilon || c2 > epsilon;
    return !(hasNegative && hasPositive);
}

int FindUvIslandAtPoint(const std::vector<UvIslandStats>& islands,
                        const std::vector<UvTriangleSample>& triangles,
                        Vector2 uvPoint)
{
    for (int islandIndex = static_cast<int>(islands.size()) - 1; islandIndex >= 0; --islandIndex)
    {
        const UvIslandStats& island = islands[static_cast<size_t>(islandIndex)];
        if (uvPoint.x < island.minU || uvPoint.x > island.maxU || uvPoint.y < island.minV || uvPoint.y > island.maxV)
        {
            continue;
        }

        for (int triangleIndex : island.triangles)
        {
            if (triangleIndex < 0 || triangleIndex >= static_cast<int>(triangles.size())) continue;

            const UvTriangleSample& triangle = triangles[static_cast<size_t>(triangleIndex)];
            if (IsPointInUvTriangle(uvPoint, triangle.uv[0], triangle.uv[1], triangle.uv[2]))
            {
                return islandIndex;
            }
        }
    }
    return -1;
}

bool IsUvIslandSelected(const std::vector<int>& selectedIslands, int islandIndex)
{
    return std::find(selectedIslands.begin(), selectedIslands.end(), islandIndex) != selectedIslands.end();
}

void AddSelectedUvIsland(std::vector<int>& selectedIslands, int islandIndex)
{
    if (islandIndex < 0 || IsUvIslandSelected(selectedIslands, islandIndex)) return;
    selectedIslands.push_back(islandIndex);
}

void ToggleSelectedUvIsland(std::vector<int>& selectedIslands, int islandIndex)
{
    if (islandIndex < 0) return;

    auto found = std::find(selectedIslands.begin(), selectedIslands.end(), islandIndex);
    if (found == selectedIslands.end())
    {
        selectedIslands.push_back(islandIndex);
    }
    else
    {
        selectedIslands.erase(found);
    }
}

void SetSingleSelectedUvIsland(std::vector<int>& selectedIslands, int islandIndex)
{
    selectedIslands.clear();
    if (islandIndex >= 0)
    {
        selectedIslands.push_back(islandIndex);
    }
}

void PruneSelectedUvIslands(std::vector<int>& selectedIslands, int islandCount)
{
    selectedIslands.erase(std::remove_if(selectedIslands.begin(),
                                         selectedIslands.end(),
                                         [islandCount](int islandIndex)
                                         {
                                             return islandIndex < 0 || islandIndex >= islandCount;
                                         }),
                          selectedIslands.end());
}

Rectangle RectangleFromPoints(Vector2 a, Vector2 b)
{
    const float minX = std::min(a.x, b.x);
    const float minY = std::min(a.y, b.y);
    return Rectangle{ minX, minY, std::fabs(a.x - b.x), std::fabs(a.y - b.y) };
}

struct UvSelectionSummary
{
    int count = 0;
    float uvArea = 0.0f;
    float density = 0.0f;
    bool mixedDensity = false;
};

UvSelectionSummary CalculateUvSelectionSummary(const std::vector<UvIslandStats>& islands,
                                               const std::vector<int>& selectedIslands)
{
    UvSelectionSummary summary;
    bool hasFirstDensity = false;
    float firstRoundedDensity = 0.0f;
    for (int islandIndex : selectedIslands)
    {
        if (islandIndex < 0 || islandIndex >= static_cast<int>(islands.size())) continue;

        const UvIslandStats& island = islands[static_cast<size_t>(islandIndex)];
        ++summary.count;
        summary.uvArea += island.uvArea;

        const float roundedDensity = std::round(island.density * 10.0f) / 10.0f;
        if (!hasFirstDensity)
        {
            summary.density = island.density;
            firstRoundedDensity = roundedDensity;
            hasFirstDensity = true;
        }
        else if (std::fabs(roundedDensity - firstRoundedDensity) > 0.0001f)
        {
            summary.mixedDensity = true;
        }
    }
    return summary;
}

int NextUvDensityTileSize(int current)
{
    if (current < 1024) return 1024;
    if (current < 2048) return 2048;
    if (current < 4096) return 4096;
    if (current < 8192) return 8192;
    return 512;
}

int GetFirstUvIslandNodeIndex(const UvIslandStats& island, const std::vector<UvTriangleSample>& triangles)
{
    if (island.nodeIndex >= 0) return island.nodeIndex;

    for (int triangleIndex : island.triangles)
    {
        if (triangleIndex < 0 || triangleIndex >= static_cast<int>(triangles.size())) continue;

        const int nodeIndex = triangles[static_cast<size_t>(triangleIndex)].nodeIndex;
        if (nodeIndex >= 0) return nodeIndex;
    }
    return -1;
}

void ValidateUvIslandOverlaps(const ModelTab& tab, std::vector<ValidatorIssue>& issues)
{
    if (!tab.loaded.hasMesh || tab.loaded.uvSets.empty()) return;

    for (int uvSetIndex = 0; uvSetIndex < static_cast<int>(tab.loaded.uvSets.size()); ++uvSetIndex)
    {
        const std::vector<float>& uvs = tab.loaded.uvSets[static_cast<size_t>(uvSetIndex)];
        if (uvs.empty()) continue;

        std::unordered_map<std::string, std::vector<int>> nodesByMaterial;
        for (int nodeIndex = 0; nodeIndex < static_cast<int>(tab.loaded.nodes.size()); ++nodeIndex)
        {
            const SceneNode& node = tab.loaded.nodes[static_cast<size_t>(nodeIndex)];
            if (node.type != SceneNodeType::Mesh || node.meshVertexStart < 0 || node.meshVertexCount <= 0) continue;
            if (uvs.size() < static_cast<size_t>(node.meshVertexStart + node.meshVertexCount) * 2) continue;

            std::string materialName = GetPrimaryMaterialName(node);
            if (materialName.empty())
            {
                materialName = "(unassigned)";
            }
            nodesByMaterial[materialName].push_back(nodeIndex);
        }

        const std::string uvSetName = uvSetIndex < static_cast<int>(tab.loaded.uvSetNames.size()) ?
                                      tab.loaded.uvSetNames[static_cast<size_t>(uvSetIndex)] :
                                      std::string("UV Set ") + std::to_string(uvSetIndex + 1);
        for (const auto& entry : nodesByMaterial)
        {
            const std::vector<UvTriangleSample> materialTriangles = BuildUvScopeTriangles(tab, uvs, entry.second);
            if (materialTriangles.empty()) continue;

            const std::vector<UvIslandStats> islands = CalculateUvIslandStats(materialTriangles, 1, 1);
            std::vector<int> islandOrder;
            for (int i = 0; i < static_cast<int>(islands.size()); ++i) islandOrder.push_back(i);
            std::sort(islandOrder.begin(), islandOrder.end(), [&](int a, int b)
            {
                return islands[static_cast<size_t>(a)].minU < islands[static_cast<size_t>(b)].minU;
            });
            // Build lazily and reuse each island's spatial index across candidate pairs.
            std::vector<UvOverlapTree> overlapTrees(islands.size());
            bool foundOverlap = false;
            int issueNode = -1;
            for (int orderA = 0; orderA < static_cast<int>(islandOrder.size()) && !foundOverlap; ++orderA)
            {
                const int islandA = islandOrder[static_cast<size_t>(orderA)];
                for (int orderB = orderA + 1; orderB < static_cast<int>(islandOrder.size()); ++orderB)
                {
                    const int islandB = islandOrder[static_cast<size_t>(orderB)];
                    if (islands[static_cast<size_t>(islandB)].minU > islands[static_cast<size_t>(islandA)].maxU) break;
                    if (!UvIslandBoundsOverlap(islands[static_cast<size_t>(islandA)], islands[static_cast<size_t>(islandB)])) continue;
                    auto& treeA = overlapTrees[static_cast<size_t>(islandA)];
                    auto& treeB = overlapTrees[static_cast<size_t>(islandB)];
                    if (treeA.nodes.empty()) treeA = BuildUvOverlapTree(islands[static_cast<size_t>(islandA)], materialTriangles);
                    if (treeB.nodes.empty()) treeB = BuildUvOverlapTree(islands[static_cast<size_t>(islandB)], materialTriangles);
                    if (!treeA.nodes.empty() && !treeB.nodes.empty() && UvOverlapTreeNodesIntersect(treeA, 0, treeB, 0, materialTriangles))
                    {
                        issueNode = GetFirstUvIslandNodeIndex(islands[static_cast<size_t>(islandB)], materialTriangles);
                        if (issueNode < 0)
                        {
                            issueNode = GetFirstUvIslandNodeIndex(islands[static_cast<size_t>(islandA)], materialTriangles);
                        }
                        foundOverlap = true;
                        break;
                    }
                }
            }

            if (foundOverlap)
            {
                AddValidationIssue(issues,
                                   ValidatorSeverity::Warning,
                                   "Overlapping UVs",
                                   entry.first + " " + uvSetName + " has overlapping UV islands in the same texture space.",
                                   issueNode);
                issues.back().uvSet = uvSetIndex;
            }
        }
    }
}

void ValidateTexelDensityConsistency(const ModelTab& tab, std::vector<ValidatorIssue>& issues)
{
    if (!tab.loaded.hasMesh || tab.loaded.uvSets.empty()) return;

    constexpr float kDensityRatioThreshold = 1.15f;
    const int uvSetIndex = ClampInt(tab.selectedUvSet, 0, static_cast<int>(tab.loaded.uvSets.size()) - 1);
    const std::vector<float>& uvs = tab.loaded.uvSets[static_cast<size_t>(uvSetIndex)];
    const std::string uvSetName = uvSetIndex < static_cast<int>(tab.loaded.uvSetNames.size()) ? tab.loaded.uvSetNames[static_cast<size_t>(uvSetIndex)] : std::string("UV Set");
    std::unordered_map<std::string, std::vector<std::pair<int, float>>> densitiesByMaterial;

    for (int i = 0; i < static_cast<int>(tab.loaded.nodes.size()); ++i)
    {
        const SceneNode& node = tab.loaded.nodes[static_cast<size_t>(i)];
        if (node.type != SceneNodeType::Mesh || node.meshVertexStart < 0 || node.meshVertexCount <= 0) continue;
        if (uvs.size() < static_cast<size_t>(node.meshVertexStart + node.meshVertexCount) * 2) continue;

        int textureWidth = 0;
        int textureHeight = 0;
        bool usingTexture = false;
        GetUvDensityTextureSize(tab, node, textureWidth, textureHeight, usingTexture);
        const std::string materialName = GetPrimaryMaterialName(node);
        std::vector<UvTriangleSample> triangles;
        AppendNodeUvTriangles(tab.loaded, uvs, i, triangles);
        for (const auto& island : CalculateUvIslandStats(triangles, textureWidth, textureHeight))
            if (island.density > 0.0f) densitiesByMaterial[materialName].push_back({ i, island.density });
    }

    for (const auto& entry : densitiesByMaterial)
    {
        const std::vector<std::pair<int, float>>& densities = entry.second;
        if (densities.size() < 2) continue;

        auto minMax = std::minmax_element(densities.begin(), densities.end(), [](const auto& a, const auto& b)
        {
            return a.second < b.second;
        });
        if (minMax.first == densities.end() || minMax.first->second <= 0.0f) continue;

        const float ratio = minMax.second->second / minMax.first->second;
        if (ratio <= kDensityRatioThreshold) continue;

        char message[256] = {};
        std::snprintf(message,
                      sizeof(message),
                      "%s %s texel density is not uniform between islands: %.1f..%.1f px/m.",
                      entry.first.c_str(),
                      uvSetName.c_str(),
                      minMax.first->second,
                      minMax.second->second);
        AddValidationIssue(issues, ValidatorSeverity::Warning, "Texel density", message, minMax.second->first);
        issues.back().uvSet = uvSetIndex;
    }
}

void RefreshUvMeshBuffers(ModelTab& tab)
{
    if (tab.loaded.uvSets.empty()) return;
    const auto& uv = tab.loaded.uvSets.front();
    for (int m = 0; m < tab.loaded.model.meshCount; ++m)
    {
        if (m >= static_cast<int>(tab.loaded.meshGlobalVertexIndices.size())) continue;
        Mesh& mesh = tab.loaded.model.meshes[m];
        if (!mesh.texcoords) continue;
        const auto& indices = tab.loaded.meshGlobalVertexIndices[m];
        for (size_t v = 0; v < indices.size() && v < static_cast<size_t>(mesh.vertexCount); ++v)
        {
            const size_t base = static_cast<size_t>(indices[v]) * 2;
            mesh.texcoords[v * 2] = base + 1 < uv.size() ? uv[base] : 0.0f;
            mesh.texcoords[v * 2 + 1] = base + 1 < uv.size() ? uv[base + 1] : 0.0f;
        }
        UpdateMeshBuffer(mesh, 1, mesh.texcoords, mesh.vertexCount * 2 * sizeof(float), 0);
    }
}

void CombineAllUvSets(ModelTab& tab, int preferred)
{
    auto& loaded = tab.loaded;
    if (loaded.uvSets.size() < 2 || preferred < 0 || preferred >= static_cast<int>(loaded.uvSets.size())) return;
    PushUndoSnapshot(tab);
    const size_t count = loaded.bindVertices.size() / 3;
    std::vector<float> combined(count * 2, 0.0f);
    std::vector<unsigned char> presence(count, 0);
    for (size_t v = 0; v < count; ++v)
    {
        for (int candidate = -1; candidate < static_cast<int>(loaded.uvSets.size()); ++candidate)
        {
            const int set = candidate < 0 ? preferred : candidate;
            if (loaded.uvSets[set].size() < v * 2 + 2) continue;
            if (set < static_cast<int>(loaded.uvSetPresence.size()) &&
                (v >= loaded.uvSetPresence[set].size() || !loaded.uvSetPresence[set][v])) continue;
            combined[v * 2] = loaded.uvSets[set][v * 2];
            combined[v * 2 + 1] = loaded.uvSets[set][v * 2 + 1];
            presence[v] = 1;
            break;
        }
    }
    const std::string name = preferred < static_cast<int>(loaded.uvSetNames.size()) ? loaded.uvSetNames[preferred] : "UVSet";
    loaded.uvSets = { std::move(combined) };
    loaded.uvSetPresence = { std::move(presence) };
    loaded.uvSetNames = { name };
    loaded.uvSetsEdited = true;
    tab.selectedUvSet = 0;
    tab.uvSetScroll = 0;
    tab.uvContextSet = -1;
    tab.selectedUvIslands.clear();
    tab.uvIslandMarqueeSelecting = false;
    tab.viewportUvIslandCache = ViewportUvIslandCache{};
    RefreshUvMeshBuffers(tab);
    RefreshDisplayedMesh(tab);
}

bool PackValidationUvIslands(ModelTab& tab, int nodeIndex, int uvSet, bool averageDensity)
{
    if (nodeIndex < 0 || nodeIndex >= static_cast<int>(tab.loaded.nodes.size()) ||
        uvSet < 0 || uvSet >= static_cast<int>(tab.loaded.uvSets.size())) return false;
    const std::string material = GetPrimaryMaterialName(tab.loaded.nodes[nodeIndex]);
    std::vector<int> nodes;
    for (int i = 0; i < static_cast<int>(tab.loaded.nodes.size()); ++i)
        if (!IsDeletedNode(tab, i) && NodeUsesSamePrimaryMaterial(tab.loaded.nodes[i], material)) nodes.push_back(i);
    auto& uvs = tab.loaded.uvSets[uvSet];
    const auto triangles = BuildUvScopeTriangles(tab, uvs, nodes);
    for (const auto& triangle : triangles)
        for (const auto& uv : triangle.uv)
            if (!std::isfinite(uv.x) || !std::isfinite(uv.y)) return false;
    const auto islands = CalculateUvIslandStats(triangles, 1, 1);
    if (islands.empty()) return false;

    // A common final scale preserves relative density after averaging the islands.
    struct Box { int island; float scale, width, height; Vector2 origin{}; };
    std::vector<Box> boxes;
    float maxDimension = 0.0f;
    for (int i = 0; i < static_cast<int>(islands.size()); ++i)
    {
        const auto& island = islands[i];
        const float scale = averageDensity && island.density > 0.0f ? 1.0f / island.density : 1.0f;
        const float width = (island.maxU - island.minU) * scale;
        const float height = (island.maxV - island.minV) * scale;
        if (!std::isfinite(width) || !std::isfinite(height)) return false;
        boxes.push_back(Box{ i, scale, width, height });
        maxDimension = std::max(maxDimension, std::max(width, height));
    }
    if (maxDimension <= 0.0f) return false;
    std::stable_sort(boxes.begin(), boxes.end(), [](const Box& a, const Box& b) { return a.height > b.height; });
    const float padding = std::min(0.002f, 0.1f / static_cast<float>(boxes.size() + 1));
    auto place = [&](float scale)
    {
        float x = padding, y = padding, rowHeight = 0.0f;
        for (auto& box : boxes)
        {
            const float width = box.width * scale, height = box.height * scale;
            if (x + width + padding > 1.0f) { x = padding; y += rowHeight + padding; rowHeight = 0.0f; }
            if (x + width + padding > 1.0f || y + height + padding > 1.0f) return false;
            box.origin = Vector2{ x, y };
            x += width + padding;
            rowHeight = std::max(rowHeight, height);
        }
        return true;
    };
    float low = 0.0f, high = 1.0f / maxDimension;
    for (int iteration = 0; iteration < 40; ++iteration)
    {
        const float middle = (low + high) * 0.5f;
        if (place(middle)) low = middle; else high = middle;
    }
    if (low <= 0.0f || !place(low)) return false;
    PushUndoSnapshot(tab);
    for (const auto& box : boxes)
    {
        const auto& island = islands[box.island];
        for (int triangleIndex : island.triangles)
        {
            const auto& triangle = triangles[triangleIndex];
            for (int corner = 0; corner < 3; ++corner)
            {
                const size_t offset = static_cast<size_t>(triangle.vertexStart + corner) * 2;
                uvs[offset] = box.origin.x + (triangle.uv[corner].x - island.minU) * box.scale * low;
                uvs[offset + 1] = box.origin.y + (triangle.uv[corner].y - island.minV) * box.scale * low;
            }
        }
    }
    tab.loaded.uvSetsEdited = true;
    tab.selectedUvSet = uvSet;
    tab.selectedUvIslands.clear();
    tab.viewportUvIslandCache = ViewportUvIslandCache{};
    RefreshUvMeshBuffers(tab);
    RefreshDisplayedMesh(tab);
    return true;
}

std::vector<int> BuildUvVertexMaterials(const ModelTab& tab)
{
    std::vector<int> materials(tab.loaded.bindVertices.size() / 3, -1);
    for (size_t mesh = 0; mesh < tab.loaded.meshGlobalVertexIndices.size(); ++mesh)
    {
        if (mesh >= static_cast<size_t>(tab.loaded.model.meshCount) || !tab.loaded.model.meshMaterial) continue;
        for (int vertex : tab.loaded.meshGlobalVertexIndices[mesh])
            if (vertex >= 0 && vertex < static_cast<int>(materials.size())) materials[vertex] = tab.loaded.model.meshMaterial[mesh];
    }
    return materials;
}

float CalculateUvTileOccupancy(const std::vector<UvTriangleSample>& triangles, int selectedNode = -1)
{
    // Union scanlines count overlapping shells once and clip coverage to the 0..1 tile.
    constexpr int resolution = 256;
    std::array<std::vector<std::pair<float, float>>, resolution> rows;
    for (const auto& triangle : triangles)
    {
        if (selectedNode >= 0 && triangle.nodeIndex != selectedNode) continue;
        bool finite = true;
        for (auto uv : triangle.uv) finite = finite && std::isfinite(uv.x) && std::isfinite(uv.y);
        if (!finite) continue;
        const float minV = std::min({triangle.uv[0].y, triangle.uv[1].y, triangle.uv[2].y});
        const float maxV = std::max({triangle.uv[0].y, triangle.uv[1].y, triangle.uv[2].y});
        const int first = static_cast<int>(ClampFloat(minV, 0, 1) * resolution);
        const int last = std::min(resolution - 1, static_cast<int>(ClampFloat(maxV, 0, 1) * resolution));
        for (int row = first; row <= last; ++row)
        {
            const float y = (row + 0.5f) / resolution;
            float intersections[3];
            int count = 0;
            for (int edge = 0; edge < 3; ++edge)
            {
                const auto a = triangle.uv[edge], b = triangle.uv[(edge + 1) % 3];
                if ((a.y <= y && b.y > y) || (b.y <= y && a.y > y))
                    intersections[count++] = a.x + (y - a.y) / (b.y - a.y) * (b.x - a.x);
            }
            if (count == 2)
            {
                const float left = ClampFloat(std::min(intersections[0], intersections[1]), 0, 1);
                const float right = ClampFloat(std::max(intersections[0], intersections[1]), 0, 1);
                if (right > left) rows[row].push_back({left, right});
            }
        }
    }
    float area = 0;
    for (auto& row : rows)
    {
        std::sort(row.begin(), row.end());
        float end = 0;
        for (const auto& interval : row)
        {
            area += std::max(0.0f, interval.second - std::max(end, interval.first));
            end = std::max(end, interval.second);
        }
    }
    return ClampFloat(area / resolution * 100.0f, 0, 100);
}

void DrawUvPanel(Font font, ModelTab& tab, RenameEditor& renameEditor, float panelX, float panelY, float panelW)
{
    const float contentX = panelX + 12.0f;
    float y = panelY;
    const bool occupancyHasMesh = tab.selectedNode >= 0 && tab.selectedNode < static_cast<int>(tab.loaded.nodes.size()) &&
        tab.loaded.nodes[tab.selectedNode].type == SceneNodeType::Mesh && !IsDeletedNode(tab, tab.selectedNode);
    auto drawOccupancy = [&](bool available)
    {
        char text[128];
        if (available && !occupancyHasMesh) std::snprintf(text, sizeof(text), "UV space: set ~%.1f%%", tab.uvTextureOccupancy);
        else if (available) std::snprintf(text, sizeof(text), "UV space: set ~%.1f%% | mesh ~%.1f%%", tab.uvTextureOccupancy, tab.uvMeshOccupancy);
        else std::snprintf(text, sizeof(text), "UV space: --");
        DrawUiTextClipped(font, text, contentX, panelY + 26, 13, panelW - 24, Color{150, 225, 170, 255});
    };

    DrawUiText(font, "UV EDITOR", contentX, y, 16.0f, Color{ 165, 182, 196, 255 });
    y += 52.0f;
    const float densityY = y;
    constexpr float densityHeight = 102.0f;

    if (tab.loaded.uvSetNames.empty() || tab.loaded.uvSets.empty())
    {
        DrawUiTextClipped(font, "No UV sets found in this FBX.", contentX, y, 14.0f, panelW - 24.0f, Color{ 128, 140, 152, 255 });
        drawOccupancy(false);
        return;
    }

    y += densityHeight;

    const int uvSetCount = static_cast<int>(tab.loaded.uvSets.size());
    tab.selectedUvSet = ClampInt(tab.selectedUvSet, 0, uvSetCount - 1);

    DrawUiText(font, "UV SETS", contentX, y, 15.0f, Color{ 165, 182, 196, 255 });
    y += 22.0f;

    constexpr float uvSetRowH = 24.0f;
    const int visibleRows = std::min(uvSetCount, 4);
    const Rectangle listBounds{ contentX, y, panelW - 24.0f, std::max(uvSetRowH, static_cast<float>(visibleRows) * uvSetRowH) };
    const int maxUvSetScroll = std::max(0, uvSetCount - visibleRows);
    const Vector2 mouse = GetMousePosition();
    if (CheckCollisionPointRec(mouse, listBounds))
    {
        const float wheel = openfbx::UiMouseWheelMove();
        if (std::fabs(wheel) > 0.0f)
        {
            tab.uvSetScroll = ClampInt(tab.uvSetScroll - static_cast<int>(wheel), 0, maxUvSetScroll);
        }
    }
    tab.uvSetScroll = ClampInt(tab.uvSetScroll, 0, maxUvSetScroll);

    DrawRectangleRec(listBounds, Color{ 14, 16, 19, 245 });
    DrawRectangleLinesEx(listBounds, 1.0f, Color{ 70, 80, 90, 255 });
    BeginScissorMode(static_cast<int>(listBounds.x),
                     static_cast<int>(listBounds.y),
                     static_cast<int>(listBounds.width),
                     static_cast<int>(listBounds.height));
    for (int visible = 0; visible < visibleRows; ++visible)
    {
        const int uvSetIndex = tab.uvSetScroll + visible;
        if (uvSetIndex >= uvSetCount) break;

        const Rectangle row{ listBounds.x + 1.0f, listBounds.y + static_cast<float>(visible) * uvSetRowH + 1.0f, listBounds.width - 2.0f, uvSetRowH - 2.0f };
        const bool selected = uvSetIndex == tab.selectedUvSet;
        const bool hovered = CheckCollisionPointRec(mouse, row);
        DrawRectangleRec(row, selected ? Color{ 50, 70, 88, 255 } : hovered ? Color{ 36, 42, 48, 255 } : Color{ 14, 16, 19, 245 });
        if (!renameEditor.active && hovered && openfbx::UiMouseButtonPressed(MOUSE_BUTTON_LEFT))
        {
            tab.selectedUvSet = uvSetIndex;
        }
        if (!renameEditor.active && hovered && openfbx::UiMouseButtonPressed(MOUSE_BUTTON_RIGHT))
        {
            tab.selectedUvSet = uvSetIndex;
            tab.uvContextSet = uvSetIndex;
        }

        char rowText[256] = {};
        const std::string uvName = uvSetIndex < static_cast<int>(tab.loaded.uvSetNames.size()) ? tab.loaded.uvSetNames[static_cast<size_t>(uvSetIndex)] : std::string("UV Set");
        std::snprintf(rowText, sizeof(rowText), "%d  %s", uvSetIndex + 1, uvName.c_str());
        DrawUiTextClipped(font, rowText, row.x + 8.0f, row.y + 4.0f, 14.0f, row.width - 16.0f, selected ? RAYWHITE : Color{ 185, 194, 202, 255 });
    }
    EndScissorMode();

    if (maxUvSetScroll > 0)
    {
        const Rectangle track{ listBounds.x + listBounds.width - 5.0f, listBounds.y + 2.0f, 3.0f, listBounds.height - 4.0f };
        const float thumbHeight = std::max(18.0f, track.height * (static_cast<float>(visibleRows) / static_cast<float>(uvSetCount)));
        const float thumbTravel = std::max(1.0f, track.height - thumbHeight);
        const float thumbY = track.y + thumbTravel * (static_cast<float>(tab.uvSetScroll) / static_cast<float>(maxUvSetScroll));
        DrawRectangleRec(track, Color{ 42, 48, 54, 255 });
        DrawRectangleRec(Rectangle{ track.x, thumbY, track.width, thumbHeight }, Color{ 130, 145, 158, 255 });
    }
    y += listBounds.height + 14.0f;
    if (tab.uvContextSet >= 0 && tab.uvContextSet < uvSetCount && !renameEditor.active)
    {
        const Rectangle menu{ contentX, y, panelW - 24.0f, 60.0f };
        DrawRectangleRec(menu, Color{ 25, 30, 36, 255 });
        if (DrawPanelButton(font, Rectangle{ contentX + 4, y + 3, menu.width - 8, 24 }, "Rename UV Set"))
        {
            renameEditor = RenameEditor{};
            renameEditor.target = RenameTarget::UvSet;
            renameEditor.uvSetIndex = tab.uvContextSet;
            renameEditor.active = true;
            renameEditor.justOpened = true;
            std::snprintf(renameEditor.text, sizeof(renameEditor.text), "%s", tab.loaded.uvSetNames[tab.uvContextSet].c_str());
            renameEditor.cursor = static_cast<int>(std::strlen(renameEditor.text));
            renameEditor.textSelected = true;
            tab.uvContextSet = -1;
        }
        if (uvSetCount > 1 && DrawPanelButton(font, Rectangle{ contentX + 4, y + 31, menu.width - 8, 24 }, "Combine All UV Sets"))
            CombineAllUvSets(tab, tab.uvContextSet);
        if (IsKeyPressed(KEY_ESCAPE) || (openfbx::UiMouseButtonPressed(MOUSE_BUTTON_LEFT) && !CheckCollisionPointRec(mouse, menu)))
            tab.uvContextSet = -1;
        y += 66.0f;
    }

    const bool hasSelectedMesh = tab.selectedNode >= 0 && tab.selectedNode < static_cast<int>(tab.loaded.nodes.size()) &&
        tab.loaded.nodes[tab.selectedNode].type == SceneNodeType::Mesh && !IsDeletedNode(tab, tab.selectedNode);
    const int selectedNodeIndex = hasSelectedMesh ? tab.selectedNode : -1;
    const std::vector<float>& uvs = tab.loaded.uvSets[static_cast<size_t>(tab.selectedUvSet)];
    const std::string meshText = hasSelectedMesh ? "Mesh: " + tab.loaded.nodes[selectedNodeIndex].name : "All meshes in texture set";
    DrawUiTextClipped(font, meshText.c_str(), contentX, y, 14, panelW - 24, Color{205, 213, 220, 255});
    y += 24;

    EnsurePbrMaterialStates(tab);
    int densityTextureWidth = 0;
    int densityTextureHeight = 0;
    bool usingDensityTexture = false;
    const auto vertexMaterials = BuildUvVertexMaterials(tab);
    std::vector<UvTriangleSample> allTriangles;
    for (int i = 0; i < static_cast<int>(tab.loaded.nodes.size()); ++i)
        if (!IsDeletedNode(tab, i)) AppendNodeUvTriangles(tab.loaded, uvs, i, allTriangles);
    auto triangleMaterial = [&](const UvTriangleSample& triangle)
    {
        const int vertex = triangle.vertexStart;
        if (vertex >= 0 && vertex < static_cast<int>(vertexMaterials.size()) && vertexMaterials[vertex] >= 0)
            return vertexMaterials[vertex];
        return GetMaterialIndexForNode(tab, tab.loaded.nodes[triangle.nodeIndex]);
    };
    std::vector<int> textureSets;
    for (int material = 0; material < static_cast<int>(tab.loaded.materialNames.size()); ++material)
        textureSets.push_back(material);
    for (const auto& triangle : allTriangles)
    {
        const int material = triangleMaterial(triangle);
        if (std::find(textureSets.begin(), textureSets.end(), material) == textureSets.end()) textureSets.push_back(material);
    }
    std::sort(textureSets.begin(), textureSets.end());
    const bool newMesh = hasSelectedMesh && tab.uvTextureSetNode != selectedNodeIndex;
    if (newMesh || std::find(textureSets.begin(), textureSets.end(), tab.uvTextureSet) == textureSets.end())
    {
        tab.uvTextureSet = textureSets.empty() ? -1 : textureSets.front();
        if (newMesh)
        {
            int firstMaterial = -1;
            for (const auto& triangle : allTriangles)
                if (triangle.nodeIndex == selectedNodeIndex)
                {
                    const int material = triangleMaterial(triangle);
                    if (firstMaterial < 0 || material < firstMaterial) firstMaterial = material;
                }
            if (firstMaterial >= 0) tab.uvTextureSet = firstMaterial;
        }
        const auto selected = std::find(textureSets.begin(), textureSets.end(), tab.uvTextureSet);
        tab.uvTextureSetScroll = selected == textureSets.end() ? 0 : static_cast<int>(selected - textureSets.begin());
    }
    tab.uvTextureSetNode = selectedNodeIndex;
    DrawUiText(font, "TEXTURE SETS", contentX, y, 14, Color{165, 182, 196, 255});
    y += 22;
    const int textureRows = std::min(3, static_cast<int>(textureSets.size()));
    const int textureMaxScroll = std::max(0, static_cast<int>(textureSets.size()) - textureRows);
    const Rectangle textureList{contentX, y, panelW - 24, std::max(1, textureRows) * 24.0f};
    if (CheckCollisionPointRec(mouse, textureList))
        tab.uvTextureSetScroll -= static_cast<int>(openfbx::UiMouseWheelMove());
    tab.uvTextureSetScroll = ClampInt(tab.uvTextureSetScroll, 0, textureMaxScroll);
    DrawRectangleRec(textureList, Color{14, 16, 19, 245});
    for (int rowIndex = 0; rowIndex < textureRows; ++rowIndex)
    {
        const int material = textureSets[tab.uvTextureSetScroll + rowIndex];
        const Rectangle row{contentX + 1, y + rowIndex * 24 + 1, textureList.width - 8, 22};
        const bool hovered = CheckCollisionPointRec(mouse, row);
        if (!renameEditor.active && hovered && openfbx::UiMouseButtonPressed(MOUSE_BUTTON_LEFT))
            tab.uvTextureSet = material;
        const bool selected = tab.uvTextureSet == material;
        DrawRectangleRec(row, selected ? Color{50, 70, 88, 255} : hovered ? Color{36, 42, 48, 255} : Color{14, 16, 19, 245});
        const std::string name = material >= 0 && material < static_cast<int>(tab.loaded.materialNames.size()) ?
            tab.loaded.materialNames[material] : "Unassigned";
        const std::string label = std::string(selected ? "> " : "  ") + name;
        DrawUiTextClipped(font, label.c_str(), row.x + 6, row.y + 4, 14, row.width - 12,
                          selected ? RAYWHITE : Color{185, 194, 202, 255});
    }
    if (textureMaxScroll > 0)
    {
        const float thumbHeight = textureList.height * textureRows / static_cast<float>(textureSets.size());
        const float thumbY = y + (textureList.height - thumbHeight) * tab.uvTextureSetScroll / textureMaxScroll;
        DrawRectangleRec(Rectangle{textureList.x + textureList.width - 4, thumbY, 3, thumbHeight}, Color{130, 145, 158, 255});
    }
    DrawRectangleLinesEx(textureList, 1, Color{70, 80, 90, 255});
    y += textureList.height + 8;
    std::vector<UvTriangleSample> scopeTriangles;
    for (const auto& triangle : allTriangles)
        if (triangleMaterial(triangle) == tab.uvTextureSet) scopeTriangles.push_back(triangle);
    size_t occupancyHash = std::hash<int>{}(selectedNodeIndex);
    auto hashValue = [&](size_t value) { occupancyHash ^= value + 0x9e3779b9 + (occupancyHash << 6) + (occupancyHash >> 2); };
    for (const auto& triangle : scopeTriangles)
    {
        hashValue(std::hash<int>{}(triangle.nodeIndex));
        for (auto uv : triangle.uv) { hashValue(std::hash<float>{}(uv.x)); hashValue(std::hash<float>{}(uv.y)); }
    }
    if (occupancyHash != tab.uvOccupancyHash)
    {
        tab.uvOccupancyHash = occupancyHash;
        tab.uvTextureOccupancy = CalculateUvTileOccupancy(scopeTriangles);
        tab.uvMeshOccupancy = hasSelectedMesh ? CalculateUvTileOccupancy(scopeTriangles, selectedNodeIndex) : 0.0f;
    }
    drawOccupancy(true);
    densityTextureWidth = densityTextureHeight = std::max(1, tab.uvDensityTileSize);
    usingDensityTexture = false;
    if (tab.uvTextureSet >= 0 && tab.uvTextureSet < static_cast<int>(tab.pbrMaterials.size()))
    {
        const auto& diffuse = GetPbrTexture(tab.pbrMaterials[tab.uvTextureSet], PbrTextureSlot::Diffuse);
        if (diffuse.loaded && diffuse.texture.width > 0 && diffuse.texture.height > 0)
        {
            densityTextureWidth = diffuse.texture.width;
            densityTextureHeight = diffuse.texture.height;
            usingDensityTexture = true;
        }
    }
    const std::vector<UvIslandStats> islands = CalculateUvIslandStats(scopeTriangles, densityTextureWidth, densityTextureHeight);
    if (tab.uvSelectionNodeIndex != selectedNodeIndex ||
        tab.uvSelectionUvSet != tab.selectedUvSet ||
        tab.uvSelectionTextureSet != tab.uvTextureSet)
    {
        tab.selectedUvIslands.clear();
        tab.uvIslandMarqueeSelecting = false;
        tab.uvSelectionNodeIndex = selectedNodeIndex;
        tab.uvSelectionUvSet = tab.selectedUvSet;
        tab.uvSelectionTextureSet = tab.uvTextureSet;
    }
    PruneSelectedUvIslands(tab.selectedUvIslands, static_cast<int>(islands.size()));

    const float editorY = y;
    y = densityY;
    DrawUiText(font, "TEXEL DENSITY", contentX, y, 14.0f, Color{ 165, 182, 196, 255 });
    y += 22.0f;
    {
        char textureLine[192] = {};
        if (usingDensityTexture)
        {
            std::snprintf(textureLine, sizeof(textureLine), "Texture: diffuse %dx%d", densityTextureWidth, densityTextureHeight);
            DrawUiTextClipped(font, textureLine, contentX, y, 14.0f, panelW - 24.0f, Color{ 190, 200, 210, 255 });
        }
        else
        {
            std::snprintf(textureLine, sizeof(textureLine), "Tile %d px", tab.uvDensityTileSize);
            if (DrawPanelButton(font, Rectangle{ contentX, y - 3.0f, panelW - 24.0f, 24.0f }, textureLine))
            {
                tab.uvDensityTileSize = NextUvDensityTileSize(tab.uvDensityTileSize);
            }
        }
        y += 28.0f;

        const UvSelectionSummary selectionSummary = CalculateUvSelectionSummary(islands, tab.selectedUvIslands);
        if (selectionSummary.count > 0)
        {
            char selectionLine[192] = {};
            if (selectionSummary.count == 1)
            {
                std::snprintf(selectionLine,
                              sizeof(selectionLine),
                              "Selected island: UV space %.2f%%",
                              selectionSummary.uvArea * 100.0f);
            }
            else
            {
                std::snprintf(selectionLine,
                              sizeof(selectionLine),
                              "Selected islands %d: UV space %.2f%%",
                              selectionSummary.count,
                              selectionSummary.uvArea * 100.0f);
            }
            DrawUiTextClipped(font, selectionLine, contentX, y, 14.0f, panelW - 24.0f, Color{ 190, 200, 210, 255 });
            y += 22.0f;

            char densityLine[192] = {};
            if (selectionSummary.mixedDensity)
            {
                std::snprintf(densityLine, sizeof(densityLine), "Texel density: multiple");
            }
            else
            {
                std::snprintf(densityLine, sizeof(densityLine), "Texel density: %.1f px/m", selectionSummary.density);
            }
            DrawUiTextClipped(font, densityLine, contentX, y, 14.0f, panelW - 24.0f, selectionSummary.density > 0.0f || selectionSummary.mixedDensity ? Color{ 150, 225, 170, 255 } : Color{ 255, 185, 125, 255 });
            y += 22.0f;
        }
        else
        {
            char islandLine[128] = {};
            std::snprintf(islandLine, sizeof(islandLine), "Click a UV island to inspect density");
            DrawUiTextClipped(font, islandLine, contentX, y, 14.0f, panelW - 24.0f, Color{ 255, 185, 125, 255 });
            y += 22.0f;
        }

        y += 8.0f;
    }

    y = editorY;

    const float panelBottom = 61.0f + GetHierarchyPanelHeight();
    const float editorWidth = panelW - 24.0f;
    const float editorHeight = panelBottom - y - 12.0f;
    if (editorWidth < 48.0f || editorHeight < 48.0f)
    {
        tab.uvIslandMarqueeSelecting = false;
        if (y + 18.0f < panelBottom)
        {
            DrawUiTextClipped(font, "Not enough panel space for the UV editor.", contentX, y, 14.0f, panelW - 24.0f, Color{ 128, 140, 152, 255 });
        }
        return;
    }

    const Rectangle editor{ contentX, y, editorWidth, editorHeight };
    DrawRectangleRec(editor, Color{ 14, 16, 19, 245 });
    DrawRectangleLinesEx(editor, 1.0f, Color{ 88, 98, 108, 255 });

    tab.uvViewZoom = ClampFloat(tab.uvViewZoom, 0.2f, 80.0f);
    const Vector2 editorCenter{ editor.x + editor.width * 0.5f, editor.y + editor.height * 0.5f };
    auto getUvScale = [&]()
    {
        const float scale = std::min(editor.width, editor.height) * tab.uvViewZoom;
        return Vector2{ scale, scale };
    };

    auto uvToScreen = [&](Vector2 uv)
    {
        const Vector2 scale = getUvScale();
        return Vector2{
            editorCenter.x + tab.uvViewPan.x + (uv.x - 0.5f) * scale.x,
            editorCenter.y + tab.uvViewPan.y - (uv.y - 0.5f) * scale.y
        };
    };
    auto screenToUv = [&](Vector2 point)
    {
        const Vector2 scale = getUvScale();
        return Vector2{
            0.5f + (point.x - editorCenter.x - tab.uvViewPan.x) / scale.x,
            0.5f - (point.y - editorCenter.y - tab.uvViewPan.y) / scale.y
        };
    };

    const bool mouseOverEditor = CheckCollisionPointRec(mouse, editor);
    const bool shiftDown = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);
    if (mouseOverEditor)
    {
        const float wheel = openfbx::UiMouseWheelMove();
        if (std::fabs(wheel) > 0.0f)
        {
            const Vector2 uvUnderMouse = screenToUv(mouse);
            tab.uvViewZoom = ClampFloat(tab.uvViewZoom * std::pow(1.18f, wheel), 0.2f, 80.0f);
            const Vector2 scale = getUvScale();
            tab.uvViewPan.x = mouse.x - editorCenter.x - (uvUnderMouse.x - 0.5f) * scale.x;
            tab.uvViewPan.y = mouse.y - editorCenter.y + (uvUnderMouse.y - 0.5f) * scale.y;
        }

        if (openfbx::UiMouseButtonPressed(MOUSE_BUTTON_RIGHT) || openfbx::UiMouseButtonPressed(MOUSE_BUTTON_MIDDLE))
        {
            tab.uvViewPanning = true;
        }
        if (openfbx::UiMouseButtonPressed(MOUSE_BUTTON_LEFT))
        {
            tab.uvIslandMarqueeSelecting = true;
            tab.uvIslandMarqueeAdditive = shiftDown;
            tab.uvIslandMarqueeStart = mouse;
            tab.uvIslandMarqueeCurrent = mouse;
        }
    }
    if (tab.uvIslandMarqueeSelecting && openfbx::UiMouseButtonDown(MOUSE_BUTTON_LEFT))
    {
        tab.uvIslandMarqueeCurrent = mouse;
    }
    if (tab.uvIslandMarqueeSelecting && openfbx::UiMouseButtonReleased(MOUSE_BUTTON_LEFT))
    {
        tab.uvIslandMarqueeCurrent = mouse;
        constexpr float kUvMarqueeThreshold = 5.0f;
        const bool dragged = Vector2Distance(tab.uvIslandMarqueeStart, tab.uvIslandMarqueeCurrent) >= kUvMarqueeThreshold;
        if (dragged)
        {
            const Rectangle marquee = RectangleFromPoints(tab.uvIslandMarqueeStart, tab.uvIslandMarqueeCurrent);
            std::vector<int> marqueeIslands;
            for (int islandIndex = 0; islandIndex < static_cast<int>(islands.size()); ++islandIndex)
            {
                const UvIslandStats& island = islands[static_cast<size_t>(islandIndex)];
                bool intersects = false;
                for (int triangleIndex : island.triangles)
                {
                    if (triangleIndex < 0 || triangleIndex >= static_cast<int>(scopeTriangles.size())) continue;

                    const UvTriangleSample& triangle = scopeTriangles[static_cast<size_t>(triangleIndex)];
                    const Vector2 a = uvToScreen(triangle.uv[0]);
                    const Vector2 b = uvToScreen(triangle.uv[1]);
                    const Vector2 c = uvToScreen(triangle.uv[2]);
                    const float minX = std::min(a.x, std::min(b.x, c.x));
                    const float minY = std::min(a.y, std::min(b.y, c.y));
                    const float maxX = std::max(a.x, std::max(b.x, c.x));
                    const float maxY = std::max(a.y, std::max(b.y, c.y));
                    if (CheckCollisionRecs(marquee, Rectangle{ minX, minY, maxX - minX, maxY - minY }))
                    {
                        intersects = true;
                        break;
                    }
                }
                if (intersects)
                {
                    marqueeIslands.push_back(islandIndex);
                }
            }

            if (!tab.uvIslandMarqueeAdditive)
            {
                tab.selectedUvIslands.clear();
            }
            for (int islandIndex : marqueeIslands)
            {
                AddSelectedUvIsland(tab.selectedUvIslands, islandIndex);
            }
        }
        else
        {
            const int pickedIsland = CheckCollisionPointRec(tab.uvIslandMarqueeCurrent, editor) ?
                                     FindUvIslandAtPoint(islands, scopeTriangles, screenToUv(tab.uvIslandMarqueeCurrent)) :
                                     -1;
            if (tab.uvIslandMarqueeAdditive)
            {
                ToggleSelectedUvIsland(tab.selectedUvIslands, pickedIsland);
            }
            else
            {
                SetSingleSelectedUvIsland(tab.selectedUvIslands, pickedIsland);
            }
        }
        tab.uvIslandMarqueeSelecting = false;
    }
    if (!openfbx::UiMouseButtonDown(MOUSE_BUTTON_LEFT) && !openfbx::UiMouseButtonReleased(MOUSE_BUTTON_LEFT))
    {
        tab.uvIslandMarqueeSelecting = false;
    }
    if (openfbx::UiMouseButtonReleased(MOUSE_BUTTON_RIGHT) || openfbx::UiMouseButtonReleased(MOUSE_BUTTON_MIDDLE))
    {
        tab.uvViewPanning = false;
    }
    if (tab.uvViewPanning && (openfbx::UiMouseButtonDown(MOUSE_BUTTON_RIGHT) || openfbx::UiMouseButtonDown(MOUSE_BUTTON_MIDDLE)))
    {
        const Vector2 delta = GetMouseDelta();
        tab.uvViewPan = Vector2Add(tab.uvViewPan, delta);
    }
    else if (!openfbx::UiMouseButtonDown(MOUSE_BUTTON_RIGHT) && !openfbx::UiMouseButtonDown(MOUSE_BUTTON_MIDDLE))
    {
        tab.uvViewPanning = false;
    }

    BeginScissorMode(static_cast<int>(editor.x),
                     static_cast<int>(editor.y),
                     static_cast<int>(editor.width),
                     static_cast<int>(editor.height));
    for (int i = 0; i <= 4; ++i)
    {
        const float p = static_cast<float>(i) / 4.0f;
        const Color gridColor = i == 0 || i == 4 ? Color{ 78, 88, 98, 255 } : Color{ 42, 48, 54, 255 };
        DrawLineV(uvToScreen(Vector2{ p, 0.0f }), uvToScreen(Vector2{ p, 1.0f }), gridColor);
        DrawLineV(uvToScreen(Vector2{ 0.0f, p }), uvToScreen(Vector2{ 1.0f, p }), gridColor);
    }

    const Color islandColors[] = {
        Color{ 95, 170, 220, 230 },
        Color{ 230, 176, 76, 230 },
        Color{ 138, 205, 132, 230 },
        Color{ 214, 128, 180, 230 },
        Color{ 140, 154, 230, 230 },
        Color{ 230, 128, 100, 230 }
    };
    constexpr int islandColorCount = static_cast<int>(sizeof(islandColors) / sizeof(islandColors[0]));
    for (int highlightPass = 0; highlightPass < 2; ++highlightPass)
    for (int islandIndex = 0; islandIndex < static_cast<int>(islands.size()); ++islandIndex)
    {
        const UvIslandStats& island = islands[static_cast<size_t>(islandIndex)];
        const bool selectedIsland = IsUvIslandSelected(tab.selectedUvIslands, islandIndex);
        const bool meshIsland = island.nodeIndex == selectedNodeIndex;
        if ((meshIsland || selectedIsland) != (highlightPass == 1)) continue;
        const Color baseColor = (meshIsland || !hasSelectedMesh) ? islandColors[islandIndex % islandColorCount] : Color{85, 95, 108, 150};
        const Color lineColor = selectedIsland ? Color{ 255, 245, 180, 255 } : baseColor;
        const unsigned char fillAlpha = selectedIsland ? 110 : meshIsland ? 65 : hasSelectedMesh ? 12 : 34;
        const Color fillColor{ lineColor.r, lineColor.g, lineColor.b, fillAlpha };
        for (int triangleIndex : island.triangles)
        {
            if (triangleIndex < 0 || triangleIndex >= static_cast<int>(scopeTriangles.size())) continue;

            const UvTriangleSample& triangle = scopeTriangles[static_cast<size_t>(triangleIndex)];
            const Vector2 a = uvToScreen(triangle.uv[0]);
            const Vector2 b = uvToScreen(triangle.uv[1]);
            const Vector2 c = uvToScreen(triangle.uv[2]);
            DrawTriangle(a, b, c, fillColor);
            DrawTriangle(c, b, a, fillColor);
            if (selectedIsland || meshIsland)
            {
                DrawLineEx(a, b, 2.0f, lineColor);
                DrawLineEx(b, c, 2.0f, lineColor);
                DrawLineEx(c, a, 2.0f, lineColor);
            }
            else
            {
                DrawLineV(a, b, lineColor);
                DrawLineV(b, c, lineColor);
                DrawLineV(c, a, lineColor);
            }
        }
    }
    if (tab.uvIslandMarqueeSelecting && Vector2Distance(tab.uvIslandMarqueeStart, tab.uvIslandMarqueeCurrent) >= 5.0f)
    {
        const Rectangle marquee = RectangleFromPoints(tab.uvIslandMarqueeStart, tab.uvIslandMarqueeCurrent);
        DrawRectangleRec(marquee, Color{ 255, 245, 180, 34 });
        DrawRectangleLinesEx(marquee, 1.0f, Color{ 255, 245, 180, 220 });
    }
    EndScissorMode();

    DrawRectangleLinesEx(editor, 1.0f, Color{ 88, 98, 108, 255 });
}

