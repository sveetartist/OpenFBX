#include "DeferredSkinning.h"
#include "core/MathUtils.h"
#include "raymath.h"
#include <unordered_map>
#include <algorithm>
#include <execution>
#include <numeric>

std::shared_ptr<const SkinningGeometry> CaptureSkinningGeometry(const LoadedFbxModel& model)
{
    auto result = std::make_shared<SkinningGeometry>();
    auto topology = std::make_shared<SkinningTopology>();
    std::unordered_map<std::string, int> slots;
    topology->nodePoseSlots.reserve(model.nodes.size());
    for (const SceneNode& node : model.nodes)
    {
        const auto entry = slots.emplace(node.name, static_cast<int>(slots.size()));
        topology->nodePoseSlots.push_back(entry.first->second);
    }
    topology->poseCount = slots.size();
    size_t influenceCount = 0;
    for (const auto& vertex : model.skinnedVertices) influenceCount += vertex.influences.size();
    result->vertices.reserve(model.skinnedVertices.size());
    topology->influences.reserve(influenceCount);
    for (const auto& vertex : model.skinnedVertices)
    {
        const size_t first = topology->influences.size();
        for (const auto& influence : vertex.influences)
        {
            const auto found = slots.find(influence.boneName);
            if (found == slots.end()) continue;
            topology->influences.push_back(SkinningInfluence{
                found->second, influence.weight, influence.bindPositionInBone, influence.bindNormalInBone});
        }
        result->vertices.push_back(SkinningVertex{
            vertex.bindPosition, vertex.bindNormal, first, topology->influences.size() - first});
    }
    result->topology = std::move(topology);
    return result;
}

std::shared_ptr<const DeferredSkinningFrame> CaptureSkinningFrame(
    std::shared_ptr<const SkinningGeometry> geometry, const BoneFrame& frame)
{
    auto result = std::make_shared<DeferredSkinningFrame>();
    result->geometry = std::move(geometry);
    result->poses.resize(result->geometry->topology->poseCount);
    result->present.resize(result->geometry->topology->poseCount, 0);
    for (const BonePose& pose : frame.poses)
    {
        if (pose.node < 0 || static_cast<size_t>(pose.node) >= result->geometry->topology->nodePoseSlots.size()) continue;
        const size_t slot = static_cast<size_t>(result->geometry->topology->nodePoseSlots[static_cast<size_t>(pose.node)]);
        // The original name lookup uses the last pose when names are duplicated.
        result->poses[slot] = pose;
        result->present[slot] = 1;
    }
    return result;
}

void ShareMeshFrame(MeshFrame& frame)
{
    if (frame.deferred || frame.shared || (frame.vertices.empty() && frame.normals.empty())) return;
    auto data = std::make_shared<MeshFrameData>();
    data->vertices = std::move(frame.vertices);
    data->normals = std::move(frame.normals);
    frame.shared = std::move(data);
}

void ResolveMeshFrame(MeshFrame& frame)
{
    if (frame.shared)
    {
        frame.vertices = frame.shared->vertices;
        frame.normals = frame.shared->normals;
        frame.shared.reset();
    }
    if (!frame.deferred) return;
    const auto& source = *frame.deferred;
    const auto& geometry = *source.geometry;
    frame.vertices.resize(geometry.vertices.size() * 3);
    frame.normals.resize(geometry.vertices.size() * 3);
    auto skinVertex = [&](size_t vertexIndex)
    {
        const auto& vertex = geometry.vertices[vertexIndex];
        const size_t base = vertexIndex * 3;
        Vector3 position{}, normal{};
        float totalWeight = 0;
        const size_t end = vertex.firstInfluence + vertex.influenceCount;
        for (size_t i = vertex.firstInfluence; i < end; ++i)
        {
            const auto& influence = geometry.topology->influences[i];
            const size_t slot = static_cast<size_t>(influence.pose);
            if (!source.present[slot]) continue;
            const BonePose& pose = source.poses[slot];
            auto transformVector = [&](Vector3 v) {
                return Vector3Add(Vector3Scale(pose.axisX, v.x * pose.scale.x),
                    Vector3Add(Vector3Scale(pose.axisY, v.y * pose.scale.y),
                               Vector3Scale(pose.axisZ, v.z * pose.scale.z)));
            };
            position = Vector3Add(position, Vector3Scale(Vector3Add(pose.position, transformVector(influence.position)), influence.weight));
            normal = Vector3Add(normal, Vector3Scale(transformVector(influence.normal), influence.weight));
            totalWeight += influence.weight;
        }
        if (totalWeight > 0.000001f)
        {
            position = Vector3Scale(position, 1.0f / totalWeight);
            normal = openfbx::NormalizeOrFallback(Vector3Scale(normal, 1.0f / totalWeight), vertex.normal);
        }
        else
        {
            position = vertex.position;
            normal = vertex.normal;
        }
        frame.vertices[base] = position.x;
        frame.vertices[base + 1] = position.y;
        frame.vertices[base + 2] = position.z;
        frame.normals[base] = normal.x;
        frame.normals[base + 1] = normal.y;
        frame.normals[base + 2] = normal.z;
    };
    constexpr size_t chunkVertices = 4096;
    if (geometry.vertices.size() < chunkVertices * 2)
    {
        for (size_t vertex = 0; vertex < geometry.vertices.size(); ++vertex) skinVertex(vertex);
    }
    else
    {
        std::vector<size_t> chunks((geometry.vertices.size() + chunkVertices - 1) / chunkVertices);
        std::iota(chunks.begin(), chunks.end(), size_t{0});
        std::for_each(std::execution::par, chunks.begin(), chunks.end(), [&](size_t chunk) {
            const size_t end = std::min((chunk + 1) * chunkVertices, geometry.vertices.size());
            for (size_t vertex = chunk * chunkVertices; vertex < end; ++vertex) skinVertex(vertex);
        });
    }
    frame.deferred.reset();
}
