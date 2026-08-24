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

struct BoneFrame
{
    float time = 0.0f;
    std::vector<BoneSegment> bones;
};

struct MeshFrame
{
    std::vector<float> vertices;
    std::vector<float> normals;
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
    BoundingBox bounds{};
    bool hasBounds = false;
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
    std::vector<BoneSegment> bones;
    std::vector<SceneNode> nodes;
    std::vector<AnimationClip> animations;
    bool hasMesh = false;
    bool valid = false;
};

bool LoadFbxModel(const std::string& path, LoadedFbxModel& outModel, std::string& error);
void UnloadFbxModel(LoadedFbxModel& model);
