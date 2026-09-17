#include "FbxLoader.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <functional>
#include <limits>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <string>
#include <vector>

#include "fbxsdk.h"
#include "raymath.h"

namespace
{
struct FbxManagerDestroy
{
    void operator()(FbxManager* manager) const
    {
        if (manager) manager->Destroy();
    }
};

struct RenderVertexRef
{
    FbxNode* node = nullptr;
    FbxMesh* mesh = nullptr;
    int controlPointIndex = 0;
    FbxVector4 localNormal{};
    bool hasNormal = false;
};

struct MeshBuilder
{
    std::vector<float> vertices;
    std::vector<float> normals;
    std::vector<float> texcoords;
    std::vector<std::string> uvSetNames;
    std::vector<std::vector<float>> uvSets;
    std::vector<std::vector<unsigned char>> uvSetPresence;
    std::vector<FbxTextureReference> textureReferences;
    std::vector<SkinnedVertex> skinnedVertices;
    std::vector<unsigned int> indices;
    std::vector<RenderVertexRef> renderVertices;
    std::vector<int> vertexMaterialIndices;
    std::vector<int> polygonVertexGlobalIndices;
    std::vector<MeshEdge> polygonEdges;
    std::vector<std::string> materialNames;
    std::unordered_map<std::string, int> materialNameToIndex;
    std::vector<BoneSegment> bones;
    std::vector<BonePose> bonePoses;
    std::vector<SceneNode> nodes;
    std::vector<AnimationClip> animations;
    std::unordered_map<FbxNode*, int> nodeToIndex;
    bool hasBounds = false;
    BoundingBox bounds{
        { std::numeric_limits<float>::max(), std::numeric_limits<float>::max(), std::numeric_limits<float>::max() },
        { -std::numeric_limits<float>::max(), -std::numeric_limits<float>::max(), -std::numeric_limits<float>::max() }
    };

    void AddBounds(Vector3 p)
    {
        hasBounds = true;
        bounds.min.x = std::min(bounds.min.x, p.x);
        bounds.min.y = std::min(bounds.min.y, p.y);
        bounds.min.z = std::min(bounds.min.z, p.z);
        bounds.max.x = std::max(bounds.max.x, p.x);
        bounds.max.y = std::max(bounds.max.y, p.y);
        bounds.max.z = std::max(bounds.max.z, p.z);
    }

    int VertexCount() const
    {
        return static_cast<int>(vertices.size() / 3);
    }
};

struct ControlPointInfluence
{
    FbxAMatrix deformation;
    double weight = 0.0;
};

struct SkinSample
{
    std::vector<std::vector<ControlPointInfluence>> influences;
    std::vector<double> weights;
    FbxCluster::ELinkMode linkMode = FbxCluster::eNormalize;
    bool hasCluster = false;
};

struct ControlPointBindInfluence
{
    std::string boneName;
    float weight = 0.0f;
    FbxAMatrix bindFromControlPoint;
};

struct SkinBindData
{
    std::vector<std::vector<ControlPointBindInfluence>> influences;
    bool hasSkin = false;
};

FbxVector4 TransformVector(const FbxAMatrix& matrix, FbxVector4 vector);
SkinBindData BuildSkinBindData(FbxNode* node, FbxMesh* mesh);
FbxAMatrix MatrixFromPose(const BonePose& pose);
std::filesystem::path MakeEmbeddedTextureExtractionDirectory(const std::string& modelPath);
std::filesystem::path MakeUniqueEmbeddedTextureExtractionDirectory(const std::string& modelPath);
std::filesystem::path MakeSaveEmbeddedTextureExtractionDirectory(const std::string& modelPath);
void RemoveEmbeddedTextureExtractionDirectory(const std::filesystem::path& directory);
void CollectImportedTextureReferences(FbxScene* scene,
                                      const std::string& modelPath,
                                      const std::filesystem::path& extractionDirectory,
                                      MeshBuilder& out);
const char* GetTextureUsagePropertyName(FbxTextureUsage usage);

void ExpandBounds(BoundingBox& bounds, Vector3 p, bool& hasBounds)
{
    if (!hasBounds)
    {
        bounds.min = p;
        bounds.max = p;
        hasBounds = true;
        return;
    }

    bounds.min.x = std::min(bounds.min.x, p.x);
    bounds.min.y = std::min(bounds.min.y, p.y);
    bounds.min.z = std::min(bounds.min.z, p.z);
    bounds.max.x = std::max(bounds.max.x, p.x);
    bounds.max.y = std::max(bounds.max.y, p.y);
    bounds.max.z = std::max(bounds.max.z, p.z);
}

Vector3 ToVector3(const FbxVector4& v)
{
    return Vector3{ static_cast<float>(v[0]), static_cast<float>(v[1]), static_cast<float>(v[2]) };
}

Vector2 ToVector2(const FbxVector2& v)
{
    return Vector2{ static_cast<float>(v[0]), 1.0f - static_cast<float>(v[1]) };
}

Vector3 NormalizeOrFallback(Vector3 n, Vector3 fallback)
{
    const float len = Vector3Length(n);
    if (len > 0.000001f) return Vector3Scale(n, 1.0f / len);
    return fallback;
}

void SetSceneNodeTransform(SceneNode& sceneNode, const FbxAMatrix& transform)
{
    const FbxVector4 origin = transform.MultT(FbxVector4(0.0, 0.0, 0.0, 1.0));
    const FbxVector4 rotation = transform.GetR();
    const FbxVector4 scale = transform.GetS();
    sceneNode.position = ToVector3(origin);
    sceneNode.axisX = NormalizeOrFallback(ToVector3(transform.MultT(FbxVector4(1.0, 0.0, 0.0, 1.0)) - origin), Vector3{ 1.0f, 0.0f, 0.0f });
    sceneNode.axisY = NormalizeOrFallback(ToVector3(transform.MultT(FbxVector4(0.0, 1.0, 0.0, 1.0)) - origin), Vector3{ 0.0f, 1.0f, 0.0f });
    sceneNode.axisZ = NormalizeOrFallback(ToVector3(transform.MultT(FbxVector4(0.0, 0.0, 1.0, 1.0)) - origin), Vector3{ 0.0f, 0.0f, 1.0f });
    sceneNode.rotation = ToVector3(rotation);
    sceneNode.scale = ToVector3(scale);
}

BonePose MakeBonePose(const FbxAMatrix& transform, int nodeIndex)
{
    const FbxVector4 origin = transform.MultT(FbxVector4(0.0, 0.0, 0.0, 1.0));
    const FbxVector4 rotation = transform.GetR();
    const FbxVector4 scale = transform.GetS();

    BonePose pose;
    pose.position = ToVector3(origin);
    pose.axisX = NormalizeOrFallback(ToVector3(transform.MultT(FbxVector4(1.0, 0.0, 0.0, 1.0)) - origin), Vector3{ 1.0f, 0.0f, 0.0f });
    pose.axisY = NormalizeOrFallback(ToVector3(transform.MultT(FbxVector4(0.0, 1.0, 0.0, 1.0)) - origin), Vector3{ 0.0f, 1.0f, 0.0f });
    pose.axisZ = NormalizeOrFallback(ToVector3(transform.MultT(FbxVector4(0.0, 0.0, 1.0, 1.0)) - origin), Vector3{ 0.0f, 0.0f, 1.0f });
    pose.rotation = ToVector3(rotation);
    pose.scale = ToVector3(scale);
    pose.node = nodeIndex;
    return pose;
}

FbxAMatrix GetNodeGeometryTransform(const FbxNode* node)
{
    FbxAMatrix geometry;
    geometry.SetIdentity();
    geometry.SetT(node->GetGeometricTranslation(FbxNode::eSourcePivot));
    geometry.SetR(node->GetGeometricRotation(FbxNode::eSourcePivot));
    geometry.SetS(node->GetGeometricScaling(FbxNode::eSourcePivot));
    return geometry;
}

SceneNodeType GetSceneNodeType(FbxNode* node)
{
    if (!node) return SceneNodeType::Empty;

    bool hasMesh = false;
    bool hasSkeleton = false;
    for (int i = 0; i < node->GetNodeAttributeCount(); ++i)
    {
        const FbxNodeAttribute* attribute = node->GetNodeAttributeByIndex(i);
        if (!attribute) continue;

        if (attribute->GetAttributeType() == FbxNodeAttribute::eMesh) hasMesh = true;
        if (attribute->GetAttributeType() == FbxNodeAttribute::eSkeleton) hasSkeleton = true;
    }

    if (hasMesh) return SceneNodeType::Mesh;
    if (hasSkeleton) return SceneNodeType::Bone;
    return SceneNodeType::Empty;
}

int AddSceneNode(FbxNode* node, int parentIndex, int depth, MeshBuilder& out)
{
    SceneNode sceneNode;
    sceneNode.name = node && node->GetName() && node->GetName()[0] ? node->GetName() : "Node";
    sceneNode.sourceName = sceneNode.name;
    sceneNode.parent = parentIndex;
    sceneNode.depth = depth;
    sceneNode.type = GetSceneNodeType(node);
    SetSceneNodeTransform(sceneNode, node->EvaluateGlobalTransform());

    const int index = static_cast<int>(out.nodes.size());
    out.nodes.push_back(sceneNode);
    out.nodeToIndex[node] = index;
    return index;
}

void ExpandSceneNodeBounds(MeshBuilder& out, FbxNode* node, Vector3 point)
{
    const auto found = out.nodeToIndex.find(node);
    if (found == out.nodeToIndex.end()) return;

    SceneNode& sceneNode = out.nodes[static_cast<size_t>(found->second)];
    ExpandBounds(sceneNode.bounds, point, sceneNode.hasBounds);
}

void AddTriangle(MeshBuilder& out,
                 const Vector3 points[3],
                 const Vector3 normals[3],
                 const Vector2 uvs[3],
                 const std::vector<std::array<Vector2, 3>>& uvSetValues,
                 const RenderVertexRef refs[3],
                 const SkinBindData& skinBind,
                 int materialIndex,
                 bool hasNormals,
                 bool hasUvs)
{
    Vector3 faceNormal = Vector3CrossProduct(Vector3Subtract(points[1], points[0]),
                                            Vector3Subtract(points[2], points[0]));
    faceNormal = NormalizeOrFallback(faceNormal, Vector3{ 0.0f, 1.0f, 0.0f });

    for (int i = 0; i < 3; ++i)
    {
        const unsigned int index = static_cast<unsigned int>(out.VertexCount());
        const Vector3 normal = hasNormals ? NormalizeOrFallback(normals[i], faceNormal) : faceNormal;
        const Vector2 uv = hasUvs ? uvs[i] : Vector2{ 0.0f, 0.0f };

        out.indices.push_back(index);
        out.renderVertices.push_back(refs[i]);
        out.vertexMaterialIndices.push_back(materialIndex);

        out.vertices.push_back(points[i].x);
        out.vertices.push_back(points[i].y);
        out.vertices.push_back(points[i].z);

        out.normals.push_back(normal.x);
        out.normals.push_back(normal.y);
        out.normals.push_back(normal.z);

        SkinnedVertex skinned;
        skinned.bindPosition = points[i];
        skinned.bindNormal = normal;
        if (refs[i].controlPointIndex >= 0 && refs[i].controlPointIndex < static_cast<int>(skinBind.influences.size()))
        {
            const FbxVector4 bindPosition(points[i].x, points[i].y, points[i].z, 1.0);
            const FbxVector4 bindNormal(normal.x, normal.y, normal.z, 0.0);
            for (const ControlPointBindInfluence& influence : skinBind.influences[static_cast<size_t>(refs[i].controlPointIndex)])
            {
                FbxVector4 localBindPosition = bindPosition;
                if (refs[i].mesh && refs[i].controlPointIndex >= 0 && refs[i].controlPointIndex < refs[i].mesh->GetControlPointsCount())
                {
                    localBindPosition = refs[i].mesh->GetControlPoints()[refs[i].controlPointIndex];
                    localBindPosition[3] = 1.0;
                }
                FbxVector4 localBindNormal = bindNormal;
                if (refs[i].hasNormal)
                {
                    localBindNormal = refs[i].localNormal;
                    localBindNormal[3] = 0.0;
                }

                const FbxVector4 positionInBone = influence.bindFromControlPoint.MultT(localBindPosition);
                const FbxVector4 normalInBone = TransformVector(influence.bindFromControlPoint, localBindNormal);
                skinned.influences.push_back(SkinnedVertexInfluence{
                    influence.boneName,
                    influence.weight,
                    ToVector3(positionInBone),
                    NormalizeOrFallback(ToVector3(normalInBone), normal)
                });
            }
        }
        out.skinnedVertices.push_back(std::move(skinned));

        out.texcoords.push_back(uv.x);
        out.texcoords.push_back(uv.y);
        for (size_t uvSet = 0; uvSet < out.uvSets.size(); ++uvSet)
        {
            const Vector2 uvSetValue = uvSet < uvSetValues.size() ? uvSetValues[uvSet][i] : Vector2{ 0.0f, 0.0f };
            out.uvSets[uvSet].push_back(uvSetValue.x);
            out.uvSets[uvSet].push_back(uvSetValue.y);
        }

        out.AddBounds(points[i]);
        if (refs[i].node)
        {
            ExpandSceneNodeBounds(out, refs[i].node, points[i]);
        }
    }
}

int GetOrAddUvSetIndex(MeshBuilder& out, const std::string& name)
{
    const std::string uvSetName = name.empty() ? "UV Set" : name;
    for (int i = 0; i < static_cast<int>(out.uvSetNames.size()); ++i)
    {
        if (out.uvSetNames[static_cast<size_t>(i)] == uvSetName) return i;
    }

    out.uvSetNames.push_back(uvSetName);
    out.uvSets.push_back(std::vector<float>(static_cast<size_t>(out.VertexCount()) * 2, 0.0f));
    return static_cast<int>(out.uvSets.size()) - 1;
}

int GetOrAddMaterialIndex(MeshBuilder& out, const std::string& name)
{
    const std::string materialName = name.empty() ? "Default" : name;
    const auto found = out.materialNameToIndex.find(materialName);
    if (found != out.materialNameToIndex.end()) return found->second;

    const int index = static_cast<int>(out.materialNames.size());
    out.materialNames.push_back(materialName);
    out.materialNameToIndex[materialName] = index;
    return index;
}

std::string GetNodeMaterialName(FbxNode* node, int localMaterialIndex)
{
    FbxSurfaceMaterial* material = node && localMaterialIndex >= 0 && localMaterialIndex < node->GetMaterialCount() ? node->GetMaterial(localMaterialIndex) : nullptr;
    return material && material->GetName() && material->GetName()[0] ? material->GetName() : "Default";
}

int GetPolygonMaterialLocalIndex(FbxMesh* mesh, int polygon)
{
    if (!mesh || mesh->GetElementMaterialCount() <= 0) return 0;

    FbxGeometryElementMaterial* materialElement = mesh->GetElementMaterial(0);
    if (!materialElement) return 0;

    if (materialElement->GetMappingMode() == FbxGeometryElement::eByPolygon)
    {
        if (materialElement->GetReferenceMode() == FbxGeometryElement::eIndexToDirect ||
            materialElement->GetReferenceMode() == FbxGeometryElement::eIndex)
        {
            return polygon < materialElement->GetIndexArray().GetCount() ? materialElement->GetIndexArray().GetAt(polygon) : 0;
        }
        return 0;
    }

    if (materialElement->GetReferenceMode() == FbxGeometryElement::eIndexToDirect ||
        materialElement->GetReferenceMode() == FbxGeometryElement::eIndex)
    {
        return materialElement->GetIndexArray().GetCount() > 0 ? materialElement->GetIndexArray().GetAt(0) : 0;
    }

    return 0;
}

FbxVector4 TransformVector(const FbxAMatrix& matrix, FbxVector4 vector);

void AppendMesh(FbxNode* node, FbxMesh* mesh, MeshBuilder& out)
{
    const int polygonCount = mesh->GetPolygonCount();
    if (polygonCount <= 0) return;
    const int meshVertexStart = out.VertexCount();
    const int meshPolygonVertexStart = static_cast<int>(out.polygonVertexGlobalIndices.size());
    const auto nodeIndexFound = out.nodeToIndex.find(node);
    const int sceneNodeIndex = nodeIndexFound != out.nodeToIndex.end() ? nodeIndexFound->second : -1;

    FbxStringList uvSetNames;
    mesh->GetUVSetNames(uvSetNames);
    const char* uvSetName = uvSetNames.GetCount() > 0 ? uvSetNames.GetStringAt(0) : nullptr;
    std::vector<int> meshUvSetIndices;
    meshUvSetIndices.reserve(static_cast<size_t>(uvSetNames.GetCount()));
    for (int i = 0; i < uvSetNames.GetCount(); ++i)
    {
        meshUvSetIndices.push_back(GetOrAddUvSetIndex(out, uvSetNames.GetStringAt(i)));
    }
    const FbxVector4* controlPoints = mesh->GetControlPoints();

    const FbxAMatrix nodeGlobal = node->EvaluateGlobalTransform();
    const FbxAMatrix geometry = GetNodeGeometryTransform(node);
    const FbxAMatrix meshTransform = nodeGlobal * geometry;
    const FbxAMatrix normalTransform = meshTransform.Inverse().Transpose();
    const SkinBindData skinBind = BuildSkinBindData(node, mesh);
    bool meshHadNormals = true;
    bool meshHadUvs = uvSetName != nullptr;
    int importedPolygonCount = 0;
    int degenerateTriangles = 0;

    for (int polygon = 0; polygon < polygonCount; ++polygon)
    {
        if (sceneNodeIndex >= 0) out.nodes[static_cast<size_t>(sceneNodeIndex)].sourcePolygonTriangleCounts.push_back(0);
        const int polygonSize = mesh->GetPolygonSize(polygon);
        if (polygonSize < 3) continue;

        std::vector<Vector3> polygonPoints(static_cast<size_t>(polygonSize));
        std::vector<Vector3> polygonNormals(static_cast<size_t>(polygonSize));
        std::vector<Vector2> polygonUvs(static_cast<size_t>(polygonSize));
        std::vector<RenderVertexRef> polygonRefs(static_cast<size_t>(polygonSize));
        std::vector<std::vector<Vector2>> polygonUvSetValues(out.uvSets.size(), std::vector<Vector2>(static_cast<size_t>(polygonSize)));
        bool polygonHasNormals = true;
        bool polygonHasUvs = uvSetName != nullptr;
        bool validPolygon = true;
        const int localMaterialIndex = GetPolygonMaterialLocalIndex(mesh, polygon);
        const int materialIndex = GetOrAddMaterialIndex(out, GetNodeMaterialName(node, localMaterialIndex));

        for (int vertex = 0; vertex < polygonSize; ++vertex)
        {
            const int controlPointIndex = mesh->GetPolygonVertex(polygon, vertex);
            if (controlPointIndex < 0 || controlPointIndex >= mesh->GetControlPointsCount())
            {
                validPolygon = false;
                break;
            }

            polygonRefs[static_cast<size_t>(vertex)] = RenderVertexRef{ node, mesh, controlPointIndex, FbxVector4(0.0, 1.0, 0.0, 0.0), false };
            polygonPoints[static_cast<size_t>(vertex)] = ToVector3(meshTransform.MultT(controlPoints[controlPointIndex]));

            FbxVector4 normal;
            if (mesh->GetPolygonVertexNormal(polygon, vertex, normal))
            {
                normal[3] = 0.0;
                polygonRefs[static_cast<size_t>(vertex)].localNormal = normal;
                polygonRefs[static_cast<size_t>(vertex)].hasNormal = true;
                polygonNormals[static_cast<size_t>(vertex)] = ToVector3(TransformVector(normalTransform, normal));
            }
            else
            {
                polygonHasNormals = false;
            }

            if (uvSetName)
            {
                FbxVector2 uv;
                bool unmapped = false;
                if (mesh->GetPolygonVertexUV(polygon, vertex, uvSetName, uv, unmapped) && !unmapped)
                {
                    polygonUvs[static_cast<size_t>(vertex)] = ToVector2(uv);
                }
                else
                {
                    polygonHasUvs = false;
                }
            }

            for (int uvSet = 0; uvSet < uvSetNames.GetCount(); ++uvSet)
            {
                const int globalUvSet = uvSet < static_cast<int>(meshUvSetIndices.size()) ? meshUvSetIndices[static_cast<size_t>(uvSet)] : -1;
                if (globalUvSet < 0 || globalUvSet >= static_cast<int>(polygonUvSetValues.size())) continue;

                FbxVector2 uv;
                bool unmapped = false;
                if (mesh->GetPolygonVertexUV(polygon, vertex, uvSetNames.GetStringAt(uvSet), uv, unmapped) && !unmapped)
                {
                    polygonUvSetValues[static_cast<size_t>(globalUvSet)][static_cast<size_t>(vertex)] = ToVector2(uv);
                }
            }
        }

        if (!validPolygon) continue;
        ++importedPolygonCount;

        if (!polygonHasNormals)
        {
            meshHadNormals = false;
        }
        if (!polygonHasUvs)
        {
            meshHadUvs = false;
        }

        std::vector<int> polygonCornerVertices(static_cast<size_t>(polygonSize), -1);
        if (sceneNodeIndex >= 0) out.nodes[static_cast<size_t>(sceneNodeIndex)].sourcePolygonTriangleCounts.back() = polygonSize - 2;
        for (int fan = 1; fan + 1 < polygonSize; ++fan)
        {
            const int sourceCorners[3] = { 0, fan, fan + 1 };
            Vector3 points[3]{};
            Vector3 normals[3]{};
            Vector2 uvs[3]{};
            RenderVertexRef refs[3]{};
            std::vector<std::array<Vector2, 3>> uvSetValues(out.uvSets.size());
            for (int corner = 0; corner < 3; ++corner)
            {
                const int sourceCorner = sourceCorners[corner];
                points[corner] = polygonPoints[static_cast<size_t>(sourceCorner)];
                normals[corner] = polygonNormals[static_cast<size_t>(sourceCorner)];
                uvs[corner] = polygonUvs[static_cast<size_t>(sourceCorner)];
                refs[corner] = polygonRefs[static_cast<size_t>(sourceCorner)];
                for (size_t uvSet = 0; uvSet < uvSetValues.size(); ++uvSet)
                {
                    uvSetValues[uvSet][corner] = polygonUvSetValues[uvSet][static_cast<size_t>(sourceCorner)];
                }
            }

            const Vector3 e0 = Vector3Subtract(points[1], points[0]);
            const Vector3 e1 = Vector3Subtract(points[2], points[0]);
            if (Vector3LengthSqr(Vector3CrossProduct(e0, e1)) <= 0.000000000001f)
            {
                ++degenerateTriangles;
            }

            const int renderVertexStart = out.VertexCount();
            AddTriangle(out, points, normals, uvs, uvSetValues, refs, skinBind, materialIndex, polygonHasNormals, polygonHasUvs);
            for (int corner = 0; corner < 3; ++corner)
            {
                const int sourceCorner = sourceCorners[corner];
                if (polygonCornerVertices[static_cast<size_t>(sourceCorner)] < 0)
                {
                    polygonCornerVertices[static_cast<size_t>(sourceCorner)] = renderVertexStart + corner;
                }
            }
        }

        if (sceneNodeIndex >= 0)
        {
            for (int vertex = 0; vertex < polygonSize; ++vertex)
            {
                out.polygonVertexGlobalIndices.push_back(polygonCornerVertices[static_cast<size_t>(vertex)]);

                const int a = polygonCornerVertices[static_cast<size_t>(vertex)];
                const int b = polygonCornerVertices[static_cast<size_t>((vertex + 1) % polygonSize)];
                if (a >= 0 && b >= 0 && a != b)
                {
                    out.polygonEdges.push_back(MeshEdge{ a, b, sceneNodeIndex });
                }
            }
        }
    }

    const int meshVertexCount = out.VertexCount() - meshVertexStart;
    out.uvSetPresence.resize(out.uvSets.size());
    for (size_t set = 0; set < out.uvSets.size(); ++set)
    {
        out.uvSetPresence[set].resize(out.VertexCount(), 0);
        if (std::find(meshUvSetIndices.begin(), meshUvSetIndices.end(), static_cast<int>(set)) != meshUvSetIndices.end())
            std::fill(out.uvSetPresence[set].begin() + meshVertexStart, out.uvSetPresence[set].end(), 1);
    }
    if (meshVertexCount > 0 && sceneNodeIndex >= 0)
    {
        const int meshPolygonVertexCount = static_cast<int>(out.polygonVertexGlobalIndices.size()) - meshPolygonVertexStart;
        SceneNode& sceneNode = out.nodes[static_cast<size_t>(sceneNodeIndex)];
        if (sceneNode.meshVertexStart < 0)
        {
            sceneNode.meshVertexStart = meshVertexStart;
        }
        if (sceneNode.meshPolygonVertexStart < 0)
        {
            sceneNode.meshPolygonVertexStart = meshPolygonVertexStart;
        }
        sceneNode.meshVertexCount += meshVertexCount;
        sceneNode.meshPolygonVertexCount += meshPolygonVertexCount;
        sceneNode.meshTriangleCount += meshVertexCount / 3;
        sceneNode.meshPolygonCount += importedPolygonCount;
        sceneNode.meshHadNormals = sceneNode.meshHadNormals && meshHadNormals;
        sceneNode.meshHadUvs = sceneNode.meshHadUvs && meshHadUvs;
        sceneNode.meshHasSkin = sceneNode.meshHasSkin || skinBind.hasSkin;
        sceneNode.degenerateTriangleCount += degenerateTriangles;

        if (skinBind.hasSkin)
        {
            for (int vertex = meshVertexStart; vertex < out.VertexCount(); ++vertex)
            {
                if (vertex < 0 || vertex >= static_cast<int>(out.skinnedVertices.size())) continue;

                const SkinnedVertex& skinned = out.skinnedVertices[static_cast<size_t>(vertex)];
                if (skinned.influences.empty())
                {
                    ++sceneNode.missingSkinWeightCount;
                    continue;
                }

                float weightSum = 0.0f;
                bool invalidWeight = false;
                for (const SkinnedVertexInfluence& influence : skinned.influences)
                {
                    weightSum += influence.weight;
                    if (!std::isfinite(influence.weight) || influence.weight < 0.0f || influence.weight > 1.0f)
                    {
                        invalidWeight = true;
                    }
                }
                if (invalidWeight || std::fabs(weightSum - 1.0f) > 0.01f)
                {
                    ++sceneNode.badSkinWeightCount;
                }
            }
        }

        if (sceneNode.materialName.empty())
        {
            FbxSurfaceMaterial* material = node->GetMaterialCount() > 0 ? node->GetMaterial(0) : nullptr;
            sceneNode.materialName = material && material->GetName() && material->GetName()[0] ? material->GetName() : "None";
            if (node->GetMaterialCount() > 1)
            {
                sceneNode.materialName += " (+";
                sceneNode.materialName += std::to_string(node->GetMaterialCount() - 1);
                sceneNode.materialName += ")";
            }
        }
    }
}

FbxVector4 AddScaledVector(FbxVector4 value, const FbxVector4& add, double scale)
{
    value[0] += add[0] * scale;
    value[1] += add[1] * scale;
    value[2] += add[2] * scale;
    value[3] += add[3] * scale;
    return value;
}

FbxVector4 TransformVector(const FbxAMatrix& matrix, FbxVector4 vector)
{
    vector[3] = 0.0;
    const FbxVector4 origin = matrix.MultT(FbxVector4(0.0, 0.0, 0.0, 1.0));
    const FbxVector4 end = matrix.MultT(FbxVector4(vector[0], vector[1], vector[2], 1.0));
    return FbxVector4(end[0] - origin[0], end[1] - origin[1], end[2] - origin[2], 0.0);
}

SkinBindData BuildSkinBindData(FbxNode* node, FbxMesh* mesh)
{
    SkinBindData data;
    if (!mesh) return data;

    const int controlPointCount = mesh->GetControlPointsCount();
    data.influences.resize(static_cast<size_t>(controlPointCount));

    const int skinCount = mesh->GetDeformerCount(FbxDeformer::eSkin);
    for (int skinIndex = 0; skinIndex < skinCount; ++skinIndex)
    {
        FbxSkin* fbxSkin = static_cast<FbxSkin*>(mesh->GetDeformer(skinIndex, FbxDeformer::eSkin));
        if (!fbxSkin) continue;
        data.hasSkin = true;

        for (int clusterIndex = 0; clusterIndex < fbxSkin->GetClusterCount(); ++clusterIndex)
        {
            FbxCluster* cluster = fbxSkin->GetCluster(clusterIndex);
            FbxNode* link = cluster ? cluster->GetLink() : nullptr;
            if (!cluster || !link || !link->GetName() || !link->GetName()[0]) continue;

            FbxAMatrix referenceGlobalInit;
            cluster->GetTransformMatrix(referenceGlobalInit);
            if (node)
            {
                referenceGlobalInit *= GetNodeGeometryTransform(node);
            }

            FbxAMatrix bindLink;
            cluster->GetTransformLinkMatrix(bindLink);
            const FbxAMatrix bindFromControlPoint = bindLink.Inverse() * referenceGlobalInit;

            const int* indices = cluster->GetControlPointIndices();
            const double* weights = cluster->GetControlPointWeights();
            const int indexCount = cluster->GetControlPointIndicesCount();
            for (int i = 0; i < indexCount; ++i)
            {
                const int controlPointIndex = indices[i];
                if (controlPointIndex < 0 || controlPointIndex >= controlPointCount) continue;
                if (weights[i] == 0.0) continue;

                data.influences[static_cast<size_t>(controlPointIndex)].push_back(ControlPointBindInfluence{
                    link->GetName(),
                    static_cast<float>(weights[i]),
                    bindFromControlPoint
                });
            }
        }
    }

    return data;
}

FbxAMatrix GetClusterDeformationMatrix(FbxNode* meshNode, FbxCluster* cluster, const FbxTime& time)
{
    FbxAMatrix referenceGlobalInit;
    FbxAMatrix referenceGlobalCurrent = meshNode->EvaluateGlobalTransform(time);
    FbxAMatrix clusterGlobalInit;
    FbxAMatrix clusterGlobalCurrent;
    FbxAMatrix referenceGeometry = GetNodeGeometryTransform(meshNode);

    cluster->GetTransformMatrix(referenceGlobalInit);
    referenceGlobalInit *= referenceGeometry;

    cluster->GetTransformLinkMatrix(clusterGlobalInit);
    clusterGlobalCurrent = cluster->GetLink()->EvaluateGlobalTransform(time);

    const FbxAMatrix clusterRelativeInit = clusterGlobalInit.Inverse() * referenceGlobalInit;
    const FbxAMatrix clusterRelativeCurrentInverse = referenceGlobalCurrent.Inverse() * clusterGlobalCurrent;
    return clusterRelativeCurrentInverse * clusterRelativeInit;
}

SkinSample BuildSkinSample(FbxNode* node, FbxMesh* mesh, const FbxTime& time)
{
    const int controlPointCount = mesh->GetControlPointsCount();
    SkinSample skin;
    skin.influences.resize(static_cast<size_t>(controlPointCount));
    skin.weights.assign(static_cast<size_t>(controlPointCount), 0.0);

    const int skinCount = mesh->GetDeformerCount(FbxDeformer::eSkin);
    if (skinCount <= 0)
    {
        return skin;
    }

    for (int skinIndex = 0; skinIndex < skinCount; ++skinIndex)
    {
        FbxSkin* fbxSkin = static_cast<FbxSkin*>(mesh->GetDeformer(skinIndex, FbxDeformer::eSkin));
        if (!fbxSkin) continue;

        for (int clusterIndex = 0; clusterIndex < fbxSkin->GetClusterCount(); ++clusterIndex)
        {
            FbxCluster* cluster = fbxSkin->GetCluster(clusterIndex);
            if (!cluster || !cluster->GetLink()) continue;

            if (!skin.hasCluster)
            {
                skin.linkMode = cluster->GetLinkMode();
                skin.hasCluster = true;
            }

            const FbxAMatrix deformation = GetClusterDeformationMatrix(node, cluster, time);
            const int* indices = cluster->GetControlPointIndices();
            const double* clusterWeights = cluster->GetControlPointWeights();
            const int indexCount = cluster->GetControlPointIndicesCount();

            for (int i = 0; i < indexCount; ++i)
            {
                const int controlPointIndex = indices[i];
                if (controlPointIndex < 0 || controlPointIndex >= controlPointCount) continue;

                const double weight = clusterWeights[i];
                if (weight == 0.0) continue;

                const size_t outIndex = static_cast<size_t>(controlPointIndex);
                skin.influences[outIndex].push_back(ControlPointInfluence{ deformation, weight });
                skin.weights[outIndex] += weight;
            }
        }
    }

    return skin;
}

std::vector<FbxVector4> DeformControlPointsLocal(FbxMesh* mesh, const SkinSample& skin)
{
    const int controlPointCount = mesh->GetControlPointsCount();
    const FbxVector4* controlPoints = mesh->GetControlPoints();
    std::vector<FbxVector4> deformed(static_cast<size_t>(controlPointCount));

    if (!skin.hasCluster)
    {
        return std::vector<FbxVector4>(controlPoints, controlPoints + controlPointCount);
    }

    for (int i = 0; i < controlPointCount; ++i)
    {
        const size_t outIndex = static_cast<size_t>(i);
        const double weight = skin.weights[outIndex];
        deformed[outIndex] = FbxVector4(0.0, 0.0, 0.0, 0.0);

        for (const ControlPointInfluence& influence : skin.influences[outIndex])
        {
            const FbxVector4 transformed = influence.deformation.MultT(controlPoints[i]);
            deformed[outIndex] = AddScaledVector(deformed[outIndex], transformed, influence.weight);
        }

        if (weight == 0.0)
        {
            deformed[outIndex] = controlPoints[i];
        }
        else if (skin.linkMode == FbxCluster::eNormalize)
        {
            deformed[outIndex][0] /= weight;
            deformed[outIndex][1] /= weight;
            deformed[outIndex][2] /= weight;
            deformed[outIndex][3] /= weight;
        }
        else if (skin.linkMode == FbxCluster::eTotalOne && weight < 1.0)
        {
            deformed[outIndex] = AddScaledVector(deformed[outIndex], controlPoints[i], 1.0 - weight);
        }
    }

    return deformed;
}

FbxVector4 DeformLocalNormal(const SkinSample& skin, int controlPointIndex, FbxVector4 normal)
{
    normal[3] = 0.0;
    if (!skin.hasCluster || controlPointIndex < 0 || controlPointIndex >= static_cast<int>(skin.weights.size()))
    {
        return normal;
    }

    const size_t outIndex = static_cast<size_t>(controlPointIndex);
    const double weight = skin.weights[outIndex];
    if (weight == 0.0)
    {
        return normal;
    }

    FbxVector4 deformed(0.0, 0.0, 0.0, 0.0);
    for (const ControlPointInfluence& influence : skin.influences[outIndex])
    {
        deformed = AddScaledVector(deformed, TransformVector(influence.deformation, normal), influence.weight);
    }

    if (skin.linkMode == FbxCluster::eNormalize)
    {
        deformed[0] /= weight;
        deformed[1] /= weight;
        deformed[2] /= weight;
    }
    else if (skin.linkMode == FbxCluster::eTotalOne && weight < 1.0)
    {
        deformed = AddScaledVector(deformed, normal, 1.0 - weight);
    }

    deformed[3] = 0.0;
    return deformed;
}

void RecomputeMeshFrameNormals(MeshFrame& frame)
{
    frame.normals.assign(frame.vertices.size(), 0.0f);

    for (size_t i = 0; i + 8 < frame.vertices.size(); i += 9)
    {
        const Vector3 p0{ frame.vertices[i], frame.vertices[i + 1], frame.vertices[i + 2] };
        const Vector3 p1{ frame.vertices[i + 3], frame.vertices[i + 4], frame.vertices[i + 5] };
        const Vector3 p2{ frame.vertices[i + 6], frame.vertices[i + 7], frame.vertices[i + 8] };
        const Vector3 normal = NormalizeOrFallback(Vector3CrossProduct(Vector3Subtract(p1, p0), Vector3Subtract(p2, p0)), Vector3{ 0.0f, 1.0f, 0.0f });

        for (size_t v = 0; v < 3; ++v)
        {
            const size_t normalIndex = i + v * 3;
            frame.normals[normalIndex] = normal.x;
            frame.normals[normalIndex + 1] = normal.y;
            frame.normals[normalIndex + 2] = normal.z;
        }
    }
}

MeshFrame SampleMeshFrame(const MeshBuilder& builder, const FbxTime& time)
{
    MeshFrame frame;
    frame.vertices.reserve(builder.vertices.size());
    frame.normals.reserve(builder.normals.size());

    struct MeshSampleCache
    {
        std::vector<FbxVector4> localPoints;
        SkinSample skin;
        FbxAMatrix meshTransform;
        FbxAMatrix normalTransform;
    };

    std::unordered_map<FbxNode*, MeshSampleCache> sampleByNode;

    for (const RenderVertexRef& ref : builder.renderVertices)
    {
        if (!ref.node || !ref.mesh)
        {
            frame.vertices.push_back(0.0f);
            frame.vertices.push_back(0.0f);
            frame.vertices.push_back(0.0f);
            continue;
        }

        auto found = sampleByNode.find(ref.node);
        if (found == sampleByNode.end())
        {
            MeshSampleCache cache;
            cache.skin = BuildSkinSample(ref.node, ref.mesh, time);
            cache.localPoints = DeformControlPointsLocal(ref.mesh, cache.skin);
            cache.meshTransform = ref.node->EvaluateGlobalTransform(time) * GetNodeGeometryTransform(ref.node);
            cache.normalTransform = cache.meshTransform.Inverse().Transpose();
            found = sampleByNode.emplace(ref.node, std::move(cache)).first;
        }

        const MeshSampleCache& cache = found->second;
        const std::vector<FbxVector4>& localPoints = cache.localPoints;
        if (ref.controlPointIndex < 0 || ref.controlPointIndex >= static_cast<int>(localPoints.size()))
        {
            frame.vertices.push_back(0.0f);
            frame.vertices.push_back(0.0f);
            frame.vertices.push_back(0.0f);
            continue;
        }

        const Vector3 point = ToVector3(cache.meshTransform.MultT(localPoints[static_cast<size_t>(ref.controlPointIndex)]));
        frame.vertices.push_back(point.x);
        frame.vertices.push_back(point.y);
        frame.vertices.push_back(point.z);

        if (ref.hasNormal)
        {
            FbxVector4 normal = DeformLocalNormal(cache.skin, ref.controlPointIndex, ref.localNormal);
            const Vector3 sampledNormal = NormalizeOrFallback(ToVector3(TransformVector(cache.normalTransform, normal)), Vector3{ 0.0f, 1.0f, 0.0f });
            frame.normals.push_back(sampledNormal.x);
            frame.normals.push_back(sampledNormal.y);
            frame.normals.push_back(sampledNormal.z);
        }
    }

    if (frame.normals.size() != frame.vertices.size())
    {
        RecomputeMeshFrameNormals(frame);
    }
    return frame;
}

bool IsSkeletonNode(const FbxNode* node)
{
    if (!node) return false;

    const int attributeCount = node->GetNodeAttributeCount();
    for (int i = 0; i < attributeCount; ++i)
    {
        const FbxNodeAttribute* attribute = node->GetNodeAttributeByIndex(i);
        if (attribute && attribute->GetAttributeType() == FbxNodeAttribute::eSkeleton)
        {
            return true;
        }
    }

    return false;
}

void AppendBone(FbxNode* node, MeshBuilder& out)
{
    FbxNode* parent = node->GetParent();
    if (!IsSkeletonNode(node) || !IsSkeletonNode(parent)) return;

    const Vector3 parentPosition = ToVector3(parent->EvaluateGlobalTransform().GetT());
    const Vector3 nodePosition = ToVector3(node->EvaluateGlobalTransform().GetT());
    const auto parentIndex = out.nodeToIndex.find(parent);
    const auto nodeIndex = out.nodeToIndex.find(node);
    const int parentSceneIndex = parentIndex != out.nodeToIndex.end() ? parentIndex->second : -1;
    const int nodeSceneIndex = nodeIndex != out.nodeToIndex.end() ? nodeIndex->second : -1;

    out.bones.push_back(BoneSegment{ parentPosition, nodePosition, parentSceneIndex, nodeSceneIndex });
    out.AddBounds(parentPosition);
    out.AddBounds(nodePosition);
    ExpandSceneNodeBounds(out, parent, parentPosition);
    ExpandSceneNodeBounds(out, node, nodePosition);
}

void CollectSkeletonNodes(FbxNode* node, std::vector<FbxNode*>& skeletonNodes)
{
    if (!node) return;

    if (IsSkeletonNode(node))
    {
        skeletonNodes.push_back(node);
    }

    for (int i = 0; i < node->GetChildCount(); ++i)
    {
        CollectSkeletonNodes(node->GetChild(i), skeletonNodes);
    }
}

void CollectSkinBindMatrices(FbxNode* node, std::unordered_map<FbxNode*, FbxAMatrix>& bindMatrices)
{
    if (!node) return;

    for (int attributeIndex = 0; attributeIndex < node->GetNodeAttributeCount(); ++attributeIndex)
    {
        FbxNodeAttribute* attribute = node->GetNodeAttributeByIndex(attributeIndex);
        if (!attribute || attribute->GetAttributeType() != FbxNodeAttribute::eMesh) continue;

        FbxMesh* mesh = static_cast<FbxMesh*>(attribute);
        for (int skinIndex = 0; skinIndex < mesh->GetDeformerCount(FbxDeformer::eSkin); ++skinIndex)
        {
            FbxSkin* skin = static_cast<FbxSkin*>(mesh->GetDeformer(skinIndex, FbxDeformer::eSkin));
            if (!skin) continue;

            for (int clusterIndex = 0; clusterIndex < skin->GetClusterCount(); ++clusterIndex)
            {
                FbxCluster* cluster = skin->GetCluster(clusterIndex);
                FbxNode* link = cluster ? cluster->GetLink() : nullptr;
                if (!link) continue;

                FbxAMatrix linkBindMatrix;
                cluster->GetTransformLinkMatrix(linkBindMatrix);
                bindMatrices[link] = linkBindMatrix;
            }
        }
    }

    for (int childIndex = 0; childIndex < node->GetChildCount(); ++childIndex)
    {
        CollectSkinBindMatrices(node->GetChild(childIndex), bindMatrices);
    }
}

FbxAMatrix GetBindOrEvaluatedGlobal(FbxNode* node, const std::unordered_map<FbxNode*, FbxAMatrix>& bindMatrices)
{
    const auto found = bindMatrices.find(node);
    if (found != bindMatrices.end())
    {
        return found->second;
    }

    return node->EvaluateGlobalTransform();
}

void RebuildBindSkeleton(FbxNode* node, MeshBuilder& out, const std::unordered_map<FbxNode*, FbxAMatrix>& bindMatrices)
{
    if (!node) return;

    if (IsSkeletonNode(node))
    {
        const auto nodeIndex = out.nodeToIndex.find(node);
        if (nodeIndex != out.nodeToIndex.end())
        {
            const FbxAMatrix bindOrEvaluated = GetBindOrEvaluatedGlobal(node, bindMatrices);
            SceneNode& sceneNode = out.nodes[static_cast<size_t>(nodeIndex->second)];
            SetSceneNodeTransform(sceneNode, bindOrEvaluated);
            sceneNode.hasSkinBindPose = bindMatrices.find(node) != bindMatrices.end();
            out.bonePoses.push_back(MakeBonePose(bindOrEvaluated, nodeIndex->second));
        }

        FbxNode* parent = node->GetParent();
        if (IsSkeletonNode(parent))
        {
            const Vector3 parentPosition = ToVector3(GetBindOrEvaluatedGlobal(parent, bindMatrices).GetT());
            const Vector3 nodePosition = ToVector3(GetBindOrEvaluatedGlobal(node, bindMatrices).GetT());
            const auto parentIndex = out.nodeToIndex.find(parent);
            const int parentSceneIndex = parentIndex != out.nodeToIndex.end() ? parentIndex->second : -1;
            const int nodeSceneIndex = nodeIndex != out.nodeToIndex.end() ? nodeIndex->second : -1;

            out.bones.push_back(BoneSegment{ parentPosition, nodePosition, parentSceneIndex, nodeSceneIndex });
            out.AddBounds(parentPosition);
            out.AddBounds(nodePosition);
            ExpandSceneNodeBounds(out, parent, parentPosition);
            ExpandSceneNodeBounds(out, node, nodePosition);
        }
    }

    for (int childIndex = 0; childIndex < node->GetChildCount(); ++childIndex)
    {
        RebuildBindSkeleton(node->GetChild(childIndex), out, bindMatrices);
    }
}

void RebuildSkinBindDataForAppPose(MeshBuilder& out)
{
    std::unordered_map<std::string, FbxAMatrix> inverseBindPosesByName;
    for (const BonePose& pose : out.bonePoses)
    {
        if (pose.node < 0 || pose.node >= static_cast<int>(out.nodes.size())) continue;
        inverseBindPosesByName[out.nodes[static_cast<size_t>(pose.node)].name] = MatrixFromPose(pose).Inverse();
    }

    for (SkinnedVertex& vertex : out.skinnedVertices)
    {
        const FbxVector4 bindPosition(vertex.bindPosition.x, vertex.bindPosition.y, vertex.bindPosition.z, 1.0);
        const FbxVector4 bindNormal(vertex.bindNormal.x, vertex.bindNormal.y, vertex.bindNormal.z, 0.0);
        for (SkinnedVertexInfluence& influence : vertex.influences)
        {
            const auto inverseBindPose = inverseBindPosesByName.find(influence.boneName);
            if (inverseBindPose == inverseBindPosesByName.end()) continue;

            const FbxVector4 positionInBone = inverseBindPose->second.MultT(bindPosition);
            const FbxVector4 normalInBone = TransformVector(inverseBindPose->second, bindNormal);
            influence.bindPositionInBone = ToVector3(positionInBone);
            influence.bindNormalInBone = NormalizeOrFallback(ToVector3(normalInBone), vertex.bindNormal);
        }
    }
}

BoneFrame SampleBoneFrame(const MeshBuilder& builder, const std::vector<FbxNode*>& skeletonNodes, const FbxTime& time, double clipStart)
{
    BoneFrame frame;
    frame.time = static_cast<float>(time.GetSecondDouble() - clipStart);

    for (FbxNode* node : skeletonNodes)
    {
        const auto poseNodeIndex = builder.nodeToIndex.find(node);
        if (poseNodeIndex != builder.nodeToIndex.end())
        {
            frame.poses.push_back(MakeBonePose(node->EvaluateGlobalTransform(time), poseNodeIndex->second));
        }

        FbxNode* parent = node->GetParent();
        if (!IsSkeletonNode(parent)) continue;

        const Vector3 parentPosition = ToVector3(parent->EvaluateGlobalTransform(time).GetT());
        const Vector3 nodePosition = ToVector3(node->EvaluateGlobalTransform(time).GetT());
        const auto parentIndex = builder.nodeToIndex.find(parent);
        const auto nodeIndex = builder.nodeToIndex.find(node);
        const int parentSceneIndex = parentIndex != builder.nodeToIndex.end() ? parentIndex->second : -1;
        const int nodeSceneIndex = nodeIndex != builder.nodeToIndex.end() ? nodeIndex->second : -1;
        frame.bones.push_back(BoneSegment{ parentPosition, nodePosition, parentSceneIndex, nodeSceneIndex });
    }

    return frame;
}

void AddAnimationBounds(const AnimationClip& clip, MeshBuilder& out)
{
    for (const BoneFrame& frame : clip.frames)
    {
        for (const BoneSegment& bone : frame.bones)
        {
            out.AddBounds(bone.start);
            out.AddBounds(bone.end);
        }
    }
}

void SampleAnimations(FbxScene* scene, MeshBuilder& out)
{
    std::vector<FbxNode*> skeletonNodes;
    CollectSkeletonNodes(scene->GetRootNode(), skeletonNodes);
    if (skeletonNodes.empty()) return;

    FbxArray<FbxString*> stackNames;
    scene->FillAnimStackNameArray(stackNames);

    constexpr double kSampleRate = 15.0;
    constexpr int kMaxFramesPerClip = 240;

    for (int stackIndex = 0; stackIndex < stackNames.GetCount(); ++stackIndex)
    {
        const char* stackName = stackNames[stackIndex]->Buffer();
        FbxAnimStack* stack = scene->FindMember<FbxAnimStack>(stackName);
        if (!stack) continue;

        scene->SetCurrentAnimationStack(stack);

        FbxTimeSpan timeSpan = stack->GetLocalTimeSpan();
        FbxTime start = timeSpan.GetStart();
        FbxTime stop = timeSpan.GetStop();

        if (stop <= start)
        {
            scene->GetGlobalSettings().GetTimelineDefaultTimeSpan(timeSpan);
            start = timeSpan.GetStart();
            stop = timeSpan.GetStop();
        }

        if (stop <= start) continue;

        const double startSeconds = start.GetSecondDouble();
        const double duration = stop.GetSecondDouble() - startSeconds;
        if (duration <= 0.0) continue;

        AnimationClip clip;
        clip.name = stackName && stackName[0] ? stackName : "Animation";
        clip.duration = static_cast<float>(duration);

        const int frameCount = std::max(2, std::min(kMaxFramesPerClip, static_cast<int>(std::ceil(duration * kSampleRate)) + 1));
        clip.frames.reserve(static_cast<size_t>(frameCount));
        clip.meshFrames.reserve(static_cast<size_t>(frameCount));

        for (int frameIndex = 0; frameIndex < frameCount; ++frameIndex)
        {
            const double alpha = frameCount > 1 ? static_cast<double>(frameIndex) / static_cast<double>(frameCount - 1) : 0.0;
            FbxTime sampleTime;
            sampleTime.SetSecondDouble(startSeconds + duration * alpha);
            clip.frames.push_back(SampleBoneFrame(out, skeletonNodes, sampleTime, startSeconds));
            clip.meshFrames.push_back(SampleMeshFrame(out, sampleTime));
        }

        AddAnimationBounds(clip, out);
        out.animations.push_back(std::move(clip));
    }

    FbxArrayDelete(stackNames);
}

FbxAMatrix MatrixFromPose(const BonePose& pose)
{
    FbxAMatrix matrix;
    matrix.SetIdentity();
    matrix.SetRow(0, FbxVector4(pose.axisX.x * pose.scale.x, pose.axisX.y * pose.scale.x, pose.axisX.z * pose.scale.x, 0.0));
    matrix.SetRow(1, FbxVector4(pose.axisY.x * pose.scale.y, pose.axisY.y * pose.scale.y, pose.axisY.z * pose.scale.y, 0.0));
    matrix.SetRow(2, FbxVector4(pose.axisZ.x * pose.scale.z, pose.axisZ.y * pose.scale.z, pose.axisZ.z * pose.scale.z, 0.0));
    matrix.SetRow(3, FbxVector4(pose.position.x, pose.position.y, pose.position.z, 1.0));
    return matrix;
}

const BonePose* FindFramePose(const BoneFrame& frame, int nodeIndex)
{
    for (const BonePose& pose : frame.poses)
    {
        if (pose.node == nodeIndex) return &pose;
    }
    return nullptr;
}

FbxAMatrix MatrixFromSceneNode(const SceneNode& node)
{
    BonePose pose;
    pose.position = node.position;
    pose.axisX = node.axisX;
    pose.axisY = node.axisY;
    pose.axisZ = node.axisZ;
    pose.rotation = node.rotation;
    pose.scale = node.scale;
    return MatrixFromPose(pose);
}

FbxVector4 ToFbxPoint(Vector3 value, double w = 1.0)
{
    return FbxVector4(value.x, value.y, value.z, w);
}

FbxDouble3 ToFbxDouble3(const FbxVector4& value)
{
    return FbxDouble3(value[0], value[1], value[2]);
}

void CollectSceneNodes(FbxNode* node, std::vector<FbxNode*>& nodes)
{
    if (!node) return;
    nodes.push_back(node);
    for (int i = 0; i < node->GetChildCount(); ++i)
    {
        CollectSceneNodes(node->GetChild(i), nodes);
    }
}

std::vector<FbxNode*> BuildSceneNodeIndex(FbxScene* scene)
{
    std::vector<FbxNode*> nodes;
    CollectSceneNodes(scene ? scene->GetRootNode() : nullptr, nodes);
    return nodes;
}

std::vector<FbxNode*> BuildMappedSceneNodes(const LoadedFbxModel& model, const std::vector<FbxNode*>& sceneNodes)
{
    std::vector<FbxNode*> mapped(model.nodes.size(), nullptr);
    if (sceneNodes.size() == model.nodes.size())
    {
        for (size_t i = 0; i < model.nodes.size(); ++i)
        {
            mapped[i] = sceneNodes[i];
        }
        return mapped;
    }

    std::vector<bool> used(sceneNodes.size(), false);
    auto findByName = [&](const std::string& name) -> FbxNode*
    {
        if (name.empty()) return nullptr;
        for (size_t i = 0; i < sceneNodes.size(); ++i)
        {
            FbxNode* node = sceneNodes[i];
            if (used[i] || !node || !node->GetName()) continue;
            if (name == node->GetName())
            {
                used[i] = true;
                return node;
            }
        }
        return nullptr;
    };

    for (size_t i = 0; i < model.nodes.size(); ++i)
    {
        const SceneNode& node = model.nodes[i];
        mapped[i] = findByName(node.sourceName);
        if (!mapped[i])
        {
            mapped[i] = findByName(node.name);
        }
    }

    return mapped;
}

bool IsDeletedModelNode(const std::vector<bool>& deletedNodes, int nodeIndex)
{
    return nodeIndex >= 0 &&
           nodeIndex < static_cast<int>(deletedNodes.size()) &&
           deletedNodes[static_cast<size_t>(nodeIndex)];
}

std::vector<int> BuildGlobalVertexMaterialMap(const LoadedFbxModel& model)
{
    const int vertexCount = static_cast<int>(model.bindVertices.size() / 3);
    std::vector<int> materialForVertex(static_cast<size_t>(vertexCount), 0);
    for (int meshIndex = 0; meshIndex < static_cast<int>(model.meshGlobalVertexIndices.size()); ++meshIndex)
    {
        int materialIndex = model.model.meshMaterial && meshIndex < model.model.meshCount ? model.model.meshMaterial[meshIndex] : meshIndex;
        materialIndex = std::max(0, std::min(materialIndex, std::max(0, static_cast<int>(model.materialNames.size()) - 1)));
        for (int globalVertex : model.meshGlobalVertexIndices[static_cast<size_t>(meshIndex)])
        {
            if (globalVertex >= 0 && globalVertex < vertexCount)
            {
                materialForVertex[static_cast<size_t>(globalVertex)] = materialIndex;
            }
        }
    }
    return materialForVertex;
}

bool IsDeletedModelSubtreeRoot(const LoadedFbxModel& model, const std::vector<bool>& deletedNodes, int nodeIndex)
{
    if (!IsDeletedModelNode(deletedNodes, nodeIndex)) return false;

    int parent = nodeIndex >= 0 && nodeIndex < static_cast<int>(model.nodes.size()) ? model.nodes[static_cast<size_t>(nodeIndex)].parent : -1;
    while (parent >= 0)
    {
        if (IsDeletedModelNode(deletedNodes, parent)) return false;
        parent = parent < static_cast<int>(model.nodes.size()) ? model.nodes[static_cast<size_t>(parent)].parent : -1;
    }
    return true;
}

FbxAMatrix GetEditedGlobalMatrix(const LoadedFbxModel& model, int nodeIndex)
{
    if (nodeIndex >= 0 && nodeIndex < static_cast<int>(model.nodes.size()))
    {
        return MatrixFromSceneNode(model.nodes[static_cast<size_t>(nodeIndex)]);
    }

    FbxAMatrix identity;
    identity.SetIdentity();
    return identity;
}

const BonePose* FindBindPoseByNode(const LoadedFbxModel& model, int nodeIndex)
{
    for (const BonePose& pose : model.bonePoses)
    {
        if (pose.node == nodeIndex) return &pose;
    }
    return nullptr;
}

FbxAMatrix GetEditedBindGlobalMatrix(const LoadedFbxModel& model, int nodeIndex)
{
    if (const BonePose* pose = FindBindPoseByNode(model, nodeIndex))
    {
        return MatrixFromPose(*pose);
    }

    return GetEditedGlobalMatrix(model, nodeIndex);
}

FbxSurfaceMaterial* FindOrCreateSceneMaterial(FbxScene* scene, const std::string& name)
{
    if (!scene) return nullptr;

    const std::string materialName = name.empty() ? "Default" : name;
    if (FbxSurfaceMaterial* existing = scene->FindMember<FbxSurfaceMaterial>(materialName.c_str()))
    {
        return existing;
    }

    FbxSurfacePhong* material = FbxSurfacePhong::Create(scene, materialName.c_str());
    if (!material) return nullptr;
    material->Diffuse.Set(FbxDouble3(0.66, 0.66, 0.66));
    return material;
}

bool CreateGeneratedMeshSceneNode(FbxScene* scene,
                                  const LoadedFbxModel& model,
                                  int nodeIndex,
                                  const std::vector<int>& materialForVertex,
                                  std::vector<FbxNode*>& sceneNodes)
{
    if (!scene || nodeIndex < 0 || nodeIndex >= static_cast<int>(model.nodes.size())) return false;
    if (nodeIndex < static_cast<int>(sceneNodes.size()) && sceneNodes[static_cast<size_t>(nodeIndex)]) return true;

    const SceneNode& sceneNode = model.nodes[static_cast<size_t>(nodeIndex)];
    if (sceneNode.type != SceneNodeType::Mesh ||
        !sceneNode.sourceName.empty() ||
        sceneNode.meshVertexStart < 0 ||
        sceneNode.meshVertexCount < 3)
    {
        return true;
    }

    const int start = sceneNode.meshVertexStart;
    const int count = sceneNode.meshVertexCount;
    const int end = start + count;
    if (end * 3 > static_cast<int>(model.bindVertices.size())) return false;

    FbxNode* node = FbxNode::Create(scene, sceneNode.name.empty() ? "Merged Geometry" : sceneNode.name.c_str());
    FbxMesh* mesh = FbxMesh::Create(scene, (sceneNode.name.empty() ? std::string("MergedGeometryMesh") : sceneNode.name + "Mesh").c_str());
    if (!node || !mesh) return false;

    const FbxAMatrix global = MatrixFromSceneNode(sceneNode);
    const FbxAMatrix worldToLocal = global.Inverse();
    mesh->InitControlPoints(count);
    for (int vertex = 0; vertex < count; ++vertex)
    {
        const size_t base = static_cast<size_t>(start + vertex) * 3;
        const FbxVector4 world(model.bindVertices[base], model.bindVertices[base + 1], model.bindVertices[base + 2], 1.0);
        mesh->SetControlPointAt(worldToLocal.MultT(world), vertex);
    }

    FbxGeometryElementNormal* normalElement = nullptr;
    if (model.bindNormals.size() == model.bindVertices.size())
    {
        normalElement = mesh->CreateElementNormal();
        if (normalElement)
        {
            normalElement->SetMappingMode(FbxGeometryElement::eByPolygonVertex);
            normalElement->SetReferenceMode(FbxGeometryElement::eDirect);
        }
    }

    FbxGeometryElementUV* uvElement = nullptr;
    const std::vector<float>* uvSet = model.uvSets.empty() ? nullptr : &model.uvSets.front();
    if (uvSet && uvSet->size() >= static_cast<size_t>(end) * 2)
    {
        const char* uvName = model.uvSetNames.empty() ? "UVSet" : model.uvSetNames.front().c_str();
        uvElement = mesh->CreateElementUV(uvName);
        if (uvElement)
        {
            uvElement->SetMappingMode(FbxGeometryElement::eByPolygonVertex);
            uvElement->SetReferenceMode(FbxGeometryElement::eDirect);
        }
    }

    std::unordered_map<int, int> nodeMaterialByGlobalMaterial;
    auto getNodeMaterialIndex = [&](int globalMaterialIndex)
    {
        globalMaterialIndex = std::max(0, std::min(globalMaterialIndex, std::max(0, static_cast<int>(model.materialNames.size()) - 1)));
        const auto found = nodeMaterialByGlobalMaterial.find(globalMaterialIndex);
        if (found != nodeMaterialByGlobalMaterial.end()) return found->second;

        const std::string materialName = globalMaterialIndex < static_cast<int>(model.materialNames.size()) ? model.materialNames[static_cast<size_t>(globalMaterialIndex)] : "Default";
        if (FbxSurfaceMaterial* material = FindOrCreateSceneMaterial(scene, materialName))
        {
            node->AddMaterial(material);
        }
        const int localMaterialIndex = node->GetMaterialCount() - 1;
        nodeMaterialByGlobalMaterial.emplace(globalMaterialIndex, localMaterialIndex);
        return localMaterialIndex;
    };

    FbxGeometryElementMaterial* materialElement = mesh->CreateElementMaterial();
    if (materialElement)
    {
        materialElement->SetMappingMode(FbxGeometryElement::eByPolygon);
        materialElement->SetReferenceMode(FbxGeometryElement::eIndexToDirect);
    }

    const FbxAMatrix worldNormalToLocal = global.Transpose();
    for (int vertex = 0; vertex + 2 < count; vertex += 3)
    {
        const int globalVertex = start + vertex;
        const int materialIndex = globalVertex < static_cast<int>(materialForVertex.size()) ? materialForVertex[static_cast<size_t>(globalVertex)] : 0;
        const int localMaterialIndex = getNodeMaterialIndex(materialIndex);
        mesh->BeginPolygon(localMaterialIndex);
        for (int corner = 0; corner < 3; ++corner)
        {
            const int localVertex = vertex + corner;
            const int currentGlobalVertex = start + localVertex;
            mesh->AddPolygon(localVertex);

            if (normalElement)
            {
                const size_t normalBase = static_cast<size_t>(currentGlobalVertex) * 3;
                FbxVector4 normal(model.bindNormals[normalBase], model.bindNormals[normalBase + 1], model.bindNormals[normalBase + 2], 0.0);
                normal = TransformVector(worldNormalToLocal, normal);
                const double length = std::sqrt(normal[0] * normal[0] + normal[1] * normal[1] + normal[2] * normal[2]);
                if (length > 0.000001)
                {
                    normal[0] /= length;
                    normal[1] /= length;
                    normal[2] /= length;
                }
                normalElement->GetDirectArray().Add(normal);
            }
            if (uvElement)
            {
                const size_t uvBase = static_cast<size_t>(currentGlobalVertex) * 2;
                uvElement->GetDirectArray().Add(FbxVector2((*uvSet)[uvBase], 1.0f - (*uvSet)[uvBase + 1]));
            }
        }
        mesh->EndPolygon();
        if (materialElement)
        {
            materialElement->GetIndexArray().Add(localMaterialIndex);
        }
    }

    if (sceneNode.meshHasSkin &&
        model.skinnedVertices.size() >= static_cast<size_t>(end) &&
        !model.bonePoses.empty())
    {
        std::unordered_map<std::string, FbxCluster*> clustersByBoneName;
        FbxSkin* skin = FbxSkin::Create(scene, (sceneNode.name + "Skin").c_str());
        if (skin)
        {
            auto findBoneNode = [&](const std::string& boneName) -> std::pair<FbxNode*, int>
            {
                for (int boneIndex = 0; boneIndex < static_cast<int>(model.nodes.size()) && boneIndex < static_cast<int>(sceneNodes.size()); ++boneIndex)
                {
                    const SceneNode& bone = model.nodes[static_cast<size_t>(boneIndex)];
                    if (bone.type != SceneNodeType::Bone) continue;
                    if (bone.name == boneName || (!bone.sourceName.empty() && bone.sourceName == boneName))
                    {
                        return { sceneNodes[static_cast<size_t>(boneIndex)], boneIndex };
                    }
                }
                return { nullptr, -1 };
            };

            for (int localVertex = 0; localVertex < count; ++localVertex)
            {
                const SkinnedVertex& skinned = model.skinnedVertices[static_cast<size_t>(start + localVertex)];
                for (const SkinnedVertexInfluence& influence : skinned.influences)
                {
                    auto foundCluster = clustersByBoneName.find(influence.boneName);
                    if (foundCluster == clustersByBoneName.end())
                    {
                        const auto [boneNode, boneIndex] = findBoneNode(influence.boneName);
                        if (!boneNode || boneIndex < 0) continue;

                        FbxCluster* cluster = FbxCluster::Create(scene, (sceneNode.name + "_" + influence.boneName).c_str());
                        if (!cluster) continue;
                        cluster->SetLink(boneNode);
                        cluster->SetLinkMode(FbxCluster::eNormalize);
                        cluster->SetTransformMatrix(global);
                        cluster->SetTransformLinkMatrix(GetEditedBindGlobalMatrix(model, boneIndex));
                        skin->AddCluster(cluster);
                        foundCluster = clustersByBoneName.emplace(influence.boneName, cluster).first;
                    }
                    foundCluster->second->AddControlPointIndex(localVertex, static_cast<double>(influence.weight));
                }
            }
            if (skin->GetClusterCount() > 0)
            {
                mesh->AddDeformer(skin);
            }
        }
    }

    node->SetNodeAttribute(mesh);
    FbxNode* parent = scene->GetRootNode();
    if (sceneNode.parent >= 0 && sceneNode.parent < static_cast<int>(sceneNodes.size()) && sceneNodes[static_cast<size_t>(sceneNode.parent)])
    {
        parent = sceneNodes[static_cast<size_t>(sceneNode.parent)];
    }
    if (parent) parent->AddChild(node);
    if (nodeIndex >= static_cast<int>(sceneNodes.size())) sceneNodes.resize(static_cast<size_t>(nodeIndex + 1), nullptr);
    sceneNodes[static_cast<size_t>(nodeIndex)] = node;
    return true;
}

bool CreateGeneratedMeshSceneNodes(FbxScene* scene,
                                   const LoadedFbxModel& model,
                                   const std::vector<bool>& deletedNodes,
                                   std::vector<FbxNode*>& sceneNodes,
                                   std::string& error)
{
    const std::vector<int> materialForVertex = BuildGlobalVertexMaterialMap(model);
    for (int nodeIndex = 0; nodeIndex < static_cast<int>(model.nodes.size()); ++nodeIndex)
    {
        if (IsDeletedModelNode(deletedNodes, nodeIndex)) continue;
        if (!CreateGeneratedMeshSceneNode(scene, model, nodeIndex, materialForVertex, sceneNodes))
        {
            error = "Failed to create merged geometry for export.";
            return false;
        }
    }
    return true;
}

void ApplyEditedNodeTransforms(const LoadedFbxModel& model,
                               const std::vector<bool>& deletedNodes,
                               const std::vector<FbxNode*>& sceneNodes)
{
    const int count = static_cast<int>(model.nodes.size());
    for (int nodeIndex = 0; nodeIndex < count; ++nodeIndex)
    {
        if (IsDeletedModelNode(deletedNodes, nodeIndex)) continue;

        FbxNode* targetNode = sceneNodes[static_cast<size_t>(nodeIndex)];
        if (!targetNode) continue;

        const SceneNode& sceneNode = model.nodes[static_cast<size_t>(nodeIndex)];
        FbxAMatrix global = MatrixFromSceneNode(sceneNode);
        FbxAMatrix local = global;
        if (sceneNode.parent >= 0 && sceneNode.parent < static_cast<int>(model.nodes.size()))
        {
            local = GetEditedGlobalMatrix(model, sceneNode.parent).Inverse() * global;
        }

        targetNode->LclTranslation.Set(ToFbxDouble3(local.GetT()));
        targetNode->LclRotation.Set(ToFbxDouble3(local.GetR()));
        targetNode->LclScaling.Set(ToFbxDouble3(local.GetS()));
    }
}

void ApplyEditedNodeParents(FbxScene* scene,
                            const LoadedFbxModel& model,
                            const std::vector<bool>& deletedNodes,
                            const std::vector<FbxNode*>& sceneNodes)
{
    if (!scene) return;

    const int count = std::min(static_cast<int>(model.nodes.size()), static_cast<int>(sceneNodes.size()));
    for (int nodeIndex = 0; nodeIndex < count; ++nodeIndex)
    {
        if (IsDeletedModelNode(deletedNodes, nodeIndex)) continue;

        FbxNode* targetNode = sceneNodes[static_cast<size_t>(nodeIndex)];
        if (!targetNode || targetNode == scene->GetRootNode()) continue;

        const int parentIndex = model.nodes[static_cast<size_t>(nodeIndex)].parent;
        FbxNode* targetParent = parentIndex >= 0 && parentIndex < count ? sceneNodes[static_cast<size_t>(parentIndex)] : scene->GetRootNode();
        if (!targetParent || targetParent == targetNode || IsDeletedModelNode(deletedNodes, parentIndex)) continue;
        if (targetNode->GetParent() == targetParent) continue;

        if (FbxNode* currentParent = targetNode->GetParent())
        {
            currentParent->RemoveChild(targetNode);
        }
        targetParent->AddChild(targetNode);
    }
}

std::unordered_map<FbxNode*, int> BuildFbxNodeToModelIndex(const LoadedFbxModel& model,
                                                           const std::vector<FbxNode*>& sceneNodes)
{
    std::unordered_map<FbxNode*, int> nodeToIndex;
    const int count = std::min(static_cast<int>(model.nodes.size()), static_cast<int>(sceneNodes.size()));
    for (int nodeIndex = 0; nodeIndex < count; ++nodeIndex)
    {
        FbxNode* node = sceneNodes[static_cast<size_t>(nodeIndex)];
        if (node) nodeToIndex.emplace(node, nodeIndex);
    }
    return nodeToIndex;
}

void ApplyEditedSkinBindMatrices(const LoadedFbxModel& model,
                                 const std::vector<bool>& deletedNodes,
                                 const std::vector<FbxNode*>& sceneNodes)
{
    const std::unordered_map<FbxNode*, int> nodeToIndex = BuildFbxNodeToModelIndex(model, sceneNodes);
    const int nodeCount = std::min(static_cast<int>(model.nodes.size()), static_cast<int>(sceneNodes.size()));
    for (int nodeIndex = 0; nodeIndex < nodeCount; ++nodeIndex)
    {
        if (IsDeletedModelNode(deletedNodes, nodeIndex)) continue;

        const SceneNode& sceneNode = model.nodes[static_cast<size_t>(nodeIndex)];
        if (sceneNode.type != SceneNodeType::Mesh) continue;

        FbxNode* fbxNode = sceneNodes[static_cast<size_t>(nodeIndex)];
        if (!fbxNode) continue;

        const FbxAMatrix meshBindGlobal = MatrixFromSceneNode(sceneNode);
        for (int attributeIndex = 0; attributeIndex < fbxNode->GetNodeAttributeCount(); ++attributeIndex)
        {
            FbxNodeAttribute* attribute = fbxNode->GetNodeAttributeByIndex(attributeIndex);
            if (!attribute || attribute->GetAttributeType() != FbxNodeAttribute::eMesh) continue;

            FbxMesh* mesh = static_cast<FbxMesh*>(attribute);
            const int skinCount = mesh->GetDeformerCount(FbxDeformer::eSkin);
            for (int skinIndex = 0; skinIndex < skinCount; ++skinIndex)
            {
                FbxSkin* skin = static_cast<FbxSkin*>(mesh->GetDeformer(skinIndex, FbxDeformer::eSkin));
                if (!skin) continue;

                for (int clusterIndex = 0; clusterIndex < skin->GetClusterCount(); ++clusterIndex)
                {
                    FbxCluster* cluster = skin->GetCluster(clusterIndex);
                    FbxNode* link = cluster ? cluster->GetLink() : nullptr;
                    if (!cluster) continue;

                    cluster->SetTransformMatrix(meshBindGlobal);

                    const auto found = nodeToIndex.find(link);
                    if (found == nodeToIndex.end()) continue;

                    const int linkNodeIndex = found->second;
                    if (IsDeletedModelNode(deletedNodes, linkNodeIndex)) continue;

                    cluster->SetTransformLinkMatrix(GetEditedBindGlobalMatrix(model, linkNodeIndex));
                }
            }
        }
    }
}

void RemoveExistingBindPoses(FbxScene* scene)
{
    if (!scene) return;

    for (int poseIndex = scene->GetPoseCount() - 1; poseIndex >= 0; --poseIndex)
    {
        FbxPose* pose = scene->GetPose(poseIndex);
        if (!pose || !pose->IsBindPose()) continue;

        scene->RemovePose(poseIndex);
        pose->Destroy(true);
    }
}

void RebuildEditedBindPose(FbxScene* scene,
                           const LoadedFbxModel& model,
                           const std::vector<bool>& deletedNodes,
                           const std::vector<FbxNode*>& sceneNodes)
{
    if (!scene) return;

    RemoveExistingBindPoses(scene);

    FbxPose* bindPose = FbxPose::Create(scene, "openfbx_bind_pose");
    if (!bindPose) return;

    bindPose->SetIsBindPose(true);

    bool addedAnyNode = false;
    const int nodeCount = std::min(static_cast<int>(model.nodes.size()), static_cast<int>(sceneNodes.size()));
    for (int nodeIndex = 0; nodeIndex < nodeCount; ++nodeIndex)
    {
        if (IsDeletedModelNode(deletedNodes, nodeIndex)) continue;

        FbxNode* node = sceneNodes[static_cast<size_t>(nodeIndex)];
        if (!node) continue;

        const SceneNode& sceneNode = model.nodes[static_cast<size_t>(nodeIndex)];
        const FbxAMatrix global = sceneNode.type == SceneNodeType::Bone
            ? GetEditedBindGlobalMatrix(model, nodeIndex)
            : MatrixFromSceneNode(sceneNode);

        const int itemIndex = bindPose->Add(node, FbxMatrix(global), false, true);
        addedAnyNode = addedAnyNode || itemIndex >= 0;
    }

    if (addedAnyNode)
    {
        scene->AddPose(bindPose);
    }
    else
    {
        bindPose->Destroy(true);
    }
}

void ApplyEditedMeshGeometry(const LoadedFbxModel& model,
                             const std::vector<bool>& deletedNodes,
                             const std::vector<FbxNode*>& sceneNodes)
{
    if (model.bindVertices.empty()) return;

    const int nodeCount = static_cast<int>(model.nodes.size());
    for (int nodeIndex = 0; nodeIndex < nodeCount; ++nodeIndex)
    {
        if (IsDeletedModelNode(deletedNodes, nodeIndex)) continue;

        const SceneNode& sceneNode = model.nodes[static_cast<size_t>(nodeIndex)];
        if (sceneNode.type != SceneNodeType::Mesh ||
            sceneNode.meshVertexStart < 0 ||
            sceneNode.meshVertexCount <= 0)
        {
            continue;
        }

        FbxNode* fbxNode = sceneNodes[static_cast<size_t>(nodeIndex)];
        if (!fbxNode) continue;

        FbxAMatrix editedMeshGlobal = MatrixFromSceneNode(sceneNode) * GetNodeGeometryTransform(fbxNode);
        FbxAMatrix worldToLocal = editedMeshGlobal.Inverse();

        int globalVertex = sceneNode.meshVertexStart;
        const int globalVertexEnd = sceneNode.meshVertexStart + sceneNode.meshVertexCount;
        int nodePolygonVertex = sceneNode.meshPolygonVertexStart;

        for (int attributeIndex = 0; attributeIndex < fbxNode->GetNodeAttributeCount(); ++attributeIndex)
        {
            FbxNodeAttribute* attribute = fbxNode->GetNodeAttributeByIndex(attributeIndex);
            if (!attribute || attribute->GetAttributeType() != FbxNodeAttribute::eMesh) continue;

            FbxMesh* mesh = static_cast<FbxMesh*>(attribute);
            const int controlPointCount = mesh->GetControlPointsCount();
            if (controlPointCount <= 0) continue;

            std::vector<FbxVector4> accumulated(static_cast<size_t>(controlPointCount), FbxVector4(0.0, 0.0, 0.0, 0.0));
            std::vector<int> counts(static_cast<size_t>(controlPointCount), 0);
            const bool hasEditedNormals = model.bindNormals.size() == model.bindVertices.size();
            int rawPolygonVertexCount = 0;
            for (int polygon = 0; polygon < mesh->GetPolygonCount(); ++polygon)
            {
                rawPolygonVertexCount += std::max(0, mesh->GetPolygonSize(polygon));
            }

            FbxGeometryElementNormal* normalElement = nullptr;
            const bool canRewritePolygonVertexNormals =
                hasEditedNormals &&
                rawPolygonVertexCount > 0 &&
                nodePolygonVertex >= 0 &&
                nodePolygonVertex + rawPolygonVertexCount <= static_cast<int>(model.meshPolygonVertexGlobalIndices.size());
            FbxAMatrix worldNormalToLocal = editedMeshGlobal.Transpose();
            if (canRewritePolygonVertexNormals)
            {
                normalElement = mesh->GetElementNormalCount() > 0
                    ? mesh->GetElementNormal(0)
                    : mesh->CreateElementNormal();
                if (normalElement)
                {
                    normalElement->SetMappingMode(FbxGeometryElement::eByPolygonVertex);
                    normalElement->SetReferenceMode(FbxGeometryElement::eDirect);
                    normalElement->GetDirectArray().Clear();
                    normalElement->GetIndexArray().Clear();
                }
            }

            for (int currentGlobalVertex = sceneNode.meshVertexStart; currentGlobalVertex < globalVertexEnd; ++currentGlobalVertex)
            {
                if (currentGlobalVertex < 0 || currentGlobalVertex >= static_cast<int>(model.meshControlPointIndices.size())) continue;

                const int controlPoint = model.meshControlPointIndices[static_cast<size_t>(currentGlobalVertex)];
                if (controlPoint < 0 || controlPoint >= controlPointCount) continue;

                const size_t base = static_cast<size_t>(currentGlobalVertex) * 3;
                if (base + 2 >= model.bindVertices.size()) continue;

                const Vector3 editedWorld{
                    model.bindVertices[base],
                    model.bindVertices[base + 1],
                    model.bindVertices[base + 2]
                };
                const FbxVector4 editedLocal = worldToLocal.MultT(ToFbxPoint(editedWorld));
                FbxVector4& sum = accumulated[static_cast<size_t>(controlPoint)];
                sum[0] += editedLocal[0];
                sum[1] += editedLocal[1];
                sum[2] += editedLocal[2];
                ++counts[static_cast<size_t>(controlPoint)];
            }

            globalVertex = sceneNode.meshVertexStart;
            int polygonVertex = nodePolygonVertex;
            std::vector<std::pair<size_t, FbxGeometryElementUV*>> editedUvElements;
            if (model.uvSetsEdited && nodePolygonVertex >= 0 &&
                nodePolygonVertex + rawPolygonVertexCount <= static_cast<int>(model.meshPolygonVertexGlobalIndices.size()))
            {
                while (mesh->GetElementUVCount() > 0) mesh->RemoveElementUV(mesh->GetElementUV(0));
                for (size_t set = 0; set < model.uvSets.size(); ++set)
                {
                    bool present = set >= model.uvSetPresence.size();
                    for (int p = 0; !present && p < rawPolygonVertexCount; ++p)
                    {
                        const int v = model.meshPolygonVertexGlobalIndices[nodePolygonVertex + p];
                        present = v >= 0 && v < static_cast<int>(model.uvSetPresence[set].size()) && model.uvSetPresence[set][v];
                    }
                    if (!present) continue;
                    const char* name = set < model.uvSetNames.size() ? model.uvSetNames[set].c_str() : "UVSet";
                    auto* element = mesh->CreateElementUV(name);
                    element->SetMappingMode(FbxGeometryElement::eByPolygonVertex);
                    element->SetReferenceMode(FbxGeometryElement::eDirect);
                    editedUvElements.emplace_back(set, element);
                }
            }
            const int polygonCount = mesh->GetPolygonCount();
            for (int polygon = 0; polygon < polygonCount; ++polygon)
            {
                const int polygonSize = mesh->GetPolygonSize(polygon);
                for (int vertex = 0; vertex < polygonSize; ++vertex)
                {
                    const int currentGlobalVertex = canRewritePolygonVertexNormals
                        ? model.meshPolygonVertexGlobalIndices[static_cast<size_t>(polygonVertex)]
                        : globalVertex;
                    ++polygonVertex;
                    for (const auto& entry : editedUvElements)
                    {
                        const auto& uv = model.uvSets[entry.first];
                        const size_t base = static_cast<size_t>(currentGlobalVertex) * 2;
                        entry.second->GetDirectArray().Add(base + 1 < uv.size()
                            ? FbxVector2(uv[base], 1.0f - uv[base + 1]) : FbxVector2(0, 0));
                    }
                    if (globalVertex < globalVertexEnd)
                    {
                        ++globalVertex;
                    }

                    if (normalElement)
                    {
                        const size_t normalBase = static_cast<size_t>(currentGlobalVertex) * 3;
                        FbxVector4 editedLocalNormal(0.0, 1.0, 0.0, 0.0);
                        if (normalBase + 2 < model.bindNormals.size())
                        {
                            const FbxVector4 editedWorldNormal(model.bindNormals[normalBase],
                                                               model.bindNormals[normalBase + 1],
                                                               model.bindNormals[normalBase + 2],
                                                               0.0);
                            editedLocalNormal = TransformVector(worldNormalToLocal, editedWorldNormal);
                            const double length = std::sqrt(editedLocalNormal[0] * editedLocalNormal[0] +
                                                            editedLocalNormal[1] * editedLocalNormal[1] +
                                                            editedLocalNormal[2] * editedLocalNormal[2]);
                            if (length > 0.000001)
                            {
                                editedLocalNormal[0] /= length;
                                editedLocalNormal[1] /= length;
                                editedLocalNormal[2] /= length;
                            }
                            else
                            {
                                editedLocalNormal = FbxVector4(0.0, 1.0, 0.0, 0.0);
                            }
                        }
                        normalElement->GetDirectArray().Add(editedLocalNormal);
                    }
                }
            }
            if (nodePolygonVertex >= 0)
            {
                nodePolygonVertex += rawPolygonVertexCount;
            }

            for (int controlPoint = 0; controlPoint < controlPointCount; ++controlPoint)
            {
                const int count = counts[static_cast<size_t>(controlPoint)];
                if (count <= 0) continue;

                FbxVector4 value = accumulated[static_cast<size_t>(controlPoint)];
                value[0] /= static_cast<double>(count);
                value[1] /= static_cast<double>(count);
                value[2] /= static_cast<double>(count);
                value[3] = 1.0;
                mesh->SetControlPointAt(value, controlPoint);
            }

        }
    }
}

void ApplyEditedNodeNames(const LoadedFbxModel& model,
                          const std::vector<bool>& deletedNodes,
                          const std::vector<FbxNode*>& sceneNodes)
{
    const int count = static_cast<int>(model.nodes.size());
    for (int nodeIndex = 0; nodeIndex < count; ++nodeIndex)
    {
        if (IsDeletedModelNode(deletedNodes, nodeIndex)) continue;
        FbxNode* targetNode = sceneNodes[static_cast<size_t>(nodeIndex)];
        if (!targetNode) continue;

        const std::string& name = model.nodes[static_cast<size_t>(nodeIndex)].name;
        if (!name.empty())
        {
            targetNode->SetName(name.c_str());
        }
    }
}

void ApplyDeletedNodes(FbxScene* scene,
                       const LoadedFbxModel& model,
                       const std::vector<bool>& deletedNodes,
                       const std::vector<FbxNode*>& sceneNodes)
{
    if (deletedNodes.empty()) return;

    const int count = static_cast<int>(model.nodes.size());
    for (int nodeIndex = 0; nodeIndex < count; ++nodeIndex)
    {
        if (!IsDeletedModelSubtreeRoot(model, deletedNodes, nodeIndex)) continue;

        FbxNode* node = sceneNodes[static_cast<size_t>(nodeIndex)];
        if (!node || node == scene->GetRootNode()) continue;

        if (FbxNode* parent = node->GetParent())
        {
            parent->RemoveChild(node);
        }
        node->Destroy(true);
    }
}

const char* GetTextureUsagePropertyName(FbxTextureUsage usage)
{
    switch (usage)
    {
    case FbxTextureUsage::Diffuse: return "DiffuseColor";
    case FbxTextureUsage::Normal: return "NormalMap";
    case FbxTextureUsage::Roughness: return "Maya|roughness";
    case FbxTextureUsage::Metallic: return "Maya|metalness";
    case FbxTextureUsage::AmbientOcclusion: return "Maya|ambientOcclusion";
    case FbxTextureUsage::Emissive: return "EmissiveColor";
    case FbxTextureUsage::Opacity: return "TransparencyFactor";
    }
    return "DiffuseColor";
}

const char* GetTextureUsageLabel(FbxTextureUsage usage)
{
    switch (usage)
    {
    case FbxTextureUsage::Diffuse: return "Diffuse";
    case FbxTextureUsage::Normal: return "Normal";
    case FbxTextureUsage::Roughness: return "Roughness";
    case FbxTextureUsage::Metallic: return "Metallic";
    case FbxTextureUsage::AmbientOcclusion: return "AmbientOcclusion";
    case FbxTextureUsage::Emissive: return "Emissive";
    case FbxTextureUsage::Opacity: return "Opacity";
    }
    return "Texture";
}

bool IsScalarTextureUsage(FbxTextureUsage usage)
{
    return usage == FbxTextureUsage::Roughness ||
           usage == FbxTextureUsage::Metallic ||
           usage == FbxTextureUsage::AmbientOcclusion ||
           usage == FbxTextureUsage::Opacity;
}

FbxProperty GetOrCreateTextureProperty(FbxSurfaceMaterial* material, FbxTextureUsage usage)
{
    if (!material) return FbxProperty();

    const char* propertyName = GetTextureUsagePropertyName(usage);
    FbxProperty property = material->FindProperty(propertyName);
    if (property.IsValid()) return property;

    property = FbxProperty::Create(material, IsScalarTextureUsage(usage) ? FbxDoubleDT : FbxDouble3DT, propertyName);
    if (property.IsValid())
    {
        property.ModifyFlag(FbxPropertyFlags::eUserDefined, true);
    }
    return property;
}

void DisconnectTextureSources(FbxProperty& property, bool destroyDisconnectedTextures)
{
    if (!property.IsValid()) return;

    for (int textureIndex = property.GetSrcObjectCount<FbxTexture>() - 1; textureIndex >= 0; --textureIndex)
    {
        FbxTexture* texture = property.GetSrcObject<FbxTexture>(textureIndex);
        if (texture)
        {
            property.DisconnectSrcObject(texture);
            if (destroyDisconnectedTextures)
            {
                texture->Destroy(true);
            }
        }
    }
}

void CollectMaterialsByName(FbxNode* node,
                            std::unordered_map<std::string, std::vector<FbxSurfaceMaterial*>>& materialsByName,
                            std::unordered_set<FbxSurfaceMaterial*>& seenMaterials)
{
    if (!node) return;

    for (int materialIndex = 0; materialIndex < node->GetMaterialCount(); ++materialIndex)
    {
        FbxSurfaceMaterial* material = node->GetMaterial(materialIndex);
        if (!material || !seenMaterials.insert(material).second) continue;

        const std::string name = material->GetName() && material->GetName()[0] ? material->GetName() : "Default";
        materialsByName[name].push_back(material);
    }

    for (int childIndex = 0; childIndex < node->GetChildCount(); ++childIndex)
    {
        CollectMaterialsByName(node->GetChild(childIndex), materialsByName, seenMaterials);
    }
}

std::string MakeSafeDirectoryName(std::string value)
{
    if (value.empty()) return "fbx";

    for (char& c : value)
    {
        const unsigned char ch = static_cast<unsigned char>(c);
        if (!std::isalnum(ch) && c != '-' && c != '_')
        {
            c = '_';
        }
    }
    return value;
}

std::filesystem::path MakeEmbeddedTextureExtractionDirectory(const std::string& modelPath)
{
    std::error_code pathError;
    std::filesystem::path base = std::filesystem::temp_directory_path(pathError);
    if (pathError)
    {
        base = std::filesystem::current_path(pathError);
    }
    if (base.empty())
    {
        base = ".";
    }

    const std::filesystem::path absoluteModel = std::filesystem::absolute(modelPath, pathError);
    const std::string hashSource = pathError ? modelPath : absoluteModel.string();
    const std::string folderName = MakeSafeDirectoryName(std::filesystem::path(modelPath).stem().string()) +
                                   "_" +
                                   std::to_string(std::hash<std::string>{}(hashSource));
    return base / "openfbx_embedded_media" / folderName;
}

std::filesystem::path MakeUniqueEmbeddedTextureExtractionDirectory(const std::string& modelPath)
{
    const std::filesystem::path base = MakeEmbeddedTextureExtractionDirectory(modelPath);
    for (int suffix = 0; suffix < 10000; ++suffix)
    {
        std::filesystem::path candidate = base;
        if (suffix > 0)
        {
            candidate += "_";
            candidate += std::to_string(suffix + 1);
        }

        std::error_code existsError;
        if (!std::filesystem::exists(candidate, existsError) && !existsError)
        {
            return candidate;
        }
    }

    std::filesystem::path fallback = base;
    fallback += "_copy";
    return fallback;
}

std::filesystem::path MakeSaveEmbeddedTextureExtractionDirectory(const std::string& modelPath)
{
    return MakeEmbeddedTextureExtractionDirectory(modelPath) / "save_import";
}

std::filesystem::path AbsoluteNormalizedPathForFbxLoader(const std::filesystem::path& path)
{
    std::error_code pathError;
    std::filesystem::path result = std::filesystem::weakly_canonical(path, pathError);
    if (!pathError) return result.lexically_normal();

    result = std::filesystem::absolute(path, pathError);
    if (!pathError) return result.lexically_normal();

    return path.lexically_normal();
}

std::string ToLowerAsciiForFbxLoader(std::string value)
{
    for (char& c : value)
    {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return value;
}

bool IsOwnedEmbeddedTextureExtractionDirectory(const std::filesystem::path& directory)
{
    if (directory.empty()) return false;

    std::error_code pathError;
    std::filesystem::path base = std::filesystem::temp_directory_path(pathError);
    if (pathError)
    {
        base = std::filesystem::current_path(pathError);
    }
    if (base.empty())
    {
        base = ".";
    }

    const std::filesystem::path root = AbsoluteNormalizedPathForFbxLoader(base / "openfbx_embedded_media");
    const std::filesystem::path target = AbsoluteNormalizedPathForFbxLoader(directory);
    std::string rootText = ToLowerAsciiForFbxLoader(root.string());
    std::string targetText = ToLowerAsciiForFbxLoader(target.string());

    if (!rootText.empty() && rootText.back() != '\\' && rootText.back() != '/')
    {
        rootText += std::filesystem::path::preferred_separator;
    }
    return targetText.rfind(rootText, 0) == 0 && targetText.size() > rootText.size();
}

void RemoveEmbeddedTextureExtractionDirectory(const std::filesystem::path& directory)
{
    if (!IsOwnedEmbeddedTextureExtractionDirectory(directory)) return;

    std::error_code removeError;
    std::filesystem::remove_all(directory, removeError);
}

FbxCallback::State MarkEmbeddedFileRead(void* userData,
                                        FbxClassId,
                                        const char*,
                                        const void*,
                                        size_t)
{
    bool* hasEmbeddedMedia = static_cast<bool*>(userData);
    if (hasEmbeddedMedia)
    {
        *hasEmbeddedMedia = true;
    }
    return FbxCallback::eNotHandled;
}

bool ExistingFile(const std::filesystem::path& path)
{
    std::error_code existsError;
    return !path.empty() && std::filesystem::is_regular_file(path, existsError) && !existsError;
}

std::filesystem::path AbsoluteExistingFile(const std::filesystem::path& path)
{
    std::error_code pathError;
    std::filesystem::path result = std::filesystem::weakly_canonical(path, pathError);
    if (!pathError) return result.lexically_normal();

    result = std::filesystem::absolute(path, pathError);
    if (!pathError) return result.lexically_normal();

    return path.lexically_normal();
}

std::filesystem::path FindFileBelowDirectory(const std::filesystem::path& directory, const std::filesystem::path& filename)
{
    if (directory.empty() || filename.empty()) return {};

    std::error_code existsError;
    if (!std::filesystem::exists(directory, existsError) || existsError) return {};

    std::error_code iterateError;
    for (std::filesystem::recursive_directory_iterator it(directory, iterateError), end; it != end && !iterateError; it.increment(iterateError))
    {
        if (!it->is_regular_file()) continue;
        if (it->path().filename() == filename)
        {
            return it->path();
        }
    }

    return {};
}

std::string ResolveFbxTexturePath(const FbxFileTexture* texture,
                                  const std::string& modelPath,
                                  const std::filesystem::path& extractionDirectory)
{
    if (!texture) return {};

    const std::filesystem::path modelDirectory = std::filesystem::path(modelPath).parent_path();
    const std::filesystem::path fileName = texture->GetFileName() ? texture->GetFileName() : "";
    const std::filesystem::path relativeName = texture->GetRelativeFileName() ? texture->GetRelativeFileName() : "";

    std::vector<std::filesystem::path> candidates;
    if (!fileName.empty())
    {
        candidates.push_back(fileName);
        candidates.push_back(modelDirectory / fileName);
        candidates.push_back(extractionDirectory / fileName.filename());
    }
    if (!relativeName.empty())
    {
        candidates.push_back(relativeName);
        candidates.push_back(modelDirectory / relativeName);
        candidates.push_back(extractionDirectory / relativeName);
        candidates.push_back(extractionDirectory / relativeName.filename());
    }

    for (const std::filesystem::path& candidate : candidates)
    {
        if (ExistingFile(candidate))
        {
            return AbsoluteExistingFile(candidate).string();
        }
    }

    const std::filesystem::path searchName = !relativeName.filename().empty() ? relativeName.filename() : fileName.filename();
    const std::filesystem::path foundExtractedFile = FindFileBelowDirectory(extractionDirectory, searchName);
    if (!foundExtractedFile.empty())
    {
        return AbsoluteExistingFile(foundExtractedFile).string();
    }

    if (!fileName.empty()) return fileName.string();
    return relativeName.string();
}

std::string GetTextureRelativeName(const FbxFileTexture* texture)
{
    if (!texture) return {};

    std::filesystem::path relativeName = texture->GetRelativeFileName() ? texture->GetRelativeFileName() : "";
    if (!relativeName.empty()) return relativeName.generic_string();

    std::filesystem::path fileName = texture->GetFileName() ? texture->GetFileName() : "";
    return fileName.filename().generic_string();
}

void AddImportedTextureReference(FbxSurfaceMaterial* material,
                                 FbxTextureUsage usage,
                                 FbxFileTexture* texture,
                                 const std::string& modelPath,
                                 const std::filesystem::path& extractionDirectory,
                                 std::unordered_set<std::string>& seenReferences,
                                 MeshBuilder& out)
{
    if (!material || !texture) return;

    const std::string materialName = material->GetName() && material->GetName()[0] ? material->GetName() : "Default";
    const std::string texturePath = ResolveFbxTexturePath(texture, modelPath, extractionDirectory);
    if (texturePath.empty()) return;

    const std::string key = materialName + "|" + std::to_string(static_cast<int>(usage)) + "|" + texturePath;
    if (!seenReferences.insert(key).second) return;

    FbxTextureReference reference;
    reference.materialName = materialName;
    reference.usage = usage;
    reference.filePath = texturePath;
    reference.relativePath = GetTextureRelativeName(texture);
    out.textureReferences.push_back(reference);
}

void CollectTextureReferencesFromProperty(FbxSurfaceMaterial* material,
                                          FbxTextureUsage usage,
                                          FbxProperty property,
                                          const std::string& modelPath,
                                          const std::filesystem::path& extractionDirectory,
                                          std::unordered_set<std::string>& seenReferences,
                                          MeshBuilder& out)
{
    if (!property.IsValid()) return;

    for (int layeredIndex = 0; layeredIndex < property.GetSrcObjectCount<FbxLayeredTexture>(); ++layeredIndex)
    {
        FbxLayeredTexture* layeredTexture = property.GetSrcObject<FbxLayeredTexture>(layeredIndex);
        if (!layeredTexture) continue;

        for (int textureIndex = 0; textureIndex < layeredTexture->GetSrcObjectCount<FbxFileTexture>(); ++textureIndex)
        {
            AddImportedTextureReference(material,
                                        usage,
                                        layeredTexture->GetSrcObject<FbxFileTexture>(textureIndex),
                                        modelPath,
                                        extractionDirectory,
                                        seenReferences,
                                        out);
        }
    }

    for (int textureIndex = 0; textureIndex < property.GetSrcObjectCount<FbxFileTexture>(); ++textureIndex)
    {
        AddImportedTextureReference(material,
                                    usage,
                                    property.GetSrcObject<FbxFileTexture>(textureIndex),
                                    modelPath,
                                    extractionDirectory,
                                    seenReferences,
                                    out);
    }
}

void CollectImportedTextureReferences(FbxScene* scene,
                                      const std::string& modelPath,
                                      const std::filesystem::path& extractionDirectory,
                                      MeshBuilder& out)
{
    if (!scene) return;

    std::unordered_map<std::string, std::vector<FbxSurfaceMaterial*>> materialsByName;
    std::unordered_set<FbxSurfaceMaterial*> seenMaterials;
    CollectMaterialsByName(scene->GetRootNode(), materialsByName, seenMaterials);

    std::unordered_set<std::string> seenReferences;
    const FbxTextureUsage usages[] = {
        FbxTextureUsage::Diffuse,
        FbxTextureUsage::Normal,
        FbxTextureUsage::Roughness,
        FbxTextureUsage::Metallic,
        FbxTextureUsage::AmbientOcclusion,
        FbxTextureUsage::Emissive,
        FbxTextureUsage::Opacity
    };

    for (const auto& entry : materialsByName)
    {
        for (FbxSurfaceMaterial* material : entry.second)
        {
            if (!material) continue;

            for (FbxTextureUsage usage : usages)
            {
                const char* propertyName = GetTextureUsagePropertyName(usage);
                FbxProperty property = material->FindProperty(propertyName);
                if (!property.IsValid())
                {
                    property = material->FindPropertyHierarchical(propertyName);
                }
                CollectTextureReferencesFromProperty(material, usage, property, modelPath, extractionDirectory, seenReferences, out);
            }

            CollectTextureReferencesFromProperty(material,
                                                 FbxTextureUsage::Normal,
                                                 material->FindProperty(FbxSurfaceMaterial::sBump),
                                                 modelPath,
                                                 extractionDirectory,
                                                 seenReferences,
                                                 out);
            CollectTextureReferencesFromProperty(material,
                                                 FbxTextureUsage::Opacity,
                                                 material->FindProperty(FbxSurfaceMaterial::sTransparentColor),
                                                 modelPath,
                                                 extractionDirectory,
                                                 seenReferences,
                                                 out);
        }
    }
}

void ApplyTextureReferencesToScene(FbxScene* scene, const std::vector<FbxTextureReference>& textureReferences)
{
    if (!scene || textureReferences.empty()) return;

    std::unordered_map<std::string, std::vector<FbxSurfaceMaterial*>> materialsByName;
    std::unordered_set<FbxSurfaceMaterial*> seenMaterials;
    CollectMaterialsByName(scene->GetRootNode(), materialsByName, seenMaterials);

    int textureObjectIndex = 0;
    for (const FbxTextureReference& reference : textureReferences)
    {
        if (reference.materialName.empty() || reference.relativePath.empty()) continue;

        const auto found = materialsByName.find(reference.materialName);
        if (found == materialsByName.end()) continue;

        for (FbxSurfaceMaterial* material : found->second)
        {
            FbxProperty property = GetOrCreateTextureProperty(material, reference.usage);
            if (!property.IsValid()) continue;

            DisconnectTextureSources(property, true);

            std::string textureName = reference.materialName;
            textureName += "_";
            textureName += GetTextureUsageLabel(reference.usage);
            textureName += "_openfbx_";
            textureName += std::to_string(++textureObjectIndex);

            FbxFileTexture* texture = FbxFileTexture::Create(scene, textureName.c_str());
            if (!texture) continue;

            texture->SetFileName(reference.filePath.empty() ? reference.relativePath.c_str() : reference.filePath.c_str());
            texture->SetRelativeFileName(reference.relativePath.c_str());
            texture->SetTextureUse(FbxTexture::eStandard);
            texture->SetMappingType(FbxTexture::eUV);
            texture->SetMaterialUse(FbxFileTexture::eModelMaterial);
            texture->SetSwapUV(false);
            texture->SetTranslation(0.0, 0.0);
            texture->SetScale(1.0, 1.0);
            texture->SetRotation(0.0, 0.0);

            property.ConnectSrcObject(texture);
        }
    }
}

bool ApplyRemovedDegenerateTriangles(const LoadedFbxModel& model,
                                     const std::vector<bool>& deletedNodes, const std::vector<FbxNode*>& nodes,
                                     std::string& error)
{
    for (int index = 0; index < static_cast<int>(model.nodes.size()); ++index)
    {
        const SceneNode& edited = model.nodes[static_cast<size_t>(index)];
        if (edited.removedTriangleStarts.empty() || IsDeletedModelNode(deletedNodes, index)) continue;
        FbxNode* node = nodes[static_cast<size_t>(index)];
        if (!node) continue;
        int vertex = edited.meshVertexStart;
        size_t sourcePolygon = 0;
        for (int attribute = 0; attribute < node->GetNodeAttributeCount(); ++attribute)
        {
            auto* source = node->GetNodeAttributeByIndex(attribute);
            if (!source || source->GetAttributeType() != FbxNodeAttribute::eMesh) continue;
            auto* mesh = static_cast<FbxMesh*>(source);
            std::vector<int> removePolygons;
            for (int polygon = 0; polygon < mesh->GetPolygonCount(); ++polygon, ++sourcePolygon)
            {
                const int count = sourcePolygon < edited.sourcePolygonTriangleCounts.size()
                    ? edited.sourcePolygonTriangleCounts[sourcePolygon] : std::max(0, mesh->GetPolygonSize(polygon) - 2);
                bool removed = count > 0;
                for (int triangle = 0; triangle < count; ++triangle)
                    removed = removed && IsRemovedTriangle(edited, vertex + triangle * 3);
                if (removed) removePolygons.push_back(polygon);
                vertex += count * 3;
            }
            // Remove in reverse order; FBX updates polygon-associated layer data.
            // Control points, skin clusters, and surviving polygon topology stay intact.
            for (auto polygon = removePolygons.rbegin(); polygon != removePolygons.rend(); ++polygon)
            {
                if (mesh->RemovePolygon(*polygon) < 0)
                {
                    error = "Unable to remove degenerate face from " + edited.name;
                    return false;
                }
            }
        }
    }
    return true;
}

bool ApplyEditedModelToScene(FbxScene* scene,
                             const LoadedFbxModel& model,
                             const std::vector<bool>& deletedNodes,
                             std::string& error)
{
    const std::vector<FbxNode*> sceneNodes = BuildSceneNodeIndex(scene);
    if (sceneNodes.empty())
    {
        error = "FBX scene has no nodes to save.";
        return false;
    }

    std::vector<FbxNode*> mappedSceneNodes = BuildMappedSceneNodes(model, sceneNodes);
    if (!CreateGeneratedMeshSceneNodes(scene, model, deletedNodes, mappedSceneNodes, error))
    {
        return false;
    }
    ApplyEditedNodeParents(scene, model, deletedNodes, mappedSceneNodes);
    ApplyEditedNodeTransforms(model, deletedNodes, mappedSceneNodes);
    ApplyEditedSkinBindMatrices(model, deletedNodes, mappedSceneNodes);
    RebuildEditedBindPose(scene, model, deletedNodes, mappedSceneNodes);
    ApplyEditedMeshGeometry(model, deletedNodes, mappedSceneNodes);
    if (!ApplyRemovedDegenerateTriangles(model, deletedNodes, mappedSceneNodes, error)) return false;
    ApplyEditedNodeNames(model, deletedNodes, mappedSceneNodes);
    ApplyDeletedNodes(scene, model, deletedNodes, mappedSceneNodes);
    return true;
}

FbxAMatrix GetFrameNodeGlobalMatrix(const LoadedFbxModel& model, const BoneFrame& frame, int nodeIndex)
{
    const BonePose* pose = FindFramePose(frame, nodeIndex);
    if (pose) return MatrixFromPose(*pose);

    if (nodeIndex >= 0 && nodeIndex < static_cast<int>(model.nodes.size()))
    {
        return MatrixFromSceneNode(model.nodes[static_cast<size_t>(nodeIndex)]);
    }

    FbxAMatrix identity;
    identity.SetIdentity();
    return identity;
}

void AddSkeletonNodesByName(FbxNode* node, std::unordered_map<std::string, FbxNode*>& nodesByName)
{
    if (!node) return;
    if (IsSkeletonNode(node) && node->GetName() && node->GetName()[0])
    {
        nodesByName.emplace(node->GetName(), node);
    }
    for (int i = 0; i < node->GetChildCount(); ++i)
    {
        AddSkeletonNodesByName(node->GetChild(i), nodesByName);
    }
}

void AddCurveKey(FbxAnimCurve* curve, const FbxTime& time, float value)
{
    if (!curve) return;
    const int keyIndex = curve->KeyAdd(time);
    curve->KeySet(keyIndex, time, value, FbxAnimCurveDef::eInterpolationLinear);
}

void AddVectorKey(FbxPropertyT<FbxDouble3>& property, FbxAnimLayer* layer, const FbxTime& time, const FbxVector4& value)
{
    AddCurveKey(property.GetCurve(layer, FBXSDK_CURVENODE_COMPONENT_X, true), time, static_cast<float>(value[0]));
    AddCurveKey(property.GetCurve(layer, FBXSDK_CURVENODE_COMPONENT_Y, true), time, static_cast<float>(value[1]));
    AddCurveKey(property.GetCurve(layer, FBXSDK_CURVENODE_COMPONENT_Z, true), time, static_cast<float>(value[2]));
}

std::string MakeUniqueAnimationName(const std::string& name, std::unordered_set<std::string>& usedNames)
{
    std::string result = name.empty() ? "Animation" : name;
    if (usedNames.insert(result).second) return result;

    for (int suffix = 2; suffix < 10000; ++suffix)
    {
        const std::string candidate = result + "_" + std::to_string(suffix);
        if (usedNames.insert(candidate).second) return candidate;
    }
    return result + "_copy";
}

bool WriteAnimationStacks(FbxScene* scene,
                          const LoadedFbxModel& model,
                          const std::vector<FbxNode*>& sceneNodes,
                          std::string& error)
{
    FbxArray<FbxString*> stackNames;
    scene->FillAnimStackNameArray(stackNames);
    for (int i = 0; i < stackNames.GetCount(); ++i)
    {
        FbxAnimStack* stack = scene->FindMember<FbxAnimStack>(stackNames[i]->Buffer());
        if (stack)
        {
            scene->RemoveMember(stack);
            stack->Destroy(true);
        }
    }
    FbxArrayDelete(stackNames);

    std::unordered_set<std::string> usedNames;
    for (const AnimationClip& clip : model.animations)
    {
        if (clip.frames.empty()) continue;

        const std::string stackName = MakeUniqueAnimationName(clip.name, usedNames);
        FbxAnimStack* stack = FbxAnimStack::Create(scene, stackName.c_str());
        FbxAnimLayer* layer = FbxAnimLayer::Create(scene, "BaseLayer");
        if (!stack || !layer)
        {
            error = "Failed to create FBX animation stack.";
            return false;
        }
        stack->AddMember(layer);

        FbxTimeSpan timeSpan;
        FbxTime startTime;
        FbxTime stopTime;
        startTime.SetSecondDouble(0.0);
        stopTime.SetSecondDouble(std::max(0.0f, clip.duration));
        timeSpan.Set(startTime, stopTime);
        stack->SetLocalTimeSpan(timeSpan);

        for (const BoneFrame& frame : clip.frames)
        {
            FbxTime time;
            time.SetSecondDouble(frame.time);

            for (const BonePose& pose : frame.poses)
            {
                if (pose.node < 0 || pose.node >= static_cast<int>(model.nodes.size())) continue;
                FbxNode* targetNode = pose.node < static_cast<int>(sceneNodes.size()) ? sceneNodes[static_cast<size_t>(pose.node)] : nullptr;
                if (!targetNode || !IsSkeletonNode(targetNode)) continue;

                const SceneNode& sceneNode = model.nodes[static_cast<size_t>(pose.node)];

                FbxAMatrix global = MatrixFromPose(pose);
                FbxAMatrix local = global;
                const int parentIndex = sceneNode.parent;
                if (parentIndex >= 0)
                {
                    local = GetFrameNodeGlobalMatrix(model, frame, parentIndex).Inverse() * global;
                }

                AddVectorKey(targetNode->LclTranslation, layer, time, local.GetT());
                AddVectorKey(targetNode->LclRotation, layer, time, local.GetR());
                AddVectorKey(targetNode->LclScaling, layer, time, local.GetS());
            }
        }
    }

    return true;
}

void TraverseNode(FbxNode* node, int parentIndex, int depth, MeshBuilder& out)
{
    if (!node) return;

    const int nodeIndex = AddSceneNode(node, parentIndex, depth, out);

    const int attributeCount = node->GetNodeAttributeCount();
    for (int i = 0; i < attributeCount; ++i)
    {
        FbxNodeAttribute* attribute = node->GetNodeAttributeByIndex(i);
        if (attribute && attribute->GetAttributeType() == FbxNodeAttribute::eMesh)
        {
            AppendMesh(node, static_cast<FbxMesh*>(attribute), out);
        }
    }

    for (int i = 0; i < node->GetChildCount(); ++i)
    {
        TraverseNode(node->GetChild(i), nodeIndex, depth + 1, out);
    }
}

void PrepareMeshNormals(FbxNode* node, FbxGeometryConverter& converter)
{
    if (!node) return;

    for (int i = 0; i < node->GetNodeAttributeCount(); ++i)
    {
        FbxNodeAttribute* attribute = node->GetNodeAttributeByIndex(i);
        if (attribute && attribute->GetAttributeType() == FbxNodeAttribute::eMesh)
        {
            FbxMesh* mesh = static_cast<FbxMesh*>(attribute);
            if (mesh->GetElementNormalCount() == 0)
            {
                if (mesh->GetElementSmoothingCount() > 0)
                {
                    converter.ComputeEdgeSmoothingFromPolygonSmoothing(mesh);
                }
                mesh->GenerateNormals(false, false);
            }
        }
    }

    for (int i = 0; i < node->GetChildCount(); ++i)
    {
        PrepareMeshNormals(node->GetChild(i), converter);
    }
}

template <typename T>
T* CopyToRaylibBuffer(const std::vector<T>& values)
{
    if (values.empty()) return nullptr;

    const size_t byteCount = values.size() * sizeof(T);
    void* memory = MemAlloc(static_cast<unsigned int>(byteCount));
    if (!memory) return nullptr;

    std::memcpy(memory, values.data(), values.size() * sizeof(T));
    return static_cast<T*>(memory);
}

Vector3 ChoosePerpendicular(Vector3 normal)
{
    const Vector3 axis = std::fabs(normal.y) < 0.9f ? Vector3{ 0.0f, 1.0f, 0.0f } : Vector3{ 1.0f, 0.0f, 0.0f };
    return NormalizeOrFallback(Vector3CrossProduct(axis, normal), Vector3{ 1.0f, 0.0f, 0.0f });
}

std::vector<float> BuildTangents(const std::vector<float>& vertices, const std::vector<float>& normals, const std::vector<float>& texcoords)
{
    const size_t vertexCount = vertices.size() / 3;
    std::vector<float> tangents(vertexCount * 4, 0.0f);
    if (normals.size() < vertexCount * 3 || texcoords.size() < vertexCount * 2) return tangents;

    for (size_t vertex = 0; vertex + 2 < vertexCount; vertex += 3)
    {
        const size_t p0 = vertex * 3;
        const size_t p1 = (vertex + 1) * 3;
        const size_t p2 = (vertex + 2) * 3;
        const size_t uv0 = vertex * 2;
        const size_t uv1 = (vertex + 1) * 2;
        const size_t uv2 = (vertex + 2) * 2;

        const Vector3 v0{ vertices[p0], vertices[p0 + 1], vertices[p0 + 2] };
        const Vector3 v1{ vertices[p1], vertices[p1 + 1], vertices[p1 + 2] };
        const Vector3 v2{ vertices[p2], vertices[p2 + 1], vertices[p2 + 2] };
        const Vector2 t0{ texcoords[uv0], texcoords[uv0 + 1] };
        const Vector2 t1{ texcoords[uv1], texcoords[uv1 + 1] };
        const Vector2 t2{ texcoords[uv2], texcoords[uv2 + 1] };

        const Vector3 edge1 = Vector3Subtract(v1, v0);
        const Vector3 edge2 = Vector3Subtract(v2, v0);
        const Vector2 delta1{ t1.x - t0.x, t1.y - t0.y };
        const Vector2 delta2{ t2.x - t0.x, t2.y - t0.y };
        const float determinant = delta1.x * delta2.y - delta2.x * delta1.y;

        Vector3 tangent{};
        Vector3 bitangent{};
        if (std::fabs(determinant) > 0.00000001f)
        {
            const float inverseDeterminant = 1.0f / determinant;
            tangent = Vector3Scale(Vector3Subtract(Vector3Scale(edge1, delta2.y), Vector3Scale(edge2, delta1.y)), inverseDeterminant);
            bitangent = Vector3Scale(Vector3Subtract(Vector3Scale(edge2, delta1.x), Vector3Scale(edge1, delta2.x)), inverseDeterminant);
        }

        for (size_t local = 0; local < 3; ++local)
        {
            const size_t global = vertex + local;
            const size_t normalBase = global * 3;
            const Vector3 normal = NormalizeOrFallback(Vector3{ normals[normalBase], normals[normalBase + 1], normals[normalBase + 2] }, Vector3{ 0.0f, 1.0f, 0.0f });
            Vector3 orthogonalTangent = Vector3Subtract(tangent, Vector3Scale(normal, Vector3DotProduct(normal, tangent)));
            orthogonalTangent = NormalizeOrFallback(orthogonalTangent, ChoosePerpendicular(normal));
            const float handedness = Vector3DotProduct(Vector3CrossProduct(normal, orthogonalTangent), bitangent) < 0.0f ? -1.0f : 1.0f;
            const size_t tangentBase = global * 4;
            tangents[tangentBase] = orthogonalTangent.x;
            tangents[tangentBase + 1] = orthogonalTangent.y;
            tangents[tangentBase + 2] = orthogonalTangent.z;
            tangents[tangentBase + 3] = handedness;
        }
    }

    return tangents;
}

bool BuildRaylibModel(const MeshBuilder& builder, LoadedFbxModel& outModel, std::string& error)
{
    if (builder.vertices.empty() && builder.bones.empty())
    {
        error = "The FBX scene contains no renderable mesh triangles or skeleton bones.";
        return false;
    }

    outModel.bones = builder.bones;
    outModel.bonePoses = builder.bonePoses;
    outModel.nodes = builder.nodes;
    outModel.animations = builder.animations;
    outModel.bindVertices = builder.vertices;
    outModel.bindNormals = builder.normals;
    outModel.skinnedVertices = builder.skinnedVertices;
    outModel.meshControlPointIndices.clear();
    outModel.meshControlPointIndices.reserve(builder.renderVertices.size());
    for (const RenderVertexRef& ref : builder.renderVertices)
    {
        outModel.meshControlPointIndices.push_back(ref.controlPointIndex);
    }
    outModel.meshPolygonVertexGlobalIndices = builder.polygonVertexGlobalIndices;
    outModel.meshPolygonEdges = builder.polygonEdges;
    outModel.uvSetNames = builder.uvSetNames;
    outModel.uvSets = builder.uvSets;
    outModel.uvSetPresence = builder.uvSetPresence;
    outModel.materialNames = builder.materialNames.empty() ? std::vector<std::string>{ "Default" } : builder.materialNames;
    outModel.textureReferences = builder.textureReferences;
    outModel.bounds = builder.hasBounds ? builder.bounds : BoundingBox{ { -1.0f, -1.0f, -1.0f }, { 1.0f, 1.0f, 1.0f } };
    outModel.valid = true;

    if (builder.vertices.empty())
    {
        return true;
    }

    std::vector<std::vector<int>> verticesByMaterial(outModel.materialNames.size());
    for (int vertex = 0; vertex < builder.VertexCount(); ++vertex)
    {
        int materialIndex = vertex < static_cast<int>(builder.vertexMaterialIndices.size()) ? builder.vertexMaterialIndices[static_cast<size_t>(vertex)] : 0;
        materialIndex = std::max(0, std::min(materialIndex, static_cast<int>(verticesByMaterial.size()) - 1));
        verticesByMaterial[static_cast<size_t>(materialIndex)].push_back(vertex);
    }

    std::vector<Mesh> meshes;
    std::vector<int> meshMaterials;
    for (int materialIndex = 0; materialIndex < static_cast<int>(verticesByMaterial.size()); ++materialIndex)
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
            const int vertexIndex = globalVertex * 3;
            const int texcoordIndex = globalVertex * 2;
            vertices.push_back(builder.vertices[static_cast<size_t>(vertexIndex)]);
            vertices.push_back(builder.vertices[static_cast<size_t>(vertexIndex + 1)]);
            vertices.push_back(builder.vertices[static_cast<size_t>(vertexIndex + 2)]);
            normals.push_back(builder.normals[static_cast<size_t>(vertexIndex)]);
            normals.push_back(builder.normals[static_cast<size_t>(vertexIndex + 1)]);
            normals.push_back(builder.normals[static_cast<size_t>(vertexIndex + 2)]);
            texcoords.push_back(builder.texcoords[static_cast<size_t>(texcoordIndex)]);
            texcoords.push_back(builder.texcoords[static_cast<size_t>(texcoordIndex + 1)]);
        }
        std::vector<float> tangents = BuildTangents(vertices, normals, texcoords);

        Mesh mesh{};
        mesh.vertexCount = static_cast<int>(globalIndices.size());
        mesh.triangleCount = mesh.vertexCount / 3;
        mesh.vertices = CopyToRaylibBuffer(vertices);
        mesh.normals = CopyToRaylibBuffer(normals);
        mesh.texcoords = CopyToRaylibBuffer(texcoords);
        mesh.tangents = CopyToRaylibBuffer(tangents);

        if (!mesh.vertices || !mesh.normals || !mesh.texcoords || !mesh.tangents)
        {
            if (mesh.vertices) MemFree(mesh.vertices);
            if (mesh.normals) MemFree(mesh.normals);
            if (mesh.texcoords) MemFree(mesh.texcoords);
            if (mesh.tangents) MemFree(mesh.tangents);
            error = "Failed to allocate raylib mesh buffers.";
            return false;
        }

        UploadMesh(&mesh, true);
        meshes.push_back(mesh);
        meshMaterials.push_back(materialIndex);
        outModel.meshGlobalVertexIndices.push_back(globalIndices);
    }

    outModel.model.transform = MatrixIdentity();
    outModel.model.meshCount = static_cast<int>(meshes.size());
    outModel.model.materialCount = static_cast<int>(outModel.materialNames.size());
    outModel.model.meshes = CopyToRaylibBuffer(meshes);
    outModel.model.materials = static_cast<Material*>(MemAlloc(static_cast<unsigned int>(sizeof(Material) * outModel.model.materialCount)));
    outModel.model.meshMaterial = CopyToRaylibBuffer(meshMaterials);
    if (!outModel.model.meshes || !outModel.model.materials || !outModel.model.meshMaterial)
    {
        error = "Failed to allocate raylib model.";
        return false;
    }
    for (int i = 0; i < outModel.model.materialCount; ++i)
    {
        outModel.model.materials[i] = LoadMaterialDefault();
    }
    outModel.hasMesh = true;
    return true;
}
}

std::vector<std::string> GetSupportedModelExtensions()
{
    return { "fbx", "obj", "glb", "gltf", "blend", "dae", "stl" };
}

bool PrepareModelForOpening(const std::string& sourcePath, std::string& outputPath, std::string& error)
{
    std::string extension = std::filesystem::path(sourcePath).extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (extension == ".fbx")
    {
        outputPath = sourcePath;
        error.clear();
        return true;
    }
    return ConvertModelToFbx(sourcePath, outputPath, error);
}

bool ConvertModelToFbx(const std::string& sourcePath, std::string& outputPath, std::string& error)
{
    outputPath.clear();
    error.clear();
    std::unique_ptr<FbxManager, FbxManagerDestroy> manager(FbxManager::Create());
    if (!manager)
    {
        error = "Failed to create FBX SDK manager.";
        return false;
    }
    try
    {
        const auto source = std::filesystem::absolute(sourcePath);
        std::string extension = source.extension().string();
        std::transform(extension.begin(), extension.end(), extension.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        auto destination = source.parent_path() / (source.stem().string() + "_converted.fbx");
        for (int suffix = 2; std::filesystem::exists(destination); ++suffix)
            destination = source.parent_path() / (source.stem().string() + "_converted_" + std::to_string(suffix) + ".fbx");
        if (extension == ".glb" || extension == ".gltf" || extension == ".blend" || extension == ".stl")
        {
            if (!RunBlenderConversion(source.string(), destination.string(), error)) return false;
            outputPath = destination.string();
            return true;
        }
        const int reader = manager->GetIOPluginRegistry()->FindReaderIDByExtension(
            extension.empty() ? "" : extension.c_str() + 1);
        if (reader < 0)
        {
            error = "Unsupported model format: " + extension + ". Supported formats:";
            for (const auto& supported : GetSupportedModelExtensions()) error += " ." + supported;
            return false;
        }
        auto* settings = FbxIOSettings::Create(manager.get(), IOSROOT);
        manager->SetIOSettings(settings);
        auto* importer = FbxImporter::Create(manager.get(), "conversion-import");
        auto* scene = FbxScene::Create(manager.get(), "conversion-scene");
        if (!importer->Initialize(source.string().c_str(), reader, settings) || !importer->Import(scene))
        {
            error = std::string("Failed to import model for conversion: ") + importer->GetStatus().GetErrorString();
            return false;
        }
        importer->Destroy();

        // Resolve source-relative textures before exporting beside the source model.
        for (int i = 0; i < scene->GetSrcObjectCount<FbxFileTexture>(); ++i)
        {
            auto* texture = scene->GetSrcObject<FbxFileTexture>(i);
            std::filesystem::path texturePath(texture->GetFileName());
            if (texturePath.empty()) texturePath = texture->GetRelativeFileName();
            if (!texturePath.empty() && texturePath.is_relative())
                texture->SetFileName((source.parent_path() / texturePath).lexically_normal().string().c_str());
        }
        auto* exporter = FbxExporter::Create(manager.get(), "conversion-export");
        settings->SetBoolProp(EXP_FBX_EMBEDDED, true);
        if (!exporter->Initialize(destination.string().c_str(), manager->GetIOPluginRegistry()->GetNativeWriterFormat(), settings) ||
            !exporter->Export(scene))
        {
            error = std::string("Failed to convert model to FBX: ") + exporter->GetStatus().GetErrorString();
            exporter->Destroy();
            std::error_code cleanupError;
            std::filesystem::remove(destination, cleanupError);
            return false;
        }
        exporter->Destroy();
        outputPath = destination.string();
        return true;
    }
    catch (const std::filesystem::filesystem_error& exception)
    {
        error = std::string("Failed to convert model to FBX: ") + exception.what();
        return false;
    }
}

bool LoadFbxModel(const std::string& path, LoadedFbxModel& outModel, std::string& error)
{
    outModel = LoadedFbxModel{};
    error.clear();

    std::unique_ptr<FbxManager, FbxManagerDestroy> manager(FbxManager::Create());
    if (!manager)
    {
        error = "Failed to create FBX SDK manager.";
        return false;
    }

    FbxIOSettings* ioSettings = FbxIOSettings::Create(manager.get(), IOSROOT);
    manager->SetIOSettings(ioSettings);
    ioSettings->SetBoolProp(IMP_FBX_MATERIAL, true);
    ioSettings->SetBoolProp(IMP_FBX_TEXTURE, true);
    ioSettings->SetBoolProp(IMP_FBX_EXTRACT_EMBEDDED_DATA, true);

    FbxImporter* importer = FbxImporter::Create(manager.get(), "");
    if (!importer->Initialize(path.c_str(), -1, manager->GetIOSettings()))
    {
        error = std::string("Failed to open FBX file: ") + importer->GetStatus().GetErrorString();
        importer->Destroy();
        return false;
    }
    bool hasEmbeddedMedia = false;
    FbxEmbeddedFileCallback* embeddedFileCallback = FbxEmbeddedFileCallback::Create(manager.get(), "embedded-file-read");
    if (embeddedFileCallback)
    {
        embeddedFileCallback->RegisterReadFunction(MarkEmbeddedFileRead, &hasEmbeddedMedia);
        importer->SetEmbeddedFileReadCallback(embeddedFileCallback);
    }

    const std::filesystem::path embeddedExtractionDirectory = MakeUniqueEmbeddedTextureExtractionDirectory(path);
    std::error_code extractionDirectoryError;
    std::filesystem::create_directories(embeddedExtractionDirectory, extractionDirectoryError);
    if (!extractionDirectoryError)
    {
        const std::string extractionDirectoryString = embeddedExtractionDirectory.string();
        importer->SetEmbeddingExtractionFolder(extractionDirectoryString.c_str());
    }

    FbxScene* scene = FbxScene::Create(manager.get(), "scene");
    if (!importer->Import(scene))
    {
        error = std::string("Failed to import FBX scene: ") + importer->GetStatus().GetErrorString();
        importer->Destroy();
        RemoveEmbeddedTextureExtractionDirectory(embeddedExtractionDirectory);
        return false;
    }
    importer->Destroy();

    FbxAxisSystem::OpenGL.ConvertScene(scene);
    FbxSystemUnit::m.ConvertScene(scene);

    FbxGeometryConverter converter(manager.get());
    PrepareMeshNormals(scene->GetRootNode(), converter);

    MeshBuilder builder;
    TraverseNode(scene->GetRootNode(), -1, 0, builder);

    std::unordered_map<FbxNode*, FbxAMatrix> bindMatrices;
    CollectSkinBindMatrices(scene->GetRootNode(), bindMatrices);
    RebuildBindSkeleton(scene->GetRootNode(), builder, bindMatrices);
    RebuildSkinBindDataForAppPose(builder);

    SampleAnimations(scene, builder);
    CollectImportedTextureReferences(scene, path, embeddedExtractionDirectory, builder);

    if (!BuildRaylibModel(builder, outModel, error))
    {
        RemoveEmbeddedTextureExtractionDirectory(embeddedExtractionDirectory);
        return false;
    }

    outModel.sourceHasEmbeddedMedia = hasEmbeddedMedia;
    if (hasEmbeddedMedia && !extractionDirectoryError)
    {
        outModel.embeddedMediaExtractionDirectory = embeddedExtractionDirectory.string();
    }
    else
    {
        RemoveEmbeddedTextureExtractionDirectory(embeddedExtractionDirectory);
    }
    return true;
}

bool SaveFbxModelAnimations(const std::string& sourcePath,
                            const std::string& outputPath,
                            const LoadedFbxModel& model,
                            const std::vector<bool>& deletedNodes,
                            std::string& error)
{
    static const std::vector<FbxTextureReference> noTextureReferences;
    return model.sourceHasEmbeddedMedia
        ? SaveFbxModelAnimations(sourcePath, outputPath, model, deletedNodes, noTextureReferences, true, false, error)
        : SaveFbxModelAnimations(sourcePath, outputPath, model, deletedNodes, noTextureReferences, false, false, error);
}

bool SaveFbxModelAnimations(const std::string& sourcePath,
                            const std::string& outputPath,
                            const LoadedFbxModel& model,
                            const std::vector<bool>& deletedNodes,
                            const std::vector<FbxTextureReference>& textureReferences,
                            bool embedMedia,
                            bool replaceTextureReferences,
                            std::string& error)
{
    error.clear();
    if (sourcePath.empty() || outputPath.empty())
    {
        error = "Missing FBX save path.";
        return false;
    }

    const std::filesystem::path sourceFile(sourcePath);
    const std::filesystem::path outputFile(outputPath);
    std::error_code pathError;
    bool savingInPlace = std::filesystem::equivalent(sourceFile, outputFile, pathError);
    if (pathError)
    {
        savingInPlace = std::filesystem::absolute(sourceFile).lexically_normal() ==
                        std::filesystem::absolute(outputFile).lexically_normal();
    }

    std::filesystem::path exportFile = outputFile;
    if (savingInPlace)
    {
        for (int suffix = 0; suffix < 1000; ++suffix)
        {
            std::filesystem::path candidate = outputFile;
            candidate += ".openfbx_tmp_";
            candidate += std::to_string(suffix);
            candidate += outputFile.extension();
            std::error_code existsError;
            if (!std::filesystem::exists(candidate, existsError))
            {
                exportFile = candidate;
                break;
            }
        }
        if (exportFile == outputFile)
        {
            error = "Failed to choose a temporary FBX save path.";
            return false;
        }
    }

    std::unique_ptr<FbxManager, FbxManagerDestroy> manager(FbxManager::Create());
    if (!manager)
    {
        error = "Failed to create FBX SDK manager.";
        return false;
    }

    FbxIOSettings* ioSettings = FbxIOSettings::Create(manager.get(), IOSROOT);
    manager->SetIOSettings(ioSettings);
    ioSettings->SetBoolProp(IMP_FBX_MATERIAL, true);
    ioSettings->SetBoolProp(IMP_FBX_TEXTURE, true);
    const bool importEmbeddedMedia = embedMedia || model.sourceHasEmbeddedMedia;
    ioSettings->SetBoolProp(IMP_FBX_EXTRACT_EMBEDDED_DATA, importEmbeddedMedia);
    ioSettings->SetBoolProp(EXP_FBX_MATERIAL, true);
    ioSettings->SetBoolProp(EXP_FBX_TEXTURE, true);
    ioSettings->SetBoolProp(EXP_FBX_EMBEDDED, embedMedia);
    ioSettings->SetBoolProp(EXP_EMBEDTEXTURE, embedMedia);

    FbxImporter* importer = FbxImporter::Create(manager.get(), "");
    if (!importer->Initialize(sourcePath.c_str(), -1, manager->GetIOSettings()))
    {
        error = std::string("Failed to open source FBX: ") + importer->GetStatus().GetErrorString();
        importer->Destroy();
        return false;
    }
    const std::filesystem::path saveExtractionDirectory = importEmbeddedMedia
        ? MakeSaveEmbeddedTextureExtractionDirectory(sourcePath)
        : std::filesystem::path{};
    if (!saveExtractionDirectory.empty())
    {
        std::error_code extractionDirectoryError;
        std::filesystem::create_directories(saveExtractionDirectory, extractionDirectoryError);
        if (!extractionDirectoryError)
        {
            const std::string extractionDirectoryString = saveExtractionDirectory.string();
            importer->SetEmbeddingExtractionFolder(extractionDirectoryString.c_str());
        }
    }

    FbxScene* scene = FbxScene::Create(manager.get(), "scene");
    if (!importer->Import(scene))
    {
        error = std::string("Failed to import source FBX: ") + importer->GetStatus().GetErrorString();
        importer->Destroy();
        RemoveEmbeddedTextureExtractionDirectory(saveExtractionDirectory);
        return false;
    }
    importer->Destroy();

    FbxAxisSystem::OpenGL.ConvertScene(scene);
    FbxSystemUnit::m.ConvertScene(scene);

    const std::vector<FbxNode*> sceneNodes = BuildMappedSceneNodes(model, BuildSceneNodeIndex(scene));
    if (!WriteAnimationStacks(scene, model, sceneNodes, error))
    {
        RemoveEmbeddedTextureExtractionDirectory(saveExtractionDirectory);
        return false;
    }

    if (!ApplyEditedModelToScene(scene, model, deletedNodes, error))
    {
        RemoveEmbeddedTextureExtractionDirectory(saveExtractionDirectory);
        return false;
    }
    if (replaceTextureReferences)
    {
        ApplyTextureReferencesToScene(scene, textureReferences);
    }

    FbxExporter* exporter = FbxExporter::Create(manager.get(), "");
    const std::string exportPathString = exportFile.string();
    if (!exporter->Initialize(exportPathString.c_str(), -1, manager->GetIOSettings()))
    {
        error = std::string("Failed to initialize FBX exporter: ") + exporter->GetStatus().GetErrorString();
        exporter->Destroy();
        RemoveEmbeddedTextureExtractionDirectory(saveExtractionDirectory);
        return false;
    }

    const bool exported = exporter->Export(scene);
    if (!exported)
    {
        error = std::string("Failed to export FBX: ") + exporter->GetStatus().GetErrorString();
        exporter->Destroy();
        RemoveEmbeddedTextureExtractionDirectory(saveExtractionDirectory);
        return false;
    }
    exporter->Destroy();
    RemoveEmbeddedTextureExtractionDirectory(saveExtractionDirectory);

    if (savingInPlace)
    {
        std::error_code copyError;
        if (!std::filesystem::copy_file(exportFile, outputFile, std::filesystem::copy_options::overwrite_existing, copyError))
        {
            error = "Failed to replace original FBX after temporary export: " + copyError.message();
            return false;
        }

        std::error_code removeError;
        std::filesystem::remove(exportFile, removeError);
    }

    return true;
}

void UnloadFbxModel(LoadedFbxModel& model)
{
    RemoveEmbeddedTextureExtractionDirectory(model.embeddedMediaExtractionDirectory);

    if (model.hasMesh)
    {
        UnloadModel(model.model);
    }

    model = LoadedFbxModel{};
}
