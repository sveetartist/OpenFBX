#pragma once

#include <string>
#include <vector>

#include "raylib.h"

struct BoneSegment
{
    Vector3 start{};
    Vector3 end{};
    int startNode = -1;
    int endNode = -1;
};

struct BonePose
{
    Vector3 position{};
    Vector3 axisX{ 1.0f, 0.0f, 0.0f };
    Vector3 axisY{ 0.0f, 1.0f, 0.0f };
    Vector3 axisZ{ 0.0f, 0.0f, 1.0f };
    Vector3 rotation{};
    Vector3 scale{ 1.0f, 1.0f, 1.0f };
    int node = -1;
};

struct BoneFrame
{
    float time = 0.0f;
    std::vector<BoneSegment> bones;
    std::vector<BonePose> poses;
};

struct MeshFrame
{
    std::vector<float> vertices;
    std::vector<float> normals;
};

struct SkinnedVertexInfluence
{
    std::string boneName;
    float weight = 0.0f;
    Vector3 bindPositionInBone{};
    Vector3 bindNormalInBone{};
};

struct SkinnedVertex
{
    Vector3 bindPosition{};
    Vector3 bindNormal{ 0.0f, 1.0f, 0.0f };
    std::vector<SkinnedVertexInfluence> influences;
};

enum class SceneNodeType
{
    Empty,
    Mesh,
    Bone
};

struct SceneNode
{
    std::string name;
    int parent = -1;
    int depth = 0;
    SceneNodeType type = SceneNodeType::Empty;
    Vector3 position{};
    Vector3 axisX{ 1.0f, 0.0f, 0.0f };
    Vector3 axisY{ 0.0f, 1.0f, 0.0f };
    Vector3 axisZ{ 0.0f, 0.0f, 1.0f };
    Vector3 rotation{};
    Vector3 scale{ 1.0f, 1.0f, 1.0f };
    BoundingBox bounds{};
    bool hasBounds = false;
    int meshVertexStart = -1;
    int meshVertexCount = 0;
    int meshTriangleCount = 0;
    std::string materialName;
};

struct AnimationClip
{
    std::string name;
    float duration = 0.0f;
    std::vector<BoneFrame> frames;
    std::vector<MeshFrame> meshFrames;
};

struct LoadedFbxModel
{
    Model model{};
    BoundingBox bounds{};
    std::vector<float> bindVertices;
    std::vector<float> bindNormals;
    std::vector<SkinnedVertex> skinnedVertices;
    std::vector<std::vector<int>> meshGlobalVertexIndices;
    std::vector<std::string> uvSetNames;
    std::vector<std::vector<float>> uvSets;
    std::vector<std::string> materialNames;
    std::vector<BoneSegment> bones;
    std::vector<BonePose> bonePoses;
    std::vector<SceneNode> nodes;
    std::vector<AnimationClip> animations;
    bool hasMesh = false;
    bool valid = false;
};

bool LoadFbxModel(const std::string& path, LoadedFbxModel& outModel, std::string& error);
bool SaveFbxModelAnimations(const std::string& sourcePath, const std::string& outputPath, const LoadedFbxModel& model, std::string& error);
void UnloadFbxModel(LoadedFbxModel& model);
