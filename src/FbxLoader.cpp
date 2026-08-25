#include "FbxLoader.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
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
    std::vector<SkinnedVertex> skinnedVertices;
    std::vector<unsigned int> indices;
    std::vector<RenderVertexRef> renderVertices;
    std::vector<int> vertexMaterialIndices;
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
    FbxAMatrix inverseBindLink;
};

struct SkinBindData
{
    std::vector<std::vector<ControlPointBindInfluence>> influences;
    bool hasSkin = false;
};

FbxVector4 TransformVector(const FbxAMatrix& matrix, FbxVector4 vector);
SkinBindData BuildSkinBindData(FbxMesh* mesh);

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
                const FbxVector4 positionInBone = influence.inverseBindLink.MultT(bindPosition);
                const FbxVector4 normalInBone = TransformVector(influence.inverseBindLink, bindNormal);
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
    const SkinBindData skinBind = BuildSkinBindData(mesh);
    bool meshHadNormals = true;
    bool meshHadUvs = uvSetName != nullptr;
    int degenerateTriangles = 0;

    for (int polygon = 0; polygon < polygonCount; ++polygon)
    {
        if (mesh->GetPolygonSize(polygon) != 3) continue;

        Vector3 points[3]{};
        Vector3 normals[3]{};
        Vector2 uvs[3]{};
        std::vector<std::array<Vector2, 3>> uvSetValues(out.uvSets.size());
        RenderVertexRef refs[3]{};
        bool triangleHasNormals = true;
        bool triangleHasUvs = uvSetName != nullptr;
        const int localMaterialIndex = GetPolygonMaterialLocalIndex(mesh, polygon);
        const int materialIndex = GetOrAddMaterialIndex(out, GetNodeMaterialName(node, localMaterialIndex));

        for (int vertex = 0; vertex < 3; ++vertex)
        {
            const int controlPointIndex = mesh->GetPolygonVertex(polygon, vertex);
            refs[vertex] = RenderVertexRef{ node, mesh, controlPointIndex, FbxVector4(0.0, 1.0, 0.0, 0.0), false };
            points[vertex] = ToVector3(meshTransform.MultT(controlPoints[controlPointIndex]));

            FbxVector4 normal;
            if (mesh->GetPolygonVertexNormal(polygon, vertex, normal))
            {
                normal[3] = 0.0;
                refs[vertex].localNormal = normal;
                refs[vertex].hasNormal = true;
                normals[vertex] = ToVector3(TransformVector(normalTransform, normal));
            }
            else
            {
                triangleHasNormals = false;
            }

            if (uvSetName)
            {
                FbxVector2 uv;
                bool unmapped = false;
                if (mesh->GetPolygonVertexUV(polygon, vertex, uvSetName, uv, unmapped) && !unmapped)
                {
                    uvs[vertex] = ToVector2(uv);
                }
                else
                {
                    triangleHasUvs = false;
                }
            }

            for (int uvSet = 0; uvSet < uvSetNames.GetCount(); ++uvSet)
            {
                const int globalUvSet = uvSet < static_cast<int>(meshUvSetIndices.size()) ? meshUvSetIndices[static_cast<size_t>(uvSet)] : -1;
                if (globalUvSet < 0 || globalUvSet >= static_cast<int>(uvSetValues.size())) continue;

                FbxVector2 uv;
                bool unmapped = false;
                if (mesh->GetPolygonVertexUV(polygon, vertex, uvSetNames.GetStringAt(uvSet), uv, unmapped) && !unmapped)
                {
                    uvSetValues[static_cast<size_t>(globalUvSet)][vertex] = ToVector2(uv);
                }
            }
        }

        if (!triangleHasNormals)
        {
            meshHadNormals = false;
        }
        if (!triangleHasUvs)
        {
            meshHadUvs = false;
        }

        const Vector3 e0 = Vector3Subtract(points[1], points[0]);
        const Vector3 e1 = Vector3Subtract(points[2], points[0]);
        if (Vector3LengthSqr(Vector3CrossProduct(e0, e1)) <= 0.000000000001f)
        {
            ++degenerateTriangles;
        }

        AddTriangle(out, points, normals, uvs, uvSetValues, refs, skinBind, materialIndex, triangleHasNormals, triangleHasUvs);
    }

    const int meshVertexCount = out.VertexCount() - meshVertexStart;
    const auto foundNode = out.nodeToIndex.find(node);
    if (meshVertexCount > 0 && foundNode != out.nodeToIndex.end())
    {
        SceneNode& sceneNode = out.nodes[static_cast<size_t>(foundNode->second)];
        if (sceneNode.meshVertexStart < 0)
        {
            sceneNode.meshVertexStart = meshVertexStart;
        }
        sceneNode.meshVertexCount += meshVertexCount;
        sceneNode.meshTriangleCount += meshVertexCount / 3;
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

SkinBindData BuildSkinBindData(FbxMesh* mesh)
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

            FbxAMatrix bindLink;
            cluster->GetTransformLinkMatrix(bindLink);
            const FbxAMatrix inverseBindLink = bindLink.Inverse();

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
                    inverseBindLink
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

bool WriteAnimationStacks(FbxScene* scene, const LoadedFbxModel& model, std::string& error)
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

    std::unordered_map<std::string, FbxNode*> nodesByName;
    AddSkeletonNodesByName(scene->GetRootNode(), nodesByName);

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
                const SceneNode& sceneNode = model.nodes[static_cast<size_t>(pose.node)];
                const std::string targetName = sceneNode.sourceName.empty() ? sceneNode.name : sceneNode.sourceName;
                const auto target = nodesByName.find(targetName);
                if (target == nodesByName.end() || !target->second) continue;

                FbxAMatrix global = MatrixFromPose(pose);
                FbxAMatrix local = global;
                const int parentIndex = sceneNode.parent;
                if (parentIndex >= 0)
                {
                    local = GetFrameNodeGlobalMatrix(model, frame, parentIndex).Inverse() * global;
                }

                FbxNode* targetNode = target->second;
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
    outModel.uvSetNames = builder.uvSetNames;
    outModel.uvSets = builder.uvSets;
    outModel.materialNames = builder.materialNames.empty() ? std::vector<std::string>{ "Default" } : builder.materialNames;
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

    FbxImporter* importer = FbxImporter::Create(manager.get(), "");
    if (!importer->Initialize(path.c_str(), -1, manager->GetIOSettings()))
    {
        error = std::string("Failed to open FBX file: ") + importer->GetStatus().GetErrorString();
        importer->Destroy();
        return false;
    }

    FbxScene* scene = FbxScene::Create(manager.get(), "scene");
    if (!importer->Import(scene))
    {
        error = std::string("Failed to import FBX scene: ") + importer->GetStatus().GetErrorString();
        importer->Destroy();
        return false;
    }
    importer->Destroy();

    FbxAxisSystem::OpenGL.ConvertScene(scene);
    FbxSystemUnit::m.ConvertScene(scene);

    FbxGeometryConverter converter(manager.get());
    if (!converter.Triangulate(scene, true))
    {
        error = "FBX SDK triangulation failed.";
        return false;
    }
    PrepareMeshNormals(scene->GetRootNode(), converter);

    MeshBuilder builder;
    TraverseNode(scene->GetRootNode(), -1, 0, builder);

    std::unordered_map<FbxNode*, FbxAMatrix> bindMatrices;
    CollectSkinBindMatrices(scene->GetRootNode(), bindMatrices);
    RebuildBindSkeleton(scene->GetRootNode(), builder, bindMatrices);

    SampleAnimations(scene, builder);

    return BuildRaylibModel(builder, outModel, error);
}

bool SaveFbxModelAnimations(const std::string& sourcePath, const std::string& outputPath, const LoadedFbxModel& model, std::string& error)
{
    error.clear();
    if (sourcePath.empty() || outputPath.empty())
    {
        error = "Missing FBX save path.";
        return false;
    }

    std::unique_ptr<FbxManager, FbxManagerDestroy> manager(FbxManager::Create());
    if (!manager)
    {
        error = "Failed to create FBX SDK manager.";
        return false;
    }

    FbxIOSettings* ioSettings = FbxIOSettings::Create(manager.get(), IOSROOT);
    manager->SetIOSettings(ioSettings);

    FbxImporter* importer = FbxImporter::Create(manager.get(), "");
    if (!importer->Initialize(sourcePath.c_str(), -1, manager->GetIOSettings()))
    {
        error = std::string("Failed to open source FBX: ") + importer->GetStatus().GetErrorString();
        importer->Destroy();
        return false;
    }

    FbxScene* scene = FbxScene::Create(manager.get(), "scene");
    if (!importer->Import(scene))
    {
        error = std::string("Failed to import source FBX: ") + importer->GetStatus().GetErrorString();
        importer->Destroy();
        return false;
    }
    importer->Destroy();

    FbxAxisSystem::OpenGL.ConvertScene(scene);
    FbxSystemUnit::m.ConvertScene(scene);

    if (!WriteAnimationStacks(scene, model, error))
    {
        return false;
    }

    FbxExporter* exporter = FbxExporter::Create(manager.get(), "");
    if (!exporter->Initialize(outputPath.c_str(), -1, manager->GetIOSettings()))
    {
        error = std::string("Failed to initialize FBX exporter: ") + exporter->GetStatus().GetErrorString();
        exporter->Destroy();
        return false;
    }

    const bool exported = exporter->Export(scene);
    if (!exported)
    {
        error = std::string("Failed to export FBX: ") + exporter->GetStatus().GetErrorString();
        exporter->Destroy();
        return false;
    }
    exporter->Destroy();
    return true;
}

void UnloadFbxModel(LoadedFbxModel& model)
{
    if (model.hasMesh)
    {
        UnloadModel(model.model);
    }

    model = LoadedFbxModel{};
}
