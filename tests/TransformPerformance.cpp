#include <chrono>
#include <fbxsdk.h>
#include <execution>
#include <functional>
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

static void TestMarqueeSelection()
{
    ModelTab tab;
    tab.loaded.valid = true;
    tab.loaded.hasMesh = true;
    tab.orbit.camera = Camera3D{ Vector3{ 0, 0, 10 }, Vector3{}, Vector3{ 0, 1, 0 }, 90.0f, CAMERA_PERSPECTIVE };
    tab.loaded.nodes.resize(5);
    for (int index = 1; index < 5; ++index) tab.loaded.nodes[index].parent = 0;
    tab.loaded.nodes[1].type = SceneNodeType::Mesh;
    tab.loaded.nodes[1].meshVertexStart = 0;
    tab.loaded.nodes[1].meshVertexCount = 3;
    tab.loaded.nodes[2].type = SceneNodeType::Empty;
    tab.loaded.nodes[2].position = Vector3{ 3, 0, 0 };
    tab.loaded.nodes[3].type = SceneNodeType::Bone;
    tab.loaded.nodes[4].type = SceneNodeType::Mesh;
    tab.loaded.nodes[4].meshVertexStart = 3;
    tab.loaded.nodes[4].meshVertexCount = 3;
    tab.loaded.bindVertices = { -2,-2,0, 2,-2,0, 0,2,0, -2,-2,20, 2,-2,20, 0,2,20 };
    BonePose pose;
    pose.node = 3;
    tab.loaded.bonePoses.push_back(pose);
    VisibilityState visibility;
    visibility.geometry = true;
    visibility.bones = false;
    visibility.empties = false;
    const Rectangle center = GetMarqueeRectangle(Vector2{ 410,310 }, Vector2{ 390,290 });
    SelectNodesInMarquee(tab, center, visibility, false, 800, 600);
    Require(tab.selectedNodes == std::vector<int>{1}, "Marquee should hit a face interior and reject geometry behind the camera");
    visibility.geometry = false;
    visibility.empties = true;
    SelectNodesInMarquee(tab, Rectangle{ 480,290,20,20 }, visibility, true, 800, 600);
    Require(tab.selectedNodes == std::vector<int>({1,2}), "Shift marquee must preserve and add selection");
    SelectNodesInMarquee(tab, Rectangle{ 480,290,20,20 }, visibility, true, 800, 600);
    Require(tab.selectedNodes == std::vector<int>({1,2}), "Shift marquee must not toggle or duplicate existing selection");
    SelectNodesInMarquee(tab, Rectangle{ 0,0,5,5 }, visibility, true, 800, 600);
    Require(tab.selectedNodes.size() == 2, "Empty additive marquee cleared selection");
    SelectNodesInMarquee(tab, Rectangle{ 0,0,5,5 }, visibility, false, 800, 600, true);
    Require(tab.selectedNodes.size() == 2, "Empty subtractive marquee cleared selection");
    SelectNodesInMarquee(tab, Rectangle{ 480,290,20,20 }, visibility, true, 800, 600, true);
    Require(tab.selectedNodes == std::vector<int>{1} && tab.selectedNode == 1, "Ctrl must subtract with precedence over Shift and update the active node");
    SelectNodesInMarquee(tab, Rectangle{ 480,290,20,20 }, visibility, false, 800, 600, true);
    Require(tab.selectedNodes == std::vector<int>{1}, "Repeated subtraction must not add an unselected node");
    visibility.geometry = true;
    SelectNodesInMarquee(tab, center, visibility, false, 800, 600, true);
    Require(tab.selectedNodes.empty() && tab.selectedNode == -1, "Subtracting the last node left a stale active selection");
    visibility.geometry = false;
    SetSingleSelectedNode(tab, 2);
    SelectNodesInMarquee(tab, Rectangle{ 0,0,5,5 }, visibility, false, 800, 600);
    Require(tab.selectedNodes.empty() && tab.selectedNode == -1, "Empty replacement marquee must clear selection");
    visibility.bones = true;
    SelectNodesInMarquee(tab, center, visibility, false, 800, 600);
    Require(tab.selectedNodes == std::vector<int>{3}, "Marquee must select displayed bone poses");
    visibility.bones = false;
    visibility.empties = false;
    visibility.geometry = true;
    tab.deletedNodes.assign(5, false);
    tab.deletedNodes[1] = true;
    SelectNodesInMarquee(tab, center, visibility, false, 800, 600);
    Require(tab.selectedNodes.empty(), "Marquee selected deleted geometry");
    tab.deletedNodes[1] = false;
    tab.isolatedNode = 2;
    SelectNodesInMarquee(tab, center, visibility, false, 800, 600);
    Require(tab.selectedNodes.empty(), "Marquee selected geometry outside isolation");
    tab.isolatedNode = -1;
    tab.currentVertices = tab.loaded.bindVertices;
    for (size_t index = 0; index < tab.currentVertices.size(); index += 3) tab.currentVertices[index] += 50;
    SelectNodesInMarquee(tab, center, visibility, false, 800, 600);
    Require(tab.selectedNodes.empty(), "Marquee used bind geometry instead of the displayed frame");
    const Vector3 crossing[] = { {-2,0,0}, {2,0,0} };
    Require(IntersectsMarquee(crossing, 2, tab.orbit.camera, center, 800, 600), "Marquee missed a crossing bone segment");
    const Vector3 behind[] = { {-2,0,20}, {2,0,20} };
    Require(!IntersectsMarquee(behind, 2, tab.orbit.camera, center, 800, 600), "Marquee selected a segment behind the camera");
    tab.orbit.camera.projection = CAMERA_ORTHOGRAPHIC;
    tab.orbit.camera.fovy = 10;
    tab.currentVertices.clear();
    SelectNodesInMarquee(tab, center, visibility, false, 800, 600);
    Require(tab.selectedNodes == std::vector<int>{1}, "Orthographic marquee failed");
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

static void TestMergeSelectedGeometryPreservesSkinning()
{
    SetConfigFlags(FLAG_WINDOW_HIDDEN);
    InitWindow(64, 64, "Merge regression");
    Require(IsWindowReady(), "Could not create merge test rendering context");
    ModelTab tab;
    tab.loaded.valid = true;
    tab.loaded.hasMesh = true;
    tab.loaded.materialNames = { "Default" };
    tab.loaded.uvSetNames = { "UVSet" };
    tab.loaded.uvSets.resize(1);
    tab.loaded.nodes.resize(3);
    tab.loaded.nodes[0].name = "Bone";
    tab.loaded.nodes[0].parent = -1;
    tab.loaded.nodes[0].type = SceneNodeType::Bone;
    tab.loaded.nodes[1].name = "MeshA";
    tab.loaded.nodes[1].parent = 0;
    tab.loaded.nodes[1].type = SceneNodeType::Mesh;
    tab.loaded.nodes[1].meshVertexStart = 0;
    tab.loaded.nodes[1].meshVertexCount = 3;
    tab.loaded.nodes[1].meshTriangleCount = 1;
    tab.loaded.nodes[1].meshPolygonCount = 1;
    tab.loaded.nodes[1].meshHasSkin = true;
    tab.loaded.nodes[2].name = "MeshB";
    tab.loaded.nodes[2].parent = 0;
    tab.loaded.nodes[2].type = SceneNodeType::Mesh;
    tab.loaded.nodes[2].meshVertexStart = 3;
    tab.loaded.nodes[2].meshVertexCount = 3;
    tab.loaded.nodes[2].meshTriangleCount = 1;
    tab.loaded.nodes[2].meshPolygonCount = 1;
    tab.loaded.nodes[2].meshHasSkin = true;
    tab.deletedNodes.assign(tab.loaded.nodes.size(), false);
    tab.collapsedNodes.assign(tab.loaded.nodes.size(), false);

    tab.loaded.bindVertices = {
        0.0f, 0.0f, 0.0f,
        1.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f,
        2.0f, 0.0f, 0.0f,
        3.0f, 0.0f, 0.0f,
        2.0f, 1.0f, 0.0f
    };
    tab.loaded.bindNormals.assign(tab.loaded.bindVertices.size(), 0.0f);
    for (size_t normal = 1; normal < tab.loaded.bindNormals.size(); normal += 3)
    {
        tab.loaded.bindNormals[normal] = 1.0f;
    }
    tab.loaded.uvSets[0] = {
        0.0f, 0.0f,
        1.0f, 0.0f,
        0.0f, 1.0f,
        0.0f, 0.0f,
        1.0f, 0.0f,
        0.0f, 1.0f
    };
    tab.loaded.meshGlobalVertexIndices = { { 0, 1, 2, 3, 4, 5 } };
    tab.loaded.meshControlPointIndices = { 0, 1, 2, 0, 1, 2 };
    tab.loaded.meshPolygonVertexGlobalIndices = { 0, 1, 2, 3, 4, 5 };
    tab.loaded.meshPolygonEdges = {
        MeshEdge{ 0, 1, 1 }, MeshEdge{ 1, 2, 1 }, MeshEdge{ 2, 0, 1 },
        MeshEdge{ 3, 4, 2 }, MeshEdge{ 4, 5, 2 }, MeshEdge{ 5, 3, 2 }
    };

    BonePose pose;
    pose.node = 0;
    pose.position = Vector3Zero();
    tab.loaded.bonePoses = { pose };
    for (int vertex = 0; vertex < 6; ++vertex)
    {
        const size_t base = static_cast<size_t>(vertex) * 3;
        SkinnedVertex skinned;
        skinned.bindPosition = Vector3{ tab.loaded.bindVertices[base], tab.loaded.bindVertices[base + 1], tab.loaded.bindVertices[base + 2] };
        skinned.bindNormal = Vector3{ 0.0f, 1.0f, 0.0f };
        skinned.influences.push_back(SkinnedVertexInfluence{ "Bone", 1.0f, skinned.bindPosition, skinned.bindNormal });
        tab.loaded.skinnedVertices.push_back(skinned);
    }

    tab.loaded.model = LoadModelFromMesh(GenMeshCube(1, 1, 1));
    LitShader lit = LoadBasicLitShader();
    Require(lit.valid, "Could not create merge test lighting shader");
    ApplyLitShader(tab.loaded, lit);
    Material* originalMaterials = tab.loaded.model.materials;
    MaterialMap* originalMaps = originalMaterials[0].maps;
    originalMaps[MATERIAL_MAP_DIFFUSE].color = Color{ 61, 112, 193, 255 };
    std::string error;
    const int mergedCount = MergeSelectedGeometry(tab, { 1, 2 }, error);
    Require(tab.loaded.model.materials == originalMaterials, "Merge replaced existing materials");
    Require(tab.loaded.model.materials[0].shader.id == lit.shader.id, "Merge lost the lighting shader");
    Require(tab.loaded.model.materials[0].maps == originalMaps, "Merge replaced material maps");
    Require(originalMaps[MATERIAL_MAP_DIFFUSE].color.r == 61, "Merge reset the material color");
    Require(mergedCount == 2, "Expected two meshes to merge");
    Require(error.empty(), "Merge reported an unexpected error");
    Require(tab.loaded.nodes.size() == 4, "Merge did not add a mesh node");
    Require(tab.deletedNodes[1] && tab.deletedNodes[2], "Merge did not delete source mesh nodes");
    const SceneNode& merged = tab.loaded.nodes.back();
    Require(merged.type == SceneNodeType::Mesh, "Merged node is not a mesh");
    Require(merged.meshVertexStart == 6 && merged.meshVertexCount == 6, "Merged mesh range is wrong");
    Require(tab.loaded.skinnedVertices.size() == tab.loaded.bindVertices.size() / 3, "Merged skinning vertex count is out of sync");
    Require(tab.loaded.skinnedVertices[6].influences.size() == 1, "Merged vertex lost its skin influence");
    Require(tab.loaded.skinnedVertices[6].influences[0].boneName == "Bone", "Merged vertex changed its skin bone");
    Equal(ReferenceSkin(tab.loaded, BuildBindBoneFrame(tab.loaded)), MeshFrame{ tab.currentVertices, tab.currentNormals });
    const size_t uvVertexCount = tab.loaded.bindVertices.size() / 3;
    tab.loaded.uvSetNames = { "First", "Preferred" };
    tab.loaded.uvSets = { std::vector<float>(uvVertexCount * 2, 0.25f), std::vector<float>(uvVertexCount * 2, 0.75f) };
    tab.loaded.uvSetPresence = { std::vector<unsigned char>(uvVertexCount, 1), std::vector<unsigned char>(uvVertexCount, 1) };
    tab.loaded.uvSetPresence[1][0] = 0;
    tab.loaded.uvSets[1][2] = 0.0f;
    tab.loaded.uvSets[1][3] = 0.0f;
    const auto originalUvSets = tab.loaded.uvSets;
    CombineAllUvSets(tab, 1);
    Require(tab.loaded.uvSetNames == std::vector<std::string>{ "Preferred" }, "Combine did not retain preferred name");
    Require(tab.loaded.uvSets.size() == 1, "Combine did not collapse UV sets");
    Require(tab.loaded.uvSets[0][0] == 0.25f, "Combine did not fill missing UVs");
    Require(tab.loaded.uvSets[0][2] == 0.0f && tab.loaded.uvSets[0][3] == 0.0f, "Combine overwrote valid zero UVs");
    Require(tab.loaded.uvSets[0][4] == 0.75f, "Combine did not prefer chosen set");
    Require(tab.loaded.model.meshes[0].texcoords[4] == 0.75f, "Combine did not update render UVs");
    Require(UndoEdit(tab) && tab.loaded.uvSets == originalUvSets, "Undo did not restore UV sets");
    Require(!tab.loaded.uvSetsEdited, "Undo did not restore UV export state");
    Require(RedoEdit(tab) && tab.loaded.uvSets.size() == 1, "Redo did not combine UV sets");
    Equal(ReferenceSkin(tab.loaded, BuildBindBoneFrame(tab.loaded)), MeshFrame{ tab.currentVertices, tab.currentNormals });
    UnloadFbxModel(tab.loaded);
    UnloadShader(lit.shader);
    CloseWindow();
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

static void TestValidationCache()
{
    ModelTab tab;
    tab.loaded.nodes.resize(20000);
    for (size_t i = 0; i < tab.loaded.nodes.size(); ++i)
    {
        SceneNode& node = tab.loaded.nodes[i];
        node.parent = i == 0 ? -1 : 0;
        node.name = "Repeated name";
        node.scale = Vector3{ 2, 2, 2 };
    }
    const auto start = std::chrono::steady_clock::now();
    const auto expected = BuildValidationIssues(tab);
    const double uncachedMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    const auto& cache = GetValidationCache(tab);
    Require(cache.issues.size() == expected.size(), "Cached validation lost issues");
    for (size_t i = 0; i < expected.size(); ++i)
    {
        Require(cache.issues[i].message == expected[i].message && cache.issues[i].category == expected[i].category &&
                cache.issues[i].severity == expected[i].severity && cache.issues[i].node == expected[i].node,
                "Cached validation differs from full validation");
    }
    size_t grouped = 0;
    for (const auto& group : cache.groups) grouped += group.issues.size();
    Require(grouped == expected.size(), "Grouping lost or duplicated issues");
    const auto* storage = cache.issues.data();
    const auto reuseStart = std::chrono::steady_clock::now();
    for (int i = 0; i < 1000; ++i)
        Require(GetValidationCache(tab).issues.data() == storage, "Unchanged validation rebuilt its results");
    const double cachedMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - reuseStart).count() / 1000;
    std::cout << "Validation: " << expected.size() << " issues, full scan " << uncachedMs
              << " ms, cached lookup " << cachedMs << " ms\n";
    RenameSceneNode(tab, 1, "Unique name");
    Require(tab.validationCache.dirty, "Rename did not invalidate validation");
    const auto& renamed = GetValidationCache(tab).issues;
    Require(std::any_of(renamed.begin(), renamed.end(), [](const ValidatorIssue& issue)
    {
        return issue.node == 1 && issue.message.find("Unique name") != std::string::npos;
    }), "Rename left stale validation text");
    tab.selectedUvSet = 1;
    Require(GetValidationCache(tab).uvSet == 1, "UV selection left stale validation");
    PushUndoSnapshot(tab);
    Require(tab.validationCache.dirty, "Edit did not invalidate validation");
    GetValidationCache(tab);
    Require(UndoEdit(tab) && tab.validationCache.dirty, "Undo did not invalidate validation");
    GetValidationCache(tab);
    Require(RedoEdit(tab) && tab.validationCache.dirty, "Redo did not invalidate validation");
}

static void TestValidationFixes()
{
    UvTriangleSample coverage;
    coverage.nodeIndex = 1;
    coverage.uv[0] = {0, 0}; coverage.uv[1] = {1, 0}; coverage.uv[2] = {0, 1};
    Require(std::fabs(CalculateUvTileOccupancy({coverage}) - 50.0f) < 0.01f, "UV occupancy area incorrect");
    Require(std::fabs(CalculateUvTileOccupancy({coverage, coverage}) - 50.0f) < 0.01f, "UV occupancy double-counted overlap");
    Require(CalculateUvTileOccupancy({coverage}, 2) == 0, "UV occupancy included another mesh");
    for (auto& uv : coverage.uv) uv.x += 2;
    Require(CalculateUvTileOccupancy({coverage}) == 0, "UV occupancy included outside-tile UVs");
    ModelTab tab;
    tab.loaded.hasMesh = true;
    tab.loaded.nodes.resize(3);
    tab.loaded.nodes[0].name = "RootNode";
    for (int i = 1; i <= 2; ++i)
    {
        auto& node = tab.loaded.nodes[i];
        node.parent = 0;
        node.type = SceneNodeType::Mesh;
        node.name = "Mesh";
        node.materialName = "Material";
        node.meshVertexStart = (i - 1) * 3;
        node.meshVertexCount = 3;
        node.meshTriangleCount = 1;
    }
    tab.loaded.materialNames = { "Material" };
    tab.loaded.bindVertices = { 0,0,0, 1,0,0, 0,1,0, 2,0,0, 4,0,0, 2,2,0 };
    tab.loaded.bindNormals.assign(18, 0.0f);
    tab.loaded.uvSets = { { 0,0, 1,0, 0,1, 0,0, 1,0, 0,1 },
                          { 0,0, 0.5f,0, 0,0.5f, 0,0, 1,0, 0,1 } };
    tab.loaded.uvSetNames = { "First", "Second" };
    tab.loaded.uvSetPresence = { {1,1,1,1,1,1}, {1,1,1,1,1,1} };
    const auto original = tab.loaded.uvSets;
    auto hasIssue = [&](const char* category)
    {
        const auto issues = BuildValidationIssues(tab);
        return std::any_of(issues.begin(), issues.end(), [&](const auto& issue) { return issue.category == category; });
    };
    Require(hasIssue("Multiple UV sets"), "Missing multiple UV sets warning");
    Require(hasIssue("Overlapping UVs"), "Stacked disconnected islands were not detected");
    Require(hasIssue("Texel density"), "Unequal island density was not detected");
    Require(!hasIssue("Low texel density"), "Density at the environment baseline must not warn");
    tab.uvDensityTileSize = 512;
    Require(hasIssue("Low texel density"), "Missing low density warning below environment baseline");
    tab.selectedUvSet = 1;
    std::vector<ValidatorIssue> lowDensityIssues;
    ValidateTexelDensityConsistency(tab, lowDensityIssues);
    Require(std::count_if(lowDensityIssues.begin(), lowDensityIssues.end(), [](const auto& issue)
        { return issue.category == "Low texel density" && issue.uvSet == 1 && issue.severity == ValidatorSeverity::Warning; }) == 2,
        "Uniformly low density must warn for each mesh in the selected UV set");
    tab.selectedUvSet = 0;
    tab.uvDensityTileSize = 1024;
    Require(PackValidationUvIslands(tab, 1, 0, false), "UV packing failed");
    Require(tab.loaded.uvSets[1] == original[1], "Packing changed another UV set");
    auto triangles = BuildUvScopeTriangles(tab, tab.loaded.uvSets[0], {1, 2});
    auto islands = CalculateUvIslandStats(triangles, 1, 1);
    Require(islands.size() == 2 && !UvIslandsOverlap(islands[0], islands[1], triangles), "Packed islands overlap");
    Require(std::fabs(islands[0].density / islands[1].density - 2.0f) < 0.001f ||
            std::fabs(islands[1].density / islands[0].density - 2.0f) < 0.001f, "Packing changed relative density");
    Require(UndoEdit(tab) && tab.loaded.uvSets == original, "UV packing undo failed");
    Require(PackValidationUvIslands(tab, 1, 0, true), "Average and pack failed");
    triangles = BuildUvScopeTriangles(tab, tab.loaded.uvSets[0], {1, 2});
    islands = CalculateUvIslandStats(triangles, 1, 1);
    Require(!UvIslandsOverlap(islands[0], islands[1], triangles), "Averaged islands overlap");
    Require(std::fabs(islands[0].density / islands[1].density - 1.0f) < 0.001f, "Island density was not equalized");
    Require(!hasIssue("Texel density"), "Density warning persisted after fix");
    for (float uv : tab.loaded.uvSets[0]) Require(uv > 0.0f && uv < 1.0f, "Packed UV outside tile or padding");
    Require(UndoEdit(tab), "Average islands undo failed");
    CombineAllUvSets(tab, 1);
    Require(tab.loaded.uvSets.size() == 1 && tab.loaded.uvSets[0] == original[1], "Merge did not prefer chosen UV set");
    Require(!hasIssue("Multiple UV sets"), "Merge warning persisted");
    Require(UndoEdit(tab) && tab.loaded.uvSets == original, "Merge undo failed");
    tab.loaded.nodes.push_back(SceneNode{});
    tab.loaded.nodes.back().parent = 0;
    tab.loaded.nodes.back().name = "Mesh_1";
    tab.loaded.animations.resize(3);
    tab.loaded.animations[0].name = "Take";
    tab.loaded.animations[1].name = "Take";
    tab.loaded.animations[2].name = "Take_1";
    tab.loaded.materialNames = { "Material", "Material", "Material_1" };
    Require(FixDuplicateNames(tab) == 3, "Duplicate names fix count incorrect");
    Require(tab.loaded.nodes[2].name == "Mesh_2" && tab.loaded.animations[1].name == "Take_2" &&
            tab.loaded.materialNames[1] == "Material_2", "Suffix collided with an existing name");
    Require(!hasIssue("Duplicate name"), "Duplicate warning persisted");
    Require(UndoEdit(tab) && tab.loaded.nodes[2].name == "Mesh" && tab.loaded.materialNames[1] == "Material" &&
            tab.loaded.animations[1].name == "Take", "Duplicate names undo failed");

    ModelTab bones;
    bones.loaded.nodes.resize(3);
    bones.loaded.nodes[0].name = "RootNode";
    for (int i = 1; i <= 2; ++i)
    {
        bones.loaded.nodes[i].parent = 0;
        bones.loaded.nodes[i].type = SceneNodeType::Bone;
        bones.loaded.nodes[i].name = "Bone";
        BonePose pose;
        pose.node = i;
        bones.loaded.bonePoses.push_back(pose);
    }
    bones.loaded.skinnedVertices.resize(1);
    bones.loaded.skinnedVertices[0].influences.push_back(SkinnedVertexInfluence{ "Bone", 1.0f });
    Require(FixDuplicateNames(bones) == 1 && bones.loaded.skinnedVertices[0].influences[0].boneName == "Bone_1",
            "Duplicate bone repair changed the name-based skin target");
    Require(UndoEdit(bones) && bones.loaded.skinnedVertices[0].influences[0].boneName == "Bone", "Bone rename undo failed");
}

static void TestUvOverlapSpatialSearch()
{
    for (int scenario = 0; scenario < 80; ++scenario)
    {
        std::vector<UvTriangleSample> triangles;
        UvIslandStats a, b;
        a.minU = b.minU = -100.0f;
        a.maxU = b.maxU = 100.0f;
        a.minV = b.minV = -100.0f;
        a.maxV = b.maxV = 100.0f;
        for (int side = 0; side < 2; ++side)
        {
            for (int i = 0; i < 48; ++i)
            {
                const float offset = side == 0 ? 0.0f : static_cast<float>(scenario % 10) * 0.2f;
                const float x = static_cast<float>(i % 8) * 2.0f + offset - 8.0f;
                const float y = static_cast<float>(i / 8) * 2.0f + (side == 0 ? 0.0f : 0.3f);
                UvTriangleSample triangle;
                triangle.uv[0] = Vector2{ x, y };
                triangle.uv[1] = Vector2{ x + 0.5f, y };
                triangle.uv[2] = Vector2{ x, y + (scenario % 7 == 0 ? 0.0f : 0.5f) };
                if (scenario % 2) std::swap(triangle.uv[1], triangle.uv[2]);
                (side == 0 ? a : b).triangles.push_back(static_cast<int>(triangles.size()));
                triangles.push_back(triangle);
            }
        }
        bool expected = false;
        for (int ai : a.triangles)
            for (int bi : b.triangles)
                if (TriangleUvOverlapArea(triangles[static_cast<size_t>(ai)], triangles[static_cast<size_t>(bi)]) > 0.0000001f)
                    expected = true;
        Require(UvIslandsOverlap(a, b, triangles) == expected,
                "Spatial UV search differs from exhaustive triangle overlap search");
    }
    UvIslandStats empty;
    Require(!UvIslandsOverlap(empty, empty, {}), "Empty islands overlap");
}

static void TestDegenerateTriangleRepair()
{
    const auto directory = std::filesystem::temp_directory_path() / ("openfbx-repair-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(directory);
    const std::string source = (directory / "source.fbx").string();
    const std::string output = (directory / "fixed.fbx").string();
    FbxManager* manager = FbxManager::Create();
    manager->SetIOSettings(FbxIOSettings::Create(manager, IOSROOT));
    FbxScene* scene = FbxScene::Create(manager, "repair");
    FbxNode* node = FbxNode::Create(scene, "Mesh");
    FbxMesh* mesh = FbxMesh::Create(scene, "Mesh");
    mesh->InitControlPoints(5);
    mesh->SetControlPointAt(FbxVector4(0, 0, 0), 0);
    mesh->SetControlPointAt(FbxVector4(100, 0, 0), 1);
    mesh->SetControlPointAt(FbxVector4(0, 100, 0), 2);
    mesh->SetControlPointAt(FbxVector4(200, 0, 0), 3);
    mesh->SetControlPointAt(FbxVector4(100, 100, 0), 4);
    const std::vector<std::vector<int>> faces{
        {0,1,2}, {0,1,3}, {0,0,1}, {0,1,4,2}, {0,1,3,1}, {0,1,3,2},
        {0,1,3,1,0}, {0,1,3,4,2}
    };
    node->AddMaterial(FbxSurfacePhong::Create(scene, "First"));
    node->AddMaterial(FbxSurfacePhong::Create(scene, "Second"));
    auto* materials = mesh->CreateElementMaterial();
    materials->SetMappingMode(FbxLayerElement::eByPolygon);
    materials->SetReferenceMode(FbxLayerElement::eIndexToDirect);
    auto* uvs = mesh->CreateElementUV("RepairUV");
    uvs->SetMappingMode(FbxLayerElement::eByPolygonVertex);
    uvs->SetReferenceMode(FbxLayerElement::eIndexToDirect);
    auto* normals = mesh->CreateElementNormal();
    normals->SetMappingMode(FbxLayerElement::eByPolygonVertex);
    normals->SetReferenceMode(FbxLayerElement::eDirect);
    for (int polygon = 0; polygon < static_cast<int>(faces.size()); ++polygon)
    {
        mesh->BeginPolygon(polygon % 2);
        for (int corner = 0; corner < static_cast<int>(faces[polygon].size()); ++corner)
        {
            const int uvIndex = uvs->GetDirectArray().GetCount();
            uvs->GetDirectArray().Add(FbxVector2(polygon * 0.1, corner * 0.1));
            mesh->AddPolygon(faces[polygon][corner], uvIndex);
            normals->GetDirectArray().Add(FbxVector4(0, 0, 1));
        }
        mesh->EndPolygon();
    }
    node->SetNodeAttribute(mesh);
    scene->GetRootNode()->AddChild(node);
    FbxExporter* exporter = FbxExporter::Create(manager, "export");
    Require(exporter->Initialize(source.c_str(), -1, manager->GetIOSettings()) && exporter->Export(scene), "Could not write repair fixture");
    manager->Destroy();
    SetTraceLogLevel(LOG_WARNING);
    SetConfigFlags(FLAG_WINDOW_HIDDEN);
    InitWindow(64, 64, "Repair test");
    ModelTab tab;
    std::string error;
    Require(LoadFbxModel(source, tab.loaded, error), error.c_str());
    int meshIndex = -1;
    for (int i = 0; i < static_cast<int>(tab.loaded.nodes.size()); ++i)
        if (tab.loaded.nodes[static_cast<size_t>(i)].type == SceneNodeType::Mesh) meshIndex = i;
    Require(meshIndex >= 0 && tab.loaded.nodes[static_cast<size_t>(meshIndex)].degenerateTriangleCount == 9, "Fixture must include degenerate faces and valid polygons with collinear fan corners");
    Require(FixDegenerateTriangles(tab, meshIndex) == 9, "Repair did not remove every degenerate triangle");
    Require(tab.loaded.nodes[static_cast<size_t>(meshIndex)].meshTriangleCount == 6, "Repair removed valid geometry");
    Require(tab.loaded.nodes[static_cast<size_t>(meshIndex)].degenerateTriangleCount == 0, "Repair left validation errors");
    Require(tab.loaded.nodes[static_cast<size_t>(meshIndex)].meshPolygonCount == 5, "Repair reported the wrong polygon count");
    auto checkWireframe = [&](int expectedEdges)
    {
        int edges = 0;
        ForEachMeshNodeWireframeEdge(tab.loaded, tab.loaded.nodes[static_cast<size_t>(meshIndex)], [&](int a, int b)
        {
            ++edges;
            const auto& node = tab.loaded.nodes[static_cast<size_t>(meshIndex)];
            Require(!IsRemovedTriangle(node, a) && !IsRemovedTriangle(node, b),
                "Repair left a removed triangle in the wireframe");
        });
        Require(edges == expectedEdges, "Wireframe has missing or removed polygon edges");
    };
    checkWireframe(16);
    Require(FixDegenerateTriangles(tab, meshIndex) == 0, "Repair is not idempotent");
    Require(UndoEdit(tab) && tab.loaded.nodes[static_cast<size_t>(meshIndex)].degenerateTriangleCount == 9, "Undo did not restore bad faces");
    checkWireframe(31);
    Require(RedoEdit(tab) && tab.loaded.nodes[static_cast<size_t>(meshIndex)].degenerateTriangleCount == 0, "Redo did not restore repair");
    checkWireframe(16);
    Require(SaveFbxModelAnimations(source, output, tab.loaded, tab.deletedNodes, error), error.c_str());
    LoadedFbxModel reopened;
    Require(LoadFbxModel(output, reopened, error), error.c_str());
    int triangles = 0;
    int degenerate = 0;
    for (const auto& saved : reopened.nodes)
    {
        degenerate += saved.degenerateTriangleCount;
        triangles += saved.meshTriangleCount;
    }
    Require(triangles == 6 && degenerate == 0, "Saved repair retained degenerate triangles or removed valid geometry");
    FbxManager* verifier = FbxManager::Create();
    verifier->SetIOSettings(FbxIOSettings::Create(verifier, IOSROOT));
    auto* imported = FbxScene::Create(verifier, "verify topology");
    auto* importer = FbxImporter::Create(verifier, "import");
    Require(importer->Initialize(output.c_str(), -1, verifier->GetIOSettings()) && importer->Import(imported), "Cannot inspect saved topology");
    auto* savedMesh = imported->GetRootNode()->GetChild(0)->GetMesh();
    Require(savedMesh && savedMesh->GetPolygonCount() == 5, "Wrong saved face count");
    const int survivingPolygons[] = { 0, 3, 5, 7, 7 };
    const std::vector<std::vector<int>> survivingCorners{{0,1,2}, {0,1,2,3}, {0,2,3}, {0,2,3}, {0,3,4}};
    for (int polygon = 0; polygon < 5; ++polygon)
    {
        const int sourcePolygon = survivingPolygons[polygon];
        const auto& face = faces[sourcePolygon];
        const auto& sourceCorners = survivingCorners[polygon];
        Require(savedMesh->GetPolygonSize(polygon) == static_cast<int>(sourceCorners.size()), "Repair changed unaffected polygon topology");
        Require(savedMesh->GetElementMaterial()->GetIndexArray().GetAt(polygon) == sourcePolygon % 2, "Repair changed material assignment");
        for (int corner = 0; corner < static_cast<int>(sourceCorners.size()); ++corner)
        {
            Require(savedMesh->GetPolygonVertex(polygon, corner) == face[sourceCorners[corner]], "Repair changed polygon corners or winding");
            FbxVector2 uv;
            bool unmapped = false;
            Require(savedMesh->GetPolygonVertexUV(polygon, corner, "RepairUV", uv, unmapped) && !unmapped, "Repair lost polygon UVs");
            Require(std::fabs(uv[0] - sourcePolygon * 0.1) < 0.000001 && std::fabs(uv[1] - sourceCorners[corner] * 0.1) < 0.000001, "Repair changed polygon UVs");
            FbxVector4 normal;
            Require(savedMesh->GetPolygonVertexNormal(polygon, corner, normal) && std::fabs(normal[2] - 1.0) < 0.000001,
                "Repair lost direct polygon vertex normals");
        }
    }
    Require(savedMesh->GetControlPointsCount() == 5, "Repair changed control points");
    auto& repairedNode = tab.loaded.nodes[static_cast<size_t>(meshIndex)];
    for (int vertex = repairedNode.meshVertexStart; vertex < repairedNode.meshVertexStart + repairedNode.meshVertexCount; vertex += 3)
        repairedNode.removedTriangleStarts.push_back(vertex);
    std::sort(repairedNode.removedTriangleStarts.begin(), repairedNode.removedTriangleStarts.end());
    checkWireframe(0);
    verifier->Destroy();
    UnloadFbxModel(reopened);
    UnloadFbxModel(tab.loaded);
    CloseWindow();
    std::filesystem::remove_all(directory);
}

// Optional real-model regression: --repair <fbx>. Writes only to a temporary folder.
static int TestModelRepair(const char* path)
{
    struct Topology
    {
        int controlPoints = 0;
        std::vector<std::vector<int>> polygons;
    };
    auto readTopology = [](const std::string& file)
    {
        std::unordered_map<std::string, Topology> result;
        auto* manager = FbxManager::Create();
        manager->SetIOSettings(FbxIOSettings::Create(manager, IOSROOT));
        manager->GetIOSettings()->SetBoolProp(IMP_FBX_EXTRACT_EMBEDDED_DATA, false);
        auto* scene = FbxScene::Create(manager, "topology");
        auto* importer = FbxImporter::Create(manager, "import");
        Require(importer->Initialize(file.c_str(), -1, manager->GetIOSettings()) && importer->Import(scene), "Cannot inspect model topology");
        std::function<void(FbxNode*, std::string)> visit = [&](FbxNode* node, std::string key)
        {
            key += "/" + std::string(node->GetName());
            for (int attribute = 0; attribute < node->GetNodeAttributeCount(); ++attribute)
            {
                auto* source = node->GetNodeAttributeByIndex(attribute);
                if (!source || source->GetAttributeType() != FbxNodeAttribute::eMesh) continue;
                auto* mesh = static_cast<FbxMesh*>(source);
                auto& topology = result[key + ":" + std::to_string(attribute)];
                topology.controlPoints = mesh->GetControlPointsCount();
                for (int polygon = 0; polygon < mesh->GetPolygonCount(); ++polygon)
                {
                    std::vector<int> corners;
                    for (int corner = 0; corner < mesh->GetPolygonSize(polygon); ++corner)
                        corners.push_back(mesh->GetPolygonVertex(polygon, corner));
                    topology.polygons.push_back(std::move(corners));
                }
            }
            for (int child = 0; child < node->GetChildCount(); ++child)
                visit(node->GetChild(child), key);
        };
        for (int child = 0; child < scene->GetRootNode()->GetChildCount(); ++child)
            visit(scene->GetRootNode()->GetChild(child), "");
        manager->Destroy();
        return result;
    };
    const auto before = readTopology(path);
    SetTraceLogLevel(LOG_WARNING);
    SetConfigFlags(FLAG_WINDOW_HIDDEN);
    InitWindow(64, 64, "Model repair test");
    ModelTab tab;
    std::string error;
    Require(LoadFbxModel(path, tab.loaded, error), error.c_str());
    int removedTriangles = 0;
    int removedFaces = 0;
    for (int index = 0; index < static_cast<int>(tab.loaded.nodes.size()); ++index)
    {
        const auto& node = tab.loaded.nodes[index];
        if (node.type != SceneNodeType::Mesh) continue;
        const int polygonsBefore = node.meshPolygonCount;
        removedTriangles += FixDegenerateTriangles(tab, index);
        removedFaces += polygonsBefore - node.meshPolygonCount;
        Require(node.degenerateTriangleCount == 0, "Cleanup left degenerate triangles");
        ForEachMeshNodeWireframeEdge(tab.loaded, node, [&](int a, int b)
        {
            Require(!IsRemovedTriangle(node, a) && !IsRemovedTriangle(node, b), "Cleanup left removed wireframe edges");
        });
    }
    const auto directory = std::filesystem::temp_directory_path() / ("openfbx-model-repair-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(directory);
    const std::string output = (directory / "fixed.fbx").string();
    Require(SaveFbxModelAnimations(path, output, tab.loaded, tab.deletedNodes, error), error.c_str());
    const auto after = readTopology(output);
    Require(before.size() == after.size(), "Repair changed the mesh count");
    int savedRemovedFaces = 0;
    int savedRemovedTriangles = 0;
    int quads = 0;
    int ngons = 0;
    for (const auto& entry : before)
    {
        const auto found = after.find(entry.first);
        Require(found != after.end(), "Repair lost a mesh");
        const auto& original = entry.second;
        const auto& saved = found->second;
        Require(original.controlPoints == saved.controlPoints, "Repair changed control points");
        size_t cursor = 0;
        for (const auto& polygon : saved.polygons)
        {
            auto matchesSource = [&](const auto& source)
            {
                if (source == polygon) return true;
                if (polygon.size() != 3) return false;
                for (size_t fan = 1; fan + 1 < source.size(); ++fan)
                    if (polygon[0] == source[0] && polygon[1] == source[fan] && polygon[2] == source[fan + 1]) return true;
                return false;
            };
            while (cursor < original.polygons.size() && !matchesSource(original.polygons[cursor])) ++cursor;
            Require(cursor < original.polygons.size(), "Repair changed surviving polygon topology or winding");
            if (original.polygons[cursor] == polygon) ++cursor;
            quads += polygon.size() == 4;
            ngons += polygon.size() > 4;
        }
        savedRemovedFaces += static_cast<int>(original.polygons.size()) - static_cast<int>(saved.polygons.size());
        for (const auto& polygon : original.polygons) savedRemovedTriangles += std::max(0, static_cast<int>(polygon.size()) - 2);
        for (const auto& polygon : saved.polygons) savedRemovedTriangles -= std::max(0, static_cast<int>(polygon.size()) - 2);
    }
    Require(savedRemovedFaces == removedFaces && savedRemovedTriangles == removedTriangles, "Saved repair removed the wrong geometry");
    std::cout << "PASS repair " << path << ": removed " << removedFaces << " faces (" << removedTriangles
              << " display triangles); preserved " << quads << " quads and " << ngons << " ngons, polygon corners, and wireframe edges\n";
    UnloadFbxModel(tab.loaded);
    CloseWindow();
    std::filesystem::remove_all(directory);
    return 0;
}

static int BenchmarkValidation(const char* path)
{
    SetTraceLogLevel(LOG_WARNING);
    SetConfigFlags(FLAG_WINDOW_HIDDEN);
    InitWindow(64, 64, "Validation benchmark");
    ModelTab tab;
    std::string error;
    Require(LoadFbxModel(path, tab.loaded, error), error.c_str());
    auto stage = [&](const char* name, auto validate)
    {
        std::vector<ValidatorIssue> issues;
        const auto start = std::chrono::steady_clock::now();
        validate(issues);
        std::cout << name << ": " << std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count()
                  << " ms, " << issues.size() << " issues" << std::endl;
    };
    stage("Textures", [&](auto& out) { ValidateTextures(tab, out); });
    stage("Names", [&](auto& out) { ValidateDuplicateNames(tab.loaded, out); });
    stage("Transforms", [&](auto& out) { ValidateTransforms(tab.loaded, out); });
    stage("Geometry", [&](auto& out) { ValidateGeometry(tab.loaded, out); });
    stage("Density", [&](auto& out) { ValidateTexelDensityConsistency(tab, out); });
    stage("UV overlaps", [&](auto& out) { ValidateUvIslandOverlaps(tab, out); });
    stage("Skin", [&](auto& out) { ValidateSkinning(tab.loaded, out); });
    UnloadFbxModel(tab.loaded);
    CloseWindow();
    return 0;
}

static int TestLogPanel(const char* previewPath = nullptr)
{
    LogPanelState panel;
    Require(panel.collapsed, "Log should start collapsed");
    panel.collapsed = false;
    std::string notice = "Saved FBX.";
    std::string error = "Missing texture.";
    CollectLogMessages(panel, notice, error);
    Require(panel.entries.size() == 2 && !panel.entries[0].error && panel.entries[1].error, "Log lost a simultaneous notice/error");
    CollectLogMessages(panel, notice, error);
    Require(panel.entries.size() == 2, "Log repeated a stale message");
    notice = "Saved FBX.";
    CollectLogMessages(panel, notice, error);
    Require(panel.entries.size() == 3, "Log dropped a repeated user action");
    for (int index = 0; index < 510; ++index)
    {
        notice = "Message " + std::to_string(index);
        CollectLogMessages(panel, notice, error);
    }
    Require(panel.entries.size() == 500 && panel.entries.front().text == "Message 10", "Log history must retain only the newest 500 messages");
    Require(GetLogPanelText(panel).find("INFO: Message 509\n") != std::string::npos, "Copy log lost the latest message");
    SetTraceLogLevel(LOG_WARNING);
    SetConfigFlags(FLAG_WINDOW_HIDDEN);
    InitWindow(900, 600, "Log panel test");
    Font font = LoadTechnicalFont();
    notice = "Removed 52 zero-area faces; surviving quads and ngons preserved.";
    error = "Cannot load texture: C:/Projects/Hero/Textures/A_very_long_texture_filename_that_needs_to_wrap_without_hiding_the_end_of_the_message_BaseColor.png\nThe original mesh is still available.";
    CollectLogMessages(panel, notice, error);
    WrapLogMessages(font, panel, 862.0f);
    for (const auto& line : panel.lines)
        Require(MeasureTextEx(font, line.text.c_str(), 14.0f, 1.0f).x <= 862.0f, "Wrapped log message exceeds panel width");
    const size_t wideLines = panel.lines.size();
    WrapLogMessages(font, panel, 320.0f);
    Require(panel.lines.size() > wideLines, "Log did not rewrap after a resize");
    RenderTexture2D preview = LoadRenderTexture(900, 600);
    auto draw = [&]()
    {
        BeginDrawing();
        BeginTextureMode(preview);
        ClearBackground(Color{ 28, 31, 35, 255 });
        DrawUiText(font, "VIEWPORT", 350, 180, 20, GRAY);
        gBottomPanelReservedHeight = kTimelinePanelHeight + GetLogPanelHeight(panel);
        DrawRectangle(0, 61, 250, static_cast<int>(GetHierarchyPanelHeight()), Color{ 18, 20, 23, 255 });
        DrawUiText(font, "SCENE", 12, 72, 16, GRAY);
        const int animationY = 600 - static_cast<int>(GetLogPanelHeight(panel)) - 124;
        DrawRectangle(0, animationY, 900, 124, Color{ 20, 22, 24, 255 });
        DrawUiText(font, "ANIMATIONS", 12, static_cast<float>(animationY + 9), 16, GRAY);
        DrawLogPanel(font, panel, false);
        EndTextureMode();
        EndDrawing();
    };
    draw();
    auto savePreview = [&](const char* path)
    {
        Image image = LoadImageFromTexture(preview.texture);
        ImageFlipVertical(&image);
        Require(ExportImage(image, path), "Could not write log panel preview");
        UnloadImage(image);
    };
    if (previewPath) savePreview(previewPath);
    panel.followLatest = false;
    panel.scroll = 5;
    notice = "New message while reading history";
    CollectLogMessages(panel, notice, error);
    draw();
    Require(panel.scroll == 5, "New log message interrupted reading older history");
    panel.collapsed = true;
    draw();
    if (previewPath) savePreview((std::string(previewPath) + ".collapsed.png").c_str());
    UnloadRenderTexture(preview);
    if (font.texture.id != GetFontDefault().texture.id) UnloadFont(font);
    CloseWindow();
    gBottomPanelReservedHeight = kTimelinePanelHeight;
    return 0;
}

int main(int argc, char** argv)
{
    try
    {
        if (argc > 2 && std::string(argv[1]) == "--log-preview") return TestLogPanel(argv[2]);
        if (argc > 2 && std::string(argv[1]) == "--repair") return TestModelRepair(argv[2]);
        if (argc > 2 && std::string(argv[1]) == "--validation") return BenchmarkValidation(argv[2]);
        if (argc > 1) return BenchmarkFbx(argv[1]);
        TestLogPanel();
        TestDegenerateTriangleRepair();
        TestUvOverlapSpatialSearch();
        TestValidationFixes();
        TestValidationCache();
        TestVisibility();
        TestMarqueeSelection();
        TestTangents();
        TestMergeSelectedGeometryPreservesSkinning();
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
