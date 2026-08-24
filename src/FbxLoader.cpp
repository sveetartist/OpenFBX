#include "FbxLoader.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <memory>
#include <unordered_map>
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
    std::vector<unsigned int> indices;
    std::vector<RenderVertexRef> renderVertices;
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
    return Vector2{ static_cast<float>(v[0]), static_cast<float>(v[1]) };
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
                 const RenderVertexRef refs[3],
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

        out.vertices.push_back(points[i].x);
        out.vertices.push_back(points[i].y);
        out.vertices.push_back(points[i].z);

        out.normals.push_back(normal.x);
        out.normals.push_back(normal.y);
        out.normals.push_back(normal.z);

        out.texcoords.push_back(uv.x);
        out.texcoords.push_back(uv.y);

        out.AddBounds(points[i]);
        if (refs[i].node)
        {
            ExpandSceneNodeBounds(out, refs[i].node, points[i]);
        }
    }
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
    const FbxVector4* controlPoints = mesh->GetControlPoints();

    const FbxAMatrix nodeGlobal = node->EvaluateGlobalTransform();
    const FbxAMatrix geometry = GetNodeGeometryTransform(node);
    const FbxAMatrix meshTransform = nodeGlobal * geometry;
    const FbxAMatrix normalTransform = meshTransform.Inverse().Transpose();

    for (int polygon = 0; polygon < polygonCount; ++polygon)
    {
        if (mesh->GetPolygonSize(polygon) != 3) continue;

        Vector3 points[3]{};
        Vector3 normals[3]{};
        Vector2 uvs[3]{};
        RenderVertexRef refs[3]{};
        bool triangleHasNormals = true;
        bool triangleHasUvs = uvSetName != nullptr;

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
        }

        AddTriangle(out, points, normals, uvs, refs, triangleHasNormals, triangleHasUvs);
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
            SetSceneNodeTransform(out.nodes[static_cast<size_t>(nodeIndex->second)], GetBindOrEvaluatedGlobal(node, bindMatrices));
            out.bonePoses.push_back(MakeBonePose(GetBindOrEvaluatedGlobal(node, bindMatrices), nodeIndex->second));
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
    outModel.bounds = builder.hasBounds ? builder.bounds : BoundingBox{ { -1.0f, -1.0f, -1.0f }, { 1.0f, 1.0f, 1.0f } };
    outModel.valid = true;

    if (builder.vertices.empty())
    {
        return true;
    }

    Mesh mesh{};
    mesh.vertexCount = builder.VertexCount();
    mesh.triangleCount = mesh.vertexCount / 3;
    mesh.vertices = CopyToRaylibBuffer(builder.vertices);
    mesh.normals = CopyToRaylibBuffer(builder.normals);
    mesh.texcoords = CopyToRaylibBuffer(builder.texcoords);

    if (!mesh.vertices || !mesh.normals || !mesh.texcoords)
    {
        if (mesh.vertices) MemFree(mesh.vertices);
        if (mesh.normals) MemFree(mesh.normals);
        if (mesh.texcoords) MemFree(mesh.texcoords);
        error = "Failed to allocate raylib mesh buffers.";
        return false;
    }

    UploadMesh(&mesh, true);

    outModel.model = LoadModelFromMesh(mesh);
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

void UnloadFbxModel(LoadedFbxModel& model)
{
    if (model.hasMesh)
    {
        UnloadModel(model.model);
    }

    model = LoadedFbxModel{};
}
