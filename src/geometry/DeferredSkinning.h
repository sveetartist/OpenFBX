#pragma once

#include "FbxLoader.h"

struct SkinningInfluence
{
    int pose = -1;
    float weight = 0;
    Vector3 position{};
    Vector3 normal{};
};

struct SkinningVertex
{
    Vector3 position{};
    Vector3 normal{};
    size_t firstInfluence = 0;
    size_t influenceCount = 0;
};

// One immutable geometry snapshot is shared by every pending animation frame.
struct SkinningTopology
{
    std::vector<SkinningInfluence> influences;
    std::vector<int> nodePoseSlots;
    size_t poseCount = 0;
};

struct SkinningGeometry
{
    std::vector<SkinningVertex> vertices;
    std::shared_ptr<const SkinningTopology> topology;
};

struct DeferredSkinningFrame
{
    std::shared_ptr<const SkinningGeometry> geometry;
    std::vector<BonePose> poses;
    std::vector<unsigned char> present;
};

std::shared_ptr<const SkinningGeometry> CaptureSkinningGeometry(const LoadedFbxModel& model);
std::shared_ptr<const DeferredSkinningFrame> CaptureSkinningFrame(
    std::shared_ptr<const SkinningGeometry> geometry, const BoneFrame& frame);
void ResolveMeshFrame(MeshFrame& frame);
void ShareMeshFrame(MeshFrame& frame);
