#include <chrono>
#include <execution>
#include <stdexcept>
#include "editor/OpenFbxApp.cpp"

static void Require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

// Original skinning calculation retained as an independent regression oracle.
static MeshFrame ReferenceSkin(const LoadedFbxModel& target, const BoneFrame& frame)
{
    MeshFrame result;
    if (target.skinnedVertices.size() != target.bindVertices.size() / 3)
        return MeshFrame{target.bindVertices, target.bindNormals};
    std::unordered_map<std::string, const BonePose*> poses;
    for (const BonePose& pose : frame.poses)
        if (pose.node >= 0 && pose.node < static_cast<int>(target.nodes.size()))
            poses[target.nodes[static_cast<size_t>(pose.node)].name] = &pose;
    result.vertices.reserve(target.bindVertices.size());
    result.normals.reserve(target.bindNormals.size());
    for (const SkinnedVertex& vertex : target.skinnedVertices)
    {
        Vector3 position{}, normal{};
        float total = 0;
        for (const auto& influence : vertex.influences)
        {
            const auto found = poses.find(influence.boneName);
            if (found == poses.end()) continue;
            const BonePose& pose = *found->second;
            auto vector = [&](Vector3 v) {
                return Vector3Add(Vector3Scale(pose.axisX, v.x * pose.scale.x),
                    Vector3Add(Vector3Scale(pose.axisY, v.y * pose.scale.y),
                               Vector3Scale(pose.axisZ, v.z * pose.scale.z)));
            };
            position = Vector3Add(position, Vector3Scale(Vector3Add(pose.position, vector(influence.bindPositionInBone)), influence.weight));
            normal = Vector3Add(normal, Vector3Scale(vector(influence.bindNormalInBone), influence.weight));
            total += influence.weight;
        }
        if (total > 0.000001f)
        {
            position = Vector3Scale(position, 1.0f / total);
            normal = NormalizeOrFallback(Vector3Scale(normal, 1.0f / total), vertex.bindNormal);
        }
        else
        {
            position = vertex.bindPosition;
            normal = vertex.bindNormal;
        }
        result.vertices.insert(result.vertices.end(), {position.x, position.y, position.z});
        result.normals.insert(result.normals.end(), {normal.x, normal.y, normal.z});
    }
    return result;
}

static void Equal(const MeshFrame& a, const MeshFrame& b)
{
    Require(a.vertices == b.vertices, "Vertex results differ from original skinning");
    Require(a.normals == b.normals, "Normal results differ from original skinning");
}

static void TestVisibility()
{
    ModelTab tab;
    tab.loaded.nodes.resize(100);
    for (int i = 1; i < 100; ++i)
    {
        auto& node = tab.loaded.nodes[static_cast<size_t>(i)];
        node.parent = 0;
        node.type = i % 3 ? SceneNodeType::Bone : SceneNodeType::Mesh;
        node.meshVertexStart = i * 7;
        node.meshVertexCount = 40;
    }
    tab.deletedNodes.resize(100, false);
    tab.deletedNodes[6] = true;
    for (int isolated : {-1, 3, 6, 0})
    {
        tab.isolatedNode = isolated;
        const auto hidden = BuildHiddenMeshNodes(tab, 720);
        for (int vertex = 0; vertex < 720; ++vertex)
        {
            int expected = -1;
            for (int index = 0; index < 100; ++index)
            {
                const auto& node = tab.loaded.nodes[static_cast<size_t>(index)];
                if (node.type != SceneNodeType::Mesh || node.meshVertexStart < 0 || node.meshVertexCount <= 0) continue;
                if (vertex >= node.meshVertexStart && vertex < node.meshVertexStart + node.meshVertexCount)
                {
                    if (!IsViewportNodeVisible(tab, index)) expected = index;
                    break;
                }
            }
            Require(std::max(-1, hidden[static_cast<size_t>(vertex)]) == expected, "Visibility differs from original lookup");
        }
    }
    tab.deletedNodes.clear();
    tab.isolatedNode = -1;
    Require(BuildHiddenMeshNodes(tab, 720).empty(), "Unfiltered visibility should require no lookup");
}

static void TestTangents()
{
    std::vector<float> vertices(10002 * 3), normals(vertices.size()), uv(10002 * 2);
    for (size_t i = 0; i < 10002; ++i)
    {
        vertices[i * 3] = float(i % 7);
        vertices[i * 3 + 1] = float(i % 13);
        vertices[i * 3 + 2] = float(i % 17);
        normals[i * 3 + 1] = i % 11 ? 2.0f : 0.0f;
        uv[i * 2] = float(i % 5);
        uv[i * 2 + 1] = float(i % 3);
    }
    std::vector<float> parallel, sequential;
    BuildMeshTangents(vertices, normals, uv.data(), parallel);
    for (size_t first = 0; first < 10002; first += 3000)
    {
        const size_t end = std::min(first + 3000, size_t{10002});
        const std::vector<float> v(vertices.begin() + first * 3, vertices.begin() + end * 3);
        const std::vector<float> n(normals.begin() + first * 3, normals.begin() + end * 3);
        std::vector<float> result;
        BuildMeshTangents(v, n, uv.data() + first * 2, result);
        sequential.insert(sequential.end(), result.begin(), result.end());
    }
    Require(parallel == sequential, "Parallel tangents differ from sequential output");
}

static int BenchmarkFbx(const char* path)
{
    SetTraceLogLevel(LOG_WARNING);
    SetConfigFlags(FLAG_WINDOW_HIDDEN);
    InitWindow(640, 480, "Transform benchmark");
    Require(IsWindowReady(), "Could not create test rendering context");
    ModelTab tab;
    std::string error;
    Require(LoadFbxModel(path, tab.loaded, error), error.c_str());
    tab.currentVertices = tab.loaded.bindVertices;
    tab.currentNormals = tab.loaded.bindNormals;
    tab.deletedNodes.resize(tab.loaded.nodes.size(), false);
    for (int i = 0; i < static_cast<int>(tab.loaded.nodes.size()); ++i)
        if (tab.loaded.nodes[static_cast<size_t>(i)].type == SceneNodeType::Bone && IsValidSelectableNode(tab, i))
        {
            SetSingleSelectedNode(tab, i);
            break;
        }
    Require(tab.selectedNode >= 0, "Fixture has no selectable bone");
    auto stage = [](const char* name, auto work) {
        const auto start = std::chrono::steady_clock::now();
        for (int i = 0; i < 10; ++i) work();
        std::cout << name << ": " << std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count() / 10 << " ms\n";
    };
    stage("Bind skinning", [&] { BuildSkinnedMeshFrame(tab.loaded, BuildBindBoneFrame(tab.loaded)); });
    stage("Geometry capture", [&] { CaptureSkinningGeometry(tab.loaded); });
    stage("Mesh upload with tangents", [&] { RefreshDisplayedMesh(tab); });
    stage("Bone-frame transform", [&] {
        TransformBoneData(tab, tab.selectedNode, [](Vector3 p) { return p; }, [](Vector3 n) { return n; },
            TransformTool::Rotate, TransformAxis::Y, 0);
    });
    size_t frames = 0;
    for (const auto& clip : tab.loaded.animations) frames += clip.frames.size();
    for (int clipIndex : {-1, tab.loaded.animations.empty() ? -1 : 0})
    {
        tab.animation.clipIndex = clipIndex;
        std::vector<double> timings;
        for (int step = 0; step < 25; ++step)
        {
            const auto start = std::chrono::steady_clock::now();
            RotateSelectedSubtree(tab, tab.loaded.nodes[static_cast<size_t>(tab.selectedNode)].position,
                TransformAxis::Y, Vector3{0, 1, 0}, 0.005f);
            ApplyAnimatedMeshFrame(tab);
            ApplyAnimatedBoneFrame(tab);
            const double elapsed = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
            if (step > 0) timings.push_back(elapsed);
        }
        std::sort(timings.begin(), timings.end());
        std::cout << "FBX " << path << ": " << tab.loaded.bindVertices.size() / 3 << " vertices, " << frames
                  << " animation frames, " << (clipIndex < 0 ? "bind pose" : "animation pose")
                  << "; transform + mesh upload median " << timings[timings.size() / 2]
                  << " ms, max " << timings.back() << " ms\n";
    }
    UnloadFbxModel(tab.loaded);
    CloseWindow();
    return 0;
}

int main(int argc, char** argv)
{
    try
    {
        if (argc > 1) return BenchmarkFbx(argv[1]);
        TestVisibility();
        TestTangents();
        ModelTab tab;
        auto& loaded = tab.loaded;
        loaded.hasMesh = true;
        constexpr int vertexCount = 20000;
        constexpr int boneCount = 96;
        loaded.nodes.resize(boneCount);
        BoneFrame frame;
        for (int bone = 0; bone < boneCount; ++bone)
        {
            auto& node = loaded.nodes[static_cast<size_t>(bone)];
            node.name = "Bone" + std::to_string(bone % (boneCount - 1));
            node.parent = bone - 1;
            node.type = SceneNodeType::Bone;
            BonePose pose;
            pose.node = bone;
            pose.position = Vector3{float(bone), 1.0f, -2.0f};
            pose.scale = Vector3{1.2f, -0.5f, 0.0f};
            frame.poses.push_back(pose);
            frame.bones.push_back(BoneSegment{{}, {}, bone - 1, bone});
        }
        loaded.bindVertices.resize(vertexCount * 3);
        loaded.bindNormals.resize(vertexCount * 3);
        loaded.skinnedVertices.resize(vertexCount);
        for (int v = 0; v < vertexCount; ++v)
        {
            auto& vertex = loaded.skinnedVertices[static_cast<size_t>(v)];
            vertex.bindPosition = Vector3{float(v % 13), 2, 3};
            for (int influence = 0; influence < (v % 7); ++influence)
                vertex.influences.push_back(SkinnedVertexInfluence{
                    v % 11 == 0 ? "Missing" : "Bone" + std::to_string((v + influence) % boneCount),
                    0.2f, vertex.bindPosition, Vector3{0, 1, 0}});
        }
        Equal(ReferenceSkin(loaded, frame), BuildSkinnedMeshFrame(loaded, frame));
        loaded.animations.resize(1);
        auto& clip = loaded.animations.front();
        clip.frames.assign(60, frame);
        for (size_t i = 0; i < clip.frames.size(); ++i)
        {
            clip.frames[i].poses[0].position.x += float(i);
            if (i % 2) std::reverse(clip.frames[i].poses.begin(), clip.frames[i].poses.end());
        }
        auto measure = [](auto work) {
            const auto start = std::chrono::steady_clock::now();
            work();
            return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        };
        std::vector<MeshFrame> expected;
        const double originalMs = measure([&] {
            for (const auto& pose : clip.frames) expected.push_back(ReferenceSkin(loaded, pose));
        });
        Require(RebuildSkinnedAnimationMeshFrames(tab), "Rebuild failed");
        std::vector<MeshFrame> previousOptimization(clip.frames.size());
        auto eagerRebuild = [&] {
            std::for_each(std::execution::par, clip.frames.cbegin(), clip.frames.cend(), [&](const BoneFrame& pose) {
                BuildSkinnedMeshFrameInto(loaded, pose, previousOptimization[static_cast<size_t>(&pose - clip.frames.data())]);
            });
        };
        eagerRebuild();
        const double previousMs = measure(eagerRebuild);
        const double optimizedMs = measure([&] {
            RebuildSkinnedAnimationMeshFrames(tab);
            ResolveMeshFrame(clip.meshFrames[0]);
            ResolveMeshFrame(clip.meshFrames[1]);
        });
        Require(clip.meshFrames[2].vertices.empty() && clip.meshFrames[2].deferred,
            "Unseen frames should not be skinned during a drag");
        for (size_t i = 0; i < expected.size(); ++i)
        {
            MeshFrame copy = clip.meshFrames[i];
            ResolveMeshFrame(copy);
            Equal(expected[i], copy);
        }
        const auto frozenFrames = clip.meshFrames;

        const auto before = clip.frames;
        const Vector3 delta{1, -2, 3};
        TransformBoneData(tab, 48, [&](Vector3 p) { return Vector3Add(p, delta); },
            [](Vector3 v) { return v; }, TransformTool::Move, TransformAxis::None, 0);
        for (size_t i = 0; i < clip.frames.size(); ++i)
            for (size_t p = 0; p < clip.frames[i].poses.size(); ++p)
            {
                const auto& old = before[i].poses[p];
                const auto& current = clip.frames[i].poses[p];
                const Vector3 wanted = IsNodeInTransformScope(loaded, old.node, 48) ? Vector3Add(old.position, delta) : old.position;
                Require(Vector3Distance(wanted, current.position) == 0, "Transform scope changed");
            }
        RebuildSkinnedAnimationMeshFrames(tab);
        for (size_t i = 0; i < clip.frames.size(); ++i)
        {
            ResolveMeshFrame(clip.meshFrames[i]);
            Equal(ReferenceSkin(loaded, clip.frames[i]), clip.meshFrames[i]);
            MeshFrame frozen = frozenFrames[i];
            ResolveMeshFrame(frozen);
            Equal(expected[i], frozen);
        }
        // Playback must resolve the two interpolation endpoints without changing the sample.
        RebuildSkinnedAnimationMeshFrames(tab);
        clip.duration = 1.0f;
        tab.animation.clipIndex = 0;
        tab.animation.time = 0.5f;
        ApplyAnimatedMeshFrame(tab);
        Require(tab.appliedMeshFrameIndex >= 0, "Playback failed to resolve pending frames");
        const MeshFrameSample sample = GetMeshFrameSample(clip, tab.animation.time);
        const auto first = ReferenceSkin(loaded, clip.frames[static_cast<size_t>(sample.first)]);
        const auto second = ReferenceSkin(loaded, clip.frames[static_cast<size_t>(sample.second)]);
        for (size_t i = 0; i < first.vertices.size(); ++i)
            Require(tab.currentVertices[i] == first.vertices[i] * (1.0f - sample.alpha) + second.vertices[i] * sample.alpha,
                "Deferred playback changed interpolation");
        // Mesh edits must consume the old deferred result before changing its inputs.
        SceneNode mesh;
        mesh.type = SceneNodeType::Mesh;
        mesh.meshVertexStart = 0;
        mesh.meshVertexCount = vertexCount;
        loaded.nodes.push_back(mesh);
        RebuildSkinnedAnimationMeshFrames(tab);
        std::vector<MeshFrame> beforeMeshEdit;
        for (const auto& pose : clip.frames) beforeMeshEdit.push_back(ReferenceSkin(loaded, pose));
        Require(FlipMeshNormals(tab, static_cast<int>(loaded.nodes.size() - 1)), "Normal edit failed");
        for (size_t i = 0; i < clip.frames.size(); ++i)
        {
            for (float& normal : beforeMeshEdit[i].normals) normal = -normal;
            Equal(beforeMeshEdit[i], clip.meshFrames[i]);
        }
        RebuildSkinnedAnimationMeshFrames(tab);
        beforeMeshEdit.clear();
        for (const auto& pose : clip.frames) beforeMeshEdit.push_back(ReferenceSkin(loaded, pose));
        TransformMeshNodeRange(tab, loaded.nodes.back(), [delta](Vector3 p) { return Vector3Add(p, delta); },
            [](Vector3 n) { return n; });
        for (size_t i = 0; i < clip.frames.size(); ++i)
        {
            for (size_t v = 0; v < beforeMeshEdit[i].vertices.size(); v += 3)
            {
                beforeMeshEdit[i].vertices[v] += delta.x;
                beforeMeshEdit[i].vertices[v + 1] += delta.y;
                beforeMeshEdit[i].vertices[v + 2] += delta.z;
            }
            Equal(beforeMeshEdit[i], clip.meshFrames[i]);
        }
        // Later name/weight/pivot changes cannot alter a previously captured mesh or undo snapshot.
        RebuildSkinnedAnimationMeshFrames(tab);
        MeshFrame frozen = clip.meshFrames[0];
        const auto frozenExpected = ReferenceSkin(loaded, clip.frames[0]);
        loaded.nodes[0].name = "Renamed";
        loaded.skinnedVertices[1].influences.clear();
        clip.frames[0].poses[0].position.x += 500;
        ResolveMeshFrame(frozen);
        Equal(frozenExpected, frozen);
        loaded.valid = true;
        loaded.bonePoses = frame.poses;
        loaded.bones = frame.bones;
        tab.skinningGeometry.reset();
        tab.animation.clipIndex = -1;
        SetSingleSelectedNode(tab, 48);
        for (int step = 0; step < 3; ++step)
        {
            MoveSelectedSubtree(tab, delta);
            Require(tab.appliedClipIndex == -1, "A completed bind-pose upload was invalidated");
            Equal(MeshFrame{loaded.bindVertices, loaded.bindNormals}, BuildCurrentBindSkin(tab));
            for (size_t i = 0; i < clip.frames.size(); ++i)
            {
                MeshFrame result = clip.meshFrames[i];
                ResolveMeshFrame(result);
                Equal(ReferenceSkin(loaded, clip.frames[i]), result);
            }
        }
        ResolveMeshFrame(clip.meshFrames[0]);
        const float* capturedVertices = clip.meshFrames[0].vertices.data();
        const auto snapshot = CaptureEditSnapshot(tab);
        Require(snapshot.animations[0].meshFrames[0].shared == clip.meshFrames[0].shared,
            "Undo should share the animation cache");
        Require(snapshot.animations[0].meshFrames[0].shared->vertices.data() == capturedVertices,
            "Undo copied a cached vertex buffer");
        ResolveMeshFrame(clip.meshFrames[0]);
        clip.meshFrames[0].vertices[0] += 200;
        MeshFrame unchangedSnapshot = snapshot.animations[0].meshFrames[0];
        ResolveMeshFrame(unchangedSnapshot);
        Require(unchangedSnapshot.vertices[0] != clip.meshFrames[0].vertices[0], "Mesh edit changed undo data");
        const auto snapshotFrame = ReferenceSkin(loaded, clip.frames[0]);
        RenameSceneNode(tab, 48, "EditedBone");
        Require(!tab.skinningGeometry, "Rename did not invalidate the skinning cache");
        MoveSelectedSubtree(tab, delta);
        RestoreEditSnapshot(tab, snapshot);
        Require(!tab.skinningGeometry, "Undo did not invalidate the skinning cache");
        MeshFrame restored = tab.loaded.animations[0].meshFrames[0];
        ResolveMeshFrame(restored);
        Equal(snapshotFrame, restored);
        loaded.skinnedVertices.pop_back();
        Equal(ReferenceSkin(loaded, frame), BuildSkinnedMeshFrame(loaded, frame));
        std::cout << "PASS: skinning, repeated rebuilds, duplicate/missing bones, varying pose order, transform scope, visibility\n"
                  << "Synthetic rebuild (20000 vertices, 96 bones, 60 frames): original " << originalMs
                  << " ms; previous parallel " << previousMs << " ms; deferred + 2 displayed frames " << optimizedMs
                  << " ms; improvement over previous " << previousMs / optimizedMs << "x\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
