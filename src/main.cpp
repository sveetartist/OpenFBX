#include <cmath>
#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

#ifdef _WIN32
using WindowsBool = int;
using WindowsDword = unsigned long;
using WindowsHinstance = void*;
using WindowsHwnd = void*;
using WindowsLparam = long long;
using WindowsLpstr = char*;
using WindowsLpcstr = const char*;
using WindowsWord = unsigned short;

struct WindowsOpenFileNameA
{
    WindowsDword lStructSize;
    WindowsHwnd hwndOwner;
    WindowsHinstance hInstance;
    WindowsLpcstr lpstrFilter;
    WindowsLpstr lpstrCustomFilter;
    WindowsDword nMaxCustFilter;
    WindowsDword nFilterIndex;
    WindowsLpstr lpstrFile;
    WindowsDword nMaxFile;
    WindowsLpstr lpstrFileTitle;
    WindowsDword nMaxFileTitle;
    WindowsLpcstr lpstrInitialDir;
    WindowsLpcstr lpstrTitle;
    WindowsDword Flags;
    WindowsWord nFileOffset;
    WindowsWord nFileExtension;
    WindowsLpcstr lpstrDefExt;
    WindowsLparam lCustData;
    void* lpfnHook;
    WindowsLpcstr lpTemplateName;
    void* pvReserved;
    WindowsDword dwReserved;
    WindowsDword FlagsEx;
};

extern "C" __declspec(dllimport) WindowsBool __stdcall GetOpenFileNameA(WindowsOpenFileNameA* dialog);
extern "C" __declspec(dllimport) WindowsBool __stdcall GetSaveFileNameA(WindowsOpenFileNameA* dialog);

constexpr WindowsDword kOfnFileMustExist = 0x00001000;
constexpr WindowsDword kOfnPathMustExist = 0x00000800;
constexpr WindowsDword kOfnNoChangeDir = 0x00000008;
constexpr WindowsDword kOfnOverwritePrompt = 0x00000002;
#endif

#include "FbxLoader.h"
#include "raylib.h"
#include "raymath.h"
#include "rlgl.h"

namespace
{
constexpr float kTimelinePanelHeight = 124.0f;
constexpr float kTimelineCollapsedHeight = 28.0f;
constexpr float kMetersPerGridCell = 1.0f;
constexpr double kNearClipPlane = 0.0005;
constexpr double kFarClipPlane = 10000.0;
constexpr Color kSelectionColor{ 204, 154, 42, 255 };
constexpr float kMinTransformGizmoScale = 0.5f;
constexpr float kMaxTransformGizmoScale = 2.0f;
constexpr float kNodeContextMenuW = 152.0f;
constexpr float kNodeContextMenuBaseH = 92.0f;
constexpr float kNodeContextMenuBoneH = 122.0f;
float gBottomPanelReservedHeight = kTimelinePanelHeight;
float gTransformGizmoScale = 1.0f;

struct OrbitCamera
{
    Camera3D camera{};
    Vector3 target{ 0.0f, 0.0f, 0.0f };
    float distance = 6.0f;
    float yaw = 45.0f * DEG2RAD;
    float pitch = 30.0f * DEG2RAD;
    float focusDistance = 6.0f;
    float snapDragX = 0.0f;
    float snapDragY = 0.0f;
    bool snapping = false;
    bool snappedView = false;
};

struct AnimationState
{
    int clipIndex = -1;
    int clipScroll = 0;
    int contextClipIndex = -1;
    Vector2 contextPosition{};
    bool contextMenuOpen = false;
    bool contextMenuJustOpened = false;
    float time = 0.0f;
    bool playing = false;
    bool scrubbing = false;
};

enum class RenameTarget
{
    None,
    SceneNode,
    AnimationClip
};

struct RenameEditor
{
    RenameTarget target = RenameTarget::None;
    int nodeIndex = -1;
    int clipIndex = -1;
    char text[256]{};
    int cursor = 0;
    bool textSelected = false;
    bool active = false;
    bool justOpened = false;
};

enum class ViewMode
{
    Shaded,
    ShadedWireframe,
    Wireframe
};

enum class NavigationPreset
{
    Blender,
    Maya
};

enum class OpenMenu
{
    None,
    File,
    Edit,
    View,
    Preferences
};

enum class LeftPanelTab
{
    Hierarchy,
    Stats,
    Materials,
    UV,
    SkinWeights,
    Validator
};

enum class PbrTextureSlot
{
    Diffuse,
    Normal,
    Roughness,
    Metallic,
    AmbientOcclusion,
    Emissive,
    Opacity,
    Count
};

enum class PackedChannel
{
    R,
    G,
    B
};

enum class OpacityChannel
{
    RGB,
    A
};

enum class MaterialPreviewMode
{
    Shaded,
    Diffuse,
    Normal,
    Roughness,
    Metallic,
    AmbientOcclusion,
    Emissive,
    Opacity
};

enum class TransformTool
{
    Select,
    Move,
    Rotate,
    Scale,
    WeightsBrush
};

enum class TransformAxis
{
    None,
    X,
    Y,
    Z,
    Center
};

enum class GizmoOrientation
{
    Global,
    Local
};

struct PbrTexture
{
    Texture2D texture{};
    std::string path;
    bool loaded = false;
};

struct PbrMaterialState
{
    std::array<PbrTexture, static_cast<size_t>(PbrTextureSlot::Count)> textures;
    bool normalDirectX = false;
    PackedChannel roughnessChannel = PackedChannel::G;
    PackedChannel metallicChannel = PackedChannel::B;
    PackedChannel aoChannel = PackedChannel::R;
    OpacityChannel opacityChannel = OpacityChannel::RGB;
};

struct PbrTextureSnapshot
{
    std::string path;
    bool loaded = false;
};

struct PbrMaterialSnapshot
{
    std::array<PbrTextureSnapshot, static_cast<size_t>(PbrTextureSlot::Count)> textures;
    bool normalDirectX = false;
    PackedChannel roughnessChannel = PackedChannel::G;
    PackedChannel metallicChannel = PackedChannel::B;
    PackedChannel aoChannel = PackedChannel::R;
    OpacityChannel opacityChannel = OpacityChannel::RGB;
};

struct EditSnapshot
{
    BoundingBox bounds{};
    std::vector<SceneNode> nodes;
    std::vector<BoneSegment> bones;
    std::vector<BonePose> bonePoses;
    std::vector<float> bindVertices;
    std::vector<float> bindNormals;
    std::vector<float> currentVertices;
    std::vector<float> currentNormals;
    std::vector<SkinnedVertex> skinnedVertices;
    std::vector<BoneSegment> visibleBones;
    std::vector<BonePose> visibleBonePoses;
    std::vector<BonePose> originalBindBonePoses;
    std::vector<AnimationClip> animations;
    std::vector<bool> deletedNodes;
    std::vector<PbrMaterialSnapshot> pbrMaterials;
    int selectedNode = -1;
    std::vector<int> selectedNodes;
    int selectedMaterial = 0;
    int animationClipIndex = -1;
    int animationClipScroll = 0;
    float animationTime = 0.0f;
    bool manualSkinnedMeshPose = false;
};

struct TransformGizmoState
{
    bool dragging = false;
    TransformAxis axis = TransformAxis::None;
    int nodeIndex = -1;
    Vector2 lastMouse{};
    float lastAngle = 0.0f;
};

struct WeightBrushSettings
{
    float sizePixels = 48.0f;
    float strength = 0.35f;
    bool autoNormalize = true;
};

struct WeightBrushState
{
    bool painting = false;
};

struct TextureClipboard
{
    std::string path;
    bool menuOpen = false;
    bool justOpened = false;
    int materialIndex = -1;
    PbrTextureSlot slot = PbrTextureSlot::Diffuse;
    Vector2 position{};
};

enum class ValidatorSeverity
{
    Info,
    Warning,
    Error
};

struct ValidatorIssue
{
    ValidatorSeverity severity = ValidatorSeverity::Info;
    std::string category;
    std::string message;
    int node = -1;
};

struct ModelTab
{
    LoadedFbxModel loaded;
    OrbitCamera orbit;
    AnimationState animation;
    std::vector<bool> collapsedNodes;
    std::vector<float> blendedVertices;
    std::vector<float> blendedNormals;
    std::vector<float> blendedTangents;
    std::vector<float> currentVertices;
    std::vector<float> currentNormals;
    std::vector<BoneSegment> visibleBones;
    std::vector<BonePose> visibleBonePoses;
    std::vector<BonePose> originalBindBonePoses;
    std::vector<PbrMaterialState> pbrMaterials;
    std::vector<bool> deletedNodes;
    std::vector<EditSnapshot> undoStack;
    std::vector<EditSnapshot> redoStack;
    int selectedMaterial = 0;
    int selectedUvSet = 0;
    int uvSetScroll = 0;
    bool showUvTexelDensity = false;
    bool showUvSameMaterialMeshes = false;
    int uvDensityTileSize = 1024;
    int selectedUvIsland = -1;
    float uvViewZoom = 1.0f;
    Vector2 uvViewPan{};
    bool uvViewPanning = false;
    int appliedClipIndex = -2;
    int appliedMeshFrameIndex = -1;
    int appliedNextMeshFrameIndex = -1;
    float appliedMeshFrameAlpha = -1.0f;
    int appliedBoneClipIndex = -2;
    int appliedBoneFrameIndex = -1;
    int appliedNextBoneFrameIndex = -1;
    float appliedBoneFrameAlpha = -1.0f;
    bool manualSkinnedMeshPose = false;
    int selectedNode = -1;
    std::vector<int> selectedNodes;
    int isolatedNode = -1;
    std::string path;
    std::string title;
};

struct HierarchyPanelState
{
    float width = 320.0f;
    float scroll = 0.0f;
    float validatorScroll = 0.0f;
    float skinWeightsScroll = 0.0f;
    int contextNodeIndex = -1;
    std::vector<int> contextNodeIndices;
    Vector2 contextPosition{};
    bool contextMenuOpen = false;
    bool contextMenuJustOpened = false;
    bool hidden = false;
    bool resizing = false;
    bool shiftDragSelecting = false;
    int shiftDragAnchorNode = -1;
    int shiftDragLastNode = -1;
    LeftPanelTab activeTab = LeftPanelTab::Hierarchy;
};

struct VisibilityState
{
    bool geometry = true;
    bool textures = true;
    bool backfaceCulling = false;
    bool bones = true;
    bool boneRotations = false;
    bool empties = true;
    bool skinWeights = false;
};

struct LitShader
{
    Shader shader{};
    int viewPositionLoc = -1;
    int lightDirectionLoc = -1;
    int lightColorLoc = -1;
    int ambientLoc = -1;
    int hasDiffuseMapLoc = -1;
    int hasNormalMapLoc = -1;
    int hasRoughnessMapLoc = -1;
    int hasMetallicMapLoc = -1;
    int hasAoMapLoc = -1;
    int hasEmissiveMapLoc = -1;
    int hasOpacityMapLoc = -1;
    int normalDirectXLoc = -1;
    int roughnessChannelLoc = -1;
    int metallicChannelLoc = -1;
    int aoChannelLoc = -1;
    int opacityChannelLoc = -1;
    int materialPreviewModeLoc = -1;
    int texturesVisibleLoc = -1;
    bool valid = false;
};

float ClampFloat(float value, float minimum, float maximum)
{
    return std::max(minimum, std::min(maximum, value));
}

int ClampInt(int value, int minimum, int maximum)
{
    return std::max(minimum, std::min(maximum, value));
}

float SnapAngle(float value, float step)
{
    return std::round(value / step) * step;
}

void SnapOrbitCamera(OrbitCamera& orbit)
{
    constexpr float yawStep = 90.0f * DEG2RAD;
    constexpr float pitchStep = 90.0f * DEG2RAD;
    orbit.yaw = SnapAngle(orbit.yaw, yawStep);
    orbit.pitch = ClampFloat(SnapAngle(orbit.pitch, pitchStep), -89.0f * DEG2RAD, 89.0f * DEG2RAD);
}

void UpdateSnappedOrbitCamera(OrbitCamera& orbit, Vector2 mouseDelta)
{
    constexpr float snapDragPixels = 70.0f;
    constexpr float snapStep = 90.0f * DEG2RAD;

    if (!orbit.snapping)
    {
        SnapOrbitCamera(orbit);
        orbit.snapDragX = 0.0f;
        orbit.snapDragY = 0.0f;
        orbit.snapping = true;
        orbit.snappedView = true;
    }

    orbit.snapDragX += mouseDelta.x;
    orbit.snapDragY += mouseDelta.y;

    while (orbit.snapDragX >= snapDragPixels)
    {
        orbit.yaw -= snapStep;
        orbit.snapDragX -= snapDragPixels;
    }
    while (orbit.snapDragX <= -snapDragPixels)
    {
        orbit.yaw += snapStep;
        orbit.snapDragX += snapDragPixels;
    }
    while (orbit.snapDragY >= snapDragPixels)
    {
        orbit.pitch = ClampFloat(orbit.pitch + snapStep, -89.0f * DEG2RAD, 89.0f * DEG2RAD);
        orbit.snapDragY -= snapDragPixels;
    }
    while (orbit.snapDragY <= -snapDragPixels)
    {
        orbit.pitch = ClampFloat(orbit.pitch - snapStep, -89.0f * DEG2RAD, 89.0f * DEG2RAD);
        orbit.snapDragY += snapDragPixels;
    }
}

void StopSnappedOrbitDrag(OrbitCamera& orbit)
{
    orbit.snapping = false;
    orbit.snapDragX = 0.0f;
    orbit.snapDragY = 0.0f;
}

void ExitSnappedOrbitView(OrbitCamera& orbit)
{
    StopSnappedOrbitDrag(orbit);
    orbit.snappedView = false;
}

Vector3 NormalizeOrFallback(Vector3 value, Vector3 fallback)
{
    const float length = Vector3Length(value);
    if (length > 0.000001f) return Vector3Scale(value, 1.0f / length);
    return fallback;
}

Vector3 ChoosePerpendicular(Vector3 normal)
{
    const Vector3 axis = std::fabs(normal.y) < 0.9f ? Vector3{ 0.0f, 1.0f, 0.0f } : Vector3{ 1.0f, 0.0f, 0.0f };
    return NormalizeOrFallback(Vector3CrossProduct(axis, normal), Vector3{ 1.0f, 0.0f, 0.0f });
}

void BuildMeshTangents(const std::vector<float>& vertices, const std::vector<float>& normals, const float* texcoords, std::vector<float>& tangents)
{
    const size_t vertexCount = vertices.size() / 3;
    tangents.assign(vertexCount * 4, 0.0f);
    if (normals.size() < vertexCount * 3 || !texcoords) return;

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
}

Vector3 LerpVector3(Vector3 a, Vector3 b, float alpha)
{
    return Vector3{
        a.x + (b.x - a.x) * alpha,
        a.y + (b.y - a.y) * alpha,
        a.z + (b.z - a.z) * alpha
    };
}

void UpdateOrbitCameraTransform(OrbitCamera& orbit)
{
    const float cosPitch = std::cos(orbit.pitch);
    const Vector3 offset{
        orbit.distance * cosPitch * std::sin(orbit.yaw),
        orbit.distance * std::sin(orbit.pitch),
        orbit.distance * cosPitch * std::cos(orbit.yaw)
    };

    orbit.camera.position = Vector3Add(orbit.target, offset);
    orbit.camera.target = orbit.target;
    orbit.camera.up = Vector3{ 0.0f, 1.0f, 0.0f };
    orbit.camera.fovy = orbit.snappedView ? std::max(0.1f, orbit.distance) : 45.0f;
    orbit.camera.projection = orbit.snappedView ? CAMERA_ORTHOGRAPHIC : CAMERA_PERSPECTIVE;
}

OrbitCamera CreateDefaultCamera()
{
    OrbitCamera orbit;
    UpdateOrbitCameraTransform(orbit);
    return orbit;
}

float GetFocusDistanceForBounds(const BoundingBox& bounds)
{
    const Vector3 size = Vector3Subtract(bounds.max, bounds.min);
    const float radius = std::max(0.001f, Vector3Length(size) * 0.5f);
    return radius / std::tan(45.0f * DEG2RAD * 0.5f) * 1.25f;
}

void FocusCameraOnBounds(OrbitCamera& orbit, const BoundingBox& bounds)
{
    orbit.target = Vector3Scale(Vector3Add(bounds.min, bounds.max), 0.5f);
    orbit.focusDistance = GetFocusDistanceForBounds(bounds);
    orbit.distance = orbit.focusDistance;
    UpdateOrbitCameraTransform(orbit);
}

void FocusCameraOnPoint(OrbitCamera& orbit, Vector3 point, float sceneScale)
{
    orbit.target = point;
    orbit.focusDistance = std::max(0.25f, sceneScale * 0.18f);
    orbit.distance = orbit.focusDistance;
    UpdateOrbitCameraTransform(orbit);
}

void UpdateBlenderNavigation(OrbitCamera& orbit)
{
    const Vector2 mouseDelta = GetMouseDelta();
    const bool shiftDown = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);
    const bool altDown = IsKeyDown(KEY_LEFT_ALT) || IsKeyDown(KEY_RIGHT_ALT);
    const bool middleDown = IsMouseButtonDown(MOUSE_BUTTON_MIDDLE);

    if (middleDown && shiftDown)
    {
        StopSnappedOrbitDrag(orbit);
        const Vector3 forward = Vector3Normalize(Vector3Subtract(orbit.target, orbit.camera.position));
        const Vector3 right = Vector3Normalize(Vector3CrossProduct(forward, Vector3{ 0.0f, 1.0f, 0.0f }));
        const Vector3 up = Vector3Normalize(Vector3CrossProduct(right, forward));
        const float panScale = orbit.distance * 0.0015f;

        orbit.target = Vector3Add(orbit.target, Vector3Scale(right, -mouseDelta.x * panScale));
        orbit.target = Vector3Add(orbit.target, Vector3Scale(up, mouseDelta.y * panScale));
    }
    else if (middleDown)
    {
        if (altDown)
        {
            UpdateSnappedOrbitCamera(orbit, mouseDelta);
        }
        else
        {
            ExitSnappedOrbitView(orbit);
            orbit.yaw -= mouseDelta.x * 0.008f;
            orbit.pitch += mouseDelta.y * 0.008f;
            orbit.pitch = ClampFloat(orbit.pitch, -89.0f * DEG2RAD, 89.0f * DEG2RAD);
        }
    }
    else
    {
        StopSnappedOrbitDrag(orbit);
    }

    const float wheel = GetMouseWheelMove();
    if (std::fabs(wheel) > 0.0f)
    {
        orbit.distance *= std::pow(0.85f, wheel);
        orbit.distance = std::max(0.001f, orbit.distance);
    }

    UpdateOrbitCameraTransform(orbit);
}

void UpdateMayaNavigation(OrbitCamera& orbit)
{
    const Vector2 mouseDelta = GetMouseDelta();
    const bool altDown = IsKeyDown(KEY_LEFT_ALT) || IsKeyDown(KEY_RIGHT_ALT);
    const bool shiftDown = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);

    if (altDown && IsMouseButtonDown(MOUSE_BUTTON_MIDDLE))
    {
        StopSnappedOrbitDrag(orbit);
        const Vector3 forward = Vector3Normalize(Vector3Subtract(orbit.target, orbit.camera.position));
        const Vector3 right = Vector3Normalize(Vector3CrossProduct(forward, Vector3{ 0.0f, 1.0f, 0.0f }));
        const Vector3 up = Vector3Normalize(Vector3CrossProduct(right, forward));
        const float panScale = orbit.distance * 0.0015f;

        orbit.target = Vector3Add(orbit.target, Vector3Scale(right, -mouseDelta.x * panScale));
        orbit.target = Vector3Add(orbit.target, Vector3Scale(up, mouseDelta.y * panScale));
    }
    else if (altDown && IsMouseButtonDown(MOUSE_BUTTON_LEFT))
    {
        if (shiftDown)
        {
            UpdateSnappedOrbitCamera(orbit, mouseDelta);
        }
        else
        {
            ExitSnappedOrbitView(orbit);
            orbit.yaw -= mouseDelta.x * 0.008f;
            orbit.pitch += mouseDelta.y * 0.008f;
            orbit.pitch = ClampFloat(orbit.pitch, -89.0f * DEG2RAD, 89.0f * DEG2RAD);
        }
    }
    else if (altDown && IsMouseButtonDown(MOUSE_BUTTON_RIGHT))
    {
        StopSnappedOrbitDrag(orbit);
        orbit.distance *= std::pow(1.01f, mouseDelta.y);
        orbit.distance = std::max(0.001f, orbit.distance);
    }
    else
    {
        StopSnappedOrbitDrag(orbit);
    }

    const float wheel = GetMouseWheelMove();
    if (std::fabs(wheel) > 0.0f)
    {
        orbit.distance *= std::pow(0.85f, wheel);
        orbit.distance = std::max(0.001f, orbit.distance);
    }

    UpdateOrbitCameraTransform(orbit);
}

void UpdateNavigation(OrbitCamera& orbit, NavigationPreset preset)
{
    if (preset == NavigationPreset::Maya)
    {
        UpdateMayaNavigation(orbit);
    }
    else
    {
        UpdateBlenderNavigation(orbit);
    }
}

std::string OpenFbxFileDialog()
{
#ifdef _WIN32
    char filePath[4096] = {};

    WindowsOpenFileNameA dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.lpstrFilter = "FBX files (*.fbx)\0*.fbx\0All files (*.*)\0*.*\0";
    dialog.lpstrFile = filePath;
    dialog.nMaxFile = sizeof(filePath);
    dialog.Flags = kOfnFileMustExist | kOfnPathMustExist | kOfnNoChangeDir;
    dialog.lpstrDefExt = "fbx";

    if (GetOpenFileNameA(&dialog))
    {
        return filePath;
    }
#endif

    return {};
}

std::string SaveAsFbxFileDialog(const std::string& sourcePath)
{
#ifdef _WIN32
    char filePath[4096] = {};
    std::filesystem::path defaultPath(sourcePath);
    if (!defaultPath.empty())
    {
        defaultPath.replace_extension("");
        const std::string suggested = defaultPath.string() + "_edited.fbx";
        std::snprintf(filePath, sizeof(filePath), "%s", suggested.c_str());
    }

    WindowsOpenFileNameA dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.lpstrFilter = "FBX files (*.fbx)\0*.fbx\0All files (*.*)\0*.*\0";
    dialog.lpstrFile = filePath;
    dialog.nMaxFile = sizeof(filePath);
    dialog.Flags = kOfnPathMustExist | kOfnNoChangeDir | kOfnOverwritePrompt;
    dialog.lpstrDefExt = "fbx";

    if (GetSaveFileNameA(&dialog))
    {
        return filePath;
    }
#endif

    return {};
}

std::string OpenTextureFileDialog()
{
#ifdef _WIN32
    char filePath[4096] = {};

    WindowsOpenFileNameA dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.lpstrFilter = "Image files (*.png;*.jpg;*.jpeg;*.tga;*.bmp;*.psd;*.gif;*.hdr)\0*.png;*.jpg;*.jpeg;*.tga;*.bmp;*.psd;*.gif;*.hdr\0All files (*.*)\0*.*\0";
    dialog.lpstrFile = filePath;
    dialog.nMaxFile = sizeof(filePath);
    dialog.Flags = kOfnFileMustExist | kOfnPathMustExist | kOfnNoChangeDir;

    if (GetOpenFileNameA(&dialog))
    {
        return filePath;
    }
#endif

    return {};
}

void ApplyNeutralMaterial(LoadedFbxModel& loaded)
{
    if (loaded.valid && loaded.model.materialCount > 0)
    {
        for (int i = 0; i < loaded.model.materialCount; ++i)
        {
            loaded.model.materials[i].maps[MATERIAL_MAP_DIFFUSE].color = Color{ 170, 170, 170, 255 };
        }
    }
}

LitShader LoadBasicLitShader()
{
    const char* vertexShader = R"(
#version 330
in vec3 vertexPosition;
in vec2 vertexTexCoord;
in vec3 vertexNormal;
in vec4 vertexTangent;
in vec4 vertexColor;

uniform mat4 mvp;
uniform mat4 matModel;
uniform mat4 matNormal;

out vec3 fragPosition;
out vec3 fragNormal;
out vec4 fragTangent;
out vec2 fragTexCoord;
out vec4 fragColor;

void main()
{
    fragPosition = vec3(matModel*vec4(vertexPosition, 1.0));
    fragNormal = normalize(vec3(matNormal*vec4(vertexNormal, 0.0)));
    fragTangent = vec4(normalize(vec3(matNormal*vec4(vertexTangent.xyz, 0.0))), vertexTangent.w);
    fragTexCoord = vertexTexCoord;
    fragColor = vertexColor;
    gl_Position = mvp*vec4(vertexPosition, 1.0);
}
)";

    const char* fragmentShader = R"(
#version 330
in vec3 fragPosition;
in vec3 fragNormal;
in vec4 fragTangent;
in vec2 fragTexCoord;
in vec4 fragColor;

uniform vec4 colDiffuse;
uniform sampler2D texture0;
uniform sampler2D texture1;
uniform sampler2D texture2;
uniform sampler2D texture3;
uniform sampler2D texture4;
uniform sampler2D texture5;
uniform sampler2D texture6;
uniform vec3 viewPos;
uniform vec3 lightDir;
uniform vec4 lightColor;
uniform vec4 ambient;
uniform int hasDiffuseMap;
uniform int hasNormalMap;
uniform int hasRoughnessMap;
uniform int hasMetallicMap;
uniform int hasAoMap;
uniform int hasEmissiveMap;
uniform int hasOpacityMap;
uniform int normalDirectX;
uniform int roughnessChannel;
uniform int metallicChannel;
uniform int aoChannel;
uniform int opacityChannel;
uniform int materialPreviewMode;
uniform int texturesVisible;

out vec4 finalColor;

float readPackedChannel(vec4 value, int channel)
{
    if (channel == 1) return value.g;
    if (channel == 2) return value.b;
    return value.r;
}

float readOpacity(vec4 value, int channel)
{
    if (channel == 1) return value.a;
    return dot(value.rgb, vec3(0.333333));
}

void main()
{
    vec3 normal = normalize(fragNormal);

    vec4 diffuseTexel = texturesVisible == 1 && hasDiffuseMap == 1 ? texture(texture0, fragTexCoord) : vec4(1.0);
    vec4 normalTexel = texturesVisible == 1 && hasNormalMap == 1 ? texture(texture2, fragTexCoord) : vec4(0.5, 0.5, 1.0, 1.0);
    if (normalDirectX == 1) normalTexel.g = 1.0 - normalTexel.g;
    if (texturesVisible == 1 && hasNormalMap == 1)
    {
        vec3 tangent = fragTangent.xyz - normal*dot(normal, fragTangent.xyz);
        float tangentLen = dot(tangent, tangent);
        if (tangentLen > 0.000000000001)
        {
            tangent = normalize(tangent);
            vec3 bitangent = normalize(cross(normal, tangent) * fragTangent.w);
            vec3 tangentNormal = normalTexel.xyz*2.0 - 1.0;
            normal = normalize(mat3(tangent, bitangent, normal)*tangentNormal);
        }
    }
    vec4 roughnessTexel = texturesVisible == 1 && hasRoughnessMap == 1 ? texture(texture3, fragTexCoord) : vec4(1.0);
    vec4 metallicTexel = texturesVisible == 1 && hasMetallicMap == 1 ? texture(texture1, fragTexCoord) : vec4(0.0);
    vec4 aoTexel = texturesVisible == 1 && hasAoMap == 1 ? texture(texture4, fragTexCoord) : vec4(1.0);
    vec4 emissiveTexel = texturesVisible == 1 && hasEmissiveMap == 1 ? texture(texture5, fragTexCoord) : vec4(0.0);
    vec4 opacityTexel = texturesVisible == 1 && hasOpacityMap == 1 ? texture(texture6, fragTexCoord) : vec4(1.0);
    float roughness = readPackedChannel(roughnessTexel, roughnessChannel);
    float metallic = readPackedChannel(metallicTexel, metallicChannel);
    float ao = readPackedChannel(aoTexel, aoChannel);
    float opacity = readOpacity(opacityTexel, opacityChannel);
    float surfaceAlpha = colDiffuse.a * fragColor.a * diffuseTexel.a * opacity;
    if (materialPreviewMode == 0 && surfaceAlpha < 0.5) discard;

    vec3 light = normalize(-lightDir);
    float diffuse = max(dot(normal, light), 0.0);

    vec3 viewDir = normalize(viewPos - fragPosition);
    vec3 halfwayDir = normalize(light + viewDir);
    float specular = pow(max(dot(normal, halfwayDir), 0.0), 48.0) * 0.18;

    vec3 base = colDiffuse.rgb * fragColor.rgb * diffuseTexel.rgb;
    vec3 shaded = base * ambient.rgb * ao + base * lightColor.rgb * diffuse * ao + lightColor.rgb * specular * (1.0 - roughness * 0.6) * (0.35 + metallic * 0.65) + emissiveTexel.rgb;

    if (materialPreviewMode == 1) finalColor = vec4(diffuseTexel.rgb, 1.0);
    else if (materialPreviewMode == 2) finalColor = vec4(normalTexel.rgb, 1.0);
    else if (materialPreviewMode == 3) finalColor = vec4(vec3(roughness), 1.0);
    else if (materialPreviewMode == 4) finalColor = vec4(vec3(metallic), 1.0);
    else if (materialPreviewMode == 5) finalColor = vec4(vec3(ao), 1.0);
    else if (materialPreviewMode == 6) finalColor = vec4(emissiveTexel.rgb, 1.0);
    else if (materialPreviewMode == 7) finalColor = vec4(vec3(opacity), 1.0);
    else finalColor = vec4(shaded, surfaceAlpha);
}
)";

    LitShader lit;
    lit.shader = LoadShaderFromMemory(vertexShader, fragmentShader);
    lit.shader.locs[SHADER_LOC_VERTEX_TANGENT] = GetShaderLocation(lit.shader, "vertexTangent");
    lit.viewPositionLoc = GetShaderLocation(lit.shader, "viewPos");
    lit.lightDirectionLoc = GetShaderLocation(lit.shader, "lightDir");
    lit.lightColorLoc = GetShaderLocation(lit.shader, "lightColor");
    lit.ambientLoc = GetShaderLocation(lit.shader, "ambient");
    lit.hasDiffuseMapLoc = GetShaderLocation(lit.shader, "hasDiffuseMap");
    lit.hasNormalMapLoc = GetShaderLocation(lit.shader, "hasNormalMap");
    lit.hasRoughnessMapLoc = GetShaderLocation(lit.shader, "hasRoughnessMap");
    lit.hasMetallicMapLoc = GetShaderLocation(lit.shader, "hasMetallicMap");
    lit.hasAoMapLoc = GetShaderLocation(lit.shader, "hasAoMap");
    lit.hasEmissiveMapLoc = GetShaderLocation(lit.shader, "hasEmissiveMap");
    lit.hasOpacityMapLoc = GetShaderLocation(lit.shader, "hasOpacityMap");
    lit.normalDirectXLoc = GetShaderLocation(lit.shader, "normalDirectX");
    lit.roughnessChannelLoc = GetShaderLocation(lit.shader, "roughnessChannel");
    lit.metallicChannelLoc = GetShaderLocation(lit.shader, "metallicChannel");
    lit.aoChannelLoc = GetShaderLocation(lit.shader, "aoChannel");
    lit.opacityChannelLoc = GetShaderLocation(lit.shader, "opacityChannel");
    lit.materialPreviewModeLoc = GetShaderLocation(lit.shader, "materialPreviewMode");
    lit.texturesVisibleLoc = GetShaderLocation(lit.shader, "texturesVisible");
    lit.shader.locs[SHADER_LOC_MAP_DIFFUSE] = GetShaderLocation(lit.shader, "texture0");
    lit.shader.locs[SHADER_LOC_MAP_METALNESS] = GetShaderLocation(lit.shader, "texture1");
    lit.shader.locs[SHADER_LOC_MAP_NORMAL] = GetShaderLocation(lit.shader, "texture2");
    lit.shader.locs[SHADER_LOC_MAP_ROUGHNESS] = GetShaderLocation(lit.shader, "texture3");
    lit.shader.locs[SHADER_LOC_MAP_OCCLUSION] = GetShaderLocation(lit.shader, "texture4");
    lit.shader.locs[SHADER_LOC_MAP_EMISSION] = GetShaderLocation(lit.shader, "texture5");
    lit.shader.locs[SHADER_LOC_MAP_HEIGHT] = GetShaderLocation(lit.shader, "texture6");
    lit.valid = lit.shader.id != 0;
    return lit;
}

void ApplyLitShader(LoadedFbxModel& loaded, const LitShader& lit)
{
    if (!loaded.hasMesh || !lit.valid) return;

    for (int i = 0; i < loaded.model.materialCount; ++i)
    {
        loaded.model.materials[i].shader = lit.shader;
    }
}

int ToInt(PackedChannel channel)
{
    return static_cast<int>(channel);
}

int ToInt(MaterialPreviewMode mode)
{
    return static_cast<int>(mode);
}

int ToInt(OpacityChannel channel)
{
    return static_cast<int>(channel);
}

const char* GetPackedChannelName(PackedChannel channel)
{
    switch (channel)
    {
    case PackedChannel::R: return "R";
    case PackedChannel::G: return "G";
    case PackedChannel::B: return "B";
    }

    return "R";
}

const char* GetOpacityChannelName(OpacityChannel channel)
{
    switch (channel)
    {
    case OpacityChannel::RGB: return "RGB";
    case OpacityChannel::A: return "A";
    }

    return "A";
}

OpacityChannel NextOpacityChannel(OpacityChannel channel)
{
    switch (channel)
    {
    case OpacityChannel::RGB: return OpacityChannel::A;
    case OpacityChannel::A: return OpacityChannel::RGB;
    }

    return OpacityChannel::A;
}

PackedChannel NextPackedChannel(PackedChannel channel)
{
    switch (channel)
    {
    case PackedChannel::R: return PackedChannel::G;
    case PackedChannel::G: return PackedChannel::B;
    case PackedChannel::B: return PackedChannel::R;
    }

    return PackedChannel::R;
}

const char* GetMaterialPreviewModeName(MaterialPreviewMode mode)
{
    switch (mode)
    {
    case MaterialPreviewMode::Shaded: return "Shaded";
    case MaterialPreviewMode::Diffuse: return "Diffuse";
    case MaterialPreviewMode::Normal: return "Normal";
    case MaterialPreviewMode::Roughness: return "Roughness";
    case MaterialPreviewMode::Metallic: return "Metallic";
    case MaterialPreviewMode::AmbientOcclusion: return "AO";
    case MaterialPreviewMode::Emissive: return "Emissive";
    case MaterialPreviewMode::Opacity: return "Opacity";
    }

    return "Shaded";
}

MaterialPreviewMode NextMaterialPreviewMode(MaterialPreviewMode mode)
{
    switch (mode)
    {
    case MaterialPreviewMode::Shaded: return MaterialPreviewMode::Diffuse;
    case MaterialPreviewMode::Diffuse: return MaterialPreviewMode::Normal;
    case MaterialPreviewMode::Normal: return MaterialPreviewMode::Roughness;
    case MaterialPreviewMode::Roughness: return MaterialPreviewMode::Metallic;
    case MaterialPreviewMode::Metallic: return MaterialPreviewMode::AmbientOcclusion;
    case MaterialPreviewMode::AmbientOcclusion: return MaterialPreviewMode::Emissive;
    case MaterialPreviewMode::Emissive: return MaterialPreviewMode::Opacity;
    case MaterialPreviewMode::Opacity: return MaterialPreviewMode::Shaded;
    }

    return MaterialPreviewMode::Shaded;
}

MaterialMapIndex GetMaterialMapIndex(PbrTextureSlot slot)
{
    switch (slot)
    {
    case PbrTextureSlot::Diffuse: return MATERIAL_MAP_DIFFUSE;
    case PbrTextureSlot::Normal: return MATERIAL_MAP_NORMAL;
    case PbrTextureSlot::Roughness: return MATERIAL_MAP_ROUGHNESS;
    case PbrTextureSlot::Metallic: return MATERIAL_MAP_METALNESS;
    case PbrTextureSlot::AmbientOcclusion: return MATERIAL_MAP_OCCLUSION;
    case PbrTextureSlot::Emissive: return MATERIAL_MAP_EMISSION;
    case PbrTextureSlot::Opacity: return MATERIAL_MAP_HEIGHT;
    case PbrTextureSlot::Count: break;
    }

    return MATERIAL_MAP_DIFFUSE;
}

const char* GetPbrTextureSlotName(PbrTextureSlot slot)
{
    switch (slot)
    {
    case PbrTextureSlot::Diffuse: return "Diffuse";
    case PbrTextureSlot::Normal: return "Normal";
    case PbrTextureSlot::Roughness: return "Roughness";
    case PbrTextureSlot::Metallic: return "Metallic";
    case PbrTextureSlot::AmbientOcclusion: return "AO";
    case PbrTextureSlot::Emissive: return "Emissive";
    case PbrTextureSlot::Opacity: return "Opacity";
    case PbrTextureSlot::Count: break;
    }

    return "Map";
}

PbrTexture& GetPbrTexture(PbrMaterialState& pbr, PbrTextureSlot slot)
{
    return pbr.textures[static_cast<size_t>(slot)];
}

const PbrTexture& GetPbrTexture(const PbrMaterialState& pbr, PbrTextureSlot slot)
{
    return pbr.textures[static_cast<size_t>(slot)];
}

void EnsurePbrMaterialStates(ModelTab& tab)
{
    const int materialCount = tab.loaded.hasMesh ? std::max(1, tab.loaded.model.materialCount) : 1;
    if (tab.pbrMaterials.size() != static_cast<size_t>(materialCount))
    {
        tab.pbrMaterials.resize(static_cast<size_t>(materialCount));
    }
    tab.selectedMaterial = std::max(0, std::min(tab.selectedMaterial, materialCount - 1));
}

PbrMaterialState& GetSelectedPbrMaterial(ModelTab& tab)
{
    EnsurePbrMaterialStates(tab);
    return tab.pbrMaterials[static_cast<size_t>(tab.selectedMaterial)];
}

const PbrMaterialState& GetSelectedPbrMaterial(const ModelTab& tab)
{
    static const PbrMaterialState empty;
    if (tab.pbrMaterials.empty()) return empty;
    const int materialCount = tab.loaded.hasMesh ? std::max(1, tab.loaded.model.materialCount) : 1;
    const int materialIndex = std::max(0, std::min(tab.selectedMaterial, materialCount - 1));
    return tab.pbrMaterials[static_cast<size_t>(materialIndex)];
}

bool IsDeletedNode(const ModelTab& tab, int nodeIndex);
bool IsSceneNodeVisible(const ModelTab& tab, const std::vector<bool>& collapsed, int nodeIndex);
bool IsNodeSelected(const ModelTab& tab, int nodeIndex);
void PruneSelectedNodes(ModelTab& tab);
void ClearNodeSelection(ModelTab& tab);
void SetSingleSelectedNode(ModelTab& tab, int nodeIndex);
void SelectNode(ModelTab& tab, int nodeIndex, bool additive);
bool ApplyScaleToNode(ModelTab& tab, int nodeIndex);
bool ResetBoneSubtreeToOriginalBindPose(ModelTab& tab, int rootNodeIndex);
std::vector<int> GetSelectedTransformRoots(const ModelTab& tab);
void InvalidateDisplayedAnimationCaches(ModelTab& tab);
void RefreshDisplayedMesh(ModelTab& tab);
bool HasCpuSkinnedMesh(const LoadedFbxModel& loaded);
const BonePose* FindBonePoseByNode(const std::vector<BonePose>& poses, int nodeIndex);
bool RebuildCurrentSkinnedMeshFromBones(ModelTab& tab);
bool RebuildSkinnedAnimationMeshFrames(ModelTab& tab);
MeshFrame BuildSkinnedMeshFrame(const LoadedFbxModel& target, const BoneFrame& boneFrame);
void DrawUiText(Font font, const char* text, float x, float y, float size, Color color);
void DrawUiTextClipped(Font font, const char* text, float x, float y, float size, float maxWidth, Color color);
bool DrawPanelButton(Font font, Rectangle bounds, const char* label);

void ApplyPbrTextureToModel(ModelTab& tab, int materialIndex, PbrTextureSlot slot)
{
    if (!tab.loaded.hasMesh || tab.loaded.model.materialCount <= 0) return;
    if (materialIndex < 0 || materialIndex >= tab.loaded.model.materialCount) return;

    EnsurePbrMaterialStates(tab);
    const PbrTexture& texture = GetPbrTexture(tab.pbrMaterials[static_cast<size_t>(materialIndex)], slot);
    const MaterialMapIndex mapIndex = GetMaterialMapIndex(slot);
    tab.loaded.model.materials[materialIndex].maps[mapIndex].texture = texture.loaded ? texture.texture : Texture2D{};
}

void UnloadPbrTexture(ModelTab& tab, int materialIndex, PbrTextureSlot slot)
{
    EnsurePbrMaterialStates(tab);
    if (materialIndex < 0 || materialIndex >= static_cast<int>(tab.pbrMaterials.size())) return;

    PbrTexture& texture = GetPbrTexture(tab.pbrMaterials[static_cast<size_t>(materialIndex)], slot);
    if (texture.loaded)
    {
        UnloadTexture(texture.texture);
    }
    texture = PbrTexture{};
    ApplyPbrTextureToModel(tab, materialIndex, slot);
}

bool LoadPbrTexture(ModelTab& tab, int materialIndex, PbrTextureSlot slot, const std::string& path, std::string& error)
{
    error.clear();
    if (path.empty()) return false;
    EnsurePbrMaterialStates(tab);
    if (materialIndex < 0 || materialIndex >= static_cast<int>(tab.pbrMaterials.size()))
    {
        error = "Invalid material index.";
        return false;
    }

    Texture2D texture = LoadTexture(path.c_str());
    if (texture.id == 0)
    {
        error = std::string("Failed to load texture: ") + path;
        return false;
    }

    SetTextureFilter(texture, TEXTURE_FILTER_BILINEAR);
    UnloadPbrTexture(tab, materialIndex, slot);
    PbrTexture& pbrTexture = GetPbrTexture(tab.pbrMaterials[static_cast<size_t>(materialIndex)], slot);
    pbrTexture.texture = texture;
    pbrTexture.path = path;
    pbrTexture.loaded = true;
    ApplyPbrTextureToModel(tab, materialIndex, slot);
    return true;
}

void UnloadPbrTextures(ModelTab& tab)
{
    for (int materialIndex = 0; materialIndex < static_cast<int>(tab.pbrMaterials.size()); ++materialIndex)
    {
        for (int i = 0; i < static_cast<int>(PbrTextureSlot::Count); ++i)
        {
            UnloadPbrTexture(tab, materialIndex, static_cast<PbrTextureSlot>(i));
        }
    }
}

EditSnapshot CaptureEditSnapshot(const ModelTab& tab)
{
    EditSnapshot snapshot;
    snapshot.bounds = tab.loaded.bounds;
    snapshot.nodes = tab.loaded.nodes;
    snapshot.bones = tab.loaded.bones;
    snapshot.bonePoses = tab.loaded.bonePoses;
    snapshot.bindVertices = tab.loaded.bindVertices;
    snapshot.bindNormals = tab.loaded.bindNormals;
    snapshot.currentVertices = tab.currentVertices;
    snapshot.currentNormals = tab.currentNormals;
    snapshot.skinnedVertices = tab.loaded.skinnedVertices;
    snapshot.visibleBones = tab.visibleBones;
    snapshot.visibleBonePoses = tab.visibleBonePoses;
    snapshot.originalBindBonePoses = tab.originalBindBonePoses;
    snapshot.animations = tab.loaded.animations;
    snapshot.deletedNodes = tab.deletedNodes;
    snapshot.selectedNode = tab.selectedNode;
    snapshot.selectedNodes = tab.selectedNodes;
    snapshot.selectedMaterial = tab.selectedMaterial;
    snapshot.animationClipIndex = tab.animation.clipIndex;
    snapshot.animationClipScroll = tab.animation.clipScroll;
    snapshot.animationTime = tab.animation.time;
    snapshot.manualSkinnedMeshPose = tab.manualSkinnedMeshPose;
    snapshot.pbrMaterials.reserve(tab.pbrMaterials.size());

    for (const PbrMaterialState& material : tab.pbrMaterials)
    {
        PbrMaterialSnapshot materialSnapshot;
        materialSnapshot.normalDirectX = material.normalDirectX;
        materialSnapshot.roughnessChannel = material.roughnessChannel;
        materialSnapshot.metallicChannel = material.metallicChannel;
        materialSnapshot.aoChannel = material.aoChannel;
        materialSnapshot.opacityChannel = material.opacityChannel;

        for (int slotIndex = 0; slotIndex < static_cast<int>(PbrTextureSlot::Count); ++slotIndex)
        {
            const PbrTexture& texture = material.textures[static_cast<size_t>(slotIndex)];
            materialSnapshot.textures[static_cast<size_t>(slotIndex)].path = texture.path;
            materialSnapshot.textures[static_cast<size_t>(slotIndex)].loaded = texture.loaded;
        }

        snapshot.pbrMaterials.push_back(std::move(materialSnapshot));
    }

    return snapshot;
}

void TrimEditHistory(std::vector<EditSnapshot>& history)
{
    constexpr size_t kMaxEditHistory = 64;
    if (history.size() > kMaxEditHistory)
    {
        history.erase(history.begin(), history.begin() + static_cast<std::ptrdiff_t>(history.size() - kMaxEditHistory));
    }
}

void PushUndoSnapshot(ModelTab& tab, EditSnapshot snapshot)
{
    tab.undoStack.push_back(std::move(snapshot));
    TrimEditHistory(tab.undoStack);
    tab.redoStack.clear();
}

void PushUndoSnapshot(ModelTab& tab)
{
    PushUndoSnapshot(tab, CaptureEditSnapshot(tab));
}

void RestoreEditSnapshot(ModelTab& tab, const EditSnapshot& snapshot)
{
    tab.loaded.bounds = snapshot.bounds;
    tab.loaded.nodes = snapshot.nodes;
    tab.loaded.bones = snapshot.bones;
    tab.loaded.bonePoses = snapshot.bonePoses;
    tab.loaded.bindVertices = snapshot.bindVertices;
    tab.loaded.bindNormals = snapshot.bindNormals;
    tab.currentVertices = snapshot.currentVertices;
    tab.currentNormals = snapshot.currentNormals;
    tab.loaded.skinnedVertices = snapshot.skinnedVertices;
    tab.visibleBones = snapshot.visibleBones;
    tab.visibleBonePoses = snapshot.visibleBonePoses;
    tab.originalBindBonePoses = snapshot.originalBindBonePoses;
    tab.loaded.animations = snapshot.animations;
    tab.deletedNodes = snapshot.deletedNodes;
    tab.selectedNode = snapshot.selectedNode;
    tab.selectedNodes = snapshot.selectedNodes;
    PruneSelectedNodes(tab);
    tab.selectedMaterial = snapshot.selectedMaterial;
    tab.animation.clipIndex = snapshot.animationClipIndex;
    tab.animation.clipScroll = snapshot.animationClipScroll;
    tab.animation.time = snapshot.animationTime;
    tab.manualSkinnedMeshPose = snapshot.manualSkinnedMeshPose;
    tab.animation.playing = false;
    tab.animation.scrubbing = false;
    tab.animation.contextMenuOpen = false;
    tab.animation.contextClipIndex = -1;

    UnloadPbrTextures(tab);
    tab.pbrMaterials.clear();
    tab.pbrMaterials.resize(snapshot.pbrMaterials.size());
    for (int materialIndex = 0; materialIndex < static_cast<int>(snapshot.pbrMaterials.size()); ++materialIndex)
    {
        const PbrMaterialSnapshot& materialSnapshot = snapshot.pbrMaterials[static_cast<size_t>(materialIndex)];
        PbrMaterialState& material = tab.pbrMaterials[static_cast<size_t>(materialIndex)];
        material.normalDirectX = materialSnapshot.normalDirectX;
        material.roughnessChannel = materialSnapshot.roughnessChannel;
        material.metallicChannel = materialSnapshot.metallicChannel;
        material.aoChannel = materialSnapshot.aoChannel;
        material.opacityChannel = materialSnapshot.opacityChannel;

        for (int slotIndex = 0; slotIndex < static_cast<int>(PbrTextureSlot::Count); ++slotIndex)
        {
            const PbrTextureSnapshot& textureSnapshot = materialSnapshot.textures[static_cast<size_t>(slotIndex)];
            if (!textureSnapshot.loaded || textureSnapshot.path.empty()) continue;

            std::string loadError;
            LoadPbrTexture(tab, materialIndex, static_cast<PbrTextureSlot>(slotIndex), textureSnapshot.path, loadError);
        }
    }

    EnsurePbrMaterialStates(tab);
    tab.selectedMaterial = std::max(0, std::min(tab.selectedMaterial, static_cast<int>(tab.pbrMaterials.size()) - 1));
    InvalidateDisplayedAnimationCaches(tab);
    RefreshDisplayedMesh(tab);
}

bool UndoEdit(ModelTab& tab)
{
    if (tab.undoStack.empty()) return false;
    tab.redoStack.push_back(CaptureEditSnapshot(tab));
    EditSnapshot snapshot = std::move(tab.undoStack.back());
    tab.undoStack.pop_back();
    RestoreEditSnapshot(tab, snapshot);
    return true;
}

bool RedoEdit(ModelTab& tab)
{
    if (tab.redoStack.empty()) return false;
    tab.undoStack.push_back(CaptureEditSnapshot(tab));
    TrimEditHistory(tab.undoStack);
    EditSnapshot snapshot = std::move(tab.redoStack.back());
    tab.redoStack.pop_back();
    RestoreEditSnapshot(tab, snapshot);
    return true;
}

void UpdateMaterialShader(const LitShader& lit, const PbrMaterialState* pbr, MaterialPreviewMode previewMode, bool texturesVisible)
{
    if (!lit.valid) return;

    const int texturesEnabled = texturesVisible ? 1 : 0;
    const int hasDiffuse = texturesVisible && pbr && GetPbrTexture(*pbr, PbrTextureSlot::Diffuse).loaded ? 1 : 0;
    const int hasNormal = texturesVisible && pbr && GetPbrTexture(*pbr, PbrTextureSlot::Normal).loaded ? 1 : 0;
    const int hasRoughness = texturesVisible && pbr && GetPbrTexture(*pbr, PbrTextureSlot::Roughness).loaded ? 1 : 0;
    const int hasMetallic = texturesVisible && pbr && GetPbrTexture(*pbr, PbrTextureSlot::Metallic).loaded ? 1 : 0;
    const int hasAo = texturesVisible && pbr && GetPbrTexture(*pbr, PbrTextureSlot::AmbientOcclusion).loaded ? 1 : 0;
    const int hasEmissive = texturesVisible && pbr && GetPbrTexture(*pbr, PbrTextureSlot::Emissive).loaded ? 1 : 0;
    const int hasOpacity = texturesVisible && pbr && GetPbrTexture(*pbr, PbrTextureSlot::Opacity).loaded ? 1 : 0;
    const int normalDirectX = pbr && pbr->normalDirectX ? 1 : 0;
    const int roughnessChannel = pbr ? ToInt(pbr->roughnessChannel) : 0;
    const int metallicChannel = pbr ? ToInt(pbr->metallicChannel) : 0;
    const int aoChannel = pbr ? ToInt(pbr->aoChannel) : 0;
    const int opacityChannel = pbr ? ToInt(pbr->opacityChannel) : ToInt(OpacityChannel::RGB);
    const int materialPreviewMode = ToInt(previewMode);

    SetShaderValue(lit.shader, lit.hasDiffuseMapLoc, &hasDiffuse, SHADER_UNIFORM_INT);
    SetShaderValue(lit.shader, lit.hasNormalMapLoc, &hasNormal, SHADER_UNIFORM_INT);
    SetShaderValue(lit.shader, lit.hasRoughnessMapLoc, &hasRoughness, SHADER_UNIFORM_INT);
    SetShaderValue(lit.shader, lit.hasMetallicMapLoc, &hasMetallic, SHADER_UNIFORM_INT);
    SetShaderValue(lit.shader, lit.hasAoMapLoc, &hasAo, SHADER_UNIFORM_INT);
    SetShaderValue(lit.shader, lit.hasEmissiveMapLoc, &hasEmissive, SHADER_UNIFORM_INT);
    SetShaderValue(lit.shader, lit.hasOpacityMapLoc, &hasOpacity, SHADER_UNIFORM_INT);
    SetShaderValue(lit.shader, lit.normalDirectXLoc, &normalDirectX, SHADER_UNIFORM_INT);
    SetShaderValue(lit.shader, lit.roughnessChannelLoc, &roughnessChannel, SHADER_UNIFORM_INT);
    SetShaderValue(lit.shader, lit.metallicChannelLoc, &metallicChannel, SHADER_UNIFORM_INT);
    SetShaderValue(lit.shader, lit.aoChannelLoc, &aoChannel, SHADER_UNIFORM_INT);
    SetShaderValue(lit.shader, lit.opacityChannelLoc, &opacityChannel, SHADER_UNIFORM_INT);
    SetShaderValue(lit.shader, lit.materialPreviewModeLoc, &materialPreviewMode, SHADER_UNIFORM_INT);
    SetShaderValue(lit.shader, lit.texturesVisibleLoc, &texturesEnabled, SHADER_UNIFORM_INT);
}

void DrawMaterialModel(ModelTab& tab, const LitShader& lit, MaterialPreviewMode previewMode, bool texturesVisible)
{
    EnsurePbrMaterialStates(tab);
    const Matrix transform = MatrixIdentity();

    auto drawMeshes = [&](bool transparentPass)
    {
        for (int meshIndex = 0; meshIndex < tab.loaded.model.meshCount; ++meshIndex)
        {
            int materialIndex = tab.loaded.model.meshMaterial ? tab.loaded.model.meshMaterial[meshIndex] : 0;
            materialIndex = std::max(0, std::min(materialIndex, static_cast<int>(tab.pbrMaterials.size()) - 1));

            const PbrMaterialState& pbr = tab.pbrMaterials[static_cast<size_t>(materialIndex)];
            const bool transparentMaterial = texturesVisible &&
                                             previewMode == MaterialPreviewMode::Shaded &&
                                             GetPbrTexture(pbr, PbrTextureSlot::Opacity).loaded;
            if (transparentMaterial != transparentPass) continue;

            UpdateMaterialShader(lit, &pbr, previewMode, texturesVisible);
            DrawMesh(tab.loaded.model.meshes[meshIndex], tab.loaded.model.materials[materialIndex], transform);
        }
    };

    drawMeshes(false);

    rlDrawRenderBatchActive();
    rlDisableDepthMask();
    drawMeshes(true);
    rlDrawRenderBatchActive();
    rlEnableDepthMask();
}

void DrawMeterGridLine(Vector3 start, Vector3 end, bool major, bool floorLine)
{
    const Color color = floorLine ? Color{ 230, 236, 242, 235 } :
                        major ? Color{ 88, 98, 108, 170 } :
                                Color{ 76, 84, 92, 140 };
    rlSetLineWidth(floorLine ? 2.5f : major ? 1.6f : 1.0f);
    DrawLine3D(start, end, color);
    rlSetLineWidth(1.0f);
}

void DrawMeterGrid(const OrbitCamera& orbit)
{
    constexpr int halfCells = 20;
    constexpr float extent = static_cast<float>(halfCells) * kMetersPerGridCell;

    if (!orbit.snappedView || orbit.camera.projection != CAMERA_ORTHOGRAPHIC)
    {
        for (int i = -halfCells; i <= halfCells; ++i)
        {
            const float offset = static_cast<float>(i) * kMetersPerGridCell;
            const bool major = i == 0 || i % 5 == 0;
            DrawMeterGridLine(Vector3{ -extent, 0.0f, offset }, Vector3{ extent, 0.0f, offset }, major, i == 0);
            DrawMeterGridLine(Vector3{ offset, 0.0f, -extent }, Vector3{ offset, 0.0f, extent }, major, i == 0);
        }
        return;
    }

    const Vector3 forward = Vector3Normalize(Vector3Subtract(orbit.camera.target, orbit.camera.position));
    const float absX = std::fabs(forward.x);
    const float absY = std::fabs(forward.y);
    const float absZ = std::fabs(forward.z);

    for (int i = -halfCells; i <= halfCells; ++i)
    {
        const float offset = static_cast<float>(i) * kMetersPerGridCell;
        const bool major = i == 0 || i % 5 == 0;
        if (absY >= absX && absY >= absZ)
        {
            DrawMeterGridLine(Vector3{ -extent, 0.0f, offset }, Vector3{ extent, 0.0f, offset }, major, i == 0);
            DrawMeterGridLine(Vector3{ offset, 0.0f, -extent }, Vector3{ offset, 0.0f, extent }, major, i == 0);
        }
        else if (absZ >= absX)
        {
            const float z = orbit.target.z;
            DrawMeterGridLine(Vector3{ -extent, offset, z }, Vector3{ extent, offset, z }, major, i == 0);
            DrawMeterGridLine(Vector3{ offset, -extent, z }, Vector3{ offset, extent, z }, major, false);
        }
        else
        {
            const float x = orbit.target.x;
            DrawMeterGridLine(Vector3{ x, offset, -extent }, Vector3{ x, offset, extent }, major, i == 0);
            DrawMeterGridLine(Vector3{ x, -extent, offset }, Vector3{ x, extent, offset }, major, false);
        }
    }
}

void UpdateLitShader(const LitShader& lit, const OrbitCamera& orbit)
{
    if (!lit.valid) return;

    const float viewPosition[3] = { orbit.camera.position.x, orbit.camera.position.y, orbit.camera.position.z };
    Vector3 light = Vector3Normalize(Vector3Subtract(orbit.target, orbit.camera.position));
    light = Vector3Normalize(Vector3Add(light, Vector3{ 0.0f, -0.35f, 0.0f }));
    const float lightDirection[3] = { light.x, light.y, light.z };
    const float lightColor[4] = { 1.12f, 1.08f, 1.0f, 1.0f };
    const float ambient[4] = { 0.46f, 0.48f, 0.50f, 1.0f };

    SetShaderValue(lit.shader, lit.viewPositionLoc, viewPosition, SHADER_UNIFORM_VEC3);
    SetShaderValue(lit.shader, lit.lightDirectionLoc, lightDirection, SHADER_UNIFORM_VEC3);
    SetShaderValue(lit.shader, lit.lightColorLoc, lightColor, SHADER_UNIFORM_VEC4);
    SetShaderValue(lit.shader, lit.ambientLoc, ambient, SHADER_UNIFORM_VEC4);
}

bool IsViewportNodeVisible(const ModelTab& tab, int nodeIndex);

const std::vector<BoneSegment>& GetVisibleBones(const ModelTab& tab)
{
    const std::vector<BoneSegment>& source = tab.visibleBones.empty() ? tab.loaded.bones : tab.visibleBones;
    if (tab.deletedNodes.empty() && tab.isolatedNode < 0) return source;

    static std::vector<BoneSegment> filteredBones;
    filteredBones.clear();
    for (const BoneSegment& bone : source)
    {
        if (!IsViewportNodeVisible(tab, bone.startNode) || !IsViewportNodeVisible(tab, bone.endNode)) continue;
        filteredBones.push_back(bone);
    }
    return filteredBones;
}

const std::vector<BonePose>& GetVisibleBonePoses(const ModelTab& tab)
{
    const std::vector<BonePose>& source = tab.visibleBonePoses.empty() ? tab.loaded.bonePoses : tab.visibleBonePoses;
    if (tab.deletedNodes.empty() && tab.isolatedNode < 0) return source;

    static std::vector<BonePose> filteredPoses;
    filteredPoses.clear();
    for (const BonePose& pose : source)
    {
        if (!IsViewportNodeVisible(tab, pose.node)) continue;
        filteredPoses.push_back(pose);
    }
    return filteredPoses;
}

int GetAnimationFrameIndex(const LoadedFbxModel& loaded, const AnimationState& animation)
{
    if (animation.clipIndex < 0 || animation.clipIndex >= static_cast<int>(loaded.animations.size()))
    {
        return -1;
    }

    const AnimationClip& clip = loaded.animations[static_cast<size_t>(animation.clipIndex)];
    if (clip.duration <= 0.0f || clip.frames.empty())
    {
        return clip.frames.empty() ? -1 : 0;
    }

    const float normalizedTime = ClampFloat(animation.time, 0.0f, clip.duration);
    const float frameAlpha = normalizedTime / clip.duration * static_cast<float>(clip.frames.size() - 1);
    return static_cast<int>(ClampFloat(std::round(frameAlpha), 0.0f, static_cast<float>(clip.frames.size() - 1)));
}

struct MeshFrameSample
{
    int first = -1;
    int second = -1;
    float alpha = 0.0f;
};

MeshFrameSample GetFrameSample(float duration, size_t frameCount, float time)
{
    if (duration <= 0.0f || frameCount == 0)
    {
        return MeshFrameSample{ frameCount == 0 ? -1 : 0, frameCount == 0 ? -1 : 0, 0.0f };
    }

    const float normalizedTime = ClampFloat(time, 0.0f, duration);
    const float framePosition = normalizedTime / duration * static_cast<float>(frameCount - 1);
    const int first = static_cast<int>(ClampFloat(std::floor(framePosition), 0.0f, static_cast<float>(frameCount - 1)));
    const int second = std::min(first + 1, static_cast<int>(frameCount) - 1);
    return MeshFrameSample{ first, second, framePosition - static_cast<float>(first) };
}

MeshFrameSample GetMeshFrameSample(const AnimationClip& clip, float time)
{
    return GetFrameSample(clip.duration, clip.meshFrames.size(), time);
}

bool IsDescendantNode(const LoadedFbxModel& loaded, int possibleDescendant, int ancestor);

bool IsNodeVisibleInIsolation(const ModelTab& tab, int nodeIndex)
{
    if (tab.isolatedNode < 0) return true;
    if (nodeIndex < 0 || nodeIndex >= static_cast<int>(tab.loaded.nodes.size())) return false;
    if (tab.isolatedNode >= static_cast<int>(tab.loaded.nodes.size())) return true;
    return nodeIndex == tab.isolatedNode || IsDescendantNode(tab.loaded, nodeIndex, tab.isolatedNode);
}

bool IsViewportNodeVisible(const ModelTab& tab, int nodeIndex)
{
    return !IsDeletedNode(tab, nodeIndex) && IsNodeVisibleInIsolation(tab, nodeIndex);
}

void UploadGlobalMeshFrame(ModelTab& tab, const float* vertices, const float* normals, size_t expectedFloats)
{
    if (!vertices || !normals || !tab.loaded.hasMesh) return;
    tab.currentVertices.assign(vertices, vertices + expectedFloats);
    tab.currentNormals.assign(normals, normals + expectedFloats);

    for (int meshIndex = 0; meshIndex < tab.loaded.model.meshCount; ++meshIndex)
    {
        if (meshIndex >= static_cast<int>(tab.loaded.meshGlobalVertexIndices.size())) continue;

        Mesh& mesh = tab.loaded.model.meshes[meshIndex];
        const std::vector<int>& globalIndices = tab.loaded.meshGlobalVertexIndices[static_cast<size_t>(meshIndex)];
        if (globalIndices.size() != static_cast<size_t>(mesh.vertexCount)) continue;

        tab.blendedVertices.resize(globalIndices.size() * 3);
        tab.blendedNormals.resize(globalIndices.size() * 3);

        for (size_t localVertex = 0; localVertex < globalIndices.size(); ++localVertex)
        {
            const size_t globalBase = static_cast<size_t>(globalIndices[localVertex]) * 3;
            const size_t localBase = localVertex * 3;
            if (globalBase + 2 >= expectedFloats) continue;

            int hiddenMeshNode = -1;
            const int globalVertex = globalIndices[localVertex];
            if (!tab.deletedNodes.empty() || tab.isolatedNode >= 0)
            {
                for (int nodeIndex = 0; nodeIndex < static_cast<int>(tab.loaded.nodes.size()); ++nodeIndex)
                {
                    const SceneNode& node = tab.loaded.nodes[static_cast<size_t>(nodeIndex)];
                    if (node.type != SceneNodeType::Mesh || node.meshVertexStart < 0 || node.meshVertexCount <= 0) continue;
                    if (globalVertex >= node.meshVertexStart && globalVertex < node.meshVertexStart + node.meshVertexCount)
                    {
                        if (!IsViewportNodeVisible(tab, nodeIndex))
                        {
                            hiddenMeshNode = nodeIndex;
                        }
                        break;
                    }
                }
            }

            if (hiddenMeshNode >= 0)
            {
                const Vector3 collapsed = tab.loaded.nodes[static_cast<size_t>(hiddenMeshNode)].position;
                tab.blendedVertices[localBase] = collapsed.x;
                tab.blendedVertices[localBase + 1] = collapsed.y;
                tab.blendedVertices[localBase + 2] = collapsed.z;
                tab.blendedNormals[localBase] = 0.0f;
                tab.blendedNormals[localBase + 1] = 1.0f;
                tab.blendedNormals[localBase + 2] = 0.0f;
            }
            else
            {
                tab.blendedVertices[localBase] = vertices[globalBase];
                tab.blendedVertices[localBase + 1] = vertices[globalBase + 1];
                tab.blendedVertices[localBase + 2] = vertices[globalBase + 2];
                tab.blendedNormals[localBase] = normals[globalBase];
                tab.blendedNormals[localBase + 1] = normals[globalBase + 1];
                tab.blendedNormals[localBase + 2] = normals[globalBase + 2];
            }
        }

        BuildMeshTangents(tab.blendedVertices, tab.blendedNormals, mesh.texcoords, tab.blendedTangents);
        const size_t vertexBytes = globalIndices.size() * 3 * sizeof(float);
        const size_t tangentBytes = globalIndices.size() * 4 * sizeof(float);
        std::memcpy(mesh.vertices, tab.blendedVertices.data(), vertexBytes);
        std::memcpy(mesh.normals, tab.blendedNormals.data(), vertexBytes);
        if (mesh.tangents && tab.blendedTangents.size() >= globalIndices.size() * 4)
        {
            std::memcpy(mesh.tangents, tab.blendedTangents.data(), tangentBytes);
        }
        UpdateMeshBuffer(mesh, 0, tab.blendedVertices.data(), static_cast<int>(vertexBytes), 0);
        UpdateMeshBuffer(mesh, 2, tab.blendedNormals.data(), static_cast<int>(vertexBytes), 0);
        if (mesh.tangents && tab.blendedTangents.size() >= globalIndices.size() * 4)
        {
            UpdateMeshBuffer(mesh, 4, tab.blendedTangents.data(), static_cast<int>(tangentBytes), 0);
        }
    }
}

bool IsDeletedNode(const ModelTab& tab, int nodeIndex)
{
    return nodeIndex >= 0 &&
           nodeIndex < static_cast<int>(tab.deletedNodes.size()) &&
           tab.deletedNodes[static_cast<size_t>(nodeIndex)];
}

bool IsValidSelectableNode(const ModelTab& tab, int nodeIndex)
{
    return nodeIndex >= 0 &&
           nodeIndex < static_cast<int>(tab.loaded.nodes.size()) &&
           !IsDeletedNode(tab, nodeIndex);
}

bool IsNodeSelected(const ModelTab& tab, int nodeIndex)
{
    return std::find(tab.selectedNodes.begin(), tab.selectedNodes.end(), nodeIndex) != tab.selectedNodes.end();
}

void PruneSelectedNodes(ModelTab& tab)
{
    tab.selectedNodes.erase(std::remove_if(tab.selectedNodes.begin(), tab.selectedNodes.end(), [&](int nodeIndex)
    {
        return !IsValidSelectableNode(tab, nodeIndex);
    }), tab.selectedNodes.end());

    if (tab.selectedNode >= 0 && !IsNodeSelected(tab, tab.selectedNode))
    {
        tab.selectedNode = tab.selectedNodes.empty() ? -1 : tab.selectedNodes.back();
    }
    if (tab.selectedNode < 0 && !tab.selectedNodes.empty())
    {
        tab.selectedNode = tab.selectedNodes.back();
    }
}

void ClearNodeSelection(ModelTab& tab)
{
    tab.selectedNode = -1;
    tab.selectedNodes.clear();
}

void SetSingleSelectedNode(ModelTab& tab, int nodeIndex)
{
    if (!IsValidSelectableNode(tab, nodeIndex))
    {
        ClearNodeSelection(tab);
        return;
    }

    tab.selectedNode = nodeIndex;
    tab.selectedNodes.assign(1, nodeIndex);
}

void ToggleSelectedNode(ModelTab& tab, int nodeIndex)
{
    if (!IsValidSelectableNode(tab, nodeIndex)) return;

    const auto found = std::find(tab.selectedNodes.begin(), tab.selectedNodes.end(), nodeIndex);
    if (found != tab.selectedNodes.end())
    {
        tab.selectedNodes.erase(found);
        if (tab.selectedNode == nodeIndex)
        {
            tab.selectedNode = tab.selectedNodes.empty() ? -1 : tab.selectedNodes.back();
        }
        return;
    }

    tab.selectedNodes.push_back(nodeIndex);
    tab.selectedNode = nodeIndex;
}

void SelectNode(ModelTab& tab, int nodeIndex, bool additive)
{
    if (additive)
    {
        ToggleSelectedNode(tab, nodeIndex);
    }
    else
    {
        SetSingleSelectedNode(tab, nodeIndex);
    }
}

void SetVisibleNodeRangeSelection(ModelTab& tab, const std::vector<bool>& collapsed, int anchorNode, int targetNode)
{
    if (!IsValidSelectableNode(tab, anchorNode) || !IsValidSelectableNode(tab, targetNode)) return;

    int anchorRow = -1;
    int targetRow = -1;
    int visibleRow = 0;
    for (int i = 0; i < static_cast<int>(tab.loaded.nodes.size()); ++i)
    {
        if (!IsSceneNodeVisible(tab, collapsed, i)) continue;
        if (i == anchorNode) anchorRow = visibleRow;
        if (i == targetNode) targetRow = visibleRow;
        ++visibleRow;
    }

    if (anchorRow < 0 || targetRow < 0) return;

    const int firstRow = std::min(anchorRow, targetRow);
    const int lastRow = std::max(anchorRow, targetRow);
    tab.selectedNodes.clear();
    visibleRow = 0;
    for (int i = 0; i < static_cast<int>(tab.loaded.nodes.size()); ++i)
    {
        if (!IsSceneNodeVisible(tab, collapsed, i)) continue;
        if (visibleRow >= firstRow && visibleRow <= lastRow && IsValidSelectableNode(tab, i))
        {
            tab.selectedNodes.push_back(i);
        }
        ++visibleRow;
    }
    tab.selectedNode = targetNode;
}

void RefreshDisplayedMesh(ModelTab& tab)
{
    if (!tab.loaded.hasMesh || tab.loaded.bindVertices.size() != tab.loaded.bindNormals.size()) return;

    if (tab.currentVertices.size() == tab.loaded.bindVertices.size() &&
        tab.currentNormals.size() == tab.loaded.bindNormals.size())
    {
        UploadGlobalMeshFrame(tab, tab.currentVertices.data(), tab.currentNormals.data(), tab.currentVertices.size());
        return;
    }

    UploadGlobalMeshFrame(tab, tab.loaded.bindVertices.data(), tab.loaded.bindNormals.data(), tab.loaded.bindVertices.size());
}

bool IsDescendantNode(const LoadedFbxModel& loaded, int possibleDescendant, int ancestor)
{
    int parent = possibleDescendant >= 0 && possibleDescendant < static_cast<int>(loaded.nodes.size()) ? loaded.nodes[static_cast<size_t>(possibleDescendant)].parent : -1;
    while (parent >= 0)
    {
        if (parent == ancestor) return true;
        parent = parent < static_cast<int>(loaded.nodes.size()) ? loaded.nodes[static_cast<size_t>(parent)].parent : -1;
    }
    return false;
}

int MarkNodeSubtreeDeleted(ModelTab& tab, int nodeIndex)
{
    if (nodeIndex < 0 || nodeIndex >= static_cast<int>(tab.loaded.nodes.size())) return 0;
    if (tab.deletedNodes.size() != tab.loaded.nodes.size())
    {
        tab.deletedNodes.resize(tab.loaded.nodes.size(), false);
    }

    int deletedCount = 0;
    for (int i = 0; i < static_cast<int>(tab.loaded.nodes.size()); ++i)
    {
        if (i != nodeIndex && !IsDescendantNode(tab.loaded, i, nodeIndex)) continue;
        if (tab.deletedNodes[static_cast<size_t>(i)]) continue;
        tab.deletedNodes[static_cast<size_t>(i)] = true;
        ++deletedCount;
    }

    PruneSelectedNodes(tab);
    if (IsDeletedNode(tab, tab.isolatedNode))
    {
        tab.isolatedNode = -1;
    }
    RefreshDisplayedMesh(tab);
    return deletedCount;
}

std::vector<int> GetContextActionNodes(const ModelTab& tab, int contextNodeIndex)
{
    std::vector<int> nodes;
    if (!IsValidSelectableNode(tab, contextNodeIndex)) return nodes;

    if (!IsNodeSelected(tab, contextNodeIndex))
    {
        nodes.push_back(contextNodeIndex);
        return nodes;
    }

    for (int nodeIndex : tab.selectedNodes)
    {
        if (IsValidSelectableNode(tab, nodeIndex))
        {
            nodes.push_back(nodeIndex);
        }
    }
    return nodes;
}

std::vector<int> GetValidContextActionNodes(const ModelTab& tab, const std::vector<int>& contextNodeIndices)
{
    std::vector<int> nodes;
    for (int nodeIndex : contextNodeIndices)
    {
        if (IsValidSelectableNode(tab, nodeIndex) && std::find(nodes.begin(), nodes.end(), nodeIndex) == nodes.end())
        {
            nodes.push_back(nodeIndex);
        }
    }
    return nodes;
}

std::vector<int> GetContextActionNodes(const ModelTab& tab, int contextNodeIndex, const std::vector<int>& contextNodeIndices)
{
    const std::vector<int> frozenNodes = GetValidContextActionNodes(tab, contextNodeIndices);
    if (!frozenNodes.empty()) return frozenNodes;
    return GetContextActionNodes(tab, contextNodeIndex);
}

std::vector<int> GetContextActionRoots(const ModelTab& tab, const std::vector<int>& actionNodes)
{
    std::vector<int> roots;
    for (int nodeIndex : actionNodes)
    {
        if (!IsValidSelectableNode(tab, nodeIndex)) continue;

        bool coveredByContextAncestor = false;
        for (int otherNode : actionNodes)
        {
            if (otherNode == nodeIndex || !IsValidSelectableNode(tab, otherNode)) continue;
            if (IsDescendantNode(tab.loaded, nodeIndex, otherNode))
            {
                coveredByContextAncestor = true;
                break;
            }
        }
        if (!coveredByContextAncestor)
        {
            roots.push_back(nodeIndex);
        }
    }
    return roots;
}

std::vector<int> GetContextActionRoots(const ModelTab& tab, int contextNodeIndex, const std::vector<int>& contextNodeIndices)
{
    return GetContextActionRoots(tab, GetContextActionNodes(tab, contextNodeIndex, contextNodeIndices));
}

bool HasBoneNode(const ModelTab& tab, const std::vector<int>& nodeIndices)
{
    for (int nodeIndex : nodeIndices)
    {
        if (!IsValidSelectableNode(tab, nodeIndex)) continue;
        if (tab.loaded.nodes[static_cast<size_t>(nodeIndex)].type == SceneNodeType::Bone)
        {
            return true;
        }
    }
    return false;
}

int DeleteContextNodeSubtrees(ModelTab& tab, const std::vector<int>& contextRoots)
{
    int deletedCount = 0;
    for (int nodeIndex : contextRoots)
    {
        deletedCount += MarkNodeSubtreeDeleted(tab, nodeIndex);
    }
    return deletedCount;
}

int ApplyScaleToContextNodes(ModelTab& tab, const std::vector<int>& contextNodes)
{
    int changedCount = 0;
    for (int nodeIndex : contextNodes)
    {
        if (ApplyScaleToNode(tab, nodeIndex))
        {
            ++changedCount;
        }
    }
    return changedCount;
}

int ResetContextBoneSubtreesToOriginalBindPose(ModelTab& tab, const std::vector<int>& actionNodes)
{
    int changedCount = 0;
    for (int nodeIndex : actionNodes)
    {
        if (!IsValidSelectableNode(tab, nodeIndex)) continue;
        if (tab.loaded.nodes[static_cast<size_t>(nodeIndex)].type != SceneNodeType::Bone) continue;

        bool coveredBySelectedBoneAncestor = false;
        for (int otherNode : actionNodes)
        {
            if (otherNode == nodeIndex || !IsValidSelectableNode(tab, otherNode)) continue;
            if (tab.loaded.nodes[static_cast<size_t>(otherNode)].type != SceneNodeType::Bone) continue;
            if (IsDescendantNode(tab.loaded, nodeIndex, otherNode))
            {
                coveredBySelectedBoneAncestor = true;
                break;
            }
        }
        if (coveredBySelectedBoneAncestor) continue;

        if (ResetBoneSubtreeToOriginalBindPose(tab, nodeIndex))
        {
            ++changedCount;
        }
    }
    return changedCount;
}

bool ToggleSelectedNodeIsolation(ModelTab& tab, std::string& notice, std::string& error)
{
    if (tab.isolatedNode >= 0 && tab.isolatedNode == tab.selectedNode)
    {
        tab.isolatedNode = -1;
        RefreshDisplayedMesh(tab);
        notice = "Isolation off.";
        error.clear();
        return true;
    }

    if (tab.selectedNode < 0 ||
        tab.selectedNode >= static_cast<int>(tab.loaded.nodes.size()) ||
        IsDeletedNode(tab, tab.selectedNode))
    {
        if (tab.isolatedNode >= 0)
        {
            tab.isolatedNode = -1;
            RefreshDisplayedMesh(tab);
            notice = "Isolation off.";
            error.clear();
            return true;
        }

        error = "Select an object to isolate.";
        notice.clear();
        return false;
    }

    tab.isolatedNode = tab.selectedNode;
    RefreshDisplayedMesh(tab);
    notice = "Isolated: " + tab.loaded.nodes[static_cast<size_t>(tab.isolatedNode)].name;
    error.clear();
    return true;
}

void RenameSceneNode(ModelTab& tab, int nodeIndex, const std::string& newName)
{
    if (nodeIndex < 0 || nodeIndex >= static_cast<int>(tab.loaded.nodes.size()) || newName.empty()) return;

    SceneNode& node = tab.loaded.nodes[static_cast<size_t>(nodeIndex)];
    const std::string oldName = node.name;
    if (oldName == newName) return;
    node.name = newName;

    if (node.type == SceneNodeType::Bone)
    {
        for (SkinnedVertex& vertex : tab.loaded.skinnedVertices)
        {
            for (SkinnedVertexInfluence& influence : vertex.influences)
            {
                if (influence.boneName == oldName)
                {
                    influence.boneName = newName;
                }
            }
        }
    }
}

void StartRenameNode(RenameEditor& editor, const LoadedFbxModel& loaded, int nodeIndex)
{
    if (nodeIndex < 0 || nodeIndex >= static_cast<int>(loaded.nodes.size())) return;
    editor = RenameEditor{};
    editor.target = RenameTarget::SceneNode;
    editor.nodeIndex = nodeIndex;
    editor.active = true;
    editor.justOpened = true;
    std::snprintf(editor.text, sizeof(editor.text), "%s", loaded.nodes[static_cast<size_t>(nodeIndex)].name.c_str());
    editor.cursor = static_cast<int>(std::strlen(editor.text));
    editor.textSelected = editor.cursor > 0;
}

void StartRenameAnimation(RenameEditor& editor, const LoadedFbxModel& loaded, int clipIndex)
{
    if (clipIndex < 0 || clipIndex >= static_cast<int>(loaded.animations.size())) return;
    editor = RenameEditor{};
    editor.target = RenameTarget::AnimationClip;
    editor.clipIndex = clipIndex;
    editor.active = true;
    editor.justOpened = true;
    std::snprintf(editor.text, sizeof(editor.text), "%s", loaded.animations[static_cast<size_t>(clipIndex)].name.c_str());
    editor.cursor = static_cast<int>(std::strlen(editor.text));
    editor.textSelected = editor.cursor > 0;
}

void ClearRenameSelection(RenameEditor& editor)
{
    if (!editor.textSelected) return;
    editor.text[0] = '\0';
    editor.cursor = 0;
    editor.textSelected = false;
}

void InsertRenameText(RenameEditor& editor, const char* text)
{
    if (!text || text[0] == '\0') return;
    ClearRenameSelection(editor);

    const int length = static_cast<int>(std::strlen(editor.text));
    editor.cursor = std::clamp(editor.cursor, 0, length);

    for (const char* read = text; *read; ++read)
    {
        const unsigned char value = static_cast<unsigned char>(*read);
        if (value < 32 || value >= 127) continue;

        const int currentLength = static_cast<int>(std::strlen(editor.text));
        if (currentLength + 1 >= static_cast<int>(sizeof(editor.text))) break;

        std::memmove(editor.text + editor.cursor + 1,
                     editor.text + editor.cursor,
                     static_cast<size_t>(currentLength - editor.cursor + 1));
        editor.text[editor.cursor] = static_cast<char>(value);
        editor.cursor++;
    }
}

void DeleteRenameSelectionOrPreviousChar(RenameEditor& editor)
{
    if (editor.textSelected)
    {
        ClearRenameSelection(editor);
        return;
    }

    const int length = static_cast<int>(std::strlen(editor.text));
    editor.cursor = std::clamp(editor.cursor, 0, length);
    if (editor.cursor <= 0) return;

    std::memmove(editor.text + editor.cursor - 1,
                 editor.text + editor.cursor,
                 static_cast<size_t>(length - editor.cursor + 1));
    editor.cursor--;
}

void DeleteRenameSelectionOrNextChar(RenameEditor& editor)
{
    if (editor.textSelected)
    {
        ClearRenameSelection(editor);
        return;
    }

    const int length = static_cast<int>(std::strlen(editor.text));
    editor.cursor = std::clamp(editor.cursor, 0, length);
    if (editor.cursor >= length) return;

    std::memmove(editor.text + editor.cursor,
                 editor.text + editor.cursor + 1,
                 static_cast<size_t>(length - editor.cursor));
}

void DrawRenameEditor(Font font, ModelTab* active, RenameEditor& editor, std::string& notice, std::string& error)
{
    if (!editor.active || !active) return;

    const bool controlDown = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL);
    int key = GetCharPressed();
    while (key > 0)
    {
        if (!controlDown && key >= 32 && key < 127)
        {
            char value[2]{ static_cast<char>(key), '\0' };
            InsertRenameText(editor, value);
        }
        key = GetCharPressed();
    }

    if (IsKeyPressed(KEY_BACKSPACE))
    {
        DeleteRenameSelectionOrPreviousChar(editor);
    }
    if (IsKeyPressed(KEY_DELETE))
    {
        DeleteRenameSelectionOrNextChar(editor);
    }
    if (controlDown && IsKeyPressed(KEY_A))
    {
        editor.textSelected = std::strlen(editor.text) > 0;
        editor.cursor = static_cast<int>(std::strlen(editor.text));
    }
    if (controlDown && IsKeyPressed(KEY_V))
    {
        InsertRenameText(editor, GetClipboardText());
    }
    if (IsKeyPressed(KEY_LEFT))
    {
        const int length = static_cast<int>(std::strlen(editor.text));
        if (editor.textSelected)
        {
            editor.cursor = 0;
            editor.textSelected = false;
        }
        else
        {
            editor.cursor = std::max(0, std::clamp(editor.cursor, 0, length) - 1);
        }
    }
    if (IsKeyPressed(KEY_RIGHT))
    {
        const int length = static_cast<int>(std::strlen(editor.text));
        if (editor.textSelected)
        {
            editor.cursor = length;
            editor.textSelected = false;
        }
        else
        {
            editor.cursor = std::min(length, std::clamp(editor.cursor, 0, length) + 1);
        }
    }
    if (IsKeyPressed(KEY_HOME))
    {
        editor.cursor = 0;
        editor.textSelected = false;
    }
    if (IsKeyPressed(KEY_END))
    {
        editor.cursor = static_cast<int>(std::strlen(editor.text));
        editor.textSelected = false;
    }
    if (IsKeyPressed(KEY_ESCAPE))
    {
        editor = RenameEditor{};
        return;
    }

    const bool submit = IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER);
    const bool cancelClick = IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
    const float dialogW = 360.0f;
    const float dialogH = 128.0f;
    const Rectangle dialog{ (static_cast<float>(GetScreenWidth()) - dialogW) * 0.5f,
                            (static_cast<float>(GetScreenHeight()) - dialogH) * 0.5f,
                            dialogW,
                            dialogH };
    const Rectangle input{ dialog.x + 16.0f, dialog.y + 48.0f, dialog.width - 32.0f, 28.0f };
    const Rectangle okButton{ dialog.x + dialog.width - 166.0f, dialog.y + dialog.height - 38.0f, 70.0f, 24.0f };
    const Rectangle cancelButton{ dialog.x + dialog.width - 86.0f, dialog.y + dialog.height - 38.0f, 70.0f, 24.0f };

    DrawRectangleRec(Rectangle{ 0.0f, 0.0f, static_cast<float>(GetScreenWidth()), static_cast<float>(GetScreenHeight()) }, Color{ 0, 0, 0, 80 });
    DrawRectangleRec(dialog, Color{ 24, 27, 31, 250 });
    DrawRectangleLinesEx(dialog, 1.0f, Color{ 86, 96, 108, 255 });
    DrawUiText(font, editor.target == RenameTarget::AnimationClip ? "Rename Animation" : "Rename Object", dialog.x + 16.0f, dialog.y + 16.0f, 16.0f, Color{ 205, 213, 220, 255 });
    DrawRectangleRec(input, Color{ 14, 16, 19, 255 });
    DrawRectangleLinesEx(input, 1.0f, Color{ 94, 156, 214, 255 });
    if (editor.textSelected && editor.text[0] != '\0')
    {
        const float selectionW = std::min(input.width - 16.0f, MeasureTextEx(font, editor.text, 15.0f, 1.0f).x + 2.0f);
        DrawRectangleRec(Rectangle{ input.x + 7.0f, input.y + 5.0f, selectionW, input.height - 10.0f }, Color{ 52, 106, 158, 210 });
    }
    DrawUiTextClipped(font, editor.text, input.x + 8.0f, input.y + 6.0f, 15.0f, input.width - 16.0f, RAYWHITE);
    if (!editor.textSelected)
    {
        const int cursor = std::clamp(editor.cursor, 0, static_cast<int>(std::strlen(editor.text)));
        const std::string beforeCursor(editor.text, editor.text + cursor);
        const float caretX = std::min(input.x + input.width - 8.0f, input.x + 8.0f + MeasureTextEx(font, beforeCursor.c_str(), 15.0f, 1.0f).x);
        DrawRectangleRec(Rectangle{ caretX, input.y + 6.0f, 1.0f, input.height - 12.0f }, Color{ 230, 236, 242, 255 });
    }

    if (editor.justOpened)
    {
        editor.justOpened = false;
        return;
    }

    if (DrawPanelButton(font, okButton, "OK") || submit)
    {
        const std::string newName = editor.text;
        if (newName.empty())
        {
            error = "Name cannot be empty.";
            notice.clear();
        }
        else if (editor.target == RenameTarget::SceneNode)
        {
            const bool changed = editor.nodeIndex >= 0 &&
                                 editor.nodeIndex < static_cast<int>(active->loaded.nodes.size()) &&
                                 active->loaded.nodes[static_cast<size_t>(editor.nodeIndex)].name != newName;
            if (changed)
            {
                PushUndoSnapshot(*active);
            }
            RenameSceneNode(*active, editor.nodeIndex, newName);
            notice = "Renamed object.";
            error.clear();
            editor = RenameEditor{};
        }
        else if (editor.target == RenameTarget::AnimationClip &&
                 editor.clipIndex >= 0 &&
                 editor.clipIndex < static_cast<int>(active->loaded.animations.size()))
        {
            AnimationClip& clip = active->loaded.animations[static_cast<size_t>(editor.clipIndex)];
            if (clip.name != newName)
            {
                PushUndoSnapshot(*active);
                clip.name = newName;
            }
            notice = "Renamed animation.";
            error.clear();
            editor = RenameEditor{};
        }
    }
    if (DrawPanelButton(font, cancelButton, "Cancel") ||
        (cancelClick && !CheckCollisionPointRec(GetMousePosition(), dialog)))
    {
        editor = RenameEditor{};
    }
}

void ApplyAnimatedMeshFrame(ModelTab& tab)
{
    if (!tab.loaded.hasMesh)
    {
        return;
    }

    if (tab.animation.clipIndex < 0)
    {
        if (tab.appliedClipIndex == -1) return;

        const bool useManualPose = tab.manualSkinnedMeshPose &&
                                   tab.currentVertices.size() == tab.loaded.bindVertices.size() &&
                                   tab.currentNormals.size() == tab.loaded.bindNormals.size();
        if (useManualPose)
        {
            UploadGlobalMeshFrame(tab, tab.currentVertices.data(), tab.currentNormals.data(), tab.currentVertices.size());
        }
        else if (tab.loaded.bindVertices.size() == tab.loaded.bindNormals.size())
        {
            UploadGlobalMeshFrame(tab, tab.loaded.bindVertices.data(), tab.loaded.bindNormals.data(), tab.loaded.bindVertices.size());
        }
        tab.appliedClipIndex = -1;
        tab.appliedMeshFrameIndex = -1;
        tab.appliedNextMeshFrameIndex = -1;
        tab.appliedMeshFrameAlpha = -1.0f;
        return;
    }

    if (tab.animation.clipIndex >= static_cast<int>(tab.loaded.animations.size()))
    {
        return;
    }

    const AnimationClip& clip = tab.loaded.animations[static_cast<size_t>(tab.animation.clipIndex)];
    const MeshFrameSample sample = GetMeshFrameSample(clip, tab.animation.time);
    if (sample.first < 0 || sample.second < 0 ||
        sample.first >= static_cast<int>(clip.meshFrames.size()) ||
        sample.second >= static_cast<int>(clip.meshFrames.size()))
    {
        return;
    }

    const MeshFrame& firstFrame = clip.meshFrames[static_cast<size_t>(sample.first)];
    const MeshFrame& secondFrame = clip.meshFrames[static_cast<size_t>(sample.second)];
    const size_t expectedFloats = tab.loaded.bindVertices.size();

    if (firstFrame.vertices.size() == expectedFloats &&
        firstFrame.normals.size() == expectedFloats &&
        secondFrame.vertices.size() == expectedFloats &&
        secondFrame.normals.size() == expectedFloats)
    {
        const bool sameSample = tab.appliedClipIndex == tab.animation.clipIndex &&
                                tab.appliedMeshFrameIndex == sample.first &&
                                tab.appliedNextMeshFrameIndex == sample.second &&
                                std::fabs(tab.appliedMeshFrameAlpha - sample.alpha) < 0.0001f;
        if (sameSample) return;

        const float alpha = ClampFloat(sample.alpha, 0.0f, 1.0f);
        const float inverseAlpha = 1.0f - alpha;
        const float* vertices = firstFrame.vertices.data();
        const float* normals = firstFrame.normals.data();

        if (sample.first != sample.second && alpha > 0.0001f)
        {
            static std::vector<float> globalBlendedVertices;
            static std::vector<float> globalBlendedNormals;
            globalBlendedVertices.resize(expectedFloats);
            globalBlendedNormals.resize(expectedFloats);
            for (size_t i = 0; i < expectedFloats; i += 3)
            {
                globalBlendedVertices[i] = firstFrame.vertices[i] * inverseAlpha + secondFrame.vertices[i] * alpha;
                globalBlendedVertices[i + 1] = firstFrame.vertices[i + 1] * inverseAlpha + secondFrame.vertices[i + 1] * alpha;
                globalBlendedVertices[i + 2] = firstFrame.vertices[i + 2] * inverseAlpha + secondFrame.vertices[i + 2] * alpha;

                const Vector3 blendedNormal = NormalizeOrFallback(Vector3{
                    firstFrame.normals[i] * inverseAlpha + secondFrame.normals[i] * alpha,
                    firstFrame.normals[i + 1] * inverseAlpha + secondFrame.normals[i + 1] * alpha,
                    firstFrame.normals[i + 2] * inverseAlpha + secondFrame.normals[i + 2] * alpha
                }, Vector3{ firstFrame.normals[i], firstFrame.normals[i + 1], firstFrame.normals[i + 2] });
                globalBlendedNormals[i] = blendedNormal.x;
                globalBlendedNormals[i + 1] = blendedNormal.y;
                globalBlendedNormals[i + 2] = blendedNormal.z;
            }
            vertices = globalBlendedVertices.data();
            normals = globalBlendedNormals.data();
        }

        UploadGlobalMeshFrame(tab, vertices, normals, expectedFloats);
        tab.appliedClipIndex = tab.animation.clipIndex;
        tab.appliedMeshFrameIndex = sample.first;
        tab.appliedNextMeshFrameIndex = sample.second;
        tab.appliedMeshFrameAlpha = sample.alpha;
    }
}

void ApplyAnimatedBoneFrame(ModelTab& tab)
{
    if (tab.animation.clipIndex < 0 ||
        tab.animation.clipIndex >= static_cast<int>(tab.loaded.animations.size()))
    {
        if (tab.appliedBoneClipIndex != -1)
        {
            tab.visibleBones = tab.loaded.bones;
            tab.visibleBonePoses = tab.loaded.bonePoses;
            tab.appliedBoneClipIndex = -1;
            tab.appliedBoneFrameIndex = -1;
            tab.appliedNextBoneFrameIndex = -1;
            tab.appliedBoneFrameAlpha = -1.0f;
        }
        return;
    }

    const AnimationClip& clip = tab.loaded.animations[static_cast<size_t>(tab.animation.clipIndex)];
    const MeshFrameSample sample = GetFrameSample(clip.duration, clip.frames.size(), tab.animation.time);
    if (sample.first < 0 || sample.second < 0 ||
        sample.first >= static_cast<int>(clip.frames.size()) ||
        sample.second >= static_cast<int>(clip.frames.size()))
    {
        return;
    }

    const bool sameSample = tab.appliedBoneClipIndex == tab.animation.clipIndex &&
                            tab.appliedBoneFrameIndex == sample.first &&
                            tab.appliedNextBoneFrameIndex == sample.second &&
                            std::fabs(tab.appliedBoneFrameAlpha - sample.alpha) < 0.0001f;
    if (sameSample) return;

    const std::vector<BoneSegment>& firstBones = clip.frames[static_cast<size_t>(sample.first)].bones;
    const std::vector<BoneSegment>& secondBones = clip.frames[static_cast<size_t>(sample.second)].bones;
    const std::vector<BonePose>& firstPoses = clip.frames[static_cast<size_t>(sample.first)].poses;
    const std::vector<BonePose>& secondPoses = clip.frames[static_cast<size_t>(sample.second)].poses;
    const float alpha = ClampFloat(sample.alpha, 0.0f, 1.0f);
    if (firstBones.size() != secondBones.size())
    {
        tab.visibleBones = firstBones;
    }
    else
    {
        tab.visibleBones.resize(firstBones.size());
        for (size_t i = 0; i < firstBones.size(); ++i)
        {
            const BoneSegment& first = firstBones[i];
            const BoneSegment& second = secondBones[i];
            tab.visibleBones[i] = BoneSegment{
                LerpVector3(first.start, second.start, alpha),
                LerpVector3(first.end, second.end, alpha),
                first.startNode,
                first.endNode
            };
        }
    }

    if (firstPoses.size() != secondPoses.size())
    {
        tab.visibleBonePoses = firstPoses;
    }
    else
    {
        tab.visibleBonePoses.resize(firstPoses.size());
        for (size_t i = 0; i < firstPoses.size(); ++i)
        {
            const BonePose& first = firstPoses[i];
            const BonePose& second = secondPoses[i];
            BonePose pose;
            pose.position = LerpVector3(first.position, second.position, alpha);
            pose.axisX = NormalizeOrFallback(LerpVector3(first.axisX, second.axisX, alpha), first.axisX);
            pose.axisY = NormalizeOrFallback(LerpVector3(first.axisY, second.axisY, alpha), first.axisY);
            pose.axisZ = NormalizeOrFallback(LerpVector3(first.axisZ, second.axisZ, alpha), first.axisZ);
            pose.rotation = LerpVector3(first.rotation, second.rotation, alpha);
            pose.scale = LerpVector3(first.scale, second.scale, alpha);
            pose.node = first.node;
            tab.visibleBonePoses[i] = pose;
        }
    }

    tab.appliedBoneClipIndex = tab.animation.clipIndex;
    tab.appliedBoneFrameIndex = sample.first;
    tab.appliedNextBoneFrameIndex = sample.second;
    tab.appliedBoneFrameAlpha = sample.alpha;
}

void DrawJointCircle(Vector3 position, Vector3 axisA, Vector3 axisB, float radius, Color color)
{
    constexpr int kSegments = 28;
    Vector3 previous = Vector3Add(position, Vector3Scale(axisA, radius));
    for (int i = 1; i <= kSegments; ++i)
    {
        const float angle = static_cast<float>(i) / static_cast<float>(kSegments) * 2.0f * PI;
        const Vector3 offset = Vector3Add(Vector3Scale(axisA, std::cos(angle) * radius),
                                          Vector3Scale(axisB, std::sin(angle) * radius));
        const Vector3 current = Vector3Add(position, offset);
        DrawLine3D(previous, current, color);
        previous = current;
    }
}

void DrawJointSphere(Vector3 position, float radius, Color color)
{
    DrawJointCircle(position, Vector3{ 1.0f, 0.0f, 0.0f }, Vector3{ 0.0f, 1.0f, 0.0f }, radius, color);
    DrawJointCircle(position, Vector3{ 1.0f, 0.0f, 0.0f }, Vector3{ 0.0f, 0.0f, 1.0f }, radius, color);
    DrawJointCircle(position, Vector3{ 0.0f, 1.0f, 0.0f }, Vector3{ 0.0f, 0.0f, 1.0f }, radius, color);
}

void DrawMayaBone(Vector3 start, Vector3 end, float radius, Color color)
{
    const Vector3 axis = Vector3Subtract(end, start);
    const float length = Vector3Length(axis);
    if (length < 0.0001f) return;

    const Vector3 forward = Vector3Scale(axis, 1.0f / length);
    Vector3 side = Vector3CrossProduct(forward, Vector3{ 0.0f, 1.0f, 0.0f });
    if (Vector3Length(side) < 0.0001f)
    {
        side = Vector3CrossProduct(forward, Vector3{ 1.0f, 0.0f, 0.0f });
    }
    side = Vector3Normalize(side);
    const Vector3 up = Vector3Normalize(Vector3CrossProduct(side, forward));
    const Vector3 base = Vector3Add(start, Vector3Scale(forward, std::min(length * 0.28f, radius * 5.0f)));
    const Vector3 points[] = {
        Vector3Add(base, Vector3Scale(side, radius)),
        Vector3Subtract(base, Vector3Scale(side, radius)),
        Vector3Add(base, Vector3Scale(up, radius)),
        Vector3Subtract(base, Vector3Scale(up, radius))
    };

    for (const Vector3& point : points)
    {
        DrawLine3D(start, point, color);
        DrawLine3D(point, end, color);
    }

    DrawLine3D(points[0], points[2], color);
    DrawLine3D(points[2], points[1], color);
    DrawLine3D(points[1], points[3], color);
    DrawLine3D(points[3], points[0], color);
}

bool IsNodeInSelectionList(const std::vector<int>& selectedNodes, int nodeIndex)
{
    return std::find(selectedNodes.begin(), selectedNodes.end(), nodeIndex) != selectedNodes.end();
}

void DrawBones(const std::vector<BoneSegment>& bones, int selectedNode, const std::vector<int>& selectedNodes)
{
    float radius = 0.035f;
    if (!bones.empty())
    {
        float totalLength = 0.0f;
        for (const BoneSegment& bone : bones)
        {
            totalLength += Vector3Length(Vector3Subtract(bone.end, bone.start));
        }
        radius = ClampFloat(totalLength / static_cast<float>(bones.size()) * 0.06f, 0.015f, 0.12f);
    }

    for (const BoneSegment& bone : bones)
    {
        const bool selected = bone.startNode == selectedNode || IsNodeInSelectionList(selectedNodes, bone.startNode);
        DrawMayaBone(bone.start, bone.end, radius, selected ? kSelectionColor : Color{ 100, 185, 255, 255 });
    }

    for (const BoneSegment& bone : bones)
    {
        const bool startSelected = bone.startNode == selectedNode || IsNodeInSelectionList(selectedNodes, bone.startNode);
        const bool endSelected = bone.endNode == selectedNode || IsNodeInSelectionList(selectedNodes, bone.endNode);
        DrawJointSphere(bone.start, startSelected ? radius * 1.15f : radius * 0.85f, startSelected ? kSelectionColor : Color{ 142, 210, 255, 255 });
        DrawJointSphere(bone.end, endSelected ? radius * 1.15f : radius * 0.85f, endSelected ? kSelectionColor : Color{ 142, 210, 255, 255 });
    }
}

void DrawBoneRotations(const std::vector<BonePose>& poses, float sceneDiagonal, int selectedNode)
{
    const float axisLength = ClampFloat(sceneDiagonal * 0.028f, 0.04f, 0.32f);
    for (const BonePose& pose : poses)
    {
        const unsigned char alpha = pose.node == selectedNode ? 255 : 190;
        DrawLine3D(pose.position, Vector3Add(pose.position, Vector3Scale(pose.axisX, axisLength)), Color{ 235, 74, 74, alpha });
        DrawLine3D(pose.position, Vector3Add(pose.position, Vector3Scale(pose.axisY, axisLength)), Color{ 92, 210, 94, alpha });
        DrawLine3D(pose.position, Vector3Add(pose.position, Vector3Scale(pose.axisZ, axisLength)), Color{ 86, 142, 255, alpha });
    }
}

float GetBoundsDiagonal(const BoundingBox& bounds)
{
    return Vector3Length(Vector3Subtract(bounds.max, bounds.min));
}

void DrawAxisLine(Vector3 origin, Vector3 axis, float length, Color color)
{
    const Vector3 end = Vector3Add(origin, Vector3Scale(axis, length));
    DrawLine3D(origin, end, color);
    DrawSphere(end, length * 0.045f, color);
}

void DrawNodeAxes(const SceneNode& node, float length, unsigned char alpha)
{
    DrawAxisLine(node.position, node.axisX, length, Color{ 235, 74, 74, alpha });
    DrawAxisLine(node.position, node.axisY, length, Color{ 92, 210, 94, alpha });
    DrawAxisLine(node.position, node.axisZ, length, Color{ 86, 142, 255, alpha });
}

void DrawEmptyCross(const SceneNode& node, float length, Color color)
{
    DrawLine3D(Vector3Subtract(node.position, Vector3Scale(node.axisX, length)), Vector3Add(node.position, Vector3Scale(node.axisX, length)), color);
    DrawLine3D(Vector3Subtract(node.position, Vector3Scale(node.axisY, length)), Vector3Add(node.position, Vector3Scale(node.axisY, length)), color);
    DrawLine3D(Vector3Subtract(node.position, Vector3Scale(node.axisZ, length)), Vector3Add(node.position, Vector3Scale(node.axisZ, length)), color);
}

void DrawMeshOriginAxis(Vector3 origin, Vector3 axis, float length, Color color)
{
    DrawLine3D(origin, Vector3Add(origin, Vector3Scale(axis, length)), color);
}

void DrawMeshOrigin(const SceneNode& node, float sceneDiagonal)
{
    const float axisLength = ClampFloat(sceneDiagonal * 0.045f, 0.07f, 0.55f);
    const float crossLength = axisLength * 0.32f;
    const float sphereRadius = axisLength * 0.026f;

    DrawEmptyCross(node, crossLength, kSelectionColor);
    DrawSphere(node.position, sphereRadius, kSelectionColor);
    DrawMeshOriginAxis(node.position, node.axisX, axisLength, Color{ 235, 74, 74, 255 });
    DrawMeshOriginAxis(node.position, node.axisY, axisLength, Color{ 92, 210, 94, 255 });
    DrawMeshOriginAxis(node.position, node.axisZ, axisLength, Color{ 86, 142, 255, 255 });
}

void DrawEmptyCrosses(const ModelTab& tab)
{
    const float length = ClampFloat(GetBoundsDiagonal(tab.loaded.bounds) * 0.035f, 0.06f, 0.6f);
    for (int i = 0; i < static_cast<int>(tab.loaded.nodes.size()); ++i)
    {
        const SceneNode& node = tab.loaded.nodes[static_cast<size_t>(i)];
        if (node.type != SceneNodeType::Empty || node.parent < 0) continue;
        if (!IsViewportNodeVisible(tab, i)) continue;
        DrawEmptyCross(node, length, i == tab.selectedNode ? kSelectionColor : Color{ 100, 185, 255, 230 });
    }
}

const float* GetCurrentMeshVertices(const ModelTab& tab)
{
    if (!tab.loaded.hasMesh) return nullptr;
    if (tab.currentVertices.size() == tab.loaded.bindVertices.size()) return tab.currentVertices.data();
    return tab.loaded.bindVertices.empty() ? nullptr : tab.loaded.bindVertices.data();
}

const float* GetCurrentMeshNormals(const ModelTab& tab)
{
    if (!tab.loaded.hasMesh) return nullptr;
    if (tab.currentNormals.size() == tab.loaded.bindNormals.size()) return tab.currentNormals.data();
    return tab.loaded.bindNormals.empty() ? nullptr : tab.loaded.bindNormals.data();
}

float GetBoneInfluenceWeight(const SkinnedVertex& vertex, const std::string& boneName)
{
    float weight = 0.0f;
    for (const SkinnedVertexInfluence& influence : vertex.influences)
    {
        if (influence.boneName == boneName)
        {
            weight += influence.weight;
        }
    }
    return weight;
}

Vector3 TransformPosePointInverse(const BonePose& pose, Vector3 point)
{
    const Vector3 delta = Vector3Subtract(point, pose.position);
    return Vector3{
        Vector3DotProduct(delta, pose.axisX) / std::max(0.000001f, std::fabs(pose.scale.x)),
        Vector3DotProduct(delta, pose.axisY) / std::max(0.000001f, std::fabs(pose.scale.y)),
        Vector3DotProduct(delta, pose.axisZ) / std::max(0.000001f, std::fabs(pose.scale.z))
    };
}

Vector3 TransformPoseVectorInverse(const BonePose& pose, Vector3 vector)
{
    return NormalizeOrFallback(Vector3{
        Vector3DotProduct(vector, pose.axisX) / std::max(0.000001f, std::fabs(pose.scale.x)),
        Vector3DotProduct(vector, pose.axisY) / std::max(0.000001f, std::fabs(pose.scale.y)),
        Vector3DotProduct(vector, pose.axisZ) / std::max(0.000001f, std::fabs(pose.scale.z))
    }, vector);
}

SkinnedVertexInfluence* FindVertexInfluence(SkinnedVertex& vertex, const std::string& boneName)
{
    for (SkinnedVertexInfluence& influence : vertex.influences)
    {
        if (influence.boneName == boneName)
        {
            return &influence;
        }
    }
    return nullptr;
}

float GetVertexWeightSum(const SkinnedVertex& vertex)
{
    float sum = 0.0f;
    for (const SkinnedVertexInfluence& influence : vertex.influences)
    {
        sum += influence.weight;
    }
    return sum;
}

void NormalizeVertexWeights(SkinnedVertex& vertex)
{
    float sum = GetVertexWeightSum(vertex);
    if (sum <= 0.000001f) return;
    for (SkinnedVertexInfluence& influence : vertex.influences)
    {
        influence.weight = ClampFloat(influence.weight / sum, 0.0f, 1.0f);
    }
}

bool AddBoneWeightToVertex(SkinnedVertex& vertex,
                           const std::string& boneName,
                           const BonePose& bonePose,
                           float amount,
                           bool autoNormalize)
{
    amount = ClampFloat(amount, 0.0f, 1.0f);
    if (amount <= 0.000001f) return false;

    SkinnedVertexInfluence* target = FindVertexInfluence(vertex, boneName);
    if (!target)
    {
        vertex.influences.push_back(SkinnedVertexInfluence{
            boneName,
            0.0f,
            TransformPosePointInverse(bonePose, vertex.bindPosition),
            TransformPoseVectorInverse(bonePose, vertex.bindNormal)
        });
        target = &vertex.influences.back();
    }

    const float oldWeight = target->weight;
    const float newWeight = ClampFloat(oldWeight + amount, 0.0f, 1.0f);
    const float gainedWeight = newWeight - oldWeight;
    if (gainedWeight <= 0.000001f) return false;

    if (autoNormalize)
    {
        float otherWeight = 0.0f;
        for (const SkinnedVertexInfluence& influence : vertex.influences)
        {
            if (&influence != target)
            {
                otherWeight += influence.weight;
            }
        }

        if (otherWeight > 0.000001f)
        {
            const float targetOtherWeight = std::max(0.0f, 1.0f - newWeight);
            const float scale = targetOtherWeight / otherWeight;
            for (SkinnedVertexInfluence& influence : vertex.influences)
            {
                if (&influence != target)
                {
                    influence.weight = ClampFloat(influence.weight * scale, 0.0f, 1.0f);
                }
            }
        }
    }

    target->weight = newWeight;
    if (autoNormalize)
    {
        NormalizeVertexWeights(vertex);
    }
    return true;
}

Color LerpColor(Color a, Color b, float t)
{
    t = ClampFloat(t, 0.0f, 1.0f);
    return Color{
        static_cast<unsigned char>(static_cast<float>(a.r) + (static_cast<float>(b.r) - static_cast<float>(a.r)) * t),
        static_cast<unsigned char>(static_cast<float>(a.g) + (static_cast<float>(b.g) - static_cast<float>(a.g)) * t),
        static_cast<unsigned char>(static_cast<float>(a.b) + (static_cast<float>(b.b) - static_cast<float>(a.b)) * t),
        static_cast<unsigned char>(static_cast<float>(a.a) + (static_cast<float>(b.a) - static_cast<float>(a.a)) * t)
    };
}

Color GetSkinWeightHeatColor(float weight)
{
    weight = ClampFloat(weight, 0.0f, 1.0f);
    if (weight < 0.25f) return LerpColor(Color{ 18, 34, 92, 120 }, Color{ 28, 170, 215, 180 }, weight / 0.25f);
    if (weight < 0.50f) return LerpColor(Color{ 28, 170, 215, 180 }, Color{ 54, 205, 92, 205 }, (weight - 0.25f) / 0.25f);
    if (weight < 0.75f) return LerpColor(Color{ 54, 205, 92, 205 }, Color{ 245, 220, 76, 230 }, (weight - 0.50f) / 0.25f);
    return LerpColor(Color{ 245, 220, 76, 230 }, Color{ 238, 54, 46, 245 }, (weight - 0.75f) / 0.25f);
}

bool GetSelectedBoneName(const ModelTab& tab, std::string& boneName)
{
    if (tab.selectedNode < 0 || tab.selectedNode >= static_cast<int>(tab.loaded.nodes.size())) return false;
    const SceneNode& node = tab.loaded.nodes[static_cast<size_t>(tab.selectedNode)];
    if (node.type != SceneNodeType::Bone || IsDeletedNode(tab, tab.selectedNode)) return false;
    boneName = node.name;
    return !boneName.empty();
}

void DrawSkinWeightHeatMap(const ModelTab& tab)
{
    std::string boneName;
    if (!GetSelectedBoneName(tab, boneName)) return;
    if (tab.loaded.skinnedVertices.empty()) return;

    const float* vertices = GetCurrentMeshVertices(tab);
    const float* normals = GetCurrentMeshNormals(tab);
    if (!vertices) return;

    const float offset = ClampFloat(GetBoundsDiagonal(tab.loaded.bounds) * 0.0008f, 0.0002f, 0.01f);
    rlDisableBackfaceCulling();
    rlDisableDepthMask();
    BeginBlendMode(BLEND_ALPHA);
    rlBegin(RL_TRIANGLES);
    for (int nodeIndex = 0; nodeIndex < static_cast<int>(tab.loaded.nodes.size()); ++nodeIndex)
    {
        const SceneNode& node = tab.loaded.nodes[static_cast<size_t>(nodeIndex)];
        if (node.type != SceneNodeType::Mesh || node.meshVertexStart < 0 || node.meshVertexCount < 3) continue;
        if (!IsViewportNodeVisible(tab, nodeIndex)) continue;

        const int start = std::max(0, node.meshVertexStart);
        const int end = std::min(node.meshVertexStart + node.meshVertexCount, static_cast<int>(tab.loaded.skinnedVertices.size()));
        for (int vertex = start; vertex + 2 < end; vertex += 3)
        {
            const int vertexIndices[3] = { vertex, vertex + 1, vertex + 2 };
            Vector3 points[3]{};
            Vector3 fallbackNormal{};
            for (int corner = 0; corner < 3; ++corner)
            {
                const int base = vertexIndices[corner] * 3;
                points[corner] = Vector3{ vertices[base], vertices[base + 1], vertices[base + 2] };
            }
            fallbackNormal = NormalizeOrFallback(Vector3CrossProduct(Vector3Subtract(points[1], points[0]), Vector3Subtract(points[2], points[0])), Vector3{ 0.0f, 1.0f, 0.0f });

            for (int corner = 0; corner < 3; ++corner)
            {
                const int globalVertex = vertexIndices[corner];
                const int base = globalVertex * 3;
                Vector3 normal = fallbackNormal;
                if (normals && base + 2 < static_cast<int>(tab.loaded.bindNormals.size()))
                {
                    normal = NormalizeOrFallback(Vector3{ normals[base], normals[base + 1], normals[base + 2] }, fallbackNormal);
                }
                const float weight = GetBoneInfluenceWeight(tab.loaded.skinnedVertices[static_cast<size_t>(globalVertex)], boneName);
                const Color color = GetSkinWeightHeatColor(weight);
                const Vector3 point = Vector3Add(points[corner], Vector3Scale(normal, offset));
                rlColor4ub(color.r, color.g, color.b, color.a);
                rlVertex3f(point.x, point.y, point.z);
            }
        }
    }
    rlEnd();
    EndBlendMode();
    rlEnableDepthMask();
}

Vector3 GetTransformAxisVector(TransformAxis axis)
{
    switch (axis)
    {
    case TransformAxis::X: return Vector3{ 1.0f, 0.0f, 0.0f };
    case TransformAxis::Y: return Vector3{ 0.0f, 1.0f, 0.0f };
    case TransformAxis::Z: return Vector3{ 0.0f, 0.0f, 1.0f };
    case TransformAxis::Center:
    case TransformAxis::None: break;
    }
    return Vector3Zero();
}

Vector3 GetCameraForward(const Camera3D& camera)
{
    return NormalizeOrFallback(Vector3Subtract(camera.target, camera.position), Vector3{ 0.0f, 0.0f, -1.0f });
}

Vector3 GetCameraRight(const Camera3D& camera)
{
    const Vector3 forward = GetCameraForward(camera);
    return NormalizeOrFallback(Vector3CrossProduct(forward, camera.up), Vector3{ 1.0f, 0.0f, 0.0f });
}

Vector3 GetCameraUpVector(const Camera3D& camera)
{
    const Vector3 forward = GetCameraForward(camera);
    const Vector3 right = GetCameraRight(camera);
    return NormalizeOrFallback(Vector3CrossProduct(right, forward), Vector3{ 0.0f, 1.0f, 0.0f });
}

bool GetSelectedNodeLocalAxis(const ModelTab& tab, TransformAxis axis, Vector3& outAxis)
{
    if (tab.selectedNode < 0 || tab.selectedNode >= static_cast<int>(tab.loaded.nodes.size())) return false;
    const SceneNode& node = tab.loaded.nodes[static_cast<size_t>(tab.selectedNode)];
    if (axis == TransformAxis::X)
    {
        outAxis = NormalizeOrFallback(node.axisX, Vector3{ 1.0f, 0.0f, 0.0f });
        return true;
    }
    if (axis == TransformAxis::Y)
    {
        outAxis = NormalizeOrFallback(node.axisY, Vector3{ 0.0f, 1.0f, 0.0f });
        return true;
    }
    if (axis == TransformAxis::Z)
    {
        outAxis = NormalizeOrFallback(node.axisZ, Vector3{ 0.0f, 0.0f, 1.0f });
        return true;
    }
    return false;
}

Vector3 GetTransformAxisVector(const ModelTab& tab, TransformAxis axis, GizmoOrientation orientation)
{
    if (orientation == GizmoOrientation::Local)
    {
        Vector3 localAxis{};
        if (GetSelectedNodeLocalAxis(tab, axis, localAxis)) return localAxis;
    }
    return GetTransformAxisVector(axis);
}

bool GetTransformPlaneBasis(const ModelTab& tab,
                            TransformAxis axis,
                            GizmoOrientation orientation,
                            Vector3& axisA,
                            Vector3& axisB)
{
    if (orientation == GizmoOrientation::Local && tab.selectedNode >= 0 && tab.selectedNode < static_cast<int>(tab.loaded.nodes.size()))
    {
        const SceneNode& node = tab.loaded.nodes[static_cast<size_t>(tab.selectedNode)];
        if (axis == TransformAxis::X)
        {
            axisA = NormalizeOrFallback(node.axisY, Vector3{ 0.0f, 1.0f, 0.0f });
            axisB = NormalizeOrFallback(node.axisZ, Vector3{ 0.0f, 0.0f, 1.0f });
            return true;
        }
        if (axis == TransformAxis::Y)
        {
            axisA = NormalizeOrFallback(node.axisX, Vector3{ 1.0f, 0.0f, 0.0f });
            axisB = NormalizeOrFallback(node.axisZ, Vector3{ 0.0f, 0.0f, 1.0f });
            return true;
        }
        if (axis == TransformAxis::Z)
        {
            axisA = NormalizeOrFallback(node.axisX, Vector3{ 1.0f, 0.0f, 0.0f });
            axisB = NormalizeOrFallback(node.axisY, Vector3{ 0.0f, 1.0f, 0.0f });
            return true;
        }
    }

    axisA = Vector3{ 1.0f, 0.0f, 0.0f };
    axisB = Vector3{ 0.0f, 1.0f, 0.0f };
    if (axis == TransformAxis::X)
    {
        axisA = Vector3{ 0.0f, 1.0f, 0.0f };
        axisB = Vector3{ 0.0f, 0.0f, 1.0f };
        return true;
    }
    if (axis == TransformAxis::Y)
    {
        axisA = Vector3{ 1.0f, 0.0f, 0.0f };
        axisB = Vector3{ 0.0f, 0.0f, 1.0f };
        return true;
    }
    if (axis == TransformAxis::Z)
    {
        axisA = Vector3{ 1.0f, 0.0f, 0.0f };
        axisB = Vector3{ 0.0f, 1.0f, 0.0f };
        return true;
    }
    return false;
}

Color GetTransformAxisColor(TransformAxis axis, bool active = false)
{
    const unsigned char alpha = active ? 255 : 220;
    switch (axis)
    {
    case TransformAxis::X: return Color{ 235, 74, 74, alpha };
    case TransformAxis::Y: return Color{ 92, 210, 94, alpha };
    case TransformAxis::Z: return Color{ 86, 142, 255, alpha };
    case TransformAxis::Center:
    case TransformAxis::None: break;
    }
    return Color{ 210, 218, 226, alpha };
}

bool IsNodeInTransformScope(const LoadedFbxModel& loaded, int nodeIndex, int rootNode)
{
    if (nodeIndex < 0 || rootNode < 0) return false;
    return nodeIndex == rootNode || IsDescendantNode(loaded, nodeIndex, rootNode);
}

Vector3 RotatePointAroundAxis(Vector3 point, Vector3 pivot, Vector3 axis, float radians)
{
    return Vector3Add(pivot, Vector3RotateByAxisAngle(Vector3Subtract(point, pivot), axis, radians));
}

Vector3 ScalePointAlongAxis(Vector3 point, Vector3 pivot, Vector3 axis, float factor)
{
    const Vector3 offset = Vector3Subtract(point, pivot);
    const float along = Vector3DotProduct(offset, axis);
    const Vector3 parallel = Vector3Scale(axis, along);
    const Vector3 perpendicular = Vector3Subtract(offset, parallel);
    return Vector3Add(pivot, Vector3Add(perpendicular, Vector3Scale(parallel, factor)));
}

Vector3 ScalePointUniform(Vector3 point, Vector3 pivot, float factor)
{
    return Vector3Add(pivot, Vector3Scale(Vector3Subtract(point, pivot), factor));
}

Vector3 ScaleNormalAlongAxis(Vector3 normal, Vector3 axis, float factor)
{
    const float safeFactor = std::max(0.001f, std::fabs(factor));
    const float along = Vector3DotProduct(normal, axis);
    const Vector3 parallel = Vector3Scale(axis, along / safeFactor);
    const Vector3 perpendicular = Vector3Subtract(normal, Vector3Scale(axis, along));
    return NormalizeOrFallback(Vector3Add(perpendicular, parallel), normal);
}

Vector3 EulerDegreesFromAxes(Vector3 axisX, Vector3 axisY, Vector3 axisZ)
{
    Matrix rotationMatrix = MatrixIdentity();
    axisX = NormalizeOrFallback(axisX, Vector3{ 1.0f, 0.0f, 0.0f });
    axisY = NormalizeOrFallback(axisY, Vector3{ 0.0f, 1.0f, 0.0f });
    axisZ = NormalizeOrFallback(axisZ, Vector3{ 0.0f, 0.0f, 1.0f });
    rotationMatrix.m0 = axisX.x;
    rotationMatrix.m1 = axisX.y;
    rotationMatrix.m2 = axisX.z;
    rotationMatrix.m4 = axisY.x;
    rotationMatrix.m5 = axisY.y;
    rotationMatrix.m6 = axisY.z;
    rotationMatrix.m8 = axisZ.x;
    rotationMatrix.m9 = axisZ.y;
    rotationMatrix.m10 = axisZ.z;
    return Vector3Scale(QuaternionToEuler(QuaternionFromMatrix(rotationMatrix)), RAD2DEG);
}

void ApplyTransformValueUpdates(SceneNode& node, TransformTool tool, TransformAxis axis, float amount)
{
    if (tool == TransformTool::Rotate)
    {
        node.rotation = EulerDegreesFromAxes(node.axisX, node.axisY, node.axisZ);
    }
    else if (tool == TransformTool::Scale)
    {
        if (axis == TransformAxis::X) node.scale.x *= amount;
        else if (axis == TransformAxis::Y) node.scale.y *= amount;
        else if (axis == TransformAxis::Z) node.scale.z *= amount;
        else if (axis == TransformAxis::Center)
        {
            node.scale.x *= amount;
            node.scale.y *= amount;
            node.scale.z *= amount;
        }
    }
}

void ApplyTransformValueUpdates(BonePose& pose, TransformTool tool, TransformAxis axis, float amount)
{
    if (tool == TransformTool::Rotate)
    {
        pose.rotation = EulerDegreesFromAxes(pose.axisX, pose.axisY, pose.axisZ);
    }
    else if (tool == TransformTool::Scale)
    {
        if (axis == TransformAxis::X) pose.scale.x *= amount;
        else if (axis == TransformAxis::Y) pose.scale.y *= amount;
        else if (axis == TransformAxis::Z) pose.scale.z *= amount;
        else if (axis == TransformAxis::Center)
        {
            pose.scale.x *= amount;
            pose.scale.y *= amount;
            pose.scale.z *= amount;
        }
    }
}

void RecomputeMeshNodeBounds(ModelTab& tab, SceneNode& node)
{
    if (node.type != SceneNodeType::Mesh || node.meshVertexStart < 0 || node.meshVertexCount <= 0 ||
        tab.loaded.bindVertices.empty())
    {
        return;
    }

    const std::vector<float>& vertices = tab.currentVertices.size() == tab.loaded.bindVertices.size() ? tab.currentVertices : tab.loaded.bindVertices;
    const int start = std::max(0, node.meshVertexStart);
    const int end = std::min(node.meshVertexStart + node.meshVertexCount, static_cast<int>(vertices.size() / 3));
    if (start >= end) return;

    const int firstBase = start * 3;
    BoundingBox bounds{
        Vector3{ vertices[static_cast<size_t>(firstBase)], vertices[static_cast<size_t>(firstBase + 1)], vertices[static_cast<size_t>(firstBase + 2)] },
        Vector3{ vertices[static_cast<size_t>(firstBase)], vertices[static_cast<size_t>(firstBase + 1)], vertices[static_cast<size_t>(firstBase + 2)] }
    };
    for (int vertex = start + 1; vertex < end; ++vertex)
    {
        const int base = vertex * 3;
        const Vector3 point{ vertices[static_cast<size_t>(base)], vertices[static_cast<size_t>(base + 1)], vertices[static_cast<size_t>(base + 2)] };
        bounds.min.x = std::min(bounds.min.x, point.x);
        bounds.min.y = std::min(bounds.min.y, point.y);
        bounds.min.z = std::min(bounds.min.z, point.z);
        bounds.max.x = std::max(bounds.max.x, point.x);
        bounds.max.y = std::max(bounds.max.y, point.y);
        bounds.max.z = std::max(bounds.max.z, point.z);
    }

    node.bounds = bounds;
    node.hasBounds = true;
}

void RecomputeSceneBounds(ModelTab& tab)
{
    bool found = false;
    BoundingBox bounds{};
    for (const SceneNode& node : tab.loaded.nodes)
    {
        if (!node.hasBounds) continue;
        if (!found)
        {
            bounds = node.bounds;
            found = true;
            continue;
        }
        bounds.min.x = std::min(bounds.min.x, node.bounds.min.x);
        bounds.min.y = std::min(bounds.min.y, node.bounds.min.y);
        bounds.min.z = std::min(bounds.min.z, node.bounds.min.z);
        bounds.max.x = std::max(bounds.max.x, node.bounds.max.x);
        bounds.max.y = std::max(bounds.max.y, node.bounds.max.y);
        bounds.max.z = std::max(bounds.max.z, node.bounds.max.z);
    }
    if (found)
    {
        tab.loaded.bounds = bounds;
    }
}

template <typename PointTransform>
void TransformPositionBuffer(std::vector<float>& values, int startVertex, int vertexCount, PointTransform transform)
{
    if (values.empty() || startVertex < 0 || vertexCount <= 0) return;
    const int start = std::max(0, startVertex);
    const int end = std::min(startVertex + vertexCount, static_cast<int>(values.size() / 3));
    for (int vertex = start; vertex < end; ++vertex)
    {
        const int base = vertex * 3;
        const Vector3 value{ values[static_cast<size_t>(base)], values[static_cast<size_t>(base + 1)], values[static_cast<size_t>(base + 2)] };
        const Vector3 transformed = transform(value);
        values[static_cast<size_t>(base)] = transformed.x;
        values[static_cast<size_t>(base + 1)] = transformed.y;
        values[static_cast<size_t>(base + 2)] = transformed.z;
    }
}

template <typename DirectionTransform>
void TransformDirectionBuffer(std::vector<float>& values, int startVertex, int vertexCount, DirectionTransform transform)
{
    if (values.empty() || startVertex < 0 || vertexCount <= 0) return;
    const int start = std::max(0, startVertex);
    const int end = std::min(startVertex + vertexCount, static_cast<int>(values.size() / 3));
    for (int vertex = start; vertex < end; ++vertex)
    {
        const int base = vertex * 3;
        const Vector3 value{ values[static_cast<size_t>(base)], values[static_cast<size_t>(base + 1)], values[static_cast<size_t>(base + 2)] };
        const Vector3 transformed = transform(value);
        values[static_cast<size_t>(base)] = transformed.x;
        values[static_cast<size_t>(base + 1)] = transformed.y;
        values[static_cast<size_t>(base + 2)] = transformed.z;
    }
}

template <typename PointTransform, typename DirectionTransform>
void TransformMeshNodeRange(ModelTab& tab, SceneNode& node, PointTransform transformPoint, DirectionTransform transformDirection)
{
    TransformPositionBuffer(tab.loaded.bindVertices, node.meshVertexStart, node.meshVertexCount, transformPoint);
    TransformDirectionBuffer(tab.loaded.bindNormals, node.meshVertexStart, node.meshVertexCount, transformDirection);
    TransformPositionBuffer(tab.currentVertices, node.meshVertexStart, node.meshVertexCount, transformPoint);
    TransformDirectionBuffer(tab.currentNormals, node.meshVertexStart, node.meshVertexCount, transformDirection);

    const int start = std::max(0, node.meshVertexStart);
    const int end = std::min(node.meshVertexStart + node.meshVertexCount, static_cast<int>(tab.loaded.skinnedVertices.size()));
    for (int vertex = start; vertex < end; ++vertex)
    {
        SkinnedVertex& skinned = tab.loaded.skinnedVertices[static_cast<size_t>(vertex)];
        skinned.bindPosition = transformPoint(skinned.bindPosition);
        skinned.bindNormal = transformDirection(skinned.bindNormal);
    }

    for (AnimationClip& clip : tab.loaded.animations)
    {
        for (MeshFrame& frame : clip.meshFrames)
        {
            TransformPositionBuffer(frame.vertices, node.meshVertexStart, node.meshVertexCount, transformPoint);
            TransformDirectionBuffer(frame.normals, node.meshVertexStart, node.meshVertexCount, transformDirection);
        }
    }

    RecomputeMeshNodeBounds(tab, node);
}

template <typename PointTransform, typename DirectionTransform>
void TransformBoneData(ModelTab& tab,
                       int rootNode,
                       PointTransform transformPoint,
                       DirectionTransform transformDirection,
                       TransformTool tool,
                       TransformAxis axis,
                       float amount)
{
    auto transformBoneSegment = [&](BoneSegment& bone)
    {
        if (IsNodeInTransformScope(tab.loaded, bone.startNode, rootNode))
        {
            bone.start = transformPoint(bone.start);
        }
        if (IsNodeInTransformScope(tab.loaded, bone.endNode, rootNode))
        {
            bone.end = transformPoint(bone.end);
        }
    };
    auto transformBonePose = [&](BonePose& pose)
    {
        if (!IsNodeInTransformScope(tab.loaded, pose.node, rootNode)) return;
        pose.position = transformPoint(pose.position);
        pose.axisX = NormalizeOrFallback(transformDirection(pose.axisX), pose.axisX);
        pose.axisY = NormalizeOrFallback(transformDirection(pose.axisY), pose.axisY);
        pose.axisZ = NormalizeOrFallback(transformDirection(pose.axisZ), pose.axisZ);
        ApplyTransformValueUpdates(pose, tool, axis, amount);
    };

    for (BoneSegment& bone : tab.loaded.bones) transformBoneSegment(bone);
    for (BoneSegment& bone : tab.visibleBones) transformBoneSegment(bone);
    for (BonePose& pose : tab.loaded.bonePoses) transformBonePose(pose);
    for (BonePose& pose : tab.visibleBonePoses) transformBonePose(pose);
    for (AnimationClip& clip : tab.loaded.animations)
    {
        for (BoneFrame& frame : clip.frames)
        {
            for (BoneSegment& bone : frame.bones) transformBoneSegment(bone);
            for (BonePose& pose : frame.poses) transformBonePose(pose);
        }
    }
}

void InvalidateDisplayedAnimationCaches(ModelTab& tab)
{
    tab.appliedClipIndex = -2;
    tab.appliedMeshFrameIndex = -1;
    tab.appliedNextMeshFrameIndex = -1;
    tab.appliedMeshFrameAlpha = -1.0f;
    tab.appliedBoneClipIndex = -2;
    tab.appliedBoneFrameIndex = -1;
    tab.appliedNextBoneFrameIndex = -1;
    tab.appliedBoneFrameAlpha = -1.0f;
}

bool HasCpuSkinnedMesh(const LoadedFbxModel& loaded)
{
    return loaded.hasMesh &&
           !loaded.skinnedVertices.empty() &&
           loaded.skinnedVertices.size() == loaded.bindVertices.size() / 3 &&
           loaded.bindVertices.size() == loaded.bindNormals.size();
}

BoneFrame BuildBindBoneFrame(const LoadedFbxModel& loaded)
{
    BoneFrame frame;
    frame.bones = loaded.bones;
    frame.poses = loaded.bonePoses;
    return frame;
}

void RecomputeAllMeshNodeBounds(ModelTab& tab)
{
    for (SceneNode& node : tab.loaded.nodes)
    {
        if (node.type == SceneNodeType::Mesh)
        {
            RecomputeMeshNodeBounds(tab, node);
        }
    }
    RecomputeSceneBounds(tab);
}

bool RebuildCurrentSkinnedMeshFromBones(ModelTab& tab)
{
    if (!HasCpuSkinnedMesh(tab.loaded) || tab.loaded.bonePoses.empty()) return false;

    const MeshFrame meshFrame = BuildSkinnedMeshFrame(tab.loaded, BuildBindBoneFrame(tab.loaded));
    if (meshFrame.vertices.size() != tab.loaded.bindVertices.size() ||
        meshFrame.normals.size() != tab.loaded.bindNormals.size())
    {
        return false;
    }

    tab.currentVertices = meshFrame.vertices;
    tab.currentNormals = meshFrame.normals;
    tab.manualSkinnedMeshPose = true;
    RecomputeAllMeshNodeBounds(tab);
    InvalidateDisplayedAnimationCaches(tab);
    RefreshDisplayedMesh(tab);
    return true;
}

bool RebuildSkinnedAnimationMeshFrames(ModelTab& tab)
{
    if (!HasCpuSkinnedMesh(tab.loaded)) return false;

    bool rebuilt = false;
    for (AnimationClip& clip : tab.loaded.animations)
    {
        if (clip.frames.empty()) continue;
        clip.meshFrames.clear();
        clip.meshFrames.reserve(clip.frames.size());
        for (const BoneFrame& frame : clip.frames)
        {
            clip.meshFrames.push_back(BuildSkinnedMeshFrame(tab.loaded, frame));
        }
        rebuilt = true;
    }
    if (rebuilt)
    {
        InvalidateDisplayedAnimationCaches(tab);
    }
    return rebuilt;
}

bool IsScaleApproximatelyApplied(Vector3 scale)
{
    constexpr float epsilon = 0.000001f;
    return std::fabs(scale.x - 1.0f) <= epsilon &&
           std::fabs(scale.y - 1.0f) <= epsilon &&
           std::fabs(scale.z - 1.0f) <= epsilon;
}

bool SetScaleToApplied(Vector3& scale)
{
    if (IsScaleApproximatelyApplied(scale)) return false;
    scale = Vector3{ 1.0f, 1.0f, 1.0f };
    return true;
}

Vector3 MultiplyComponents(Vector3 a, Vector3 b)
{
    return Vector3{ a.x * b.x, a.y * b.y, a.z * b.z };
}

void BakeBoneScaleIntoSkinBindData(ModelTab& tab, const SceneNode& node, Vector3 scale)
{
    if (!HasCpuSkinnedMesh(tab.loaded) || IsScaleApproximatelyApplied(scale)) return;

    for (SkinnedVertex& vertex : tab.loaded.skinnedVertices)
    {
        for (SkinnedVertexInfluence& influence : vertex.influences)
        {
            const bool nameMatches = influence.boneName == node.name ||
                                     (!node.sourceName.empty() && influence.boneName == node.sourceName);
            if (!nameMatches) continue;

            influence.bindPositionInBone = MultiplyComponents(influence.bindPositionInBone, scale);
            influence.bindNormalInBone = NormalizeOrFallback(MultiplyComponents(influence.bindNormalInBone, scale), influence.bindNormalInBone);
        }
    }
}

bool ApplyScaleToNode(ModelTab& tab, int nodeIndex)
{
    if (nodeIndex < 0 ||
        nodeIndex >= static_cast<int>(tab.loaded.nodes.size()) ||
        IsDeletedNode(tab, nodeIndex))
    {
        return false;
    }

    SceneNode& node = tab.loaded.nodes[static_cast<size_t>(nodeIndex)];
    bool changed = false;
    Vector3 boneScaleToBake = node.scale;

    if (node.type == SceneNodeType::Bone)
    {
        for (const BonePose& pose : tab.loaded.bonePoses)
        {
            if (pose.node == nodeIndex && !IsScaleApproximatelyApplied(pose.scale))
            {
                boneScaleToBake = pose.scale;
                break;
            }
        }
        BakeBoneScaleIntoSkinBindData(tab, node, boneScaleToBake);
    }

    changed = SetScaleToApplied(node.scale) || changed;

    auto applyPoseScale = [&](BonePose& pose)
    {
        if (pose.node == nodeIndex)
        {
            changed = SetScaleToApplied(pose.scale) || changed;
        }
    };

    if (node.type == SceneNodeType::Bone)
    {
        for (BonePose& pose : tab.loaded.bonePoses) applyPoseScale(pose);
        for (BonePose& pose : tab.visibleBonePoses) applyPoseScale(pose);
        for (AnimationClip& clip : tab.loaded.animations)
        {
            for (BoneFrame& frame : clip.frames)
            {
                for (BonePose& pose : frame.poses) applyPoseScale(pose);
            }
        }
    }

    if (!changed) return false;

    if (node.type == SceneNodeType::Bone)
    {
        RebuildSkinnedAnimationMeshFrames(tab);
        if (tab.animation.clipIndex < 0 && RebuildCurrentSkinnedMeshFromBones(tab))
        {
            return true;
        }
        InvalidateDisplayedAnimationCaches(tab);
    }

    RecomputeSceneBounds(tab);
    RefreshDisplayedMesh(tab);
    return true;
}

const BonePose* FindBonePoseByNode(const std::vector<BonePose>& poses, int nodeIndex)
{
    for (const BonePose& pose : poses)
    {
        if (pose.node == nodeIndex) return &pose;
    }
    return nullptr;
}

bool AssignBonePose(BonePose& target, const BonePose& source)
{
    const bool changed = Vector3Distance(target.position, source.position) > 0.000001f ||
                         Vector3Distance(target.axisX, source.axisX) > 0.000001f ||
                         Vector3Distance(target.axisY, source.axisY) > 0.000001f ||
                         Vector3Distance(target.axisZ, source.axisZ) > 0.000001f ||
                         Vector3Distance(target.rotation, source.rotation) > 0.000001f ||
                         Vector3Distance(target.scale, source.scale) > 0.000001f;
    if (changed)
    {
        const int node = target.node;
        target = source;
        target.node = node;
    }
    return changed;
}

bool AssignSceneNodePose(SceneNode& node, const BonePose& pose)
{
    const bool changed = Vector3Distance(node.position, pose.position) > 0.000001f ||
                         Vector3Distance(node.axisX, pose.axisX) > 0.000001f ||
                         Vector3Distance(node.axisY, pose.axisY) > 0.000001f ||
                         Vector3Distance(node.axisZ, pose.axisZ) > 0.000001f ||
                         Vector3Distance(node.rotation, pose.rotation) > 0.000001f ||
                         Vector3Distance(node.scale, pose.scale) > 0.000001f;
    if (changed)
    {
        node.position = pose.position;
        node.axisX = pose.axisX;
        node.axisY = pose.axisY;
        node.axisZ = pose.axisZ;
        node.rotation = pose.rotation;
        node.scale = pose.scale;
    }
    return changed;
}

void UpdateBoneSegmentsForNode(std::vector<BoneSegment>& bones, int nodeIndex, Vector3 position)
{
    for (BoneSegment& bone : bones)
    {
        if (bone.startNode == nodeIndex)
        {
            bone.start = position;
        }
        if (bone.endNode == nodeIndex)
        {
            bone.end = position;
        }
    }
}

bool ResetSingleBoneToOriginalBindPose(ModelTab& tab, int nodeIndex)
{
    if (nodeIndex < 0 ||
        nodeIndex >= static_cast<int>(tab.loaded.nodes.size()) ||
        IsDeletedNode(tab, nodeIndex) ||
        tab.loaded.nodes[static_cast<size_t>(nodeIndex)].type != SceneNodeType::Bone)
    {
        return false;
    }

    const BonePose* bindPose = FindBonePoseByNode(tab.originalBindBonePoses, nodeIndex);
    if (!bindPose)
    {
        bindPose = FindBonePoseByNode(tab.loaded.bonePoses, nodeIndex);
    }
    if (!bindPose)
    {
        return false;
    }

    bool changed = AssignSceneNodePose(tab.loaded.nodes[static_cast<size_t>(nodeIndex)], *bindPose);
    bool foundLoadedPose = false;
    for (BonePose& pose : tab.loaded.bonePoses)
    {
        if (pose.node != nodeIndex) continue;
        changed = AssignBonePose(pose, *bindPose) || changed;
        foundLoadedPose = true;
    }
    if (!foundLoadedPose)
    {
        tab.loaded.bonePoses.push_back(*bindPose);
        changed = true;
    }

    for (BonePose& pose : tab.visibleBonePoses)
    {
        if (pose.node == nodeIndex)
        {
            changed = AssignBonePose(pose, *bindPose) || changed;
        }
    }
    UpdateBoneSegmentsForNode(tab.loaded.bones, nodeIndex, bindPose->position);
    UpdateBoneSegmentsForNode(tab.visibleBones, nodeIndex, bindPose->position);
    return changed;
}

bool ResetBoneSubtreeToOriginalBindPose(ModelTab& tab, int rootNodeIndex)
{
    if (rootNodeIndex < 0 ||
        rootNodeIndex >= static_cast<int>(tab.loaded.nodes.size()) ||
        IsDeletedNode(tab, rootNodeIndex) ||
        tab.loaded.nodes[static_cast<size_t>(rootNodeIndex)].type != SceneNodeType::Bone)
    {
        return false;
    }

    bool changed = false;
    for (int nodeIndex = 0; nodeIndex < static_cast<int>(tab.loaded.nodes.size()); ++nodeIndex)
    {
        if (!IsNodeInTransformScope(tab.loaded, nodeIndex, rootNodeIndex)) continue;
        if (tab.loaded.nodes[static_cast<size_t>(nodeIndex)].type != SceneNodeType::Bone) continue;
        changed = ResetSingleBoneToOriginalBindPose(tab, nodeIndex) || changed;
    }
    if (!changed) return false;

    if (tab.animation.clipIndex < 0)
    {
        tab.visibleBones = tab.loaded.bones;
        tab.visibleBonePoses = tab.loaded.bonePoses;
    }
    InvalidateDisplayedAnimationCaches(tab);
    if (tab.animation.clipIndex < 0 && !RebuildCurrentSkinnedMeshFromBones(tab))
    {
        RecomputeSceneBounds(tab);
        RefreshDisplayedMesh(tab);
    }
    return true;
}

std::vector<int> GetSelectedTransformRoots(const ModelTab& tab)
{
    std::vector<int> roots;
    if (tab.selectedNodes.empty())
    {
        if (IsValidSelectableNode(tab, tab.selectedNode))
        {
            roots.push_back(tab.selectedNode);
        }
        return roots;
    }

    for (int nodeIndex : tab.selectedNodes)
    {
        if (!IsValidSelectableNode(tab, nodeIndex)) continue;

        bool coveredBySelectedAncestor = false;
        for (int otherNode : tab.selectedNodes)
        {
            if (otherNode == nodeIndex || !IsValidSelectableNode(tab, otherNode)) continue;
            if (IsDescendantNode(tab.loaded, nodeIndex, otherNode))
            {
                coveredBySelectedAncestor = true;
                break;
            }
        }
        if (!coveredBySelectedAncestor)
        {
            roots.push_back(nodeIndex);
        }
    }
    return roots;
}

template <typename PointTransform, typename DirectionTransform>
void ApplyTransformToSelectedSubtree(ModelTab& tab,
                                     PointTransform transformPoint,
                                     DirectionTransform transformDirection,
                                     TransformTool tool,
                                     TransformAxis axis,
                                     float amount)
{
    const std::vector<int> rootNodes = GetSelectedTransformRoots(tab);
    if (rootNodes.empty()) return;
    const bool canCpuSkin = HasCpuSkinnedMesh(tab.loaded);
    bool touchedBoneRoot = false;

    for (int rootNode : rootNodes)
    {
        if (!IsValidSelectableNode(tab, rootNode)) continue;
        const bool rootIsBone = tab.loaded.nodes[static_cast<size_t>(rootNode)].type == SceneNodeType::Bone;
        touchedBoneRoot = touchedBoneRoot || rootIsBone;

        for (int nodeIndex = 0; nodeIndex < static_cast<int>(tab.loaded.nodes.size()); ++nodeIndex)
        {
            if (!IsNodeInTransformScope(tab.loaded, nodeIndex, rootNode)) continue;
            SceneNode& node = tab.loaded.nodes[static_cast<size_t>(nodeIndex)];
            node.position = transformPoint(node.position);
            node.axisX = NormalizeOrFallback(transformDirection(node.axisX), node.axisX);
            node.axisY = NormalizeOrFallback(transformDirection(node.axisY), node.axisY);
            node.axisZ = NormalizeOrFallback(transformDirection(node.axisZ), node.axisZ);
            ApplyTransformValueUpdates(node, tool, axis, amount);

            const bool skinnedMeshHandledByBones = rootIsBone && canCpuSkin && node.type == SceneNodeType::Mesh && node.meshHasSkin;
            if (node.type == SceneNodeType::Mesh && !skinnedMeshHandledByBones)
            {
                TransformMeshNodeRange(tab, node, transformPoint, transformDirection);
            }
        }

        TransformBoneData(tab, rootNode, transformPoint, transformDirection, tool, axis, amount);
    }

    if (touchedBoneRoot && canCpuSkin)
    {
        RebuildSkinnedAnimationMeshFrames(tab);
        if (!RebuildCurrentSkinnedMeshFromBones(tab))
        {
            RecomputeSceneBounds(tab);
            RefreshDisplayedMesh(tab);
        }
    }
    else
    {
        RecomputeSceneBounds(tab);
        RefreshDisplayedMesh(tab);
    }
}

void MoveSelectedSubtree(ModelTab& tab, Vector3 delta)
{
    auto transformPoint = [&](Vector3 point) { return Vector3Add(point, delta); };
    auto transformDirection = [](Vector3 direction) { return direction; };
    ApplyTransformToSelectedSubtree(tab, transformPoint, transformDirection, TransformTool::Move, TransformAxis::None, 0.0f);
}

void RotateSelectedSubtree(ModelTab& tab, Vector3 pivot, TransformAxis axis, Vector3 axisVector, float radians)
{
    axisVector = NormalizeOrFallback(axisVector, GetTransformAxisVector(axis));
    auto transformPoint = [&](Vector3 point) { return RotatePointAroundAxis(point, pivot, axisVector, radians); };
    auto transformDirection = [&](Vector3 direction) { return NormalizeOrFallback(Vector3RotateByAxisAngle(direction, axisVector, radians), direction); };
    ApplyTransformToSelectedSubtree(tab, transformPoint, transformDirection, TransformTool::Rotate, axis, radians);
}

void RotateSelectedSubtreeArcball(ModelTab& tab, Vector3 pivot, Vector3 rightAxis, float rightRadians, Vector3 upAxis, float upRadians)
{
    rightAxis = NormalizeOrFallback(rightAxis, Vector3{ 1.0f, 0.0f, 0.0f });
    upAxis = NormalizeOrFallback(upAxis, Vector3{ 0.0f, 1.0f, 0.0f });
    auto rotatePoint = [&](Vector3 point)
    {
        point = RotatePointAroundAxis(point, pivot, rightAxis, rightRadians);
        return RotatePointAroundAxis(point, pivot, upAxis, upRadians);
    };
    auto rotateDirection = [&](Vector3 direction)
    {
        Vector3 rotated = Vector3RotateByAxisAngle(direction, rightAxis, rightRadians);
        rotated = Vector3RotateByAxisAngle(rotated, upAxis, upRadians);
        return NormalizeOrFallback(rotated, direction);
    };
    ApplyTransformToSelectedSubtree(tab, rotatePoint, rotateDirection, TransformTool::Rotate, TransformAxis::Center, 0.0f);
}

void ScaleSelectedSubtree(ModelTab& tab, Vector3 pivot, TransformAxis axis, Vector3 axisVector, float factor)
{
    factor = ClampFloat(factor, 0.05f, 20.0f);
    axisVector = NormalizeOrFallback(axisVector, GetTransformAxisVector(axis));
    auto transformPoint = [&](Vector3 point) { return ScalePointAlongAxis(point, pivot, axisVector, factor); };
    auto transformDirection = [&](Vector3 direction) { return ScaleNormalAlongAxis(direction, axisVector, factor); };
    ApplyTransformToSelectedSubtree(tab, transformPoint, transformDirection, TransformTool::Scale, axis, factor);
}

void ScaleSelectedSubtreeUniform(ModelTab& tab, Vector3 pivot, float factor)
{
    factor = ClampFloat(factor, 0.05f, 20.0f);
    auto transformPoint = [&](Vector3 point) { return ScalePointUniform(point, pivot, factor); };
    auto transformDirection = [](Vector3 direction) { return direction; };
    ApplyTransformToSelectedSubtree(tab, transformPoint, transformDirection, TransformTool::Scale, TransformAxis::Center, factor);
}

void DrawMeshNodeWireframe(const ModelTab& tab, const SceneNode& node, Color color)
{
    const float* vertices = GetCurrentMeshVertices(tab);
    if (!vertices || node.meshVertexStart < 0 || node.meshVertexCount < 3) return;

    const int start = node.meshVertexStart;
    const int end = node.meshVertexStart + node.meshVertexCount;
    for (int vertex = start; vertex + 2 < end; vertex += 3)
    {
        const int i0 = vertex * 3;
        const int i1 = (vertex + 1) * 3;
        const int i2 = (vertex + 2) * 3;
        const Vector3 p0{ vertices[i0], vertices[i0 + 1], vertices[i0 + 2] };
        const Vector3 p1{ vertices[i1], vertices[i1 + 1], vertices[i1 + 2] };
        const Vector3 p2{ vertices[i2], vertices[i2 + 1], vertices[i2 + 2] };
        DrawLine3D(p0, p1, color);
        DrawLine3D(p1, p2, color);
        DrawLine3D(p2, p0, color);
    }
}

bool GetRayCollisionMeshNodeTriangles(const ModelTab& tab, const SceneNode& node, Ray ray, RayCollision& outHit)
{
    const float* vertices = GetCurrentMeshVertices(tab);
    if (!vertices || node.meshVertexStart < 0 || node.meshVertexCount < 3) return false;

    bool hitAny = false;
    RayCollision bestHit{};
    bestHit.distance = std::numeric_limits<float>::max();

    const int start = node.meshVertexStart;
    const int end = node.meshVertexStart + node.meshVertexCount;
    for (int vertex = start; vertex + 2 < end; vertex += 3)
    {
        const int i0 = vertex * 3;
        const int i1 = (vertex + 1) * 3;
        const int i2 = (vertex + 2) * 3;
        const Vector3 p0{ vertices[i0], vertices[i0 + 1], vertices[i0 + 2] };
        const Vector3 p1{ vertices[i1], vertices[i1 + 1], vertices[i1 + 2] };
        const Vector3 p2{ vertices[i2], vertices[i2 + 1], vertices[i2 + 2] };
        const RayCollision hit = GetRayCollisionTriangle(ray, p0, p1, p2);
        if (hit.hit && hit.distance > 0.0f && hit.distance < bestHit.distance)
        {
            bestHit = hit;
            hitAny = true;
        }
    }

    if (hitAny)
    {
        outHit = bestHit;
    }
    return hitAny;
}

float DistancePointToRay(Vector3 point, Ray ray)
{
    const Vector3 toPoint = Vector3Subtract(point, ray.position);
    const Vector3 projected = Vector3Scale(ray.direction, Vector3DotProduct(toPoint, ray.direction));
    return Vector3Length(Vector3Subtract(toPoint, projected));
}

float DistancePointToScreenSegment(Vector2 point, Vector2 a, Vector2 b)
{
    const Vector2 ab = Vector2Subtract(b, a);
    const float lengthSq = Vector2DotProduct(ab, ab);
    if (lengthSq <= 0.0001f)
    {
        return Vector2Distance(point, a);
    }

    const float t = ClampFloat(Vector2DotProduct(Vector2Subtract(point, a), ab) / lengthSq, 0.0f, 1.0f);
    const Vector2 closest = Vector2Add(a, Vector2Scale(ab, t));
    return Vector2Distance(point, closest);
}

float ClosestAlphaOnScreenSegment(Vector2 point, Vector2 a, Vector2 b)
{
    const Vector2 ab = Vector2Subtract(b, a);
    const float lengthSq = Vector2DotProduct(ab, ab);
    if (lengthSq <= 0.0001f)
    {
        return 0.0f;
    }

    return ClampFloat(Vector2DotProduct(Vector2Subtract(point, a), ab) / lengthSq, 0.0f, 1.0f);
}

int PickNodeFromViewport(const ModelTab& tab, Vector2 mouse, const VisibilityState& visibility)
{
    if (!tab.loaded.valid || tab.loaded.nodes.empty()) return -1;

    const Ray ray = GetScreenToWorldRay(mouse, tab.orbit.camera);
    int bestNode = -1;
    float bestDistance = std::numeric_limits<float>::max();
    int bestBoneNode = -1;
    float bestBoneScreenDistance = std::numeric_limits<float>::max();
    float bestBoneDepth = std::numeric_limits<float>::max();
    constexpr float bonePickRadiusPixels = 18.0f;

    if (visibility.bones)
    {
        for (const BoneSegment& bone : GetVisibleBones(tab))
        {
            const float startDepth = Vector3DotProduct(Vector3Subtract(bone.start, ray.position), ray.direction);
            const float endDepth = Vector3DotProduct(Vector3Subtract(bone.end, ray.position), ray.direction);
            if (startDepth <= 0.0f && endDepth <= 0.0f) continue;

            const Vector2 startScreen = GetWorldToScreen(bone.start, tab.orbit.camera);
            const Vector2 endScreen = GetWorldToScreen(bone.end, tab.orbit.camera);
            const float screenDistance = DistancePointToScreenSegment(mouse, startScreen, endScreen);
            const float startDistance = Vector2Distance(mouse, startScreen);
            const float endDistance = Vector2Distance(mouse, endScreen);
            const float jointDistance = std::min(startDistance, endDistance);
            const float pickDistance = std::min(screenDistance, jointDistance);
            if (pickDistance <= bonePickRadiusPixels)
            {
                const float depth = std::max(0.0f, std::min(startDepth, endDepth));
                const bool closerOnScreen = pickDistance < bestBoneScreenDistance - 1.0f;
                const bool sameScreenDistanceButCloser = std::fabs(pickDistance - bestBoneScreenDistance) <= 1.0f && depth < bestBoneDepth;
                if (closerOnScreen || sameScreenDistanceButCloser)
                {
                    const float segmentAlpha = ClosestAlphaOnScreenSegment(mouse, startScreen, endScreen);
                    const bool nearEndJoint = endDistance <= bonePickRadiusPixels && endDistance <= startDistance;
                    const bool nearStartJoint = startDistance <= bonePickRadiusPixels && startDistance < endDistance;

                    bestBoneScreenDistance = pickDistance;
                    bestBoneDepth = depth;
                    if ((nearEndJoint || (!nearStartJoint && segmentAlpha >= 0.5f)) && bone.endNode >= 0)
                    {
                        bestBoneNode = bone.endNode;
                    }
                    else
                    {
                        bestBoneNode = bone.startNode >= 0 ? bone.startNode : bone.endNode;
                    }
                }
            }
        }
    }

    if (bestBoneNode >= 0)
    {
        return bestBoneNode;
    }

    for (int i = 0; i < static_cast<int>(tab.loaded.nodes.size()); ++i)
    {
        const SceneNode& node = tab.loaded.nodes[static_cast<size_t>(i)];
        if (!IsViewportNodeVisible(tab, i)) continue;

        if (node.type == SceneNodeType::Mesh)
        {
            if (!visibility.geometry) continue;
            RayCollision hit{};
            if (!GetRayCollisionMeshNodeTriangles(tab, node, ray, hit)) continue;
            if (hit.hit && hit.distance < bestDistance)
            {
                bestDistance = hit.distance;
                bestNode = i;
            }
        }
        else
        {
            if ((node.type == SceneNodeType::Bone && !visibility.bones) ||
                (node.type == SceneNodeType::Empty && !visibility.empties))
            {
                continue;
            }
            const float handleRadius = std::max(0.03f, tab.orbit.distance * 0.015f);
            const float distance = DistancePointToRay(node.position, ray);
            if (distance < handleRadius)
            {
                const float rayDepth = Vector3DotProduct(Vector3Subtract(node.position, ray.position), ray.direction);
                if (rayDepth > 0.0f && rayDepth < bestDistance)
                {
                    bestDistance = rayDepth;
                    bestNode = i;
                }
            }
        }
    }

    if (bestNode >= 0)
    {
        return bestNode;
    }

    return -1;
}

bool SelectNodeFromViewport(ModelTab& tab, Vector2 mouse, const VisibilityState& visibility, bool additive = false)
{
    const int pickedNode = PickNodeFromViewport(tab, mouse, visibility);
    if (pickedNode < 0) return false;

    SelectNode(tab, pickedNode, additive);
    return true;
}

bool PickMeshNodeFromViewport(const ModelTab& tab, Vector2 mouse, const VisibilityState& visibility, int& outNode)
{
    outNode = -1;
    if (!tab.loaded.valid || !tab.loaded.hasMesh || tab.loaded.nodes.empty()) return false;
    if (!visibility.geometry) return false;

    const Ray ray = GetScreenToWorldRay(mouse, tab.orbit.camera);
    float bestDistance = std::numeric_limits<float>::max();
    for (int nodeIndex = 0; nodeIndex < static_cast<int>(tab.loaded.nodes.size()); ++nodeIndex)
    {
        const SceneNode& node = tab.loaded.nodes[static_cast<size_t>(nodeIndex)];
        if (node.type != SceneNodeType::Mesh || !IsViewportNodeVisible(tab, nodeIndex)) continue;

        RayCollision hit{};
        if (!GetRayCollisionMeshNodeTriangles(tab, node, ray, hit)) continue;
        if (hit.hit && hit.distance < bestDistance)
        {
            bestDistance = hit.distance;
            outNode = nodeIndex;
        }
    }
    return outNode >= 0;
}

bool PaintSkinWeightsAtMouse(ModelTab& tab,
                             Vector2 mouse,
                             const VisibilityState& visibility,
                             const WeightBrushSettings& brush,
                             std::string& notice,
                             std::string& error)
{
    if (!HasCpuSkinnedMesh(tab.loaded))
    {
        error = "No CPU skin weights available to paint.";
        notice.clear();
        return false;
    }

    if (tab.selectedNode < 0 ||
        tab.selectedNode >= static_cast<int>(tab.loaded.nodes.size()) ||
        tab.loaded.nodes[static_cast<size_t>(tab.selectedNode)].type != SceneNodeType::Bone ||
        IsDeletedNode(tab, tab.selectedNode))
    {
        error = "Select a bone to paint weights.";
        notice.clear();
        return false;
    }

    int meshNodeIndex = -1;
    if (!PickMeshNodeFromViewport(tab, mouse, visibility, meshNodeIndex))
    {
        return false;
    }

    const BonePose* bonePose = FindBonePoseByNode(tab.originalBindBonePoses, tab.selectedNode);
    if (!bonePose)
    {
        bonePose = FindBonePoseByNode(tab.loaded.bonePoses, tab.selectedNode);
    }
    if (!bonePose)
    {
        error = "Selected bone has no bind pose.";
        notice.clear();
        return false;
    }

    const SceneNode& meshNode = tab.loaded.nodes[static_cast<size_t>(meshNodeIndex)];
    const int start = std::max(0, meshNode.meshVertexStart);
    const int end = std::min(meshNode.meshVertexStart + meshNode.meshVertexCount, static_cast<int>(tab.loaded.skinnedVertices.size()));
    if (start >= end) return false;

    const float* displayedVertices = GetCurrentMeshVertices(tab);
    if (!displayedVertices) return false;

    const std::string& boneName = tab.loaded.nodes[static_cast<size_t>(tab.selectedNode)].name;
    const float radius = ClampFloat(brush.sizePixels, 4.0f, 220.0f);
    const float radiusSqr = radius * radius;
    const float amountScale = ClampFloat(brush.strength, 0.0f, 1.0f) * std::max(0.0f, GetFrameTime()) * 2.0f;
    if (amountScale <= 0.000001f) return false;

    int changedVertices = 0;
    for (int vertexIndex = start; vertexIndex < end; ++vertexIndex)
    {
        const size_t base = static_cast<size_t>(vertexIndex) * 3;
        if (base + 2 >= tab.loaded.bindVertices.size()) continue;

        const Vector3 displayedPoint{
            displayedVertices[base],
            displayedVertices[base + 1],
            displayedVertices[base + 2]
        };
        const Vector2 screen = GetWorldToScreen(displayedPoint, tab.orbit.camera);
        const float distanceSqr = Vector2DistanceSqr(mouse, screen);
        if (distanceSqr > radiusSqr) continue;

        const Vector3 bindPoint{
            tab.loaded.bindVertices[base],
            tab.loaded.bindVertices[base + 1],
            tab.loaded.bindVertices[base + 2]
        };
        const float falloff = 1.0f - std::sqrt(distanceSqr) / radius;
        const float amount = amountScale * ClampFloat(falloff, 0.0f, 1.0f);
        tab.loaded.skinnedVertices[static_cast<size_t>(vertexIndex)].bindPosition = bindPoint;
        if (AddBoneWeightToVertex(tab.loaded.skinnedVertices[static_cast<size_t>(vertexIndex)], boneName, *bonePose, amount, brush.autoNormalize))
        {
            ++changedVertices;
        }
    }

    if (changedVertices <= 0) return false;

    RebuildSkinnedAnimationMeshFrames(tab);
    if (tab.animation.clipIndex < 0 && !RebuildCurrentSkinnedMeshFromBones(tab))
    {
        RefreshDisplayedMesh(tab);
    }
    notice = "Painted weights: " + std::to_string(changedVertices) + " vertices.";
    error.clear();
    return true;
}

bool GetSelectedNodePosition(const ModelTab& tab, Vector3& outPosition)
{
    if (tab.selectedNode < 0 || tab.selectedNode >= static_cast<int>(tab.loaded.nodes.size())) return false;

    const SceneNode& node = tab.loaded.nodes[static_cast<size_t>(tab.selectedNode)];
    if (node.type == SceneNodeType::Bone)
    {
        for (const BoneSegment& bone : GetVisibleBones(tab))
        {
            if (bone.endNode == tab.selectedNode)
            {
                outPosition = bone.end;
                return true;
            }
            if (bone.startNode == tab.selectedNode)
            {
                outPosition = bone.start;
                return true;
            }
        }
    }

    outPosition = node.position;
    return true;
}

bool FocusCameraOnSelection(ModelTab& tab)
{
    if (tab.selectedNode < 0 || tab.selectedNode >= static_cast<int>(tab.loaded.nodes.size())) return false;

    const SceneNode& node = tab.loaded.nodes[static_cast<size_t>(tab.selectedNode)];
    if (!IsViewportNodeVisible(tab, tab.selectedNode)) return false;
    if (node.type == SceneNodeType::Mesh && node.hasBounds)
    {
        FocusCameraOnBounds(tab.orbit, node.bounds);
        return true;
    }

    Vector3 position{};
    if (!GetSelectedNodePosition(tab, position)) return false;

    FocusCameraOnPoint(tab.orbit, position, GetBoundsDiagonal(tab.loaded.bounds));
    return true;
}

void DrawSelectedMeshOverlay(const ModelTab& tab, const VisibilityState& visibility)
{
    if (!visibility.geometry) return;

    for (int nodeIndex : tab.selectedNodes)
    {
        if (nodeIndex < 0 || nodeIndex >= static_cast<int>(tab.loaded.nodes.size())) continue;
        if (!IsViewportNodeVisible(tab, nodeIndex)) continue;

        const SceneNode& node = tab.loaded.nodes[static_cast<size_t>(nodeIndex)];
        if (node.type == SceneNodeType::Mesh && node.hasBounds)
        {
            DrawMeshNodeWireframe(tab, node, kSelectionColor);
        }
    }
}

void DrawSelectedNodeOverlay(const ModelTab& tab, const VisibilityState& visibility)
{
    for (int nodeIndex : tab.selectedNodes)
    {
        if (nodeIndex < 0 || nodeIndex >= static_cast<int>(tab.loaded.nodes.size())) continue;
        if (!IsViewportNodeVisible(tab, nodeIndex)) continue;

        const SceneNode& node = tab.loaded.nodes[static_cast<size_t>(nodeIndex)];
        if (node.type == SceneNodeType::Mesh && node.hasBounds)
        {
            DrawMeshOrigin(node, GetBoundsDiagonal(tab.loaded.bounds));
        }
        else if (node.type == SceneNodeType::Empty && visibility.empties)
        {
            const float length = ClampFloat(GetBoundsDiagonal(tab.loaded.bounds) * 0.055f, 0.08f, 0.8f);
            DrawEmptyCross(node, length, kSelectionColor);
        }
    }
}

const char* GetTransformToolName(TransformTool tool)
{
    switch (tool)
    {
    case TransformTool::Select: return "Select";
    case TransformTool::Move: return "Move";
    case TransformTool::Rotate: return "Rotate";
    case TransformTool::Scale: return "Scale";
    case TransformTool::WeightsBrush: return "Weights";
    }
    return "Select";
}

const char* GetTransformToolHotkey(TransformTool tool)
{
    switch (tool)
    {
    case TransformTool::Select: return "Q";
    case TransformTool::Move: return "W";
    case TransformTool::Rotate: return "E";
    case TransformTool::Scale: return "R";
    case TransformTool::WeightsBrush: return "A";
    }
    return "";
}

const char* GetGizmoOrientationName(GizmoOrientation orientation)
{
    switch (orientation)
    {
    case GizmoOrientation::Global: return "Global";
    case GizmoOrientation::Local: return "Local";
    }
    return "Global";
}

float GetTransformGizmoLength(const ModelTab& tab)
{
    Vector3 pivot{};
    if (!GetSelectedNodePosition(tab, pivot))
    {
        return ClampFloat(GetBoundsDiagonal(tab.loaded.bounds) * 0.18f, 0.15f, 2.0f);
    }

    const Camera3D& camera = tab.orbit.camera;
    const float screenHeight = std::max(1.0f, static_cast<float>(GetScreenHeight()));
    float worldPerPixel = 0.001f;
    if (camera.projection == CAMERA_ORTHOGRAPHIC)
    {
        worldPerPixel = std::max(0.000001f, camera.fovy / screenHeight);
    }
    else
    {
        const Vector3 forward = GetCameraForward(camera);
        const float depth = std::max(0.0001f, Vector3DotProduct(Vector3Subtract(pivot, camera.position), forward));
        worldPerPixel = std::max(0.000001f, (2.0f * depth * std::tan(camera.fovy * DEG2RAD * 0.5f)) / screenHeight);
    }

    return ClampFloat(worldPerPixel * 96.0f * gTransformGizmoScale, 0.03f, 1000.0f);
}

bool GetGizmoPivot(const ModelTab& tab, Vector3& pivot)
{
    if (tab.selectedNode < 0 || tab.selectedNode >= static_cast<int>(tab.loaded.nodes.size())) return false;
    if (!IsViewportNodeVisible(tab, tab.selectedNode)) return false;
    return GetSelectedNodePosition(tab, pivot);
}

float GetViewPlaneWorldPerPixel(const ModelTab& tab, Vector3 pivot)
{
    const Camera3D& camera = tab.orbit.camera;
    const float screenHeight = std::max(1.0f, static_cast<float>(GetScreenHeight()));
    if (camera.projection == CAMERA_ORTHOGRAPHIC)
    {
        return std::max(0.000001f, camera.fovy / screenHeight);
    }

    const Vector3 forward = GetCameraForward(camera);
    const float depth = std::max(0.0001f, Vector3DotProduct(Vector3Subtract(pivot, camera.position), forward));
    return std::max(0.000001f, (2.0f * depth * std::tan(camera.fovy * DEG2RAD * 0.5f)) / screenHeight);
}

float GetAxisWorldPerPixel(const ModelTab& tab, Vector3 pivot, Vector3 axisVector)
{
    const float length = GetTransformGizmoLength(tab);
    const Vector2 start = GetWorldToScreen(pivot, tab.orbit.camera);
    const Vector2 end = GetWorldToScreen(Vector3Add(pivot, Vector3Scale(axisVector, length)), tab.orbit.camera);
    const float screenLength = Vector2Distance(start, end);
    if (screenLength <= 0.001f) return GetViewPlaneWorldPerPixel(tab, pivot);
    return length / screenLength;
}

float GetScreenAngleAroundPivot(Vector2 mouse, Vector2 pivotScreen)
{
    return std::atan2(mouse.y - pivotScreen.y, mouse.x - pivotScreen.x);
}

float WrapAngleDelta(float radians)
{
    while (radians > PI) radians -= PI * 2.0f;
    while (radians < -PI) radians += PI * 2.0f;
    return radians;
}

Vector2 GetAxisScreenDirection(const ModelTab& tab, Vector3 pivot, TransformAxis axis, GizmoOrientation orientation)
{
    const Vector3 axisVector = GetTransformAxisVector(tab, axis, orientation);
    const float length = GetTransformGizmoLength(tab);
    const Vector2 start = GetWorldToScreen(pivot, tab.orbit.camera);
    const Vector2 end = GetWorldToScreen(Vector3Add(pivot, Vector3Scale(axisVector, length)), tab.orbit.camera);
    Vector2 direction = Vector2Subtract(end, start);
    const float lengthSq = Vector2DotProduct(direction, direction);
    if (lengthSq <= 0.001f) return Vector2{ 1.0f, 0.0f };
    return Vector2Scale(direction, 1.0f / std::sqrt(lengthSq));
}

float DistanceMouseToGizmoRing(ModelTab& tab, Vector2 mouse, Vector3 pivot, TransformAxis axis, GizmoOrientation orientation)
{
    const float radius = GetTransformGizmoLength(tab) * 0.82f;
    Vector3 axisA{};
    Vector3 axisB{};
    if (!GetTransformPlaneBasis(tab, axis, orientation, axisA, axisB)) return std::numeric_limits<float>::max();

    float best = std::numeric_limits<float>::max();
    Vector2 previous{};
    constexpr int kSegments = 64;
    for (int i = 0; i <= kSegments; ++i)
    {
        const float angle = (static_cast<float>(i) / static_cast<float>(kSegments)) * PI * 2.0f;
        const Vector3 point = Vector3Add(pivot,
                                         Vector3Add(Vector3Scale(axisA, std::cos(angle) * radius),
                                                    Vector3Scale(axisB, std::sin(angle) * radius)));
        const Vector2 screen = GetWorldToScreen(point, tab.orbit.camera);
        if (i > 0)
        {
            best = std::min(best, DistancePointToScreenSegment(mouse, previous, screen));
        }
        previous = screen;
    }
    return best;
}

TransformAxis PickTransformGizmoAxis(ModelTab& tab, TransformTool tool, GizmoOrientation orientation, Vector2 mouse)
{
    Vector3 pivot{};
    if (tool == TransformTool::Select || tool == TransformTool::WeightsBrush || !GetGizmoPivot(tab, pivot)) return TransformAxis::None;

    const Vector2 pivotScreen = GetWorldToScreen(pivot, tab.orbit.camera);
    if (Vector2Distance(mouse, pivotScreen) <= 15.0f)
    {
        return TransformAxis::Center;
    }

    TransformAxis bestAxis = TransformAxis::None;
    float bestDistance = 12.0f;
    const float length = GetTransformGizmoLength(tab);
    for (TransformAxis axis : { TransformAxis::X, TransformAxis::Y, TransformAxis::Z })
    {
        float distance = std::numeric_limits<float>::max();
        if (tool == TransformTool::Rotate)
        {
            distance = DistanceMouseToGizmoRing(tab, mouse, pivot, axis, orientation);
        }
        else
        {
            const Vector3 axisVector = GetTransformAxisVector(tab, axis, orientation);
            const Vector2 start = GetWorldToScreen(pivot, tab.orbit.camera);
            const Vector2 end = GetWorldToScreen(Vector3Add(pivot, Vector3Scale(axisVector, length)), tab.orbit.camera);
            distance = DistancePointToScreenSegment(mouse, start, end);
            distance = std::min(distance, Vector2Distance(mouse, end));
        }
        if (distance < bestDistance)
        {
            bestDistance = distance;
            bestAxis = axis;
        }
    }
    return bestAxis;
}

bool UpdateTransformGizmoInput(ModelTab* active,
                               TransformGizmoState& state,
                               TransformTool tool,
                               GizmoOrientation orientation,
                               bool mouseInViewport,
                               std::string& notice,
                               std::string& error)
{
    if (!active || !active->loaded.valid || tool == TransformTool::Select || tool == TransformTool::WeightsBrush)
    {
        state = TransformGizmoState{};
        return false;
    }

    const Vector2 mouse = GetMousePosition();
    if (state.dragging)
    {
        if (!IsMouseButtonDown(MOUSE_BUTTON_LEFT))
        {
            state = TransformGizmoState{};
            return true;
        }

        Vector3 pivot{};
        if (!GetGizmoPivot(*active, pivot))
        {
            state = TransformGizmoState{};
            return false;
        }

        const Vector2 delta = Vector2Subtract(mouse, state.lastMouse);
        state.lastMouse = mouse;
        if (state.axis == TransformAxis::Center)
        {
            if (tool == TransformTool::Move)
            {
                const float worldPerPixel = GetViewPlaneWorldPerPixel(*active, pivot);
                const Vector3 right = GetCameraRight(active->orbit.camera);
                const Vector3 up = GetCameraUpVector(active->orbit.camera);
                Vector3 moveDelta = Vector3Scale(right, delta.x * worldPerPixel);
                moveDelta = Vector3Add(moveDelta, Vector3Scale(up, -delta.y * worldPerPixel));
                if (Vector3Length(moveDelta) > 0.000001f)
                {
                    MoveSelectedSubtree(*active, moveDelta);
                }
            }
            else if (tool == TransformTool::Rotate)
            {
                const Vector3 right = GetCameraRight(active->orbit.camera);
                const Vector3 up = GetCameraUpVector(active->orbit.camera);
                const float rightRadians = delta.y * 0.006f;
                const float upRadians = delta.x * 0.006f;
                if (std::fabs(rightRadians) > 0.000001f || std::fabs(upRadians) > 0.000001f)
                {
                    RotateSelectedSubtreeArcball(*active, pivot, right, rightRadians, up, upRadians);
                }
            }
            else if (tool == TransformTool::Scale)
            {
                const float scalarPixels = delta.x - delta.y;
                if (std::fabs(scalarPixels) > 0.001f)
                {
                    ScaleSelectedSubtreeUniform(*active, pivot, std::exp(scalarPixels * 0.006f));
                }
            }
        }
        else
        {
            const Vector3 axisVector = GetTransformAxisVector(*active, state.axis, orientation);
            if (tool == TransformTool::Rotate)
            {
                const Vector2 pivotScreen = GetWorldToScreen(pivot, active->orbit.camera);
                const float currentAngle = GetScreenAngleAroundPivot(mouse, pivotScreen);
                float radians = WrapAngleDelta(currentAngle - state.lastAngle);
                state.lastAngle = currentAngle;
                const float facing = Vector3DotProduct(axisVector, GetCameraForward(active->orbit.camera));
                radians *= facing < 0.0f ? -1.0f : 1.0f;
                if (std::fabs(radians) > 0.000001f)
                {
                    RotateSelectedSubtree(*active, pivot, state.axis, axisVector, radians);
                }
            }
            else
            {
                const Vector2 axisDirection = GetAxisScreenDirection(*active, pivot, state.axis, orientation);
                const float scalarPixels = Vector2DotProduct(delta, axisDirection);
                if (std::fabs(scalarPixels) > 0.001f)
                {
                    if (tool == TransformTool::Move)
                    {
                        MoveSelectedSubtree(*active, Vector3Scale(axisVector, scalarPixels * GetAxisWorldPerPixel(*active, pivot, axisVector)));
                    }
                    else if (tool == TransformTool::Scale)
                    {
                        ScaleSelectedSubtree(*active, pivot, state.axis, axisVector, std::exp(scalarPixels * 0.006f));
                    }
                }
            }
        }
        return true;
    }

    if (!mouseInViewport || !IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) return false;
    const TransformAxis axis = PickTransformGizmoAxis(*active, tool, orientation, mouse);
    if (axis == TransformAxis::None) return false;

    PushUndoSnapshot(*active);
    state.dragging = true;
    state.axis = axis;
    state.nodeIndex = active->selectedNode;
    state.lastMouse = mouse;
    Vector3 pivot{};
    if (GetGizmoPivot(*active, pivot))
    {
        state.lastAngle = GetScreenAngleAroundPivot(mouse, GetWorldToScreen(pivot, active->orbit.camera));
    }
    notice = std::string(GetTransformToolName(tool)) + " gizmo.";
    error.clear();
    return true;
}

void DrawMoveGizmoArrow(const ModelTab& tab, Vector3 start, Vector3 end, Vector3 axisVector, float length, Color color)
{
    DrawLine3D(start, end, color);

    const Vector3 cameraForward = GetCameraForward(tab.orbit.camera);
    Vector3 side = Vector3CrossProduct(axisVector, cameraForward);
    if (Vector3Length(side) < 0.0001f)
    {
        side = ChoosePerpendicular(axisVector);
    }
    side = NormalizeOrFallback(side, Vector3{ 1.0f, 0.0f, 0.0f });

    const float headLength = length * 0.12f;
    const float headWidth = length * 0.05f;
    const Vector3 base = Vector3Subtract(end, Vector3Scale(axisVector, headLength));
    DrawLine3D(end, Vector3Add(base, Vector3Scale(side, headWidth)), color);
    DrawLine3D(end, Vector3Subtract(base, Vector3Scale(side, headWidth)), color);

    const Vector3 upWing = NormalizeOrFallback(Vector3CrossProduct(axisVector, side), ChoosePerpendicular(axisVector));
    DrawLine3D(end, Vector3Add(base, Vector3Scale(upWing, headWidth)), color);
    DrawLine3D(end, Vector3Subtract(base, Vector3Scale(upWing, headWidth)), color);
}

void DrawTransformGizmo(const ModelTab& tab, TransformTool tool, const TransformGizmoState& state, GizmoOrientation orientation)
{
    if (tool == TransformTool::Select || tool == TransformTool::WeightsBrush) return;

    Vector3 pivot{};
    if (!GetGizmoPivot(tab, pivot)) return;

    const float length = GetTransformGizmoLength(tab);
    const float handleRadius = ClampFloat(length * 0.045f, 0.012f, 0.08f);
    rlDrawRenderBatchActive();
    rlDisableDepthTest();
    rlDisableDepthMask();
    rlSetLineWidth(4.5f);

    const bool centerActive = state.dragging && state.axis == TransformAxis::Center;
    DrawSphere(pivot, handleRadius * 0.9f, centerActive ? Color{ 255, 235, 128, 255 } : Color{ 225, 232, 238, 235 });

    for (TransformAxis axis : { TransformAxis::X, TransformAxis::Y, TransformAxis::Z })
    {
        const bool active = state.dragging && state.axis == axis;
        const Color color = GetTransformAxisColor(axis, active);
        const Vector3 axisVector = GetTransformAxisVector(tab, axis, orientation);
        if (tool == TransformTool::Rotate)
        {
            const float radius = length * 0.82f;
            Vector3 axisA{};
            Vector3 axisB{};
            if (GetTransformPlaneBasis(tab, axis, orientation, axisA, axisB))
            {
                DrawJointCircle(pivot, axisA, axisB, radius, color);
            }
        }
        else
        {
            const Vector3 end = Vector3Add(pivot, Vector3Scale(axisVector, length));
            if (tool == TransformTool::Move)
            {
                DrawMoveGizmoArrow(tab, pivot, end, axisVector, length, color);
            }
            else if (tool == TransformTool::Scale)
            {
                DrawLine3D(pivot, end, color);
                DrawCubeV(end, Vector3{ handleRadius * 2.0f, handleRadius * 2.0f, handleRadius * 2.0f }, color);
            }
        }
    }
    rlSetLineWidth(1.0f);
    rlDrawRenderBatchActive();
    rlEnableDepthMask();
    rlEnableDepthTest();
}

Rectangle GetTransformToolbarButtonRect(float hierarchyBlockW, int index)
{
    return Rectangle{ hierarchyBlockW + 10.0f, 104.0f + static_cast<float>(index) * 38.0f, 112.0f, 32.0f };
}

Rectangle GetGizmoOrientationButtonRect(float hierarchyBlockW, int index)
{
    return Rectangle{ hierarchyBlockW + 10.0f + static_cast<float>(index) * 57.0f, 304.0f, 55.0f, 28.0f };
}

Rectangle GetWeightBrushSmallButtonRect(float hierarchyBlockW, int row, int column)
{
    return Rectangle{ hierarchyBlockW + 112.0f + static_cast<float>(column) * 31.0f, 366.0f + static_cast<float>(row) * 30.0f, 28.0f, 24.0f };
}

Rectangle GetWeightBrushAutoNormalizeRect(float hierarchyBlockW)
{
    return Rectangle{ hierarchyBlockW + 10.0f, 426.0f, 112.0f, 24.0f };
}

bool UpdateTransformToolbarInput(TransformTool& tool, GizmoOrientation& orientation, WeightBrushSettings& brush, float hierarchyBlockW)
{
    const Vector2 mouse = GetMousePosition();
    const TransformTool tools[] = { TransformTool::Select, TransformTool::Move, TransformTool::Rotate, TransformTool::Scale, TransformTool::WeightsBrush };
    for (int i = 0; i < 5; ++i)
    {
        if (CheckCollisionPointRec(mouse, GetTransformToolbarButtonRect(hierarchyBlockW, i)))
        {
            if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
            {
                tool = tools[i];
            }
            return true;
        }
    }

    if (tool == TransformTool::WeightsBrush)
    {
        for (int row = 0; row < 2; ++row)
        {
            for (int column = 0; column < 2; ++column)
            {
                if (!CheckCollisionPointRec(mouse, GetWeightBrushSmallButtonRect(hierarchyBlockW, row, column))) continue;
                if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
                {
                    if (row == 0)
                    {
                        brush.sizePixels = ClampFloat(brush.sizePixels + (column == 0 ? -8.0f : 8.0f), 8.0f, 220.0f);
                    }
                    else
                    {
                        brush.strength = ClampFloat(brush.strength + (column == 0 ? -0.05f : 0.05f), 0.01f, 1.0f);
                    }
                }
                return true;
            }
        }

        if (CheckCollisionPointRec(mouse, GetWeightBrushAutoNormalizeRect(hierarchyBlockW)))
        {
            if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
            {
                brush.autoNormalize = !brush.autoNormalize;
            }
            return true;
        }
    }

    const GizmoOrientation orientations[] = { GizmoOrientation::Global, GizmoOrientation::Local };
    for (int i = 0; i < 2; ++i)
    {
        if (CheckCollisionPointRec(mouse, GetGizmoOrientationButtonRect(hierarchyBlockW, i)))
        {
            if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
            {
                orientation = orientations[i];
            }
            return true;
        }
    }
    return false;
}

void DrawWeightBrushControls(Font font, const WeightBrushSettings& brush, float hierarchyBlockW)
{
    DrawUiText(font, "Brush", hierarchyBlockW + 12.0f, 342.0f, 13.0f, Color{ 154, 166, 178, 255 });

    char value[64] = {};
    DrawUiText(font, "Size", hierarchyBlockW + 12.0f, 370.0f, 13.0f, Color{ 190, 200, 210, 255 });
    std::snprintf(value, sizeof(value), "%.0f", brush.sizePixels);
    DrawUiText(font, value, hierarchyBlockW + 52.0f, 370.0f, 13.0f, Color{ 205, 224, 238, 255 });
    DrawPanelButton(font, GetWeightBrushSmallButtonRect(hierarchyBlockW, 0, 0), "-");
    DrawPanelButton(font, GetWeightBrushSmallButtonRect(hierarchyBlockW, 0, 1), "+");

    DrawUiText(font, "Strength", hierarchyBlockW + 12.0f, 400.0f, 13.0f, Color{ 190, 200, 210, 255 });
    std::snprintf(value, sizeof(value), "%.2f", brush.strength);
    DrawUiText(font, value, hierarchyBlockW + 70.0f, 400.0f, 13.0f, Color{ 205, 224, 238, 255 });
    DrawPanelButton(font, GetWeightBrushSmallButtonRect(hierarchyBlockW, 1, 0), "-");
    DrawPanelButton(font, GetWeightBrushSmallButtonRect(hierarchyBlockW, 1, 1), "+");

    DrawPanelButton(font, GetWeightBrushAutoNormalizeRect(hierarchyBlockW), brush.autoNormalize ? "[x] Normalize" : "[ ] Normalize");
}

void DrawTransformToolbar(Font font, TransformTool tool, GizmoOrientation orientation, const WeightBrushSettings& brush, float hierarchyBlockW)
{
    const TransformTool tools[] = { TransformTool::Select, TransformTool::Move, TransformTool::Rotate, TransformTool::Scale, TransformTool::WeightsBrush };
    for (int i = 0; i < 5; ++i)
    {
        const Rectangle bounds = GetTransformToolbarButtonRect(hierarchyBlockW, i);
        const bool selected = tool == tools[i];
        const bool hovered = CheckCollisionPointRec(GetMousePosition(), bounds);
        DrawRectangleRec(bounds, selected ? Color{ 58, 78, 98, 245 } : hovered ? Color{ 42, 48, 55, 245 } : Color{ 24, 27, 31, 232 });
        DrawRectangleLinesEx(bounds, 1.0f, selected ? Color{ 128, 188, 235, 255 } : Color{ 78, 88, 98, 255 });
        DrawUiText(font, GetTransformToolHotkey(tools[i]), bounds.x + 8.0f, bounds.y + 4.0f, 16.0f, RAYWHITE);
        DrawUiText(font, GetTransformToolName(tools[i]), bounds.x + 48.0f, bounds.y + 7.0f, 13.0f, selected ? Color{ 205, 224, 238, 255 } : Color{ 154, 166, 178, 255 });
    }

    DrawUiText(font, "Space", hierarchyBlockW + 12.0f, 280.0f, 13.0f, Color{ 154, 166, 178, 255 });
    const GizmoOrientation orientations[] = { GizmoOrientation::Global, GizmoOrientation::Local };
    for (int i = 0; i < 2; ++i)
    {
        const Rectangle bounds = GetGizmoOrientationButtonRect(hierarchyBlockW, i);
        const bool selected = orientation == orientations[i];
        const bool hovered = CheckCollisionPointRec(GetMousePosition(), bounds);
        DrawRectangleRec(bounds, selected ? Color{ 58, 78, 98, 245 } : hovered ? Color{ 42, 48, 55, 245 } : Color{ 24, 27, 31, 232 });
        DrawRectangleLinesEx(bounds, 1.0f, selected ? Color{ 128, 188, 235, 255 } : Color{ 78, 88, 98, 255 });
        DrawUiText(font, GetGizmoOrientationName(orientations[i]), bounds.x + 8.0f, bounds.y + 7.0f, 13.0f, selected ? Color{ 205, 224, 238, 255 } : Color{ 154, 166, 178, 255 });
    }

    if (tool == TransformTool::WeightsBrush)
    {
        DrawWeightBrushControls(font, brush, hierarchyBlockW);
    }
}

void DrawWeightBrushCursor(Font font, const ModelTab* active, TransformTool tool, const WeightBrushSettings& brush, bool mouseInViewport)
{
    if (!active || tool != TransformTool::WeightsBrush || !mouseInViewport) return;

    const Vector2 mouse = GetMousePosition();
    const float radius = ClampFloat(brush.sizePixels, 4.0f, 220.0f);
    DrawCircleV(mouse, radius, Color{ 204, 154, 42, 28 });
    DrawCircleLines(static_cast<int>(std::round(mouse.x)), static_cast<int>(std::round(mouse.y)), radius, Color{ 235, 190, 72, 230 });

    const bool validBone = active->selectedNode >= 0 &&
                           active->selectedNode < static_cast<int>(active->loaded.nodes.size()) &&
                           active->loaded.nodes[static_cast<size_t>(active->selectedNode)].type == SceneNodeType::Bone &&
                           !IsDeletedNode(*active, active->selectedNode);
    const char* label = validBone ? active->loaded.nodes[static_cast<size_t>(active->selectedNode)].name.c_str() : "Select bone";
    const Vector2 labelSize = MeasureTextEx(font, label, 13.0f, 1.0f);
    Rectangle badge{ mouse.x + radius + 8.0f, mouse.y - 12.0f, std::min(labelSize.x + 12.0f, 260.0f), 22.0f };
    badge.x = ClampFloat(badge.x, 4.0f, static_cast<float>(GetScreenWidth()) - badge.width - 4.0f);
    badge.y = ClampFloat(badge.y, 4.0f, static_cast<float>(GetScreenHeight()) - badge.height - gBottomPanelReservedHeight - 4.0f);
    DrawRectangleRec(badge, Color{ 24, 27, 31, 230 });
    DrawRectangleLinesEx(badge, 1.0f, validBone ? Color{ 120, 190, 230, 255 } : Color{ 210, 110, 92, 255 });
    DrawUiTextClipped(font, label, badge.x + 6.0f, badge.y + 4.0f, 13.0f, badge.width - 12.0f, validBone ? Color{ 205, 224, 238, 255 } : Color{ 255, 170, 150, 255 });
}

Font LoadTechnicalFont()
{
    const char* candidates[] = {
        "C:/Windows/Fonts/CascadiaMono.ttf",
        "C:/Windows/Fonts/consola.ttf",
        "C:/Windows/Fonts/lucon.ttf"
    };

    for (const char* path : candidates)
    {
        if (FileExists(path))
        {
            Font font = LoadFontEx(path, 20, nullptr, 0);
            if (font.texture.id != 0)
            {
                SetTextureFilter(font.texture, TEXTURE_FILTER_BILINEAR);
                return font;
            }
        }
    }

    return GetFontDefault();
}

void DrawUiText(Font font, const char* text, float x, float y, float size, Color color)
{
    DrawTextEx(font, text, Vector2{ x, y }, size, 1.0f, color);
}

void DrawUiTextClipped(Font font, const char* text, float x, float y, float size, float maxWidth, Color color)
{
    if (!text || maxWidth <= 0.0f) return;

    if (MeasureTextEx(font, text, size, 1.0f).x <= maxWidth)
    {
        DrawUiText(font, text, x, y, size, color);
        return;
    }

    std::string clipped = text;
    const char* ellipsis = "...";
    while (!clipped.empty())
    {
        clipped.pop_back();
        const std::string candidate = clipped + ellipsis;
        if (MeasureTextEx(font, candidate.c_str(), size, 1.0f).x <= maxWidth)
        {
            DrawUiText(font, candidate.c_str(), x, y, size, color);
            return;
        }
    }
}

void DrawOrientationGizmo(Font font, const Camera3D& camera)
{
    const Vector2 center{ static_cast<float>(GetScreenWidth()) - 70.0f, 96.0f };
    constexpr float axisLength = 38.0f;
    const Vector3 forward = Vector3Normalize(Vector3Subtract(camera.target, camera.position));
    Vector3 right = Vector3CrossProduct(forward, camera.up);
    if (Vector3Length(right) < 0.0001f) right = Vector3{ 1.0f, 0.0f, 0.0f };
    right = Vector3Normalize(right);
    const Vector3 up = Vector3Normalize(Vector3CrossProduct(right, forward));

    struct GizmoAxis
    {
        Vector3 world;
        const char* label;
        Color color;
    };

    const GizmoAxis axes[] = {
        { Vector3{ 1.0f, 0.0f, 0.0f }, "X", Color{ 235, 74, 74, 255 } },
        { Vector3{ 0.0f, 1.0f, 0.0f }, "Y", Color{ 92, 210, 94, 255 } },
        { Vector3{ 0.0f, 0.0f, 1.0f }, "Z", Color{ 86, 142, 255, 255 } }
    };

    DrawCircleV(center, 4.0f, Color{ 220, 226, 232, 255 });
    for (const GizmoAxis& axis : axes)
    {
        const Vector2 dir{
            Vector3DotProduct(axis.world, right),
            -Vector3DotProduct(axis.world, up)
        };
        const Vector2 end{ center.x + dir.x * axisLength, center.y + dir.y * axisLength };
        DrawLineEx(center, end, 2.0f, axis.color);
        DrawCircleV(end, 5.0f, axis.color);
        DrawUiText(font, axis.label, end.x + 7.0f, end.y - 8.0f, 14.0f, axis.color);
    }
}

void ApplyWindowIcon()
{
    const std::string iconPath = std::string(GetApplicationDirectory()) + "open_fbx_icon.png";
    const char* path = FileExists(iconPath.c_str()) ? iconPath.c_str() : "open_fbx_icon.png";
    if (!FileExists(path)) return;

    Image icon = LoadImage(path);
    if (icon.data)
    {
        SetWindowIcon(icon);
        UnloadImage(icon);
    }
}

void DrawLoadingScreen(Font font, const std::string& path)
{
    BeginDrawing();
    ClearBackground(Color{ 24, 26, 29, 255 });

    const char* fileName = GetFileName(path.c_str());
    const char* title = "LOADING FBX";
    const char* detail = fileName && fileName[0] ? fileName : path.c_str();
    const char* note = "Importing scene, triangulating meshes, sampling skeleton and skin deformation";

    const float centerX = static_cast<float>(GetScreenWidth()) * 0.5f;
    const float centerY = static_cast<float>(GetScreenHeight()) * 0.5f;
    const Vector2 titleSize = MeasureTextEx(font, title, 24.0f, 1.0f);
    const Vector2 detailSize = MeasureTextEx(font, detail, 18.0f, 1.0f);
    const Vector2 noteSize = MeasureTextEx(font, note, 15.0f, 1.0f);

    DrawUiText(font, title, centerX - titleSize.x * 0.5f, centerY - 48.0f, 24.0f, RAYWHITE);
    DrawUiText(font, detail, centerX - detailSize.x * 0.5f, centerY - 12.0f, 18.0f, Color{ 190, 202, 214, 255 });
    DrawUiText(font, note, centerX - noteSize.x * 0.5f, centerY + 24.0f, 15.0f, Color{ 128, 140, 152, 255 });

    const Rectangle bar{ centerX - 160.0f, centerY + 58.0f, 320.0f, 8.0f };
    DrawRectangleRec(bar, Color{ 50, 56, 62, 255 });
    DrawRectangleRec(Rectangle{ bar.x, bar.y, bar.width * 0.72f, bar.height }, Color{ 94, 156, 214, 255 });

    EndDrawing();
}

const char* GetViewModeName(ViewMode mode)
{
    switch (mode)
    {
    case ViewMode::Shaded: return "SHADED";
    case ViewMode::ShadedWireframe: return "SHADED + WIREFRAME";
    case ViewMode::Wireframe: return "WIREFRAME";
    }

    return "UNKNOWN";
}

ViewMode NextViewMode(ViewMode mode)
{
    switch (mode)
    {
    case ViewMode::Shaded: return ViewMode::ShadedWireframe;
    case ViewMode::ShadedWireframe: return ViewMode::Wireframe;
    case ViewMode::Wireframe: return ViewMode::Shaded;
    }

    return ViewMode::Shaded;
}

const char* GetNavigationPresetName(NavigationPreset preset)
{
    return preset == NavigationPreset::Maya ? "MAYA" : "BLENDER";
}

const char* GetSceneNodeIcon(SceneNodeType type)
{
    switch (type)
    {
    case SceneNodeType::Mesh: return "[M]";
    case SceneNodeType::Bone: return "[J]";
    case SceneNodeType::Empty: return "[E]";
    }

    return "[?]";
}

const char* GetSceneNodeTypeName(SceneNodeType type)
{
    switch (type)
    {
    case SceneNodeType::Mesh: return "Mesh";
    case SceneNodeType::Bone: return "Bone";
    case SceneNodeType::Empty: return "Empty";
    }

    return "Node";
}

std::string MakeTabTitle(const std::string& path)
{
    const char* fileName = GetFileName(path.c_str());
    return fileName && fileName[0] ? fileName : path;
}

const BonePose* FindCurrentBonePose(const ModelTab& tab, int nodeIndex)
{
    for (const BonePose& pose : GetVisibleBonePoses(tab))
    {
        if (pose.node == nodeIndex)
        {
            return &pose;
        }
    }

    return nullptr;
}

std::vector<int> GetSelectedMeshNodeIndices(const ModelTab& tab)
{
    std::vector<int> meshNodes;
    for (int nodeIndex : tab.selectedNodes)
    {
        if (nodeIndex < 0 || nodeIndex >= static_cast<int>(tab.loaded.nodes.size())) continue;
        if (IsDeletedNode(tab, nodeIndex)) continue;

        const SceneNode& node = tab.loaded.nodes[static_cast<size_t>(nodeIndex)];
        if (node.type == SceneNodeType::Mesh)
        {
            meshNodes.push_back(nodeIndex);
        }
    }
    return meshNodes;
}

bool SameFloatValue(float a, float b)
{
    return std::fabs(a - b) <= 0.000001f;
}

std::string FormatFloatValue(float value, int decimals)
{
    char text[32] = {};
    if (decimals == 2)
    {
        std::snprintf(text, sizeof(text), "%.2f", value);
    }
    else
    {
        std::snprintf(text, sizeof(text), "%.3f", value);
    }
    return text;
}

std::string FormatVector3Value(Vector3 value, int decimals)
{
    char line[128] = {};
    if (decimals == 2)
    {
        std::snprintf(line, sizeof(line), "%.2f  %.2f  %.2f", value.x, value.y, value.z);
    }
    else
    {
        std::snprintf(line, sizeof(line), "%.3f  %.3f  %.3f", value.x, value.y, value.z);
    }
    return line;
}

std::string FormatMixedVector3Value(Vector3 first, bool sameX, bool sameY, bool sameZ, int decimals)
{
    return (sameX ? FormatFloatValue(first.x, decimals) : "multiple") + std::string("  ") +
           (sameY ? FormatFloatValue(first.y, decimals) : "multiple") + "  " +
           (sameZ ? FormatFloatValue(first.z, decimals) : "multiple");
}

std::string FormatMaterialSummary(const std::vector<std::string>& materialNames)
{
    if (materialNames.empty()) return "None";

    std::string summary;
    for (size_t i = 0; i < materialNames.size(); ++i)
    {
        if (i > 0) summary += ", ";
        summary += materialNames[i];
    }
    return summary;
}

void DrawSelectedInfoPanel(Font font, const ModelTab* active)
{
    if (!active || active->selectedNode < 0 || active->selectedNode >= static_cast<int>(active->loaded.nodes.size())) return;

    const std::vector<int> selectedMeshNodes = GetSelectedMeshNodeIndices(*active);
    if (selectedMeshNodes.size() > 1)
    {
        const SceneNode& firstMesh = active->loaded.nodes[static_cast<size_t>(selectedMeshNodes.front())];
        int totalPolys = 0;
        std::vector<std::string> materialNames;
        bool samePositionX = true;
        bool samePositionY = true;
        bool samePositionZ = true;
        bool sameRotationX = true;
        bool sameRotationY = true;
        bool sameRotationZ = true;
        bool sameScaleX = true;
        bool sameScaleY = true;
        bool sameScaleZ = true;

        for (int nodeIndex : selectedMeshNodes)
        {
            const SceneNode& meshNode = active->loaded.nodes[static_cast<size_t>(nodeIndex)];
            totalPolys += meshNode.meshTriangleCount;
            if (!SameFloatValue(firstMesh.position.x, meshNode.position.x)) samePositionX = false;
            if (!SameFloatValue(firstMesh.position.y, meshNode.position.y)) samePositionY = false;
            if (!SameFloatValue(firstMesh.position.z, meshNode.position.z)) samePositionZ = false;
            if (!SameFloatValue(firstMesh.rotation.x, meshNode.rotation.x)) sameRotationX = false;
            if (!SameFloatValue(firstMesh.rotation.y, meshNode.rotation.y)) sameRotationY = false;
            if (!SameFloatValue(firstMesh.rotation.z, meshNode.rotation.z)) sameRotationZ = false;
            if (!SameFloatValue(firstMesh.scale.x, meshNode.scale.x)) sameScaleX = false;
            if (!SameFloatValue(firstMesh.scale.y, meshNode.scale.y)) sameScaleY = false;
            if (!SameFloatValue(firstMesh.scale.z, meshNode.scale.z)) sameScaleZ = false;

            const std::string materialName = meshNode.materialName.empty() ? "None" : meshNode.materialName;
            if (std::find(materialNames.begin(), materialNames.end(), materialName) == materialNames.end())
            {
                materialNames.push_back(materialName);
            }
        }

        constexpr float panelW = 360.0f;
        constexpr float panelH = 176.0f;
        const float panelX = static_cast<float>(GetScreenWidth()) - panelW - 12.0f;
        const float panelY = static_cast<float>(GetScreenHeight()) - gBottomPanelReservedHeight - panelH - 12.0f;

        DrawRectangleRec(Rectangle{ panelX, panelY, panelW, panelH }, Color{ 18, 20, 23, 225 });
        DrawRectangleLinesEx(Rectangle{ panelX, panelY, panelW, panelH }, 1.0f, Color{ 70, 78, 88, 255 });

        DrawUiText(font, "SELECTION", panelX + 12.0f, panelY + 10.0f, 15.0f, Color{ 165, 182, 196, 255 });

        char line[256] = {};
        std::snprintf(line, sizeof(line), "%d meshes", static_cast<int>(selectedMeshNodes.size()));
        DrawUiTextClipped(font, line, panelX + 108.0f, panelY + 10.0f, 15.0f, panelW - 120.0f, RAYWHITE);

        std::snprintf(line, sizeof(line), "Type: Mesh selection");
        DrawUiText(font, line, panelX + 12.0f, panelY + 36.0f, 14.0f, Color{ 205, 213, 220, 255 });

        const std::string positionLine = std::string("Pos:  ") + FormatMixedVector3Value(firstMesh.position, samePositionX, samePositionY, samePositionZ, 3);
        DrawUiText(font, positionLine.c_str(), panelX + 12.0f, panelY + 58.0f, 14.0f, Color{ 205, 213, 220, 255 });

        const std::string rotationLine = std::string("Rot:  ") + FormatMixedVector3Value(firstMesh.rotation, sameRotationX, sameRotationY, sameRotationZ, 2);
        DrawUiText(font, rotationLine.c_str(), panelX + 12.0f, panelY + 80.0f, 14.0f, Color{ 205, 213, 220, 255 });

        const std::string scaleLine = std::string("Scale: ") + FormatMixedVector3Value(firstMesh.scale, sameScaleX, sameScaleY, sameScaleZ, 3);
        DrawUiText(font, scaleLine.c_str(), panelX + 12.0f, panelY + 102.0f, 14.0f, Color{ 205, 213, 220, 255 });

        std::snprintf(line, sizeof(line), "Total polys: %d", totalPolys);
        DrawUiText(font, line, panelX + 12.0f, panelY + 124.0f, 14.0f, Color{ 205, 213, 220, 255 });

        const std::string materialsLine = std::string("Materials: ") + std::to_string(static_cast<int>(materialNames.size())) + " - " + FormatMaterialSummary(materialNames);
        DrawUiTextClipped(font, materialsLine.c_str(), panelX + 12.0f, panelY + 146.0f, 14.0f, panelW - 24.0f, Color{ 205, 213, 220, 255 });
        return;
    }

    const SceneNode& node = active->loaded.nodes[static_cast<size_t>(active->selectedNode)];
    const BonePose* currentBonePose = node.type == SceneNodeType::Bone ? FindCurrentBonePose(*active, active->selectedNode) : nullptr;
    const Vector3 position = currentBonePose ? currentBonePose->position : node.position;
    const Vector3 rotation = currentBonePose ? currentBonePose->rotation : node.rotation;
    const Vector3 scale = currentBonePose ? currentBonePose->scale : node.scale;
    constexpr float panelW = 330.0f;
    constexpr float panelH = 154.0f;
    const float panelX = static_cast<float>(GetScreenWidth()) - panelW - 12.0f;
    const float panelY = static_cast<float>(GetScreenHeight()) - gBottomPanelReservedHeight - panelH - 12.0f;

    DrawRectangleRec(Rectangle{ panelX, panelY, panelW, panelH }, Color{ 18, 20, 23, 225 });
    DrawRectangleLinesEx(Rectangle{ panelX, panelY, panelW, panelH }, 1.0f, Color{ 70, 78, 88, 255 });

    DrawUiText(font, "SELECTION", panelX + 12.0f, panelY + 10.0f, 15.0f, Color{ 165, 182, 196, 255 });
    DrawUiTextClipped(font, node.name.c_str(), panelX + 100.0f, panelY + 10.0f, 15.0f, panelW - 112.0f, RAYWHITE);

    char line[256] = {};
    std::snprintf(line, sizeof(line), "Type: %s", GetSceneNodeTypeName(node.type));
    DrawUiText(font, line, panelX + 12.0f, panelY + 36.0f, 14.0f, Color{ 205, 213, 220, 255 });

    std::snprintf(line, sizeof(line), "Pos:  %.3f  %.3f  %.3f", position.x, position.y, position.z);
    DrawUiText(font, line, panelX + 12.0f, panelY + 58.0f, 14.0f, Color{ 205, 213, 220, 255 });

    std::snprintf(line, sizeof(line), "Rot:  %.2f  %.2f  %.2f", rotation.x, rotation.y, rotation.z);
    DrawUiText(font, line, panelX + 12.0f, panelY + 80.0f, 14.0f, Color{ 205, 213, 220, 255 });

    std::snprintf(line, sizeof(line), "Scale: %.3f  %.3f  %.3f", scale.x, scale.y, scale.z);
    DrawUiText(font, line, panelX + 12.0f, panelY + 102.0f, 14.0f, Color{ 205, 213, 220, 255 });

    std::snprintf(line, sizeof(line), "Polys: %d", node.meshTriangleCount);
    DrawUiText(font, line, panelX + 12.0f, panelY + 124.0f, 14.0f, Color{ 205, 213, 220, 255 });
    DrawUiTextClipped(font, node.materialName.empty() ? "Material: None" : (std::string("Material: ") + node.materialName).c_str(), panelX + 120.0f, panelY + 124.0f, 14.0f, panelW - 132.0f, Color{ 205, 213, 220, 255 });
}

bool HasSceneNodeChildren(const LoadedFbxModel& loaded, int nodeIndex)
{
    for (const SceneNode& node : loaded.nodes)
    {
        if (node.parent == nodeIndex) return true;
    }
    return false;
}

bool HasVisibleSceneNodeChildren(const ModelTab& tab, int nodeIndex)
{
    for (int i = 0; i < static_cast<int>(tab.loaded.nodes.size()); ++i)
    {
        if (IsDeletedNode(tab, i)) continue;
        if (tab.loaded.nodes[static_cast<size_t>(i)].parent == nodeIndex) return true;
    }
    return false;
}

bool IsSceneNodeVisible(const LoadedFbxModel& loaded, const std::vector<bool>& collapsed, int nodeIndex)
{
    int parent = loaded.nodes[static_cast<size_t>(nodeIndex)].parent;
    while (parent >= 0)
    {
        if (parent < static_cast<int>(collapsed.size()) && collapsed[static_cast<size_t>(parent)])
        {
            return false;
        }
        parent = loaded.nodes[static_cast<size_t>(parent)].parent;
    }
    return true;
}

bool IsSceneNodeVisible(const ModelTab& tab, const std::vector<bool>& collapsed, int nodeIndex)
{
    if (IsDeletedNode(tab, nodeIndex)) return false;

    int parent = tab.loaded.nodes[static_cast<size_t>(nodeIndex)].parent;
    while (parent >= 0)
    {
        if (IsDeletedNode(tab, parent)) return false;
        if (parent < static_cast<int>(collapsed.size()) && collapsed[static_cast<size_t>(parent)])
        {
            return false;
        }
        parent = parent < static_cast<int>(tab.loaded.nodes.size()) ? tab.loaded.nodes[static_cast<size_t>(parent)].parent : -1;
    }
    return true;
}

float GetHierarchyPanelHeight()
{
    return static_cast<float>(GetScreenHeight()) - 61.0f - gBottomPanelReservedHeight;
}

float GetHierarchyContentStartY()
{
    return 61.0f + 64.0f;
}

int CountVisibleSceneNodes(const LoadedFbxModel& loaded, const std::vector<bool>& collapsed)
{
    int count = 0;
    for (int i = 0; i < static_cast<int>(loaded.nodes.size()); ++i)
    {
        if (IsSceneNodeVisible(loaded, collapsed, i)) ++count;
    }
    return count;
}

int CountVisibleSceneNodes(const ModelTab& tab, const std::vector<bool>& collapsed)
{
    int count = 0;
    for (int i = 0; i < static_cast<int>(tab.loaded.nodes.size()); ++i)
    {
        if (IsSceneNodeVisible(tab, collapsed, i)) ++count;
    }
    return count;
}

int GetVisibleSceneNodeRow(const LoadedFbxModel& loaded, const std::vector<bool>& collapsed, int nodeIndex)
{
    int row = 0;
    for (int i = 0; i < static_cast<int>(loaded.nodes.size()); ++i)
    {
        if (!IsSceneNodeVisible(loaded, collapsed, i)) continue;
        if (i == nodeIndex) return row;
        ++row;
    }
    return -1;
}

int GetVisibleSceneNodeRow(const ModelTab& tab, const std::vector<bool>& collapsed, int nodeIndex)
{
    int row = 0;
    for (int i = 0; i < static_cast<int>(tab.loaded.nodes.size()); ++i)
    {
        if (!IsSceneNodeVisible(tab, collapsed, i)) continue;
        if (i == nodeIndex) return row;
        ++row;
    }
    return -1;
}

void RevealNodeInHierarchy(ModelTab& tab, HierarchyPanelState& panel, int nodeIndex)
{
    if (nodeIndex < 0 || nodeIndex >= static_cast<int>(tab.loaded.nodes.size())) return;
    if (IsDeletedNode(tab, nodeIndex)) return;

    int parent = tab.loaded.nodes[static_cast<size_t>(nodeIndex)].parent;
    while (parent >= 0)
    {
        if (parent < static_cast<int>(tab.collapsedNodes.size()))
        {
            tab.collapsedNodes[static_cast<size_t>(parent)] = false;
        }
        parent = tab.loaded.nodes[static_cast<size_t>(parent)].parent;
    }

    const int row = GetVisibleSceneNodeRow(tab, tab.collapsedNodes, nodeIndex);
    if (row < 0) return;

    constexpr float rowH = 22.0f;
    const float panelH = GetHierarchyPanelHeight();
    const float visibleRows = std::max(1.0f, std::floor((panelH - 64.0f) / rowH));
    const float maxScroll = std::max(0.0f, static_cast<float>(CountVisibleSceneNodes(tab, tab.collapsedNodes)) - visibleRows);

    if (static_cast<float>(row) < panel.scroll)
    {
        panel.scroll = static_cast<float>(row);
    }
    else if (static_cast<float>(row) >= panel.scroll + visibleRows)
    {
        panel.scroll = static_cast<float>(row) - visibleRows + 1.0f;
    }

    panel.scroll = ClampFloat(panel.scroll, 0.0f, maxScroll);
}

void UpdateHierarchyPanelInteraction(HierarchyPanelState& panel, const ModelTab* active)
{
    constexpr float panelX = 0.0f;
    constexpr float panelY = 61.0f;
    constexpr float collapsedW = 28.0f;
    constexpr float rowH = 22.0f;
    const float panelH = GetHierarchyPanelHeight();
    const Vector2 mouse = GetMousePosition();

    if (panel.hidden)
    {
        const Rectangle restoreRect{ panelX, panelY, collapsedW, panelH };
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(mouse, restoreRect))
        {
            panel.hidden = false;
        }
        return;
    }

    panel.width = ClampFloat(panel.width, 220.0f, std::min(620.0f, static_cast<float>(GetScreenWidth()) - 160.0f));

    const Rectangle titleRect{ panelX, panelY, panel.width, 30.0f };
    const Rectangle resizeRect{ panel.width - 5.0f, panelY, 10.0f, panelH };

    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(mouse, resizeRect))
    {
        panel.resizing = true;
    }
    if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT))
    {
        panel.resizing = false;
    }
    if (panel.resizing)
    {
        panel.width = ClampFloat(mouse.x, 220.0f, std::min(620.0f, static_cast<float>(GetScreenWidth()) - 160.0f));
    }
    else if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(mouse, titleRect))
    {
        panel.hidden = true;
        return;
    }

    const Rectangle panelRect{ panelX, panelY, panel.width, panelH };
    if (active && panel.activeTab == LeftPanelTab::Hierarchy && CheckCollisionPointRec(mouse, panelRect))
    {
        const float wheel = GetMouseWheelMove();
        if (std::fabs(wheel) > 0.0f)
        {
            const float visibleRows = std::max(0.0f, std::floor((panelH - 64.0f) / rowH));
            const float maxScroll = std::max(0.0f, static_cast<float>(CountVisibleSceneNodes(*active, active->collapsedNodes)) - visibleRows);
            panel.scroll = ClampFloat(panel.scroll - wheel * 3.0f, 0.0f, maxScroll);
        }
    }
}

float GetHierarchyPanelBlockWidth(const HierarchyPanelState& panel)
{
    return panel.hidden ? 28.0f : panel.width;
}

struct SceneStats
{
    int nodes = 0;
    int meshes = 0;
    int bones = 0;
    int empties = 0;
    int vertices = 0;
    int triangles = 0;
    int materials = 0;
};

SceneStats CalculateSceneStats(const LoadedFbxModel& loaded)
{
    SceneStats stats;
    std::vector<std::string> materialNames;
    stats.nodes = static_cast<int>(loaded.nodes.size());

    for (const SceneNode& node : loaded.nodes)
    {
        switch (node.type)
        {
        case SceneNodeType::Mesh:
            ++stats.meshes;
            stats.vertices += node.meshVertexCount;
            stats.triangles += node.meshTriangleCount;
            if (!node.materialName.empty() && node.materialName != "None" &&
                std::find(materialNames.begin(), materialNames.end(), node.materialName) == materialNames.end())
            {
                materialNames.push_back(node.materialName);
            }
            break;
        case SceneNodeType::Bone:
            ++stats.bones;
            break;
        case SceneNodeType::Empty:
            ++stats.empties;
            break;
        }
    }

    stats.materials = static_cast<int>(materialNames.size());
    return stats;
}

struct SkeletonEntry
{
    std::string name;
    std::string parentName;
};

int FindNearestBoneParent(const LoadedFbxModel& loaded, int nodeIndex)
{
    int parent = nodeIndex >= 0 && nodeIndex < static_cast<int>(loaded.nodes.size()) ? loaded.nodes[static_cast<size_t>(nodeIndex)].parent : -1;
    while (parent >= 0 && parent < static_cast<int>(loaded.nodes.size()))
    {
        if (loaded.nodes[static_cast<size_t>(parent)].type == SceneNodeType::Bone) return parent;
        parent = loaded.nodes[static_cast<size_t>(parent)].parent;
    }
    return -1;
}

std::vector<SkeletonEntry> BuildSkeletonSignature(const LoadedFbxModel& loaded)
{
    std::vector<SkeletonEntry> entries;
    for (int i = 0; i < static_cast<int>(loaded.nodes.size()); ++i)
    {
        const SceneNode& node = loaded.nodes[static_cast<size_t>(i)];
        if (node.type != SceneNodeType::Bone) continue;

        const int parent = FindNearestBoneParent(loaded, i);
        entries.push_back(SkeletonEntry{
            node.name,
            parent >= 0 ? loaded.nodes[static_cast<size_t>(parent)].name : std::string{}
        });
    }
    return entries;
}

const SkeletonEntry* FindSkeletonEntry(const std::vector<SkeletonEntry>& entries, const std::string& name)
{
    for (const SkeletonEntry& entry : entries)
    {
        if (entry.name == name) return &entry;
    }
    return nullptr;
}

int CountDuplicateBoneNames(const std::vector<SkeletonEntry>& entries)
{
    int duplicates = 0;
    for (size_t i = 0; i < entries.size(); ++i)
    {
        for (size_t j = i + 1; j < entries.size(); ++j)
        {
            if (entries[i].name == entries[j].name)
            {
                ++duplicates;
                break;
            }
        }
    }
    return duplicates;
}

bool CompareSkeletonCompatibility(const LoadedFbxModel& base, const LoadedFbxModel& other, std::string& result)
{
    const std::vector<SkeletonEntry> baseBones = BuildSkeletonSignature(base);
    const std::vector<SkeletonEntry> otherBones = BuildSkeletonSignature(other);
    const int baseDuplicates = CountDuplicateBoneNames(baseBones);
    const int otherDuplicates = CountDuplicateBoneNames(otherBones);
    int missing = 0;
    int extra = 0;
    int parentMismatches = 0;

    for (const SkeletonEntry& baseEntry : baseBones)
    {
        const SkeletonEntry* otherEntry = FindSkeletonEntry(otherBones, baseEntry.name);
        if (!otherEntry)
        {
            ++missing;
            continue;
        }
        if (otherEntry->parentName != baseEntry.parentName)
        {
            ++parentMismatches;
        }
    }

    for (const SkeletonEntry& otherEntry : otherBones)
    {
        if (!FindSkeletonEntry(baseBones, otherEntry.name))
        {
            ++extra;
        }
    }

    const bool compatible = !baseBones.empty() &&
                            baseBones.size() == otherBones.size() &&
                            missing == 0 &&
                            extra == 0 &&
                            parentMismatches == 0 &&
                            baseDuplicates == 0 &&
                            otherDuplicates == 0;

    char line[512] = {};
    std::snprintf(line, sizeof(line),
                  "%s\nBase bones: %zu\nCompare bones: %zu\nMissing: %d\nExtra: %d\nParent mismatches: %d\nDuplicate names: %d / %d",
                  compatible ? "Compatible" : "Not compatible",
                  baseBones.size(),
                  otherBones.size(),
                  missing,
                  extra,
                  parentMismatches,
                  baseDuplicates,
                  otherDuplicates);
    result = line;
    return compatible;
}

std::unordered_map<std::string, int> BuildBoneNodeNameMap(const LoadedFbxModel& loaded)
{
    std::unordered_map<std::string, int> result;
    for (int i = 0; i < static_cast<int>(loaded.nodes.size()); ++i)
    {
        const SceneNode& node = loaded.nodes[static_cast<size_t>(i)];
        if (node.type == SceneNodeType::Bone && !node.name.empty())
        {
            result.emplace(node.name, i);
        }
    }
    return result;
}

std::string MakeUniqueClipName(const LoadedFbxModel& loaded, const std::string& desiredName)
{
    const std::string baseName = desiredName.empty() ? "Imported Animation" : desiredName;
    auto exists = [&](const std::string& name)
    {
        return std::any_of(loaded.animations.begin(), loaded.animations.end(), [&](const AnimationClip& clip) { return clip.name == name; });
    };

    if (!exists(baseName)) return baseName;
    for (int suffix = 2; suffix < 10000; ++suffix)
    {
        const std::string candidate = baseName + "_" + std::to_string(suffix);
        if (!exists(candidate)) return candidate;
    }
    return baseName + "_copy";
}

Matrix MatrixFromPose(const BonePose& pose)
{
    Matrix matrix = MatrixIdentity();
    matrix.m0 = pose.axisX.x * pose.scale.x;
    matrix.m1 = pose.axisX.y * pose.scale.x;
    matrix.m2 = pose.axisX.z * pose.scale.x;
    matrix.m4 = pose.axisY.x * pose.scale.y;
    matrix.m5 = pose.axisY.y * pose.scale.y;
    matrix.m6 = pose.axisY.z * pose.scale.y;
    matrix.m8 = pose.axisZ.x * pose.scale.z;
    matrix.m9 = pose.axisZ.y * pose.scale.z;
    matrix.m10 = pose.axisZ.z * pose.scale.z;
    matrix.m12 = pose.position.x;
    matrix.m13 = pose.position.y;
    matrix.m14 = pose.position.z;
    return matrix;
}

BonePose PoseFromMatrix(Matrix matrix, int nodeIndex)
{
    const Vector3 xColumn{ matrix.m0, matrix.m1, matrix.m2 };
    const Vector3 yColumn{ matrix.m4, matrix.m5, matrix.m6 };
    const Vector3 zColumn{ matrix.m8, matrix.m9, matrix.m10 };

    BonePose pose;
    pose.position = Vector3{ matrix.m12, matrix.m13, matrix.m14 };
    pose.scale = Vector3{
        std::max(0.000001f, Vector3Length(xColumn)),
        std::max(0.000001f, Vector3Length(yColumn)),
        std::max(0.000001f, Vector3Length(zColumn))
    };
    pose.axisX = NormalizeOrFallback(xColumn, Vector3{ 1.0f, 0.0f, 0.0f });
    pose.axisY = NormalizeOrFallback(yColumn, Vector3{ 0.0f, 1.0f, 0.0f });
    pose.axisZ = NormalizeOrFallback(zColumn, Vector3{ 0.0f, 0.0f, 1.0f });

    Matrix rotationMatrix = matrix;
    rotationMatrix.m0 = pose.axisX.x;
    rotationMatrix.m1 = pose.axisX.y;
    rotationMatrix.m2 = pose.axisX.z;
    rotationMatrix.m4 = pose.axisY.x;
    rotationMatrix.m5 = pose.axisY.y;
    rotationMatrix.m6 = pose.axisY.z;
    rotationMatrix.m8 = pose.axisZ.x;
    rotationMatrix.m9 = pose.axisZ.y;
    rotationMatrix.m10 = pose.axisZ.z;
    rotationMatrix.m12 = 0.0f;
    rotationMatrix.m13 = 0.0f;
    rotationMatrix.m14 = 0.0f;
    const Vector3 euler = QuaternionToEuler(QuaternionFromMatrix(rotationMatrix));
    pose.rotation = Vector3Scale(euler, RAD2DEG);
    pose.node = nodeIndex;
    return pose;
}

Matrix GetPoseMatrixByNode(const std::vector<Matrix>& matrices, int nodeIndex)
{
    if (nodeIndex >= 0 && nodeIndex < static_cast<int>(matrices.size())) return matrices[static_cast<size_t>(nodeIndex)];
    return MatrixIdentity();
}

const BonePose* FindFramePoseByNode(const BoneFrame& frame, int nodeIndex)
{
    for (const BonePose& pose : frame.poses)
    {
        if (pose.node == nodeIndex) return &pose;
    }
    return nullptr;
}

std::vector<Matrix> BuildBindPoseMatrices(const LoadedFbxModel& loaded)
{
    std::vector<Matrix> matrices(loaded.nodes.size(), MatrixIdentity());
    for (int i = 0; i < static_cast<int>(loaded.nodes.size()); ++i)
    {
        const SceneNode& node = loaded.nodes[static_cast<size_t>(i)];
        BonePose pose;
        pose.position = node.position;
        pose.axisX = node.axisX;
        pose.axisY = node.axisY;
        pose.axisZ = node.axisZ;
        pose.rotation = node.rotation;
        pose.scale = node.scale;
        pose.node = i;
        matrices[static_cast<size_t>(i)] = MatrixFromPose(pose);
    }

    for (const BonePose& pose : loaded.bonePoses)
    {
        if (pose.node >= 0 && pose.node < static_cast<int>(matrices.size()))
        {
            matrices[static_cast<size_t>(pose.node)] = MatrixFromPose(pose);
        }
    }
    return matrices;
}

std::vector<Matrix> BuildFramePoseMatrices(const LoadedFbxModel& loaded, const BoneFrame& frame)
{
    std::vector<Matrix> matrices = BuildBindPoseMatrices(loaded);
    for (const BonePose& pose : frame.poses)
    {
        if (pose.node >= 0 && pose.node < static_cast<int>(matrices.size()))
        {
            matrices[static_cast<size_t>(pose.node)] = MatrixFromPose(pose);
        }
    }
    return matrices;
}

BoneFrame RemapImportedBoneFrame(const BoneFrame& sourceFrame,
                                 const LoadedFbxModel& source,
                                 const LoadedFbxModel& target,
                                 const std::unordered_map<std::string, int>& sourceBoneNodesByName)
{
    BoneFrame frame;
    frame.time = sourceFrame.time;
    const std::vector<Matrix> sourceFrameMatrices = BuildFramePoseMatrices(source, sourceFrame);
    frame.poses.reserve(target.bonePoses.size());

    for (int targetNodeIndex = 0; targetNodeIndex < static_cast<int>(target.nodes.size()); ++targetNodeIndex)
    {
        const SceneNode& targetNode = target.nodes[static_cast<size_t>(targetNodeIndex)];
        if (targetNode.type != SceneNodeType::Bone) continue;

        const auto sourceNode = sourceBoneNodesByName.find(targetNode.name);
        if (sourceNode == sourceBoneNodesByName.end()) continue;

        const BonePose* sourcePose = FindFramePoseByNode(sourceFrame, sourceNode->second);
        BonePose remappedPose = sourcePose ? *sourcePose : PoseFromMatrix(GetPoseMatrixByNode(sourceFrameMatrices, sourceNode->second), targetNodeIndex);
        remappedPose.node = targetNodeIndex;
        frame.poses.push_back(remappedPose);
    }

    const std::vector<Matrix> targetFrameMatrices = BuildFramePoseMatrices(target, frame);
    frame.bones.reserve(target.bones.size());
    for (int targetNodeIndex = 0; targetNodeIndex < static_cast<int>(target.nodes.size()); ++targetNodeIndex)
    {
        const int parent = FindNearestBoneParent(target, targetNodeIndex);
        if (parent < 0) continue;
        if (target.nodes[static_cast<size_t>(targetNodeIndex)].type != SceneNodeType::Bone) continue;

        const Vector3 parentPosition = Vector3Transform(Vector3Zero(), GetPoseMatrixByNode(targetFrameMatrices, parent));
        const Vector3 nodePosition = Vector3Transform(Vector3Zero(), GetPoseMatrixByNode(targetFrameMatrices, targetNodeIndex));
        frame.bones.push_back(BoneSegment{ parentPosition, nodePosition, parent, targetNodeIndex });
    }

    return frame;
}

Vector3 TransformPosePoint(const BonePose& pose, Vector3 point)
{
    return Vector3Add(pose.position,
                      Vector3Add(Vector3Scale(pose.axisX, point.x * pose.scale.x),
                                 Vector3Add(Vector3Scale(pose.axisY, point.y * pose.scale.y),
                                            Vector3Scale(pose.axisZ, point.z * pose.scale.z))));
}

Vector3 TransformPoseVector(const BonePose& pose, Vector3 vector)
{
    return Vector3Add(Vector3Scale(pose.axisX, vector.x * pose.scale.x),
                      Vector3Add(Vector3Scale(pose.axisY, vector.y * pose.scale.y),
                                 Vector3Scale(pose.axisZ, vector.z * pose.scale.z)));
}

MeshFrame BuildSkinnedMeshFrame(const LoadedFbxModel& target, const BoneFrame& boneFrame)
{
    MeshFrame meshFrame;
    if (target.skinnedVertices.size() != target.bindVertices.size() / 3)
    {
        meshFrame.vertices = target.bindVertices;
        meshFrame.normals = target.bindNormals;
        return meshFrame;
    }

    std::unordered_map<std::string, const BonePose*> posesByName;
    for (const BonePose& pose : boneFrame.poses)
    {
        if (pose.node >= 0 && pose.node < static_cast<int>(target.nodes.size()))
        {
            posesByName[target.nodes[static_cast<size_t>(pose.node)].name] = &pose;
        }
    }

    meshFrame.vertices.reserve(target.bindVertices.size());
    meshFrame.normals.reserve(target.bindNormals.size());
    for (const SkinnedVertex& vertex : target.skinnedVertices)
    {
        Vector3 position{};
        Vector3 normal{};
        float totalWeight = 0.0f;

        for (const SkinnedVertexInfluence& influence : vertex.influences)
        {
            const auto pose = posesByName.find(influence.boneName);
            if (pose == posesByName.end() || !pose->second) continue;
            position = Vector3Add(position, Vector3Scale(TransformPosePoint(*pose->second, influence.bindPositionInBone), influence.weight));
            normal = Vector3Add(normal, Vector3Scale(TransformPoseVector(*pose->second, influence.bindNormalInBone), influence.weight));
            totalWeight += influence.weight;
        }

        if (totalWeight > 0.000001f)
        {
            position = Vector3Scale(position, 1.0f / totalWeight);
            normal = NormalizeOrFallback(Vector3Scale(normal, 1.0f / totalWeight), vertex.bindNormal);
        }
        else
        {
            position = vertex.bindPosition;
            normal = vertex.bindNormal;
        }

        meshFrame.vertices.push_back(position.x);
        meshFrame.vertices.push_back(position.y);
        meshFrame.vertices.push_back(position.z);
        meshFrame.normals.push_back(normal.x);
        meshFrame.normals.push_back(normal.y);
        meshFrame.normals.push_back(normal.z);
    }

    return meshFrame;
}

bool ImportAnimationsFromFbx(ModelTab& targetTab, const std::string& importPath, int& importedCount, std::string& error)
{
    importedCount = 0;
    error.clear();

    LoadedFbxModel source;
    if (!LoadFbxModel(importPath, source, error))
    {
        return false;
    }

    std::string compatibility;
    if (!CompareSkeletonCompatibility(targetTab.loaded, source, compatibility))
    {
        error = "Skeletons are not compatible.\n" + compatibility;
        UnloadFbxModel(source);
        return false;
    }

    const std::unordered_map<std::string, int> sourceBoneNodesByName = BuildBoneNodeNameMap(source);
    for (const AnimationClip& sourceClip : source.animations)
    {
        AnimationClip clip;
        clip.name = MakeUniqueClipName(targetTab.loaded, sourceClip.name);
        clip.duration = sourceClip.duration;
        clip.frames.reserve(sourceClip.frames.size());
        for (const BoneFrame& sourceFrame : sourceClip.frames)
        {
            clip.frames.push_back(RemapImportedBoneFrame(sourceFrame, source, targetTab.loaded, sourceBoneNodesByName));
        }
        clip.meshFrames.reserve(clip.frames.size());
        for (const BoneFrame& frame : clip.frames)
        {
            clip.meshFrames.push_back(BuildSkinnedMeshFrame(targetTab.loaded, frame));
        }

        targetTab.loaded.animations.push_back(std::move(clip));
        ++importedCount;
    }

    UnloadFbxModel(source);
    if (importedCount == 0)
    {
        error = "Compatible FBX loaded, but it contains no animation stacks.";
        return false;
    }

    return true;
}

const char* GetSceneNodeTypeJsonName(SceneNodeType type)
{
    switch (type)
    {
    case SceneNodeType::Mesh: return "mesh";
    case SceneNodeType::Bone: return "bone";
    case SceneNodeType::Empty: return "empty";
    }

    return "unknown";
}

void WriteJsonString(std::ostream& out, const std::string& value)
{
    out << '"';
    for (char c : value)
    {
        switch (c)
        {
        case '\\': out << "\\\\"; break;
        case '"': out << "\\\""; break;
        case '\b': out << "\\b"; break;
        case '\f': out << "\\f"; break;
        case '\n': out << "\\n"; break;
        case '\r': out << "\\r"; break;
        case '\t': out << "\\t"; break;
        default:
            if (static_cast<unsigned char>(c) < 0x20)
            {
                out << ' ';
            }
            else
            {
                out << c;
            }
            break;
        }
    }
    out << '"';
}

void WriteJsonVector3(std::ostream& out, Vector3 value)
{
    out << '[' << value.x << ", " << value.y << ", " << value.z << ']';
}

void WriteJsonBounds(std::ostream& out, const BoundingBox& bounds)
{
    out << "{ \"min\": ";
    WriteJsonVector3(out, bounds.min);
    out << ", \"max\": ";
    WriteJsonVector3(out, bounds.max);
    out << " }";
}

bool ExportFbxJson(const ModelTab& tab, std::string& outputPath, std::string& error)
{
    outputPath = tab.path + ".json";
    error.clear();

    std::ofstream out(outputPath, std::ios::out | std::ios::trunc);
    if (!out)
    {
        error = "Failed to write JSON: " + outputPath;
        return false;
    }

    const LoadedFbxModel& loaded = tab.loaded;
    const SceneStats stats = CalculateSceneStats(loaded);

    out << "{\n";
    out << "  \"source\": ";
    WriteJsonString(out, tab.path);
    out << ",\n";
    out << "  \"title\": ";
    WriteJsonString(out, tab.title);
    out << ",\n";
    out << "  \"bounds\": ";
    WriteJsonBounds(out, loaded.bounds);
    out << ",\n";
    out << "  \"stats\": {\n";
    out << "    \"nodes\": " << stats.nodes << ",\n";
    out << "    \"meshes\": " << stats.meshes << ",\n";
    out << "    \"bones\": " << stats.bones << ",\n";
    out << "    \"empties\": " << stats.empties << ",\n";
    out << "    \"vertices\": " << stats.vertices << ",\n";
    out << "    \"triangles\": " << stats.triangles << ",\n";
    out << "    \"materials\": " << stats.materials << "\n";
    out << "  },\n";

    out << "  \"nodes\": [\n";
    for (size_t i = 0; i < loaded.nodes.size(); ++i)
    {
        const SceneNode& node = loaded.nodes[i];
        out << "    {\n";
        out << "      \"index\": " << i << ",\n";
        out << "      \"name\": ";
        WriteJsonString(out, node.name);
        out << ",\n";
        out << "      \"type\": \"" << GetSceneNodeTypeJsonName(node.type) << "\",\n";
        out << "      \"parent\": " << node.parent << ",\n";
        out << "      \"depth\": " << node.depth << ",\n";
        out << "      \"position\": ";
        WriteJsonVector3(out, node.position);
        out << ",\n";
        out << "      \"rotation\": ";
        WriteJsonVector3(out, node.rotation);
        out << ",\n";
        out << "      \"scale\": ";
        WriteJsonVector3(out, node.scale);
        out << ",\n";
        out << "      \"axisX\": ";
        WriteJsonVector3(out, node.axisX);
        out << ",\n";
        out << "      \"axisY\": ";
        WriteJsonVector3(out, node.axisY);
        out << ",\n";
        out << "      \"axisZ\": ";
        WriteJsonVector3(out, node.axisZ);
        out << ",\n";
        out << "      \"hasBounds\": " << (node.hasBounds ? "true" : "false") << ",\n";
        out << "      \"bounds\": ";
        WriteJsonBounds(out, node.bounds);
        out << ",\n";
        out << "      \"meshVertexStart\": " << node.meshVertexStart << ",\n";
        out << "      \"meshVertexCount\": " << node.meshVertexCount << ",\n";
        out << "      \"meshTriangleCount\": " << node.meshTriangleCount << ",\n";
        out << "      \"material\": ";
        WriteJsonString(out, node.materialName);
        out << "\n";
        out << "    }" << (i + 1 < loaded.nodes.size() ? "," : "") << "\n";
    }
    out << "  ],\n";

    out << "  \"bones\": [\n";
    for (size_t i = 0; i < loaded.bones.size(); ++i)
    {
        const BoneSegment& bone = loaded.bones[i];
        out << "    { \"startNode\": " << bone.startNode << ", \"endNode\": " << bone.endNode << ", \"start\": ";
        WriteJsonVector3(out, bone.start);
        out << ", \"end\": ";
        WriteJsonVector3(out, bone.end);
        out << " }" << (i + 1 < loaded.bones.size() ? "," : "") << "\n";
    }
    out << "  ],\n";

    out << "  \"animations\": [\n";
    for (size_t i = 0; i < loaded.animations.size(); ++i)
    {
        const AnimationClip& clip = loaded.animations[i];
        out << "    {\n";
        out << "      \"name\": ";
        WriteJsonString(out, clip.name);
        out << ",\n";
        out << "      \"duration\": " << clip.duration << ",\n";
        out << "      \"boneFrames\": " << clip.frames.size() << ",\n";
        out << "      \"meshFrames\": " << clip.meshFrames.size() << ",\n";
        out << "      \"totalFrames\": " << std::max(clip.frames.size(), clip.meshFrames.size()) << "\n";
        out << "    }" << (i + 1 < loaded.animations.size() ? "," : "") << "\n";
    }
    out << "  ]\n";
    out << "}\n";

    if (!out)
    {
        error = "Failed to finish writing JSON: " + outputPath;
        return false;
    }

    return true;
}

std::string ToLower(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

std::string CompactName(std::string value)
{
    value = ToLower(std::move(value));
    value.erase(std::remove_if(value.begin(), value.end(), [](unsigned char c)
    {
        return !std::isalnum(c);
    }), value.end());
    return value;
}

bool LowerStemContainsName(const std::string& lowerStem, const std::string& lowerName)
{
    if (lowerName.empty()) return false;
    if (lowerStem.find(lowerName) != std::string::npos) return true;

    const std::string compactStem = CompactName(lowerStem);
    const std::string compactName = CompactName(lowerName);
    return !compactName.empty() && compactStem.find(compactName) != std::string::npos;
}

bool IsTextureExtension(const std::filesystem::path& path)
{
    const std::string extension = ToLower(path.extension().string());
    return extension == ".png" || extension == ".jpg" || extension == ".jpeg" ||
           extension == ".tga" || extension == ".bmp" || extension == ".psd" ||
           extension == ".gif" || extension == ".hdr";
}

bool IsOrmTextureName(const std::string& lowerStem)
{
    const std::string compactStem = CompactName(lowerStem);
    auto hasToken = [&](const char* token)
    {
        const std::string value(token);
        size_t position = lowerStem.find(value);
        while (position != std::string::npos)
        {
            const bool startsOnBoundary = position == 0 || !std::isalnum(static_cast<unsigned char>(lowerStem[position - 1]));
            const size_t end = position + value.size();
            const bool endsOnBoundary = end >= lowerStem.size() || !std::isalnum(static_cast<unsigned char>(lowerStem[end]));
            if (startsOnBoundary && endsOnBoundary) return true;
            position = lowerStem.find(value, position + 1);
        }
        return false;
    };

    return hasToken("orm") ||
           lowerStem.find("occlusionroughnessmetallic") != std::string::npos ||
           lowerStem.find("occlusion_roughness_metallic") != std::string::npos ||
           lowerStem.find("occlusion-roughness-metallic") != std::string::npos ||
           lowerStem.find("ao_roughness_metallic") != std::string::npos ||
           lowerStem.find("ao-roughness-metallic") != std::string::npos ||
           compactStem.find("occlusionroughnessmetallic") != std::string::npos ||
           compactStem.find("aoroughnessmetallic") != std::string::npos;
}

int LoadOrmTexture(ModelTab& tab, int materialIndex, const std::string& path, std::string& error)
{
    int loadedCount = 0;
    error.clear();

    for (PbrTextureSlot slot : { PbrTextureSlot::AmbientOcclusion, PbrTextureSlot::Roughness, PbrTextureSlot::Metallic })
    {
        std::string loadError;
        if (LoadPbrTexture(tab, materialIndex, slot, path, loadError))
        {
            ++loadedCount;
        }
        else if (error.empty())
        {
            error = loadError;
        }
    }

    return loadedCount;
}

int ScoreTextureCandidate(const std::string& lowerStem, const std::string& lowerModelStem, const std::string& lowerMaterialName, PbrTextureSlot slot)
{
    int score = !lowerModelStem.empty() && LowerStemContainsName(lowerStem, lowerModelStem) ? 3 : 0;
    if (LowerStemContainsName(lowerStem, lowerMaterialName)) score += 5;

    auto hasAny = [&](std::initializer_list<const char*> tokens)
    {
        for (const char* token : tokens)
        {
            if (lowerStem.find(token) != std::string::npos) return true;
        }
        return false;
    };

    switch (slot)
    {
    case PbrTextureSlot::Diffuse:
        if (hasAny({ "diffuse", "albedo", "basecolor", "base_color", "_col", "color" })) score += 10;
        if (hasAny({ "normal", "nrm", "rough", "metal", "ao", "occlusion", "opacity", "alpha", "transparency", "mask" })) score -= 8;
        break;
    case PbrTextureSlot::Normal:
        if (hasAny({ "normal", "_nrm", "_nor" })) score += 10;
        break;
    case PbrTextureSlot::Roughness:
        if (hasAny({ "roughness", "_rough", "_rgh" })) score += 10;
        if (IsOrmTextureName(lowerStem)) score += 12;
        break;
    case PbrTextureSlot::Metallic:
        if (hasAny({ "metallic", "metalness", "_metal", "_mtl" })) score += 10;
        if (IsOrmTextureName(lowerStem)) score += 12;
        break;
    case PbrTextureSlot::AmbientOcclusion:
        if (hasAny({ "_ao", "ambientocclusion", "ambient_occlusion", "occlusion" })) score += 10;
        if (IsOrmTextureName(lowerStem)) score += 12;
        break;
    case PbrTextureSlot::Emissive:
        if (hasAny({ "emissive", "emission", "emit", "_ems", "_emiss" })) score += 10;
        break;
    case PbrTextureSlot::Opacity:
        if (hasAny({ "opacity", "alpha", "transparency", "_trans", "_mask", "cutout" })) score += 10;
        break;
    case PbrTextureSlot::Count:
        break;
    }

    return score;
}

bool TextureNameMatchesMaterial(const std::filesystem::path& texturePath, const std::string& materialName)
{
    return LowerStemContainsName(ToLower(texturePath.stem().string()), ToLower(materialName));
}

std::string FindAutoTexturePath(const std::string& modelPath, const std::filesystem::path& directory, const std::string& materialName, PbrTextureSlot slot)
{
    const std::filesystem::path sourcePath(modelPath);
    if (directory.empty() || !std::filesystem::exists(directory)) return {};

    const std::string modelStem = ToLower(sourcePath.stem().string());
    const std::string materialStem = ToLower(materialName);
    int bestScore = 0;
    std::filesystem::path bestPath;

    for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(directory))
    {
        if (!entry.is_regular_file() || !IsTextureExtension(entry.path())) continue;

        const std::string stem = ToLower(entry.path().stem().string());
        const int score = ScoreTextureCandidate(stem, modelStem, materialStem, slot);
        if (score > bestScore)
        {
            bestScore = score;
            bestPath = entry.path();
        }
    }

    return bestScore > 0 ? bestPath.string() : std::string{};
}

int AutoAssignDroppedTextures(ModelTab& tab, const std::vector<std::string>& droppedPaths, std::string& notice, std::string& error)
{
    EnsurePbrMaterialStates(tab);
    if (tab.pbrMaterials.empty()) return 0;

    const std::string modelStem = ToLower(std::filesystem::path(tab.path).stem().string());
    const bool singleMaterial = tab.pbrMaterials.size() == 1;
    int loadedCount = 0;
    std::string firstError;

    for (const std::string& droppedPath : droppedPaths)
    {
        const std::filesystem::path texturePath(droppedPath);
        if (!IsTextureExtension(texturePath)) continue;

        const std::string stem = ToLower(texturePath.stem().string());
        const bool isOrmTexture = IsOrmTextureName(stem);
        int bestMaterial = -1;
        PbrTextureSlot bestSlot = PbrTextureSlot::Diffuse;
        int bestScore = 0;

        for (int materialIndex = 0; materialIndex < static_cast<int>(tab.pbrMaterials.size()); ++materialIndex)
        {
            const std::string materialName = materialIndex < static_cast<int>(tab.loaded.materialNames.size()) ? tab.loaded.materialNames[static_cast<size_t>(materialIndex)] : std::string{};
            if (!singleMaterial && !TextureNameMatchesMaterial(texturePath, materialName)) continue;

            const std::string lowerMaterialName = ToLower(materialName);
            for (int slotIndex = 0; slotIndex < static_cast<int>(PbrTextureSlot::Count); ++slotIndex)
            {
                const PbrTextureSlot slot = static_cast<PbrTextureSlot>(slotIndex);
                const int score = ScoreTextureCandidate(stem, modelStem, lowerMaterialName, slot);
                if (score > bestScore)
                {
                    bestScore = score;
                    bestMaterial = materialIndex;
                    bestSlot = slot;
                }
            }
        }

        if (bestMaterial < 0 || bestScore < 8) continue;

        std::string loadError;
        if (isOrmTexture)
        {
            const int ormLoadedCount = LoadOrmTexture(tab, bestMaterial, droppedPath, loadError);
            if (ormLoadedCount > 0)
            {
                loadedCount += ormLoadedCount;
                tab.selectedMaterial = bestMaterial;
            }
            else if (firstError.empty())
            {
                firstError = loadError;
            }
        }
        else if (LoadPbrTexture(tab, bestMaterial, bestSlot, droppedPath, loadError))
        {
            ++loadedCount;
            tab.selectedMaterial = bestMaterial;
        }
        else if (firstError.empty())
        {
            firstError = loadError;
        }
    }

    if (loadedCount > 0)
    {
        char message[160] = {};
        std::snprintf(message, sizeof(message), "Auto-assigned %d texture slot%s by name.", loadedCount, loadedCount == 1 ? "" : "s");
        notice = message;
        error.clear();
    }
    else if (!firstError.empty())
    {
        error = firstError;
        notice.clear();
    }

    return loadedCount;
}

int LoadPbrTexturesFromFolder(ModelTab& tab, int materialIndex, const std::filesystem::path& directory, std::string& error)
{
    int loadedCount = 0;
    error.clear();
    EnsurePbrMaterialStates(tab);
    const std::string materialName = materialIndex >= 0 && materialIndex < static_cast<int>(tab.loaded.materialNames.size()) ? tab.loaded.materialNames[static_cast<size_t>(materialIndex)] : std::string{};
    for (int i = 0; i < static_cast<int>(PbrTextureSlot::Count); ++i)
    {
        const PbrTextureSlot slot = static_cast<PbrTextureSlot>(i);
        const std::string path = FindAutoTexturePath(tab.path, directory, materialName, slot);
        if (path.empty()) continue;

        std::string loadError;
        if (LoadPbrTexture(tab, materialIndex, slot, path, loadError))
        {
            ++loadedCount;
        }
        else if (error.empty())
        {
            error = loadError;
        }
    }

    return loadedCount;
}

bool DrawPanelTab(Font font, Rectangle bounds, const char* label, bool selected)
{
    const Vector2 mouse = GetMousePosition();
    const bool hovered = CheckCollisionPointRec(mouse, bounds);
    DrawRectangleRec(bounds, selected ? Color{ 48, 70, 92, 255 } : hovered ? Color{ 34, 39, 45, 255 } : Color{ 24, 27, 31, 245 });
    const float fontSize = MeasureTextEx(font, label, 14.0f, 1.0f).x > bounds.width - 10.0f ? 12.0f : 14.0f;
    DrawUiTextClipped(font, label, bounds.x + 5.0f, bounds.y + 5.0f, fontSize, bounds.width - 10.0f, selected ? RAYWHITE : Color{ 180, 190, 200, 255 });
    return hovered && IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
}

bool DrawPanelButton(Font font, Rectangle bounds, const char* label)
{
    const Vector2 mouse = GetMousePosition();
    const bool hovered = CheckCollisionPointRec(mouse, bounds);
    DrawRectangleRec(bounds, hovered ? Color{ 54, 63, 72, 255 } : Color{ 35, 40, 46, 255 });
    DrawRectangleLinesEx(bounds, 1.0f, Color{ 70, 80, 90, 255 });
    const Vector2 size = MeasureTextEx(font, label, 14.0f, 1.0f);
    DrawUiText(font, label, bounds.x + (bounds.width - size.x) * 0.5f, bounds.y + 4.0f, 14.0f, Color{ 205, 213, 220, 255 });
    return hovered && IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
}

Color GetPackedChannelColor(PackedChannel channel, bool hovered)
{
    switch (channel)
    {
    case PackedChannel::R: return hovered ? Color{ 185, 56, 56, 255 } : Color{ 140, 42, 42, 255 };
    case PackedChannel::G: return hovered ? Color{ 62, 160, 76, 255 } : Color{ 45, 118, 58, 255 };
    case PackedChannel::B: return hovered ? Color{ 66, 106, 200, 255 } : Color{ 50, 78, 152, 255 };
    }

    return hovered ? Color{ 54, 63, 72, 255 } : Color{ 35, 40, 46, 255 };
}

bool DrawChannelButton(Font font, Rectangle bounds, PackedChannel channel)
{
    const Vector2 mouse = GetMousePosition();
    const bool hovered = CheckCollisionPointRec(mouse, bounds);
    DrawRectangleRec(bounds, GetPackedChannelColor(channel, hovered));
    DrawRectangleLinesEx(bounds, 1.0f, Color{ 205, 213, 220, 120 });
    const char* label = GetPackedChannelName(channel);
    const Vector2 size = MeasureTextEx(font, label, 14.0f, 1.0f);
    DrawUiText(font, label, bounds.x + (bounds.width - size.x) * 0.5f, bounds.y + 4.0f, 14.0f, RAYWHITE);
    return hovered && IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
}

void DrawTextureThumbnail(Font font, Rectangle bounds, const PbrTexture& texture, bool dropTarget)
{
    DrawRectangleRec(bounds, dropTarget ? Color{ 42, 62, 74, 255 } : Color{ 18, 21, 25, 255 });
    if (texture.loaded)
    {
        const Rectangle source{ 0.0f, 0.0f, static_cast<float>(texture.texture.width), static_cast<float>(texture.texture.height) };
        DrawTexturePro(texture.texture, source, Rectangle{ bounds.x + 2.0f, bounds.y + 2.0f, bounds.width - 4.0f, bounds.height - 4.0f }, Vector2{}, 0.0f, WHITE);
    }
    else
    {
        const Vector2 size = MeasureTextEx(font, "-", 18.0f, 1.0f);
        DrawUiText(font, "-", bounds.x + (bounds.width - size.x) * 0.5f, bounds.y + (bounds.height - size.y) * 0.5f, 18.0f, Color{ 120, 132, 144, 255 });
    }
    DrawRectangleLinesEx(bounds, 1.0f, dropTarget ? Color{ 120, 190, 230, 255 } : Color{ 70, 80, 90, 255 });
}

void DrawTextureContextMenu(Font font, ModelTab& tab, TextureClipboard& clipboard, std::string& notice, std::string& error)
{
    if (!clipboard.menuOpen) return;

    const Rectangle menu{ clipboard.position.x, clipboard.position.y, 116.0f, 62.0f };
    DrawRectangleRec(menu, Color{ 24, 27, 31, 248 });
    DrawRectangleLinesEx(menu, 1.0f, Color{ 80, 90, 100, 255 });

    const Rectangle copyButton{ menu.x, menu.y, menu.width, 30.0f };
    const Rectangle pasteButton{ menu.x, menu.y + 30.0f, menu.width, 30.0f };
    const bool validMaterial = clipboard.materialIndex >= 0 && clipboard.materialIndex < static_cast<int>(tab.pbrMaterials.size());
    const PbrTexture* texture = validMaterial ? &GetPbrTexture(tab.pbrMaterials[static_cast<size_t>(clipboard.materialIndex)], clipboard.slot) : nullptr;
    const bool canCopy = texture && texture->loaded;
    const bool canPaste = !clipboard.path.empty();

    if (clipboard.justOpened)
    {
        clipboard.justOpened = false;
    }
    else
    {
        if (canCopy && DrawPanelButton(font, copyButton, "Copy"))
        {
            clipboard.path = texture->path;
            clipboard.menuOpen = false;
            notice = "Copied texture: " + std::string(GetFileName(clipboard.path.c_str()));
            error.clear();
            return;
        }
        if (canPaste && DrawPanelButton(font, pasteButton, "Paste"))
        {
            EditSnapshot before = CaptureEditSnapshot(tab);
            std::string loadError;
            if (LoadPbrTexture(tab, clipboard.materialIndex, clipboard.slot, clipboard.path, loadError))
            {
                PushUndoSnapshot(tab, std::move(before));
                notice = std::string("Pasted texture to ") + GetPbrTextureSlotName(clipboard.slot) + ".";
                error.clear();
            }
            else
            {
                error = loadError;
                notice.clear();
            }
            clipboard.menuOpen = false;
            return;
        }
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && !CheckCollisionPointRec(GetMousePosition(), menu))
        {
            clipboard.menuOpen = false;
        }
    }

    if (!canCopy)
    {
        DrawRectangleRec(copyButton, Color{ 28, 31, 35, 245 });
        DrawUiText(font, "Copy", copyButton.x + 10.0f, copyButton.y + 5.0f, 16.0f, Color{ 105, 115, 124, 255 });
    }
    if (!canPaste)
    {
        DrawRectangleRec(pasteButton, Color{ 28, 31, 35, 245 });
        DrawUiText(font, "Paste", pasteButton.x + 10.0f, pasteButton.y + 5.0f, 16.0f, Color{ 105, 115, 124, 255 });
    }
}

bool IsFiniteFloat(float value)
{
    return std::isfinite(value);
}

bool IsFiniteVector(Vector3 value)
{
    return IsFiniteFloat(value.x) && IsFiniteFloat(value.y) && IsFiniteFloat(value.z);
}

void AddValidationIssue(std::vector<ValidatorIssue>& issues, ValidatorSeverity severity, const std::string& category, const std::string& message, int node = -1)
{
    issues.push_back(ValidatorIssue{ severity, category, message, node });
}

int ValidatorSeverityRank(ValidatorSeverity severity)
{
    switch (severity)
    {
    case ValidatorSeverity::Error: return 2;
    case ValidatorSeverity::Warning: return 1;
    case ValidatorSeverity::Info: return 0;
    }
    return 0;
}

const char* GetValidatorSeverityName(ValidatorSeverity severity)
{
    switch (severity)
    {
    case ValidatorSeverity::Error: return "ERR";
    case ValidatorSeverity::Warning: return "WARN";
    case ValidatorSeverity::Info: return "INFO";
    }
    return "INFO";
}

Color GetValidatorSeverityColor(ValidatorSeverity severity)
{
    switch (severity)
    {
    case ValidatorSeverity::Error: return Color{ 216, 92, 92, 255 };
    case ValidatorSeverity::Warning: return Color{ 220, 168, 72, 255 };
    case ValidatorSeverity::Info: return Color{ 104, 168, 220, 255 };
    }
    return Color{ 104, 168, 220, 255 };
}

std::string GetMaterialDisplayName(const LoadedFbxModel& loaded, int materialIndex)
{
    if (materialIndex >= 0 && materialIndex < static_cast<int>(loaded.materialNames.size()))
    {
        return loaded.materialNames[static_cast<size_t>(materialIndex)];
    }
    return std::string("Material ") + std::to_string(materialIndex + 1);
}

void ValidateTextures(const ModelTab& tab, std::vector<ValidatorIssue>& issues)
{
    const int materialCount = tab.loaded.hasMesh ? std::max(1, tab.loaded.model.materialCount) : 0;
    int missingDiffuse = 0;
    int missingNormal = 0;

    for (int materialIndex = 0; materialIndex < materialCount; ++materialIndex)
    {
        const PbrMaterialState* material = materialIndex < static_cast<int>(tab.pbrMaterials.size()) ? &tab.pbrMaterials[static_cast<size_t>(materialIndex)] : nullptr;
        if (!material || !GetPbrTexture(*material, PbrTextureSlot::Diffuse).loaded)
        {
            ++missingDiffuse;
        }
        if (!material || !GetPbrTexture(*material, PbrTextureSlot::Normal).loaded)
        {
            ++missingNormal;
        }

        if (!material) continue;

        for (int slotIndex = 0; slotIndex < static_cast<int>(PbrTextureSlot::Count); ++slotIndex)
        {
            const PbrTextureSlot slot = static_cast<PbrTextureSlot>(slotIndex);
            const PbrTexture& texture = GetPbrTexture(*material, slot);
            if (texture.path.empty()) continue;

            std::error_code existsError;
            const bool sourceExists = std::filesystem::exists(texture.path, existsError);
            if (!texture.loaded || texture.texture.id == 0 || texture.texture.width <= 0 || texture.texture.height <= 0 || !sourceExists)
            {
                AddValidationIssue(issues,
                                   ValidatorSeverity::Error,
                                   "Broken texture",
                                   GetMaterialDisplayName(tab.loaded, materialIndex) + " " + GetPbrTextureSlotName(slot) + ": " + texture.path);
            }
        }
    }

    if (missingDiffuse > 0)
    {
        AddValidationIssue(issues, ValidatorSeverity::Warning, "Missing texture", std::to_string(missingDiffuse) + " material(s) have no diffuse texture assigned.");
    }
    if (missingNormal > 0)
    {
        AddValidationIssue(issues, ValidatorSeverity::Info, "Missing texture", std::to_string(missingNormal) + " material(s) have no normal texture assigned.");
    }
}

void ValidateDuplicateNames(const LoadedFbxModel& loaded, std::vector<ValidatorIssue>& issues)
{
    std::unordered_map<std::string, int> nodeNames;
    for (int i = 0; i < static_cast<int>(loaded.nodes.size()); ++i)
    {
        const std::string& name = loaded.nodes[static_cast<size_t>(i)].name;
        if (name.empty()) continue;
        const int count = ++nodeNames[name];
        if (count == 2)
        {
            AddValidationIssue(issues, ValidatorSeverity::Warning, "Duplicate name", "Duplicate node name: " + name, i);
        }
    }

    std::unordered_map<std::string, int> materialNames;
    for (const std::string& name : loaded.materialNames)
    {
        if (!name.empty() && ++materialNames[name] == 2)
        {
            AddValidationIssue(issues, ValidatorSeverity::Warning, "Duplicate name", "Duplicate material name: " + name);
        }
    }

    std::unordered_map<std::string, int> animationNames;
    for (const AnimationClip& clip : loaded.animations)
    {
        if (!clip.name.empty() && ++animationNames[clip.name] == 2)
        {
            AddValidationIssue(issues, ValidatorSeverity::Warning, "Duplicate name", "Duplicate animation name: " + clip.name);
        }
    }
}

void ValidateTransforms(const LoadedFbxModel& loaded, std::vector<ValidatorIssue>& issues)
{
    for (int i = 0; i < static_cast<int>(loaded.nodes.size()); ++i)
    {
        const SceneNode& node = loaded.nodes[static_cast<size_t>(i)];
        if (!IsFiniteVector(node.position) || !IsFiniteVector(node.rotation) || !IsFiniteVector(node.scale) ||
            !IsFiniteVector(node.axisX) || !IsFiniteVector(node.axisY) || !IsFiniteVector(node.axisZ))
        {
            AddValidationIssue(issues, ValidatorSeverity::Error, "Invalid transform", "Non-finite transform values on " + node.name, i);
            continue;
        }

        if (std::fabs(node.scale.x) <= 0.000001f || std::fabs(node.scale.y) <= 0.000001f || std::fabs(node.scale.z) <= 0.000001f)
        {
            AddValidationIssue(issues, ValidatorSeverity::Error, "Invalid transform", "Zero or near-zero scale on " + node.name, i);
        }

        const float axisXLength = Vector3Length(node.axisX);
        const float axisYLength = Vector3Length(node.axisY);
        const float axisZLength = Vector3Length(node.axisZ);
        if (axisXLength <= 0.000001f || axisYLength <= 0.000001f || axisZLength <= 0.000001f)
        {
            AddValidationIssue(issues, ValidatorSeverity::Error, "Invalid transform", "Zero transform axis on " + node.name, i);
        }
        else if (std::fabs(axisXLength - 1.0f) > 0.02f || std::fabs(axisYLength - 1.0f) > 0.02f || std::fabs(axisZLength - 1.0f) > 0.02f ||
                 std::fabs(Vector3DotProduct(node.axisX, node.axisY)) > 0.03f ||
                 std::fabs(Vector3DotProduct(node.axisX, node.axisZ)) > 0.03f ||
                 std::fabs(Vector3DotProduct(node.axisY, node.axisZ)) > 0.03f)
        {
            AddValidationIssue(issues, ValidatorSeverity::Warning, "Invalid transform", "Skewed or non-orthogonal axes on " + node.name, i);
        }
    }
}

void ValidateGeometry(const LoadedFbxModel& loaded, std::vector<ValidatorIssue>& issues)
{
    if (loaded.hasMesh && loaded.bindNormals.size() != loaded.bindVertices.size())
    {
        AddValidationIssue(issues, ValidatorSeverity::Error, "Missing normals", "Normal buffer length does not match vertex buffer length.");
    }
    if (loaded.hasMesh && loaded.uvSets.empty())
    {
        AddValidationIssue(issues, ValidatorSeverity::Warning, "Missing UVs", "Model has no UV sets.");
    }

    for (int i = 0; i < static_cast<int>(loaded.nodes.size()); ++i)
    {
        const SceneNode& node = loaded.nodes[static_cast<size_t>(i)];
        if (node.type != SceneNodeType::Mesh) continue;

        if (node.meshVertexCount <= 0)
        {
            AddValidationIssue(issues, ValidatorSeverity::Error, "Empty mesh", node.name + " contains no triangles.", i);
            continue;
        }
        if (!node.meshHadUvs)
        {
            AddValidationIssue(issues, ValidatorSeverity::Warning, "Missing UVs", node.name + " has missing or unmapped UV data.", i);
        }
        if (!node.meshHadNormals)
        {
            AddValidationIssue(issues, ValidatorSeverity::Warning, "Missing normals", node.name + " had missing authored normals.", i);
        }
        if (node.degenerateTriangleCount > 0)
        {
            AddValidationIssue(issues, ValidatorSeverity::Error, "Degenerate triangles", node.name + " has " + std::to_string(node.degenerateTriangleCount) + " degenerate triangle(s).", i);
        }
    }
}

void ValidateSkinning(const LoadedFbxModel& loaded, std::vector<ValidatorIssue>& issues)
{
    bool hasSkinnedMesh = false;
    std::unordered_map<std::string, bool> influencedBones;
    for (const SkinnedVertex& vertex : loaded.skinnedVertices)
    {
        if (!vertex.influences.empty()) hasSkinnedMesh = true;
        for (const SkinnedVertexInfluence& influence : vertex.influences)
        {
            if (!influence.boneName.empty())
            {
                influencedBones[influence.boneName] = true;
            }
        }
    }

    for (int i = 0; i < static_cast<int>(loaded.nodes.size()); ++i)
    {
        const SceneNode& node = loaded.nodes[static_cast<size_t>(i)];
        if (node.type != SceneNodeType::Mesh) continue;

        if (node.badSkinWeightCount > 0)
        {
            AddValidationIssue(issues, ValidatorSeverity::Error, "Bad weights", node.name + " has " + std::to_string(node.badSkinWeightCount) + " vertex weight sum(s) outside 1.0.", i);
        }
        if (node.missingSkinWeightCount > 0)
        {
            AddValidationIssue(issues, ValidatorSeverity::Error, "Bad weights", node.name + " has " + std::to_string(node.missingSkinWeightCount) + " skinned vertex/vertices with no weights.", i);
        }
    }

    if (!hasSkinnedMesh) return;

    if (loaded.bonePoses.empty())
    {
        AddValidationIssue(issues, ValidatorSeverity::Error, "Missing bind poses", "Skinned mesh has no skeleton bind poses.");
    }

    for (int i = 0; i < static_cast<int>(loaded.nodes.size()); ++i)
    {
        const SceneNode& node = loaded.nodes[static_cast<size_t>(i)];
        if (node.type == SceneNodeType::Bone && influencedBones.find(node.name) != influencedBones.end() && !node.hasSkinBindPose)
        {
            AddValidationIssue(issues, ValidatorSeverity::Error, "Missing bind poses", "Influenced bone has no explicit skin bind pose: " + node.name, i);
        }
    }
}

void ValidateTexelDensityConsistency(const ModelTab& tab, std::vector<ValidatorIssue>& issues);

std::vector<ValidatorIssue> BuildValidationIssues(const ModelTab& tab)
{
    std::vector<ValidatorIssue> issues;
    ValidateTextures(tab, issues);
    ValidateDuplicateNames(tab.loaded, issues);
    ValidateTransforms(tab.loaded, issues);
    ValidateGeometry(tab.loaded, issues);
    ValidateTexelDensityConsistency(tab, issues);
    ValidateSkinning(tab.loaded, issues);

    std::stable_sort(issues.begin(), issues.end(), [](const ValidatorIssue& a, const ValidatorIssue& b)
    {
        return ValidatorSeverityRank(a.severity) > ValidatorSeverityRank(b.severity);
    });
    return issues;
}

void DrawValidatorPanel(Font font, ModelTab& tab, HierarchyPanelState& panel, float panelX, float panelY, float panelW)
{
    std::vector<ValidatorIssue> issues = BuildValidationIssues(tab);
    const float contentX = panelX + 12.0f;
    const float panelH = GetHierarchyPanelHeight();
    float y = panelY;

    int errors = 0;
    int warnings = 0;
    int infos = 0;
    for (const ValidatorIssue& issue : issues)
    {
        if (issue.severity == ValidatorSeverity::Error) ++errors;
        else if (issue.severity == ValidatorSeverity::Warning) ++warnings;
        else ++infos;
    }

    DrawUiText(font, "VALIDATOR", contentX, y, 16.0f, Color{ 165, 182, 196, 255 });
    y += 28.0f;

    char summary[192] = {};
    std::snprintf(summary, sizeof(summary), "%d errors    %d warnings    %d info", errors, warnings, infos);
    DrawUiTextClipped(font, summary, contentX, y, 14.0f, panelW - 24.0f, Color{ 205, 213, 220, 255 });
    y += 28.0f;

    if (issues.empty())
    {
        DrawUiTextClipped(font, "No validation issues found.", contentX, y, 14.0f, panelW - 24.0f, Color{ 128, 140, 152, 255 });
        panel.validatorScroll = 0.0f;
        return;
    }

    constexpr float rowH = 54.0f;
    const Rectangle listBounds{ contentX, y, panelW - 24.0f, std::max(40.0f, panelH - (y - 61.0f) - 10.0f) };
    const float visibleRows = std::max(1.0f, std::floor(listBounds.height / rowH));
    const float maxScroll = std::max(0.0f, static_cast<float>(issues.size()) - visibleRows);
    if (CheckCollisionPointRec(GetMousePosition(), listBounds))
    {
        const float wheel = GetMouseWheelMove();
        if (std::fabs(wheel) > 0.0f)
        {
            panel.validatorScroll = ClampFloat(panel.validatorScroll - wheel * 2.0f, 0.0f, maxScroll);
        }
    }
    panel.validatorScroll = ClampFloat(panel.validatorScroll, 0.0f, maxScroll);

    const int firstRow = static_cast<int>(std::floor(panel.validatorScroll));
    float rowY = listBounds.y - (panel.validatorScroll - static_cast<float>(firstRow)) * rowH;
    const Vector2 mouse = GetMousePosition();

    BeginScissorMode(static_cast<int>(listBounds.x), static_cast<int>(listBounds.y), static_cast<int>(listBounds.width), static_cast<int>(listBounds.height));
    for (int index = firstRow; index < static_cast<int>(issues.size()) && rowY < listBounds.y + listBounds.height; ++index)
    {
        const ValidatorIssue& issue = issues[static_cast<size_t>(index)];
        const Rectangle row{ listBounds.x, rowY, listBounds.width - (maxScroll > 0.0f ? 10.0f : 0.0f), rowH - 4.0f };
        const bool hovered = CheckCollisionPointRec(mouse, row);
        const bool selectedNode = issue.node >= 0 && IsNodeSelected(tab, issue.node);
        DrawRectangleRec(row, selectedNode ? Color{ 48, 70, 92, 255 } : hovered ? Color{ 34, 39, 45, 255 } : Color{ 24, 27, 31, 220 });
        DrawRectangleLinesEx(row, 1.0f, Color{ 54, 62, 70, 255 });

        const Color severityColor = GetValidatorSeverityColor(issue.severity);
        DrawUiText(font, GetValidatorSeverityName(issue.severity), row.x + 8.0f, row.y + 7.0f, 13.0f, severityColor);
        DrawUiTextClipped(font, issue.category.c_str(), row.x + 58.0f, row.y + 6.0f, 14.0f, row.width - 66.0f, Color{ 205, 213, 220, 255 });
        DrawUiTextClipped(font, issue.message.c_str(), row.x + 8.0f, row.y + 27.0f, 13.0f, row.width - 16.0f, Color{ 154, 166, 178, 255 });

        if (hovered && issue.node >= 0 && IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
        {
            SelectNode(tab, issue.node, IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT));
        }

        rowY += rowH;
    }
    EndScissorMode();

    if (maxScroll > 0.0f)
    {
        const float trackX = listBounds.x + listBounds.width - 6.0f;
        const float thumbH = std::max(24.0f, listBounds.height * (visibleRows / static_cast<float>(issues.size())));
        const float thumbY = listBounds.y + (listBounds.height - thumbH) * (panel.validatorScroll / maxScroll);
        DrawRectangle(static_cast<int>(trackX), static_cast<int>(listBounds.y), 4, static_cast<int>(listBounds.height), Color{ 44, 49, 55, 255 });
        DrawRectangle(static_cast<int>(trackX - 1.0f), static_cast<int>(thumbY), 6, static_cast<int>(thumbH), Color{ 112, 124, 136, 255 });
    }
}

void DrawSceneStatsPanel(Font font, const ModelTab& tab, float panelX, float panelY, float panelW)
{
    const SceneStats stats = CalculateSceneStats(tab.loaded);
    const BoundingBox& bounds = tab.loaded.bounds;
    const Vector3 size = Vector3Subtract(bounds.max, bounds.min);
    const float contentX = panelX + 12.0f;
    float y = panelY;
    char line[256] = {};

    DrawUiText(font, "SCENE STATS", contentX, y, 16.0f, Color{ 165, 182, 196, 255 });
    y += 30.0f;

    std::snprintf(line, sizeof(line), "Nodes: %d", stats.nodes);
    DrawUiText(font, line, contentX, y, 15.0f, Color{ 205, 213, 220, 255 });
    y += 24.0f;
    std::snprintf(line, sizeof(line), "Models: %d", stats.meshes);
    DrawUiText(font, line, contentX, y, 15.0f, Color{ 205, 213, 220, 255 });
    y += 24.0f;
    std::snprintf(line, sizeof(line), "Bones: %d", stats.bones);
    DrawUiText(font, line, contentX, y, 15.0f, Color{ 205, 213, 220, 255 });
    y += 24.0f;
    std::snprintf(line, sizeof(line), "Empties: %d", stats.empties);
    DrawUiText(font, line, contentX, y, 15.0f, Color{ 205, 213, 220, 255 });
    y += 34.0f;

    DrawUiText(font, "GEOMETRY", contentX, y, 16.0f, Color{ 165, 182, 196, 255 });
    y += 30.0f;
    std::snprintf(line, sizeof(line), "Vertices: %d", stats.vertices);
    DrawUiText(font, line, contentX, y, 15.0f, Color{ 205, 213, 220, 255 });
    y += 24.0f;
    std::snprintf(line, sizeof(line), "Triangles: %d", stats.triangles);
    DrawUiText(font, line, contentX, y, 15.0f, Color{ 205, 213, 220, 255 });
    y += 24.0f;
    std::snprintf(line, sizeof(line), "Materials: %d", stats.materials);
    DrawUiText(font, line, contentX, y, 15.0f, Color{ 205, 213, 220, 255 });
    y += 24.0f;
    std::snprintf(line, sizeof(line), "Animations: %zu", tab.loaded.animations.size());
    DrawUiText(font, line, contentX, y, 15.0f, Color{ 205, 213, 220, 255 });
    y += 34.0f;

    DrawUiText(font, "SIZE IN METERS", contentX, y, 16.0f, Color{ 165, 182, 196, 255 });
    y += 30.0f;
    std::snprintf(line, sizeof(line), "Width X:  %.3f m", size.x);
    DrawUiTextClipped(font, line, contentX, y, 15.0f, panelW - 24.0f, Color{ 205, 213, 220, 255 });
    y += 24.0f;
    std::snprintf(line, sizeof(line), "Height Y: %.3f m", size.y);
    DrawUiTextClipped(font, line, contentX, y, 15.0f, panelW - 24.0f, Color{ 205, 213, 220, 255 });
    y += 24.0f;
    std::snprintf(line, sizeof(line), "Depth Z:  %.3f m", size.z);
    DrawUiTextClipped(font, line, contentX, y, 15.0f, panelW - 24.0f, Color{ 205, 213, 220, 255 });
}

void DrawPbrTextureRow(Font font,
                       ModelTab& tab,
                       int materialIndex,
                       PbrTextureSlot slot,
                       float panelX,
                       float panelW,
                       float& y,
                       const std::vector<std::string>& droppedPaths,
                       bool& droppedTextureHandled,
                       TextureClipboard& clipboard,
                       std::string& notice,
                       std::string& error)
{
    const float contentX = panelX + 12.0f;
    EnsurePbrMaterialStates(tab);
    PbrMaterialState& material = tab.pbrMaterials[static_cast<size_t>(materialIndex)];
    const PbrTexture& texture = GetPbrTexture(material, slot);
    const Rectangle thumbnail{ contentX, y - 2.0f, 44.0f, 44.0f };
    const bool thumbnailHovered = CheckCollisionPointRec(GetMousePosition(), thumbnail);
    DrawTextureThumbnail(font, thumbnail, texture, thumbnailHovered && !droppedPaths.empty());

    if (!droppedTextureHandled && thumbnailHovered)
    {
        for (const std::string& droppedPath : droppedPaths)
        {
            const std::filesystem::path texturePath(droppedPath);
            if (!IsTextureExtension(texturePath)) continue;

            std::string loadError;
            const bool shouldLoadOrm = IsOrmTextureName(ToLower(texturePath.stem().string())) &&
                                       (slot == PbrTextureSlot::AmbientOcclusion ||
                                        slot == PbrTextureSlot::Roughness ||
                                        slot == PbrTextureSlot::Metallic);
            if (shouldLoadOrm)
            {
                EditSnapshot before = CaptureEditSnapshot(tab);
                const int loadedCount = LoadOrmTexture(tab, materialIndex, droppedPath, loadError);
                if (loadedCount > 0)
                {
                    PushUndoSnapshot(tab, std::move(before));
                    notice = "Dropped ORM texture to AO, Roughness, and Metallic: " + droppedPath;
                    error.clear();
                }
                else
                {
                    error = loadError;
                    notice.clear();
                }
            }
            else
            {
                EditSnapshot before = CaptureEditSnapshot(tab);
                if (LoadPbrTexture(tab, materialIndex, slot, droppedPath, loadError))
                {
                    PushUndoSnapshot(tab, std::move(before));
                    notice = std::string("Dropped ") + GetPbrTextureSlotName(slot) + ": " + droppedPath;
                    error.clear();
                }
                else
                {
                    error = loadError;
                    notice.clear();
                }
            }
            droppedTextureHandled = true;
            break;
        }
    }

    if (thumbnailHovered && IsMouseButtonPressed(MOUSE_BUTTON_RIGHT))
    {
        clipboard.menuOpen = true;
        clipboard.justOpened = true;
        clipboard.materialIndex = materialIndex;
        clipboard.slot = slot;
        clipboard.position = GetMousePosition();
    }

    char label[128] = {};
    std::snprintf(label, sizeof(label), "%s", GetPbrTextureSlotName(slot));
    DrawUiText(font, label, contentX + 52.0f, y, 15.0f, Color{ 205, 213, 220, 255 });

    float buttonRight = panelX + panelW - 14.0f;
    if (slot == PbrTextureSlot::Roughness || slot == PbrTextureSlot::Metallic || slot == PbrTextureSlot::AmbientOcclusion)
    {
        PackedChannel* channel = slot == PbrTextureSlot::Roughness ? &material.roughnessChannel :
                                 slot == PbrTextureSlot::Metallic ? &material.metallicChannel : &material.aoChannel;
        const Rectangle channelButton{ buttonRight - 38.0f, y - 2.0f, 34.0f, 22.0f };
        if (DrawChannelButton(font, channelButton, *channel))
        {
            PushUndoSnapshot(tab);
            *channel = NextPackedChannel(*channel);
        }
        buttonRight -= 44.0f;
    }
    else if (slot == PbrTextureSlot::Opacity)
    {
        const Rectangle channelButton{ buttonRight - 48.0f, y - 2.0f, 44.0f, 22.0f };
        if (DrawPanelButton(font, channelButton, GetOpacityChannelName(material.opacityChannel)))
        {
            PushUndoSnapshot(tab);
            material.opacityChannel = NextOpacityChannel(material.opacityChannel);
        }
        buttonRight -= 54.0f;
    }

    const Rectangle clearButton{ buttonRight - 48.0f, y - 2.0f, 48.0f, 22.0f };
    const Rectangle loadButton{ buttonRight - 102.0f, y - 2.0f, 48.0f, 22.0f };
    if (DrawPanelButton(font, loadButton, "Load"))
    {
        const std::string path = OpenTextureFileDialog();
        if (!path.empty())
        {
            EditSnapshot before = CaptureEditSnapshot(tab);
            std::string loadError;
            if (LoadPbrTexture(tab, materialIndex, slot, path, loadError))
            {
                PushUndoSnapshot(tab, std::move(before));
                notice = std::string("Loaded ") + GetPbrTextureSlotName(slot) + ": " + path;
                error.clear();
            }
            else
            {
                error = loadError;
                notice.clear();
            }
        }
    }
    if (DrawPanelButton(font, clearButton, "Clear"))
    {
        if (texture.loaded)
        {
            PushUndoSnapshot(tab);
            UnloadPbrTexture(tab, materialIndex, slot);
            notice = std::string("Cleared ") + GetPbrTextureSlotName(slot) + " map.";
            error.clear();
        }
    }

    y += 22.0f;
    DrawUiTextClipped(font, texture.loaded ? GetFileName(texture.path.c_str()) : "No texture", contentX + 52.0f, y, 13.0f, panelW - 80.0f, texture.loaded ? Color{ 160, 205, 230, 255 } : Color{ 120, 130, 140, 255 });
    y += 34.0f;
}

void DrawMaterialsPanel(Font font,
                        ModelTab& tab,
                        float panelX,
                        float panelY,
                        float panelW,
                        const std::vector<std::string>& droppedPaths,
                        bool& droppedTextureHandled,
                        TextureClipboard& clipboard,
                        std::string& notice,
                        std::string& error)
{
    EnsurePbrMaterialStates(tab);
    const float contentX = panelX + 12.0f;
    const int materialCount = static_cast<int>(tab.pbrMaterials.size());
    float y = panelY;

    DrawUiText(font, "MATERIALS", contentX, y, 16.0f, Color{ 165, 182, 196, 255 });
    y += 28.0f;

    for (int i = 0; i < materialCount; ++i)
    {
        const bool selected = i == tab.selectedMaterial;
        const Rectangle row{ contentX, y, panelW - 24.0f, 22.0f };
        const Vector2 mouse = GetMousePosition();
        const bool hovered = CheckCollisionPointRec(mouse, row);
        DrawRectangleRec(row, selected ? Color{ 48, 70, 92, 255 } : hovered ? Color{ 34, 39, 45, 255 } : Color{ 24, 27, 31, 220 });
        const std::string name = i < static_cast<int>(tab.loaded.materialNames.size()) ? tab.loaded.materialNames[static_cast<size_t>(i)] : std::string("Material ") + std::to_string(i + 1);
        DrawUiTextClipped(font, name.c_str(), row.x + 8.0f, row.y + 3.0f, 14.0f, row.width - 16.0f, selected ? RAYWHITE : Color{ 185, 195, 205, 255 });
        if (hovered && IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
        {
            tab.selectedMaterial = i;
        }
        y += 24.0f;
    }
    PbrMaterialState& material = tab.pbrMaterials[static_cast<size_t>(tab.selectedMaterial)];

    const float clearAllW = 74.0f;
    const Rectangle loadFolderButton{ contentX, y, panelW - 30.0f - clearAllW, 24.0f };
    const Rectangle clearAllButton{ contentX + loadFolderButton.width + 6.0f, y, clearAllW, 24.0f };
    if (DrawPanelButton(font, loadFolderButton, "Load Textures From Folder"))
    {
        const std::string pickedPath = OpenTextureFileDialog();
        std::string autoloadError;
        EditSnapshot before = CaptureEditSnapshot(tab);
        const int loadedCount = pickedPath.empty() ? 0 : LoadPbrTexturesFromFolder(tab, tab.selectedMaterial, std::filesystem::path(pickedPath).parent_path(), autoloadError);
        if (loadedCount > 0)
        {
            PushUndoSnapshot(tab, std::move(before));
        }
        if (!autoloadError.empty())
        {
            error = autoloadError;
            notice.clear();
        }
        else
        {
            char message[128] = {};
            std::snprintf(message, sizeof(message), "Loaded %d texture%s from folder.", loadedCount, loadedCount == 1 ? "" : "s");
            notice = message;
            error.clear();
        }
    }
    if (DrawPanelButton(font, clearAllButton, "Clear All"))
    {
        bool hadAnyTexture = false;
        for (int i = 0; i < static_cast<int>(PbrTextureSlot::Count); ++i)
        {
            if (GetPbrTexture(tab.pbrMaterials[static_cast<size_t>(tab.selectedMaterial)], static_cast<PbrTextureSlot>(i)).loaded)
            {
                hadAnyTexture = true;
                break;
            }
        }
        if (hadAnyTexture)
        {
            PushUndoSnapshot(tab);
        }
        for (int i = 0; i < static_cast<int>(PbrTextureSlot::Count); ++i)
        {
            UnloadPbrTexture(tab, tab.selectedMaterial, static_cast<PbrTextureSlot>(i));
        }
        notice = "Cleared all maps for selected material.";
        error.clear();
    }
    y += 34.0f;

    DrawPbrTextureRow(font, tab, tab.selectedMaterial, PbrTextureSlot::Diffuse, panelX, panelW, y, droppedPaths, droppedTextureHandled, clipboard, notice, error);
    DrawPbrTextureRow(font, tab, tab.selectedMaterial, PbrTextureSlot::Normal, panelX, panelW, y, droppedPaths, droppedTextureHandled, clipboard, notice, error);

    const Rectangle normalModeButton{ contentX + 112.0f, y - 2.0f, panelW - 136.0f, 22.0f };
    DrawUiText(font, "Normal mode", contentX, y, 14.0f, Color{ 190, 200, 210, 255 });
    if (DrawPanelButton(font, normalModeButton, material.normalDirectX ? "DirectX" : "OpenGL"))
    {
        PushUndoSnapshot(tab);
        material.normalDirectX = !material.normalDirectX;
    }
    y += 32.0f;

    DrawPbrTextureRow(font, tab, tab.selectedMaterial, PbrTextureSlot::Roughness, panelX, panelW, y, droppedPaths, droppedTextureHandled, clipboard, notice, error);
    DrawPbrTextureRow(font, tab, tab.selectedMaterial, PbrTextureSlot::Metallic, panelX, panelW, y, droppedPaths, droppedTextureHandled, clipboard, notice, error);
    DrawPbrTextureRow(font, tab, tab.selectedMaterial, PbrTextureSlot::AmbientOcclusion, panelX, panelW, y, droppedPaths, droppedTextureHandled, clipboard, notice, error);
    DrawPbrTextureRow(font, tab, tab.selectedMaterial, PbrTextureSlot::Emissive, panelX, panelW, y, droppedPaths, droppedTextureHandled, clipboard, notice, error);
    DrawPbrTextureRow(font, tab, tab.selectedMaterial, PbrTextureSlot::Opacity, panelX, panelW, y, droppedPaths, droppedTextureHandled, clipboard, notice, error);
    DrawTextureContextMenu(font, tab, clipboard, notice, error);
}

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
        UvTriangleSample triangle;
        triangle.nodeIndex = nodeIndex;
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
            MakeUvEdgeKey(triangle.uv[0], triangle.uv[1]),
            MakeUvEdgeKey(triangle.uv[1], triangle.uv[2]),
            MakeUvEdgeKey(triangle.uv[2], triangle.uv[0])
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

int NextUvDensityTileSize(int current)
{
    if (current < 1024) return 1024;
    if (current < 2048) return 2048;
    if (current < 4096) return 4096;
    if (current < 8192) return 8192;
    return 512;
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
        const UvDensityStats stats = CalculateUvDensityStats(tab.loaded, node, uvs, textureWidth, textureHeight);
        if (stats.density <= 0.0f) continue;

        const std::string materialName = GetPrimaryMaterialName(node);
        if (materialName.empty()) continue;
        densitiesByMaterial[materialName].push_back({ i, stats.density });
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
                      "%s %s texel density is not uniform between meshes: %.1f..%.1f px/m.",
                      entry.first.c_str(),
                      uvSetName.c_str(),
                      minMax.first->second,
                      minMax.second->second);
        AddValidationIssue(issues, ValidatorSeverity::Warning, "Texel density", message, minMax.second->first);
    }
}

void DrawUvPanel(Font font, ModelTab& tab, float panelX, float panelY, float panelW)
{
    const float contentX = panelX + 12.0f;
    float y = panelY;

    DrawUiText(font, "UV EDITOR", contentX, y, 16.0f, Color{ 165, 182, 196, 255 });
    y += 30.0f;

    if (tab.loaded.uvSetNames.empty() || tab.loaded.uvSets.empty())
    {
        DrawUiTextClipped(font, "No UV sets found in this FBX.", contentX, y, 14.0f, panelW - 24.0f, Color{ 128, 140, 152, 255 });
        return;
    }

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
        const float wheel = GetMouseWheelMove();
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
        if (hovered && IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
        {
            tab.selectedUvSet = uvSetIndex;
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

    if (tab.selectedNode < 0 || tab.selectedNode >= static_cast<int>(tab.loaded.nodes.size()) ||
        tab.loaded.nodes[static_cast<size_t>(tab.selectedNode)].type != SceneNodeType::Mesh)
    {
        DrawUiTextClipped(font, "Select a mesh in the viewport or hierarchy to display its UVs.", contentX, y, 14.0f, panelW - 24.0f, Color{ 128, 140, 152, 255 });
        return;
    }

    const int selectedNodeIndex = tab.selectedNode;
    const SceneNode& node = tab.loaded.nodes[static_cast<size_t>(selectedNodeIndex)];
    const std::vector<float>& uvs = tab.loaded.uvSets[static_cast<size_t>(tab.selectedUvSet)];
    if (node.meshVertexStart < 0 || node.meshVertexCount <= 0 || uvs.size() < static_cast<size_t>(node.meshVertexStart + node.meshVertexCount) * 2)
    {
        DrawUiTextClipped(font, "Selected mesh has no UV data for this set.", contentX, y, 14.0f, panelW - 24.0f, Color{ 128, 140, 152, 255 });
        return;
    }

    char meshText[192] = {};
    std::snprintf(meshText, sizeof(meshText), "Mesh: %s", node.name.c_str());
    DrawUiTextClipped(font, meshText, contentX, y, 14.0f, panelW - 24.0f, Color{ 205, 213, 220, 255 });
    y += 24.0f;

    EnsurePbrMaterialStates(tab);
    int densityTextureWidth = 0;
    int densityTextureHeight = 0;
    bool usingDensityTexture = false;
    GetUvDensityTextureSize(tab, node, densityTextureWidth, densityTextureHeight, usingDensityTexture);
    const std::vector<int> scopeNodeIndices = GetUvScopeNodeIndices(tab, node, selectedNodeIndex);
    const std::vector<UvTriangleSample> scopeTriangles = BuildUvScopeTriangles(tab, uvs, scopeNodeIndices);
    const std::vector<UvIslandStats> islands = CalculateUvIslandStats(scopeTriangles, densityTextureWidth, densityTextureHeight);
    tab.selectedUvIsland = islands.empty() ? -1 : ClampInt(tab.selectedUvIsland, -1, static_cast<int>(islands.size()) - 1);

    const float toggleW = (panelW - 30.0f) * 0.5f;
    const Rectangle densityToggle{ contentX, y, toggleW, 24.0f };
    const Rectangle sameMaterialToggle{ contentX + toggleW + 6.0f, y, toggleW, 24.0f };
    if (DrawPanelButton(font, densityToggle, tab.showUvTexelDensity ? "Density On" : "Density Off"))
    {
        tab.showUvTexelDensity = !tab.showUvTexelDensity;
    }
    if (DrawPanelButton(font, sameMaterialToggle, tab.showUvSameMaterialMeshes ? "Same Mat On" : "Same Mat Off"))
    {
        tab.showUvSameMaterialMeshes = !tab.showUvSameMaterialMeshes;
    }
    y += 32.0f;

    if (tab.showUvTexelDensity)
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

        if (tab.selectedUvIsland >= 0 && tab.selectedUvIsland < static_cast<int>(islands.size()))
        {
            const UvIslandStats& island = islands[static_cast<size_t>(tab.selectedUvIsland)];
            char islandLine[192] = {};
            std::snprintf(islandLine,
                          sizeof(islandLine),
                          "Selected island %d: %.1f px/m",
                          tab.selectedUvIsland + 1,
                          island.density);
            DrawUiTextClipped(font, islandLine, contentX, y, 14.0f, panelW - 24.0f, island.density > 0.0f ? Color{ 150, 225, 170, 255 } : Color{ 255, 185, 125, 255 });
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

    const float editorSize = std::min(panelW - 24.0f, std::max(120.0f, GetHierarchyPanelHeight() - (y - panelY) - 24.0f));
    const Rectangle editor{ contentX, y, editorSize, editorSize };
    DrawRectangleRec(editor, Color{ 14, 16, 19, 245 });
    DrawRectangleLinesEx(editor, 1.0f, Color{ 88, 98, 108, 255 });

    tab.uvViewZoom = ClampFloat(tab.uvViewZoom, 0.2f, 80.0f);
    const Vector2 editorCenter{ editor.x + editor.width * 0.5f, editor.y + editor.height * 0.5f };
    auto getUvScale = [&]()
    {
        return editor.width * tab.uvViewZoom;
    };

    auto uvToScreen = [&](Vector2 uv)
    {
        const float scale = getUvScale();
        return Vector2{
            editorCenter.x + tab.uvViewPan.x + (uv.x - 0.5f) * scale,
            editorCenter.y + tab.uvViewPan.y - (uv.y - 0.5f) * scale
        };
    };
    auto screenToUv = [&](Vector2 point)
    {
        const float scale = std::max(0.001f, getUvScale());
        return Vector2{
            0.5f + (point.x - editorCenter.x - tab.uvViewPan.x) / scale,
            0.5f - (point.y - editorCenter.y - tab.uvViewPan.y) / scale
        };
    };

    const bool mouseOverEditor = CheckCollisionPointRec(mouse, editor);
    if (mouseOverEditor)
    {
        const float wheel = GetMouseWheelMove();
        if (std::fabs(wheel) > 0.0f)
        {
            const Vector2 uvUnderMouse = screenToUv(mouse);
            tab.uvViewZoom = ClampFloat(tab.uvViewZoom * std::pow(1.18f, wheel), 0.2f, 80.0f);
            const float scale = getUvScale();
            tab.uvViewPan.x = mouse.x - editorCenter.x - (uvUnderMouse.x - 0.5f) * scale;
            tab.uvViewPan.y = mouse.y - editorCenter.y + (uvUnderMouse.y - 0.5f) * scale;
        }

        if (IsMouseButtonPressed(MOUSE_BUTTON_RIGHT) || IsMouseButtonPressed(MOUSE_BUTTON_MIDDLE))
        {
            tab.uvViewPanning = true;
        }
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
        {
            tab.selectedUvIsland = FindUvIslandAtPoint(islands, scopeTriangles, screenToUv(mouse));
        }
    }
    if (IsMouseButtonReleased(MOUSE_BUTTON_RIGHT) || IsMouseButtonReleased(MOUSE_BUTTON_MIDDLE))
    {
        tab.uvViewPanning = false;
    }
    if (tab.uvViewPanning && (IsMouseButtonDown(MOUSE_BUTTON_RIGHT) || IsMouseButtonDown(MOUSE_BUTTON_MIDDLE)))
    {
        const Vector2 delta = GetMouseDelta();
        tab.uvViewPan = Vector2Add(tab.uvViewPan, delta);
    }
    else if (!IsMouseButtonDown(MOUSE_BUTTON_RIGHT) && !IsMouseButtonDown(MOUSE_BUTTON_MIDDLE))
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
    for (int islandIndex = 0; islandIndex < static_cast<int>(islands.size()); ++islandIndex)
    {
        const UvIslandStats& island = islands[static_cast<size_t>(islandIndex)];
        const bool selectedIsland = islandIndex == tab.selectedUvIsland;
        const Color baseColor = islandColors[islandIndex % islandColorCount];
        const Color lineColor = selectedIsland ? Color{ 255, 245, 180, 255 } : baseColor;
        const unsigned char fillAlpha = selectedIsland ? 86 : 34;
        const Color fillColor{ lineColor.r, lineColor.g, lineColor.b, fillAlpha };
        for (int triangleIndex : island.triangles)
        {
            if (triangleIndex < 0 || triangleIndex >= static_cast<int>(scopeTriangles.size())) continue;

            const UvTriangleSample& triangle = scopeTriangles[static_cast<size_t>(triangleIndex)];
            const Vector2 a = uvToScreen(triangle.uv[0]);
            const Vector2 b = uvToScreen(triangle.uv[1]);
            const Vector2 c = uvToScreen(triangle.uv[2]);
            DrawTriangle(a, b, c, fillColor);
            if (selectedIsland)
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
    EndScissorMode();

    DrawRectangleLinesEx(editor, 1.0f, Color{ 88, 98, 108, 255 });
}

struct SkinWeightStats
{
    int vertices = 0;
    int maxInfluences = 0;
    int unweighted = 0;
    int underweight = 0;
    int overweight = 0;
    int invalidWeights = 0;
};

float GetVertexWeightSum(const SkinnedVertex& vertex, bool& invalidWeight)
{
    float sum = 0.0f;
    invalidWeight = false;
    for (const SkinnedVertexInfluence& influence : vertex.influences)
    {
        if (!std::isfinite(influence.weight) || influence.weight < 0.0f)
        {
            invalidWeight = true;
        }
        sum += influence.weight;
    }
    return sum;
}

SkinWeightStats CalculateSkinWeightStats(const LoadedFbxModel& loaded, const SceneNode& node)
{
    SkinWeightStats stats;
    if (node.meshVertexStart < 0 || node.meshVertexCount <= 0) return stats;

    constexpr float kWeightEpsilon = 0.01f;
    const int start = std::max(0, node.meshVertexStart);
    const int end = std::min(node.meshVertexStart + node.meshVertexCount, static_cast<int>(loaded.skinnedVertices.size()));
    for (int vertexIndex = start; vertexIndex < end; ++vertexIndex)
    {
        const SkinnedVertex& vertex = loaded.skinnedVertices[static_cast<size_t>(vertexIndex)];
        bool invalidWeight = false;
        const float sum = GetVertexWeightSum(vertex, invalidWeight);
        stats.vertices++;
        stats.maxInfluences = std::max(stats.maxInfluences, static_cast<int>(vertex.influences.size()));
        if (vertex.influences.empty() || std::fabs(sum) <= kWeightEpsilon)
        {
            stats.unweighted++;
        }
        else if (sum < 1.0f - kWeightEpsilon)
        {
            stats.underweight++;
        }
        else if (sum > 1.0f + kWeightEpsilon)
        {
            stats.overweight++;
        }
        if (invalidWeight)
        {
            stats.invalidWeights++;
        }
    }

    return stats;
}

std::string FormatVertexInfluences(const SkinnedVertex& vertex)
{
    if (vertex.influences.empty()) return "No influences";

    std::vector<SkinnedVertexInfluence> influences = vertex.influences;
    std::sort(influences.begin(), influences.end(), [](const SkinnedVertexInfluence& a, const SkinnedVertexInfluence& b)
    {
        return a.weight > b.weight;
    });

    std::string result;
    for (const SkinnedVertexInfluence& influence : influences)
    {
        char weightText[64] = {};
        std::snprintf(weightText, sizeof(weightText), "%.3f", influence.weight);
        if (!result.empty()) result += " | ";
        result += influence.boneName.empty() ? "<unnamed>" : influence.boneName;
        result += " ";
        result += weightText;
    }
    return result;
}

struct BoneWeightHeatStats
{
    int vertices = 0;
    int influencedVertices = 0;
    int unweightedVertices = 0;
    int underweightVertices = 0;
    int overweightVertices = 0;
    int invalidWeightVertices = 0;
    int maxInfluences = 0;
    float maxBoneWeight = 0.0f;
    float averageInfluencedWeight = 0.0f;
};

BoneWeightHeatStats CalculateBoneWeightHeatStats(const ModelTab& tab, const std::string& boneName)
{
    BoneWeightHeatStats stats;
    if (boneName.empty() || tab.loaded.skinnedVertices.empty()) return stats;

    constexpr float kWeightEpsilon = 0.01f;
    float influencedWeightSum = 0.0f;
    for (int nodeIndex = 0; nodeIndex < static_cast<int>(tab.loaded.nodes.size()); ++nodeIndex)
    {
        const SceneNode& node = tab.loaded.nodes[static_cast<size_t>(nodeIndex)];
        if (node.type != SceneNodeType::Mesh || node.meshVertexStart < 0 || node.meshVertexCount <= 0) continue;
        if (IsDeletedNode(tab, nodeIndex)) continue;

        const int start = std::max(0, node.meshVertexStart);
        const int end = std::min(node.meshVertexStart + node.meshVertexCount, static_cast<int>(tab.loaded.skinnedVertices.size()));
        for (int vertexIndex = start; vertexIndex < end; ++vertexIndex)
        {
            const SkinnedVertex& vertex = tab.loaded.skinnedVertices[static_cast<size_t>(vertexIndex)];
            bool invalidWeight = false;
            const float totalWeight = GetVertexWeightSum(vertex, invalidWeight);
            const float boneWeight = GetBoneInfluenceWeight(vertex, boneName);
            stats.vertices++;
            stats.maxInfluences = std::max(stats.maxInfluences, static_cast<int>(vertex.influences.size()));
            stats.maxBoneWeight = std::max(stats.maxBoneWeight, boneWeight);

            if (boneWeight > kWeightEpsilon)
            {
                stats.influencedVertices++;
                influencedWeightSum += boneWeight;
            }
            if (vertex.influences.empty() || std::fabs(totalWeight) <= kWeightEpsilon)
            {
                stats.unweightedVertices++;
            }
            else if (totalWeight < 1.0f - kWeightEpsilon)
            {
                stats.underweightVertices++;
            }
            else if (totalWeight > 1.0f + kWeightEpsilon)
            {
                stats.overweightVertices++;
            }
            if (invalidWeight)
            {
                stats.invalidWeightVertices++;
            }
        }
    }

    if (stats.influencedVertices > 0)
    {
        stats.averageInfluencedWeight = influencedWeightSum / static_cast<float>(stats.influencedVertices);
    }
    return stats;
}

void DrawSkinWeightsPanel(Font font, ModelTab& tab, HierarchyPanelState& panel, float panelX, float panelY, float panelW)
{
    const float contentX = panelX + 12.0f;
    float y = panelY;
    panel.skinWeightsScroll = 0.0f;

    DrawUiText(font, "SKIN WEIGHTS", contentX, y, 16.0f, Color{ 165, 182, 196, 255 });
    y += 28.0f;

    if (tab.loaded.skinnedVertices.empty())
    {
        DrawUiTextClipped(font, "No skin weights found in this FBX.", contentX, y, 14.0f, panelW - 24.0f, Color{ 128, 140, 152, 255 });
        panel.skinWeightsScroll = 0.0f;
        return;
    }

    std::string boneName;
    if (!GetSelectedBoneName(tab, boneName))
    {
        DrawUiTextClipped(font, "Select a bone in the hierarchy to show its weights on the mesh.", contentX, y, 14.0f, panelW - 24.0f, Color{ 128, 140, 152, 255 });
        y += 30.0f;
        DrawUiTextClipped(font, "The viewport heat map updates immediately from the selected bone.", contentX, y, 14.0f, panelW - 24.0f, Color{ 154, 166, 178, 255 });
        return;
    }

    const BoneWeightHeatStats stats = CalculateBoneWeightHeatStats(tab, boneName);
    char line[256] = {};
    std::snprintf(line, sizeof(line), "Bone: %s", boneName.c_str());
    DrawUiTextClipped(font, line, contentX, y, 14.0f, panelW - 24.0f, Color{ 205, 213, 220, 255 });
    y += 22.0f;
    std::snprintf(line, sizeof(line), "Mesh vertices: %d    Max influences: %d", stats.vertices, stats.maxInfluences);
    DrawUiTextClipped(font, line, contentX, y, 14.0f, panelW - 24.0f, Color{ 190, 200, 210, 255 });
    y += 22.0f;
    std::snprintf(line, sizeof(line), "Influenced: %d    Max weight: %.3f", stats.influencedVertices, stats.maxBoneWeight);
    DrawUiTextClipped(font, line, contentX, y, 14.0f, panelW - 24.0f, stats.influencedVertices > 0 ? Color{ 150, 225, 170, 255 } : Color{ 255, 185, 125, 255 });
    y += 22.0f;
    std::snprintf(line, sizeof(line), "Average influenced weight: %.3f", stats.averageInfluencedWeight);
    DrawUiTextClipped(font, line, contentX, y, 14.0f, panelW - 24.0f, Color{ 190, 200, 210, 255 });
    y += 30.0f;

    DrawUiText(font, "MESH WEIGHT QUALITY", contentX, y, 15.0f, Color{ 165, 182, 196, 255 });
    y += 24.0f;
    std::snprintf(line, sizeof(line), "Unweighted: %d    Under: %d    Over: %d", stats.unweightedVertices, stats.underweightVertices, stats.overweightVertices);
    DrawUiTextClipped(font, line, contentX, y, 14.0f, panelW - 24.0f,
                      (stats.unweightedVertices || stats.underweightVertices || stats.overweightVertices) ? Color{ 255, 185, 125, 255 } : Color{ 150, 225, 170, 255 });
    y += 22.0f;
    std::snprintf(line, sizeof(line), "Invalid weights: %d", stats.invalidWeightVertices);
    DrawUiTextClipped(font, line, contentX, y, 14.0f, panelW - 24.0f, stats.invalidWeightVertices ? Color{ 255, 150, 125, 255 } : Color{ 154, 166, 178, 255 });
    y += 34.0f;

    DrawUiText(font, "HEAT MAP", contentX, y, 15.0f, Color{ 165, 182, 196, 255 });
    y += 24.0f;
    const Rectangle legend{ contentX, y, panelW - 24.0f, 18.0f };
    constexpr int kLegendSteps = 48;
    for (int i = 0; i < kLegendSteps; ++i)
    {
        const float t0 = static_cast<float>(i) / static_cast<float>(kLegendSteps);
        const float t1 = static_cast<float>(i + 1) / static_cast<float>(kLegendSteps);
        const Rectangle segment{ legend.x + legend.width * t0, legend.y, legend.width * (t1 - t0) + 1.0f, legend.height };
        DrawRectangleRec(segment, GetSkinWeightHeatColor(t0));
    }
    DrawRectangleLinesEx(legend, 1.0f, Color{ 86, 96, 108, 255 });
    y += 26.0f;
    DrawUiText(font, "0.0", legend.x, y, 13.0f, Color{ 154, 166, 178, 255 });
    const Vector2 oneSize = MeasureTextEx(font, "1.0", 13.0f, 1.0f);
    DrawUiText(font, "1.0", legend.x + legend.width - oneSize.x, y, 13.0f, Color{ 154, 166, 178, 255 });
    y += 30.0f;
    DrawUiTextClipped(font, "Blue is no influence. Red is full influence.", contentX, y, 14.0f, panelW - 24.0f, Color{ 154, 166, 178, 255 });
}

void DrawHierarchyPanel(Font font,
                        ModelTab* active,
                        HierarchyPanelState& panel,
                        RenameEditor& renameEditor,
                        const std::vector<std::string>& droppedPaths,
                        bool& droppedTextureHandled,
                        TextureClipboard& textureClipboard,
                        std::string& notice,
                        std::string& error)
{
    constexpr float panelX = 0.0f;
    constexpr float panelY = 61.0f;
    constexpr float rowH = 22.0f;
    const float panelH = GetHierarchyPanelHeight();

    if (panel.hidden)
    {
        DrawRectangle(static_cast<int>(panelX), static_cast<int>(panelY), 28, static_cast<int>(panelH), Color{ 18, 20, 23, 235 });
        DrawLine(28, static_cast<int>(panelY), 28, static_cast<int>(panelY + panelH), Color{ 64, 70, 78, 255 });
        DrawUiText(font, ">", panelX + 9.0f, panelY + 10.0f, 16.0f, Color{ 190, 198, 206, 255 });
        return;
    }

    const float panelW = panel.width;

    DrawRectangle(static_cast<int>(panelX), static_cast<int>(panelY), static_cast<int>(panelW), static_cast<int>(panelH), Color{ 18, 20, 23, 235 });
    DrawLine(static_cast<int>(panelW), static_cast<int>(panelY), static_cast<int>(panelW), static_cast<int>(panelY + panelH), Color{ 64, 70, 78, 255 });
    DrawUiText(font, "SCENE", panelX + 12.0f, panelY + 10.0f, 16.0f, Color{ 165, 182, 196, 255 });
    DrawUiText(font, "||", panelW - 16.0f, panelY + 8.0f, 16.0f, Color{ 120, 130, 140, 255 });

    const float tabW = (panelW - 36.0f) / 6.0f;
    const Rectangle hierarchyTab{ panelX + 8.0f, panelY + 34.0f, tabW, 24.0f };
    const Rectangle statsTab{ hierarchyTab.x + hierarchyTab.width + 4.0f, panelY + 34.0f, tabW, 24.0f };
    const Rectangle materialsTab{ statsTab.x + statsTab.width + 4.0f, panelY + 34.0f, tabW, 24.0f };
    const Rectangle uvTab{ materialsTab.x + materialsTab.width + 4.0f, panelY + 34.0f, tabW, 24.0f };
    const Rectangle skinTab{ uvTab.x + uvTab.width + 4.0f, panelY + 34.0f, tabW, 24.0f };
    const Rectangle validatorTab{ skinTab.x + skinTab.width + 4.0f, panelY + 34.0f, tabW, 24.0f };
    if (DrawPanelTab(font, hierarchyTab, "Tree", panel.activeTab == LeftPanelTab::Hierarchy))
    {
        panel.activeTab = LeftPanelTab::Hierarchy;
    }
    if (DrawPanelTab(font, statsTab, "Stats", panel.activeTab == LeftPanelTab::Stats))
    {
        panel.activeTab = LeftPanelTab::Stats;
    }
    if (DrawPanelTab(font, materialsTab, "Mats", panel.activeTab == LeftPanelTab::Materials))
    {
        panel.activeTab = LeftPanelTab::Materials;
    }
    if (DrawPanelTab(font, uvTab, "UV", panel.activeTab == LeftPanelTab::UV))
    {
        panel.activeTab = LeftPanelTab::UV;
    }
    if (DrawPanelTab(font, skinTab, "Skin", panel.activeTab == LeftPanelTab::SkinWeights))
    {
        panel.activeTab = LeftPanelTab::SkinWeights;
    }
    if (DrawPanelTab(font, validatorTab, "Valid", panel.activeTab == LeftPanelTab::Validator))
    {
        panel.activeTab = LeftPanelTab::Validator;
    }

    if (!active)
    {
        DrawUiText(font, "No active tab", panelX + 12.0f, GetHierarchyContentStartY(), 15.0f, Color{ 128, 136, 144, 255 });
        return;
    }

    if (panel.activeTab == LeftPanelTab::Stats)
    {
        DrawSceneStatsPanel(font, *active, panelX, GetHierarchyContentStartY() + 10.0f, panelW);
        return;
    }

    if (panel.activeTab == LeftPanelTab::Materials)
    {
        DrawMaterialsPanel(font, *active, panelX, GetHierarchyContentStartY() + 10.0f, panelW, droppedPaths, droppedTextureHandled, textureClipboard, notice, error);
        return;
    }
    if (panel.activeTab == LeftPanelTab::UV)
    {
        DrawUvPanel(font, *active, panelX, GetHierarchyContentStartY() + 10.0f, panelW);
        return;
    }
    if (panel.activeTab == LeftPanelTab::SkinWeights)
    {
        DrawSkinWeightsPanel(font, *active, panel, panelX, GetHierarchyContentStartY() + 10.0f, panelW);
        return;
    }
    if (panel.activeTab == LeftPanelTab::Validator)
    {
        DrawValidatorPanel(font, *active, panel, panelX, GetHierarchyContentStartY() + 10.0f, panelW);
        return;
    }

    if (active->collapsedNodes.size() != active->loaded.nodes.size())
    {
        active->collapsedNodes.assign(active->loaded.nodes.size(), false);
    }

    const Vector2 mouse = GetMousePosition();
    const bool additiveSelection = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);
    if (!IsMouseButtonDown(MOUSE_BUTTON_LEFT) || !additiveSelection)
    {
        panel.shiftDragSelecting = false;
        panel.shiftDragAnchorNode = -1;
        panel.shiftDragLastNode = -1;
    }

    auto getContextMenuHeight = [&]()
    {
        if (HasBoneNode(*active, GetContextActionNodes(*active, panel.contextNodeIndex, panel.contextNodeIndices)))
        {
            return kNodeContextMenuBoneH;
        }
        return kNodeContextMenuBaseH;
    };
    float rowY = GetHierarchyContentStartY();
    int visibleRow = 0;
    const int firstRow = static_cast<int>(std::floor(panel.scroll));

    for (int i = 0; i < static_cast<int>(active->loaded.nodes.size()); ++i)
    {
        if (!IsSceneNodeVisible(*active, active->collapsedNodes, i)) continue;
        if (visibleRow++ < firstRow) continue;
        if (rowY + rowH > panelY + panelH) break;

        const SceneNode& node = active->loaded.nodes[static_cast<size_t>(i)];
        const Rectangle row{ panelX + 6.0f, rowY, panelW - 12.0f, rowH };
        const bool hovered = CheckCollisionPointRec(mouse, row);
        const bool selected = IsNodeSelected(*active, i);
        const bool hasChildren = HasVisibleSceneNodeChildren(*active, i);
        const float indent = static_cast<float>(node.depth) * 14.0f;
        const float contextMenuH = panel.contextMenuOpen ? getContextMenuHeight() : kNodeContextMenuBaseH;
        const Rectangle contextMenuBounds{ panel.contextPosition.x, panel.contextPosition.y, kNodeContextMenuW, contextMenuH };
        const bool mouseOverContextMenu = panel.contextMenuOpen && CheckCollisionPointRec(mouse, contextMenuBounds);

        if (selected)
        {
            DrawRectangleRec(row, Color{ 48, 70, 92, 255 });
        }
        else if (hovered)
        {
            DrawRectangleRec(row, Color{ 34, 39, 45, 255 });
        }

        const Rectangle collapseRect{ panelX + 10.0f + indent, rowY + 3.0f, 14.0f, 16.0f };
        if (hasChildren)
        {
            DrawUiText(font, active->collapsedNodes[static_cast<size_t>(i)] ? ">" : "v", collapseRect.x, collapseRect.y, 15.0f, Color{ 190, 198, 206, 255 });
            if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(mouse, collapseRect))
            {
                active->collapsedNodes[static_cast<size_t>(i)] = !active->collapsedNodes[static_cast<size_t>(i)];
            }
        }

        char label[320] = {};
        std::snprintf(label, sizeof(label), "%s %s", GetSceneNodeIcon(node.type), node.name.c_str());
        const float labelX = panelX + 28.0f + indent;
        const float labelMaxW = panelX + panelW - 12.0f - labelX;
        BeginScissorMode(static_cast<int>(panelX), static_cast<int>(panelY), static_cast<int>(panelW), static_cast<int>(panelH));
        DrawUiTextClipped(font, label, labelX, rowY + 3.0f, 15.0f, labelMaxW, selected ? RAYWHITE : Color{ 198, 207, 216, 255 });
        EndScissorMode();

        if (hovered &&
            IsMouseButtonPressed(MOUSE_BUTTON_LEFT) &&
            !IsMouseButtonPressed(MOUSE_BUTTON_RIGHT) &&
            !IsMouseButtonDown(MOUSE_BUTTON_RIGHT) &&
            !mouseOverContextMenu &&
            !CheckCollisionPointRec(mouse, collapseRect))
        {
            if (additiveSelection)
            {
                panel.shiftDragSelecting = true;
                panel.shiftDragAnchorNode = i;
                panel.shiftDragLastNode = i;
            }
            SelectNode(*active, i, additiveSelection);
            panel.contextMenuOpen = false;
        }
        else if (hovered && panel.shiftDragSelecting && additiveSelection && IsMouseButtonDown(MOUSE_BUTTON_LEFT) &&
                 !mouseOverContextMenu && !CheckCollisionPointRec(mouse, collapseRect) &&
                 i != panel.shiftDragLastNode)
        {
            SetVisibleNodeRangeSelection(*active, active->collapsedNodes, panel.shiftDragAnchorNode, i);
            panel.shiftDragLastNode = i;
            panel.contextMenuOpen = false;
        }

        if (hovered && IsMouseButtonPressed(MOUSE_BUTTON_RIGHT) && !mouseOverContextMenu)
        {
            std::vector<int> selectedContextNodes = GetValidContextActionNodes(*active, active->selectedNodes);
            if (selectedContextNodes.size() > 1)
            {
                panel.contextNodeIndices = std::move(selectedContextNodes);
                panel.contextNodeIndex = active->selectedNode >= 0 ? active->selectedNode : panel.contextNodeIndices.front();
            }
            else if (IsNodeSelected(*active, i))
            {
                active->selectedNode = i;
                panel.contextNodeIndex = i;
                panel.contextNodeIndices = GetContextActionNodes(*active, i);
            }
            else
            {
                SetSingleSelectedNode(*active, i);
                panel.contextNodeIndex = i;
                panel.contextNodeIndices = GetContextActionNodes(*active, i);
            }
            const float menuHeight = getContextMenuHeight();
            panel.contextPosition = Vector2{
                ClampFloat(mouse.x, 4.0f, static_cast<float>(GetScreenWidth()) - kNodeContextMenuW - 4.0f),
                ClampFloat(mouse.y, 4.0f, static_cast<float>(GetScreenHeight()) - menuHeight - 4.0f)
            };
            panel.contextMenuOpen = true;
            panel.contextMenuJustOpened = true;
        }

        rowY += rowH;
    }

    const int visibleCount = CountVisibleSceneNodes(active->loaded, active->collapsedNodes);
    const float visibleRows = std::max(1.0f, std::floor((panelH - 64.0f) / rowH));
    const float maxScroll = std::max(0.0f, static_cast<float>(visibleCount) - visibleRows);
    panel.scroll = ClampFloat(panel.scroll, 0.0f, maxScroll);

    if (maxScroll > 0.0f)
    {
        const float trackY = GetHierarchyContentStartY();
        const float trackH = panelH - 68.0f;
        const float thumbH = std::max(28.0f, trackH * (visibleRows / static_cast<float>(visibleCount)));
        const float thumbY = trackY + (trackH - thumbH) * (panel.scroll / maxScroll);
        DrawRectangle(static_cast<int>(panelW - 8.0f), static_cast<int>(trackY), 4, static_cast<int>(trackH), Color{ 44, 49, 55, 255 });
        DrawRectangle(static_cast<int>(panelW - 9.0f), static_cast<int>(thumbY), 6, static_cast<int>(thumbH), Color{ 112, 124, 136, 255 });
    }

    if (panel.contextMenuOpen)
    {
        const Rectangle menu{ panel.contextPosition.x, panel.contextPosition.y, kNodeContextMenuW, getContextMenuHeight() };
        DrawRectangleRec(menu, Color{ 24, 27, 31, 248 });
        DrawRectangleLinesEx(menu, 1.0f, Color{ 84, 94, 104, 255 });
        const bool validContextNode = panel.contextNodeIndex >= 0 &&
                                      panel.contextNodeIndex < static_cast<int>(active->loaded.nodes.size()) &&
                                      !IsDeletedNode(*active, panel.contextNodeIndex);
        const std::vector<int> contextNodes = GetContextActionNodes(*active, panel.contextNodeIndex, panel.contextNodeIndices);
        const std::vector<int> contextRoots = GetContextActionRoots(*active, contextNodes);
        const bool validContextBone = validContextNode && HasBoneNode(*active, contextNodes);
        const bool multiContext = contextNodes.size() > 1;
        if (panel.contextMenuJustOpened)
        {
            panel.contextMenuJustOpened = false;
        }
        else
        {
            const Rectangle renameItem{ menu.x, menu.y, menu.width, 30.0f };
            const Rectangle deleteItem{ menu.x, menu.y + 30.0f, menu.width, 30.0f };
            const Rectangle applyScaleItem{ menu.x, menu.y + 60.0f, menu.width, 30.0f };
            const Rectangle resetBindPoseItem{ menu.x, menu.y + 90.0f, menu.width, 30.0f };
            if (DrawPanelButton(font, renameItem, "Rename"))
            {
                if (validContextNode)
                {
                    StartRenameNode(renameEditor, active->loaded, panel.contextNodeIndex);
                }
                panel.contextMenuOpen = false;
            }
            if (DrawPanelButton(font, deleteItem, "Delete"))
            {
                if (validContextNode && !contextRoots.empty())
                {
                    PushUndoSnapshot(*active);
                    const int deletedCount = DeleteContextNodeSubtrees(*active, contextRoots);
                    notice = "Deleted tree object" + std::string(deletedCount == 1 ? "." : "s.");
                    error.clear();
                }
                panel.contextMenuOpen = false;
            }
            if (DrawPanelButton(font, applyScaleItem, "Apply Scale"))
            {
                if (validContextNode)
                {
                    EditSnapshot before = CaptureEditSnapshot(*active);
                    const int changedCount = ApplyScaleToContextNodes(*active, contextNodes);
                    if (changedCount > 0)
                    {
                        PushUndoSnapshot(*active, std::move(before));
                        if (multiContext)
                        {
                            notice = "Applied scale to " + std::to_string(changedCount) + " object" + std::string(changedCount == 1 ? "." : "s.");
                        }
                        else
                        {
                            notice = "Applied scale.";
                        }
                    }
                    else
                    {
                        notice = "Scale already applied.";
                    }
                    error.clear();
                }
                panel.contextMenuOpen = false;
            }
            if (validContextBone && DrawPanelButton(font, resetBindPoseItem, "Reset Bind Pose"))
            {
                EditSnapshot before = CaptureEditSnapshot(*active);
                const int changedCount = ResetContextBoneSubtreesToOriginalBindPose(*active, contextNodes);
                if (changedCount > 0)
                {
                    PushUndoSnapshot(*active, std::move(before));
                    if (multiContext)
                    {
                        notice = "Reset " + std::to_string(changedCount) + " bone subtree" + std::string(changedCount == 1 ? " to bind pose." : "s to bind pose.");
                    }
                    else
                    {
                        notice = "Bone subtree reset to bind pose.";
                    }
                }
                else
                {
                    notice = "Bone subtree already at bind pose.";
                }
                error.clear();
                panel.contextMenuOpen = false;
            }
            if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && !CheckCollisionPointRec(mouse, menu))
            {
                panel.contextMenuOpen = false;
            }
        }
    }
}

bool DrawMenuItem(Font font, Rectangle bounds, const char* text, bool selected = false)
{
    const Vector2 mouse = GetMousePosition();
    const bool hovered = CheckCollisionPointRec(mouse, bounds);
    DrawRectangleRec(bounds, selected ? Color{ 48, 70, 90, 255 } : hovered ? Color{ 46, 50, 56, 255 } : Color{ 28, 31, 35, 245 });
    DrawUiText(font, text, bounds.x + 10.0f, bounds.y + 5.0f, 16.0f, selected ? RAYWHITE : Color{ 205, 213, 220, 255 });
    return hovered && IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
}

void DrawMenuBar(Font font,
                 OpenMenu& openMenu,
                 bool& openRequested,
                 bool& undoRequested,
                 bool& redoRequested,
                 bool& saveFbxRequested,
                 bool& saveAsFbxRequested,
                 bool& importAnimationsRequested,
                 bool& exportJsonRequested,
                 bool& compareFbxRequested,
                 bool& quitRequested,
                 ViewMode& viewMode,
                 NavigationPreset& navigation,
                 VisibilityState& visibility,
                 bool canUndo,
                 bool canRedo)
{
    const float menuHeight = 28.0f;
    DrawRectangle(0, 0, GetScreenWidth(), static_cast<int>(menuHeight), Color{ 24, 26, 29, 255 });
    DrawLine(0, static_cast<int>(menuHeight), GetScreenWidth(), static_cast<int>(menuHeight), Color{ 58, 64, 70, 255 });

    struct MenuButton
    {
        const char* label;
        OpenMenu menu;
        Rectangle bounds;
    };

    const MenuButton buttons[] = {
        { "File", OpenMenu::File, Rectangle{ 8.0f, 3.0f, 54.0f, 22.0f } },
        { "Edit", OpenMenu::Edit, Rectangle{ 66.0f, 3.0f, 58.0f, 22.0f } },
        { "View", OpenMenu::View, Rectangle{ 124.0f, 3.0f, 58.0f, 22.0f } },
        { "Preferences", OpenMenu::Preferences, Rectangle{ 186.0f, 3.0f, 118.0f, 22.0f } }
    };

    const Vector2 mouse = GetMousePosition();
    for (const MenuButton& button : buttons)
    {
        const bool active = openMenu == button.menu;
        const bool hovered = CheckCollisionPointRec(mouse, button.bounds);
        DrawRectangleRec(button.bounds, active ? Color{ 52, 60, 68, 255 } : hovered ? Color{ 42, 46, 51, 255 } : Color{ 24, 26, 29, 255 });
        DrawUiText(font, button.label, button.bounds.x + 8.0f, button.bounds.y + 3.0f, 16.0f, RAYWHITE);
        if (hovered && IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
        {
            openMenu = active ? OpenMenu::None : button.menu;
        }
    }

    Rectangle openMenuBounds{};
    switch (openMenu)
    {
    case OpenMenu::File:
        openMenuBounds = Rectangle{ 8.0f, 29.0f, 270.0f, 218.0f };
        break;
    case OpenMenu::Edit:
        openMenuBounds = Rectangle{ 66.0f, 29.0f, 230.0f, 68.0f };
        break;
    case OpenMenu::View:
        openMenuBounds = Rectangle{ 124.0f, 29.0f, 230.0f, 308.0f };
        break;
    case OpenMenu::Preferences:
        openMenuBounds = Rectangle{ 186.0f, 29.0f, 420.0f, 318.0f };
        break;
    case OpenMenu::None:
        break;
    }

    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) &&
        openMenu != OpenMenu::None &&
        mouse.y > menuHeight &&
        !CheckCollisionPointRec(mouse, openMenuBounds))
    {
        openMenu = OpenMenu::None;
    }

    if (openMenu == OpenMenu::File)
    {
        DrawRectangle(8, 29, 270, 218, Color{ 28, 31, 35, 245 });
        if (DrawMenuItem(font, Rectangle{ 8.0f, 29.0f, 270.0f, 30.0f }, "Open FBX...        Ctrl+O"))
        {
            openRequested = true;
            openMenu = OpenMenu::None;
        }
        if (DrawMenuItem(font, Rectangle{ 8.0f, 59.0f, 270.0f, 30.0f }, "Save FBX        Ctrl+S"))
        {
            saveFbxRequested = true;
            openMenu = OpenMenu::None;
        }
        if (DrawMenuItem(font, Rectangle{ 8.0f, 89.0f, 270.0f, 30.0f }, "Save As...        Ctrl+Shift+S"))
        {
            saveAsFbxRequested = true;
            openMenu = OpenMenu::None;
        }
        if (DrawMenuItem(font, Rectangle{ 8.0f, 119.0f, 270.0f, 30.0f }, "Import Animations..."))
        {
            importAnimationsRequested = true;
            openMenu = OpenMenu::None;
        }
        if (DrawMenuItem(font, Rectangle{ 8.0f, 149.0f, 270.0f, 30.0f }, "Export JSON"))
        {
            exportJsonRequested = true;
            openMenu = OpenMenu::None;
        }
        if (DrawMenuItem(font, Rectangle{ 8.0f, 179.0f, 270.0f, 30.0f }, "Compare FBX..."))
        {
            compareFbxRequested = true;
            openMenu = OpenMenu::None;
        }
        if (DrawMenuItem(font, Rectangle{ 8.0f, 209.0f, 270.0f, 30.0f }, "Exit        Ctrl+Q"))
        {
            quitRequested = true;
            openMenu = OpenMenu::None;
        }
    }
    else if (openMenu == OpenMenu::Edit)
    {
        DrawRectangle(66, 29, 230, 68, Color{ 28, 31, 35, 245 });
        if (DrawMenuItem(font, Rectangle{ 66.0f, 29.0f, 230.0f, 30.0f }, canUndo ? "Undo        Ctrl+Z" : "Undo        Ctrl+Z", false))
        {
            if (canUndo) undoRequested = true;
            openMenu = OpenMenu::None;
        }
        if (DrawMenuItem(font, Rectangle{ 66.0f, 59.0f, 230.0f, 30.0f }, canRedo ? "Redo        Ctrl+Y" : "Redo        Ctrl+Y", false))
        {
            if (canRedo) redoRequested = true;
            openMenu = OpenMenu::None;
        }
        if (!canUndo)
        {
            DrawRectangleRec(Rectangle{ 66.0f, 29.0f, 230.0f, 30.0f }, Color{ 28, 31, 35, 160 });
            DrawUiText(font, "Undo        Ctrl+Z", 76.0f, 34.0f, 16.0f, Color{ 105, 115, 124, 255 });
        }
        if (!canRedo)
        {
            DrawRectangleRec(Rectangle{ 66.0f, 59.0f, 230.0f, 30.0f }, Color{ 28, 31, 35, 160 });
            DrawUiText(font, "Redo        Ctrl+Y", 76.0f, 64.0f, 16.0f, Color{ 105, 115, 124, 255 });
        }
    }
    else if (openMenu == OpenMenu::View)
    {
        DrawRectangle(124, 29, 230, 308, Color{ 28, 31, 35, 245 });
        if (DrawMenuItem(font, Rectangle{ 124.0f, 29.0f, 230.0f, 30.0f }, "Shaded", viewMode == ViewMode::Shaded))
        {
            viewMode = ViewMode::Shaded;
        }
        if (DrawMenuItem(font, Rectangle{ 124.0f, 59.0f, 230.0f, 30.0f }, "Shaded Wireframe", viewMode == ViewMode::ShadedWireframe))
        {
            viewMode = ViewMode::ShadedWireframe;
        }
        if (DrawMenuItem(font, Rectangle{ 124.0f, 89.0f, 230.0f, 30.0f }, "Wireframe", viewMode == ViewMode::Wireframe))
        {
            viewMode = ViewMode::Wireframe;
        }
        if (DrawMenuItem(font, Rectangle{ 124.0f, 127.0f, 230.0f, 30.0f }, visibility.geometry ? "[x] Geometry        G" : "[ ] Geometry        G"))
        {
            visibility.geometry = !visibility.geometry;
        }
        if (DrawMenuItem(font, Rectangle{ 124.0f, 157.0f, 230.0f, 30.0f }, visibility.textures ? "[x] Textures" : "[ ] Textures"))
        {
            visibility.textures = !visibility.textures;
        }
        if (DrawMenuItem(font, Rectangle{ 124.0f, 187.0f, 230.0f, 30.0f }, visibility.backfaceCulling ? "[x] Backface Culling" : "[ ] Backface Culling"))
        {
            visibility.backfaceCulling = !visibility.backfaceCulling;
        }
        if (DrawMenuItem(font, Rectangle{ 124.0f, 217.0f, 230.0f, 30.0f }, visibility.bones ? "[x] Bones        B" : "[ ] Bones        B"))
        {
            visibility.bones = !visibility.bones;
        }
        if (DrawMenuItem(font, Rectangle{ 124.0f, 247.0f, 230.0f, 30.0f }, visibility.boneRotations ? "[x] Bone Orientation  O" : "[ ] Bone Orientation  O"))
        {
            visibility.boneRotations = !visibility.boneRotations;
        }
        if (DrawMenuItem(font, Rectangle{ 124.0f, 277.0f, 230.0f, 30.0f }, visibility.empties ? "[x] Empties" : "[ ] Empties"))
        {
            visibility.empties = !visibility.empties;
        }
        if (DrawMenuItem(font, Rectangle{ 124.0f, 307.0f, 230.0f, 30.0f }, visibility.skinWeights ? "[x] Skin Weights" : "[ ] Skin Weights"))
        {
            visibility.skinWeights = !visibility.skinWeights;
        }
    }
    else if (openMenu == OpenMenu::Preferences)
    {
        DrawRectangle(186, 29, 420, 318, Color{ 28, 31, 35, 245 });
        DrawUiText(font, "NAVIGATION", 198.0f, 39.0f, 16.0f, Color{ 165, 182, 196, 255 });
        if (DrawMenuItem(font, Rectangle{ 196.0f, 64.0f, 185.0f, 30.0f }, "Blender", navigation == NavigationPreset::Blender))
        {
            navigation = NavigationPreset::Blender;
        }
        if (DrawMenuItem(font, Rectangle{ 391.0f, 64.0f, 185.0f, 30.0f }, "Maya", navigation == NavigationPreset::Maya))
        {
            navigation = NavigationPreset::Maya;
        }

        DrawUiText(font, "GIZMO", 198.0f, 110.0f, 16.0f, Color{ 165, 182, 196, 255 });
        char gizmoSizeText[64] = {};
        std::snprintf(gizmoSizeText, sizeof(gizmoSizeText), "Size: %d%%", static_cast<int>(std::round(gTransformGizmoScale * 100.0f)));
        DrawUiText(font, gizmoSizeText, 198.0f, 139.0f, 15.0f, Color{ 205, 213, 220, 255 });
        if (DrawPanelButton(font, Rectangle{ 330.0f, 132.0f, 34.0f, 26.0f }, "-"))
        {
            gTransformGizmoScale = ClampFloat(gTransformGizmoScale - 0.1f, kMinTransformGizmoScale, kMaxTransformGizmoScale);
        }
        if (DrawPanelButton(font, Rectangle{ 372.0f, 132.0f, 34.0f, 26.0f }, "+"))
        {
            gTransformGizmoScale = ClampFloat(gTransformGizmoScale + 0.1f, kMinTransformGizmoScale, kMaxTransformGizmoScale);
        }
        if (DrawPanelButton(font, Rectangle{ 416.0f, 132.0f, 70.0f, 26.0f }, "Reset"))
        {
            gTransformGizmoScale = 1.0f;
        }

        DrawUiText(font, "HOTKEYS", 198.0f, 184.0f, 16.0f, Color{ 165, 182, 196, 255 });
        DrawUiText(font, "Q/W/E/R tools    Ctrl+O open FBX    V view mode", 198.0f, 210.0f, 15.0f, Color{ 205, 213, 220, 255 });
        DrawUiText(font, "Ctrl+Z undo    Ctrl+Y redo    T textures    C channels", 198.0f, 236.0f, 15.0f, Color{ 205, 213, 220, 255 });
        DrawUiText(font, "Blender: MMB orbit, Alt snap, Shift+MMB pan, Wheel zoom", 198.0f, 262.0f, 15.0f, Color{ 205, 213, 220, 255 });
        DrawUiText(font, "Maya: Alt+LMB orbit, Shift snap, Alt+MMB pan, Alt+RMB/Wheel zoom", 198.0f, 288.0f, 15.0f, Color{ 205, 213, 220, 255 });
        DrawUiText(font, "Esc deselects    Ctrl+Q quits    Tabs: X/middle closes", 198.0f, 314.0f, 15.0f, Color{ 205, 213, 220, 255 });
    }
}

void DrawSkeletonCompareResultWindow(Font font, bool& visible, bool compatible, const std::string& path, const std::string& result)
{
    if (!visible) return;

    const float w = 520.0f;
    const float h = 300.0f;
    const Rectangle bounds{ (static_cast<float>(GetScreenWidth()) - w) * 0.5f, (static_cast<float>(GetScreenHeight()) - h) * 0.5f, w, h };
    const Vector2 mouse = GetMousePosition();
    DrawRectangle(0, 0, GetScreenWidth(), GetScreenHeight(), Color{ 0, 0, 0, 90 });
    DrawRectangleRec(bounds, Color{ 22, 25, 29, 245 });
    DrawRectangleLinesEx(bounds, 1.0f, Color{ 86, 96, 108, 255 });

    DrawUiText(font, "SKELETON COMPATIBILITY", bounds.x + 16.0f, bounds.y + 14.0f, 17.0f, Color{ 165, 182, 196, 255 });
    const Rectangle closeButton{ bounds.x + bounds.width - 38.0f, bounds.y + 10.0f, 26.0f, 24.0f };
    if (DrawPanelButton(font, closeButton, "x") || IsKeyPressed(KEY_ESCAPE))
    {
        visible = false;
        return;
    }

    float y = bounds.y + 48.0f;
    DrawUiText(font, compatible ? "Compatible" : "Not compatible", bounds.x + 16.0f, y, 18.0f, compatible ? Color{ 150, 225, 170, 255 } : Color{ 255, 170, 135, 255 });
    y += 30.0f;
    DrawUiTextClipped(font, path.c_str(), bounds.x + 16.0f, y, 13.0f, bounds.width - 32.0f, Color{ 160, 205, 230, 255 });
    y += 34.0f;

    std::string remaining = result;
    while (!remaining.empty() && y < bounds.y + bounds.height - 22.0f)
    {
        const size_t newline = remaining.find('\n');
        const std::string line = newline == std::string::npos ? remaining : remaining.substr(0, newline);
        DrawUiTextClipped(font, line.c_str(), bounds.x + 16.0f, y, 15.0f, bounds.width - 32.0f, Color{ 205, 213, 220, 255 });
        y += 22.0f;
        if (newline == std::string::npos) break;
        remaining.erase(0, newline + 1);
    }

    if (CheckCollisionPointRec(mouse, bounds))
    {
        // Keep clicks inside the result dialog from interacting with UI below on the same frame.
    }
}

void CloseTab(std::vector<std::unique_ptr<ModelTab>>& tabs, int& activeTab, int tabIndex)
{
    if (tabIndex < 0 || tabIndex >= static_cast<int>(tabs.size())) return;

    UnloadPbrTextures(*tabs[static_cast<size_t>(tabIndex)]);
    UnloadFbxModel(tabs[static_cast<size_t>(tabIndex)]->loaded);
    tabs.erase(tabs.begin() + tabIndex);

    if (tabs.empty())
    {
        activeTab = -1;
    }
    else if (activeTab >= static_cast<int>(tabs.size()))
    {
        activeTab = static_cast<int>(tabs.size()) - 1;
    }
    else if (tabIndex < activeTab)
    {
        --activeTab;
    }
}

void DrawTabs(Font font, std::vector<std::unique_ptr<ModelTab>>& tabs, int& activeTab)
{
    const float y = 29.0f;
    const float height = 31.0f;
    DrawRectangle(0, static_cast<int>(y), GetScreenWidth(), static_cast<int>(height), Color{ 30, 33, 37, 255 });
    DrawLine(0, static_cast<int>(y + height), GetScreenWidth(), static_cast<int>(y + height), Color{ 58, 64, 70, 255 });

    float x = 8.0f;
    const Vector2 mouse = GetMousePosition();
    int closeIndex = -1;

    for (int i = 0; i < static_cast<int>(tabs.size()); ++i)
    {
        const std::string& title = tabs[static_cast<size_t>(i)]->title;
        const float textWidth = MeasureTextEx(font, title.c_str(), 15.0f, 1.0f).x;
        const float tabWidth = ClampFloat(textWidth + 48.0f, 120.0f, 240.0f);
        const Rectangle tab{ x, y + 4.0f, tabWidth, 26.0f };
        const Rectangle close{ x + tabWidth - 24.0f, y + 8.0f, 16.0f, 16.0f };
        const bool active = i == activeTab;
        const bool hovered = CheckCollisionPointRec(mouse, tab);

        DrawRectangleRec(tab, active ? Color{ 50, 56, 64, 255 } : hovered ? Color{ 40, 44, 50, 255 } : Color{ 35, 38, 43, 255 });
        DrawRectangleLinesEx(tab, 1.0f, Color{ 68, 75, 84, 255 });
        DrawUiText(font, title.c_str(), tab.x + 10.0f, tab.y + 5.0f, 15.0f, active ? RAYWHITE : Color{ 190, 198, 206, 255 });
        DrawUiText(font, "x", close.x + 4.0f, close.y - 1.0f, 16.0f, CheckCollisionPointRec(mouse, close) ? Color{ 255, 150, 130, 255 } : Color{ 170, 178, 186, 255 });

        if (hovered && IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && !CheckCollisionPointRec(mouse, close))
        {
            activeTab = i;
        }
        if ((hovered && IsMouseButtonPressed(MOUSE_BUTTON_MIDDLE)) ||
            (CheckCollisionPointRec(mouse, close) && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)))
        {
            closeIndex = i;
        }

        x += tabWidth + 4.0f;
        if (x > static_cast<float>(GetScreenWidth()) - 40.0f) break;
    }

    if (closeIndex >= 0)
    {
        CloseTab(tabs, activeTab, closeIndex);
    }
}

void UpdateAnimation(AnimationState& animation, const LoadedFbxModel& loaded)
{
    if (IsKeyPressed(KEY_SPACE) && !loaded.animations.empty())
    {
        animation.playing = !animation.playing;
    }

    if (animation.clipIndex < 0 || animation.clipIndex >= static_cast<int>(loaded.animations.size()))
    {
        return;
    }

    const AnimationClip& clip = loaded.animations[static_cast<size_t>(animation.clipIndex)];
    if (animation.playing && clip.duration > 0.0f)
    {
        animation.time += GetFrameTime();
        while (animation.time > clip.duration)
        {
            animation.time -= clip.duration;
        }
    }
}

void DrawTimeline(Font font, ModelTab& tab, RenameEditor& renameEditor, bool& collapsed)
{
    LoadedFbxModel& loaded = tab.loaded;
    AnimationState& animation = tab.animation;
    const int width = GetScreenWidth();
    const int height = GetScreenHeight();
    const float panelHeight = collapsed ? kTimelineCollapsedHeight : kTimelinePanelHeight;
    const float panelY = static_cast<float>(height) - panelHeight;
    const float listWidth = 300.0f;
    const Vector2 mouse = GetMousePosition();
    const Rectangle toggleButton{ static_cast<float>(width) - 34.0f, panelY + 4.0f, 24.0f, 20.0f };

    DrawRectangle(0, static_cast<int>(panelY), width, static_cast<int>(panelHeight), Color{ 20, 22, 24, 238 });
    DrawLine(0, static_cast<int>(panelY), width, static_cast<int>(panelY), Color{ 76, 84, 92, 255 });
    DrawUiText(font, "ANIMATIONS", 12.0f, panelY + 7.0f, 16.0f, Color{ 165, 182, 196, 255 });
    if (DrawPanelButton(font, toggleButton, collapsed ? "^" : "v"))
    {
        collapsed = !collapsed;
        animation.scrubbing = false;
    }
    if (collapsed)
    {
        return;
    }

    DrawLine(static_cast<int>(listWidth), static_cast<int>(panelY), static_cast<int>(listWidth), height, Color{ 64, 70, 78, 255 });

    if (loaded.animations.empty())
    {
        DrawUiText(font, "No FBX animation stacks", 12.0f, panelY + 64.0f, 16.0f, Color{ 128, 136, 144, 255 });
    }

    const Rectangle noAnimationRow{ 10.0f, panelY + 34.0f, listWidth - 20.0f, 22.0f };
    const bool noAnimationSelected = animation.clipIndex < 0;
    if (noAnimationSelected)
    {
        DrawRectangleRec(noAnimationRow, Color{ 50, 70, 88, 255 });
    }
    else if (CheckCollisionPointRec(mouse, noAnimationRow))
    {
        DrawRectangleRec(noAnimationRow, Color{ 36, 42, 48, 255 });
    }
    DrawUiText(font, "Bind pose", noAnimationRow.x + 8.0f, noAnimationRow.y + 3.0f, 15.0f, noAnimationSelected ? RAYWHITE : Color{ 185, 194, 202, 255 });
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(mouse, noAnimationRow))
    {
        animation.clipIndex = -1;
        animation.time = 0.0f;
        animation.playing = false;
        animation.scrubbing = false;
    }

    const float rowHeight = 26.0f;
    const Rectangle clipListBounds{ 8.0f, panelY + 58.0f, listWidth - 16.0f, panelHeight - 62.0f };
    const int visibleRows = std::max(1, static_cast<int>(std::floor(clipListBounds.height / rowHeight)));
    const int maxClipScroll = std::max(0, static_cast<int>(loaded.animations.size()) - visibleRows);
    animation.clipScroll = ClampInt(animation.clipScroll, 0, maxClipScroll);
    if (!loaded.animations.empty() && CheckCollisionPointRec(mouse, clipListBounds))
    {
        const float wheel = GetMouseWheelMove();
        if (std::fabs(wheel) > 0.0f)
        {
            animation.clipScroll = ClampInt(animation.clipScroll - static_cast<int>(wheel), 0, maxClipScroll);
        }
    }

    BeginScissorMode(static_cast<int>(clipListBounds.x),
                     static_cast<int>(clipListBounds.y),
                     static_cast<int>(clipListBounds.width),
                     static_cast<int>(clipListBounds.height));
    const Rectangle contextMenuBounds{ animation.contextPosition.x, animation.contextPosition.y, 152.0f, 62.0f };
    const bool mouseOverContextMenu = animation.contextMenuOpen && CheckCollisionPointRec(mouse, contextMenuBounds);
    for (int visible = 0; visible < visibleRows; ++visible)
    {
        const int i = animation.clipScroll + visible;
        if (i >= static_cast<int>(loaded.animations.size())) break;

        const float rowY = clipListBounds.y + static_cast<float>(visible) * rowHeight;
        const Rectangle row{ 10.0f, rowY, listWidth - 20.0f, 22.0f };
        const bool selected = i == animation.clipIndex;

        if (selected)
        {
            DrawRectangleRec(row, Color{ 50, 70, 88, 255 });
        }
        else if (CheckCollisionPointRec(mouse, row))
        {
            DrawRectangleRec(row, Color{ 36, 42, 48, 255 });
        }

        if (!mouseOverContextMenu && IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(mouse, row))
        {
            animation.clipIndex = i;
            animation.time = 0.0f;
            animation.playing = true;
            animation.scrubbing = false;
            animation.contextMenuOpen = false;
        }
        if (!mouseOverContextMenu && IsMouseButtonPressed(MOUSE_BUTTON_RIGHT) && CheckCollisionPointRec(mouse, row))
        {
            animation.contextClipIndex = i;
            animation.contextPosition = Vector2{
                ClampFloat(mouse.x, 4.0f, static_cast<float>(GetScreenWidth()) - 156.0f),
                ClampFloat(mouse.y, 4.0f, static_cast<float>(GetScreenHeight()) - 66.0f)
            };
            animation.contextMenuOpen = true;
            animation.contextMenuJustOpened = true;
        }

        char rowText[256] = {};
        std::snprintf(rowText, sizeof(rowText), "%s  %.2fs", loaded.animations[static_cast<size_t>(i)].name.c_str(), loaded.animations[static_cast<size_t>(i)].duration);
        DrawUiText(font, rowText, row.x + 8.0f, row.y + 3.0f, 15.0f, selected ? RAYWHITE : Color{ 185, 194, 202, 255 });
    }
    EndScissorMode();

    if (maxClipScroll > 0)
    {
        const Rectangle track{ listWidth - 10.0f, clipListBounds.y, 4.0f, clipListBounds.height };
        const float thumbHeight = std::max(18.0f, track.height * (static_cast<float>(visibleRows) / static_cast<float>(loaded.animations.size())));
        const float thumbTravel = std::max(1.0f, track.height - thumbHeight);
        const float thumbY = track.y + thumbTravel * (static_cast<float>(animation.clipScroll) / static_cast<float>(maxClipScroll));
        DrawRectangleRec(track, Color{ 42, 48, 54, 255 });
        DrawRectangleRec(Rectangle{ track.x, thumbY, track.width, thumbHeight }, Color{ 130, 145, 158, 255 });
    }

    if (animation.contextMenuOpen)
    {
        const Rectangle menu{ animation.contextPosition.x, animation.contextPosition.y, 152.0f, 62.0f };
        DrawRectangleRec(menu, Color{ 24, 27, 31, 248 });
        DrawRectangleLinesEx(menu, 1.0f, Color{ 84, 94, 104, 255 });
        const bool validContextClip = animation.contextClipIndex >= 0 && animation.contextClipIndex < static_cast<int>(loaded.animations.size());
        if (animation.contextMenuJustOpened)
        {
            animation.contextMenuJustOpened = false;
        }
        else
        {
            const Rectangle renameItem{ menu.x, menu.y, menu.width, 30.0f };
            const Rectangle deleteItem{ menu.x, menu.y + 30.0f, menu.width, 30.0f };
            if (validContextClip && DrawPanelButton(font, renameItem, "Rename"))
            {
                StartRenameAnimation(renameEditor, loaded, animation.contextClipIndex);
                animation.contextMenuOpen = false;
                animation.contextClipIndex = -1;
            }
            if (validContextClip && DrawPanelButton(font, deleteItem, "Delete"))
            {
                PushUndoSnapshot(tab);
                loaded.animations.erase(loaded.animations.begin() + animation.contextClipIndex);
                if (animation.clipIndex == animation.contextClipIndex)
                {
                    animation.clipIndex = -1;
                    animation.time = 0.0f;
                    animation.playing = false;
                    animation.scrubbing = false;
                }
                else if (animation.clipIndex > animation.contextClipIndex)
                {
                    --animation.clipIndex;
                }
                animation.contextMenuOpen = false;
                animation.contextClipIndex = -1;
                animation.clipScroll = ClampInt(animation.clipScroll, 0, std::max(0, static_cast<int>(loaded.animations.size()) - visibleRows));
            }
            else if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && !CheckCollisionPointRec(mouse, menu))
            {
                animation.contextMenuOpen = false;
            }
        }
    }

    const AnimationClip* clip = nullptr;
    if (animation.clipIndex >= 0 && animation.clipIndex < static_cast<int>(loaded.animations.size()))
    {
        clip = &loaded.animations[static_cast<size_t>(animation.clipIndex)];
    }

    const float timelineX = listWidth + 24.0f;
    const float timelineY = panelY + 48.0f;
    const float timelineW = static_cast<float>(width) - timelineX - 24.0f;
    const Rectangle scrub{ timelineX, timelineY, timelineW, 14.0f };
    const Rectangle scrubHitbox{ scrub.x, scrub.y - 12.0f, scrub.width, scrub.height + 24.0f };

    DrawUiText(font, animation.playing ? "PLAYING  [SPACE]" : "PAUSED   [SPACE]", timelineX, panelY + 14.0f, 16.0f, Color{ 165, 182, 196, 255 });
    DrawRectangleRec(scrub, Color{ 58, 64, 70, 255 });

    if (clip && clip->duration > 0.0f)
    {
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(mouse, scrubHitbox))
        {
            animation.scrubbing = true;
        }
        if (!IsMouseButtonDown(MOUSE_BUTTON_LEFT))
        {
            animation.scrubbing = false;
        }
        if (animation.scrubbing)
        {
            const float alpha = ClampFloat((mouse.x - scrub.x) / scrub.width, 0.0f, 1.0f);
            animation.time = alpha * clip->duration;
            animation.playing = false;
        }

        const float progress = ClampFloat(animation.time / clip->duration, 0.0f, 1.0f);
        DrawRectangleRec(Rectangle{ scrub.x, scrub.y, scrub.width * progress, scrub.height }, Color{ 94, 156, 214, 255 });
        DrawRectangle(static_cast<int>(scrub.x + scrub.width * progress - 2.0f), static_cast<int>(scrub.y - 5.0f), 4, 24, Color{ 220, 232, 242, 255 });

        char timeText[128] = {};
        const int totalFrames = static_cast<int>(std::max(clip->frames.size(), clip->meshFrames.size()));
        const int currentFrame = totalFrames > 0 ? static_cast<int>(ClampFloat(std::round(progress * static_cast<float>(totalFrames - 1)), 0.0f, static_cast<float>(totalFrames - 1))) + 1 : 0;
        std::snprintf(timeText, sizeof(timeText), "%.2fs / %.2fs    frame: %d / %d", animation.time, clip->duration, currentFrame, totalFrames);
        DrawUiText(font, timeText, timelineX, timelineY + 28.0f, 16.0f, Color{ 190, 200, 210, 255 });
    }
    else
    {
        animation.scrubbing = false;
        DrawUiText(font, "Bind pose", timelineX, timelineY + 28.0f, 16.0f, Color{ 128, 136, 144, 255 });
    }
}
}

int main(int argc, char** argv)
{
    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_MSAA_4X_HINT);
    InitWindow(1280, 800, "openfbx");
    rlSetClipPlanes(kNearClipPlane, kFarClipPlane);
    SetExitKey(KEY_NULL);
    ApplyWindowIcon();
    SetTargetFPS(60);

    Font uiFont = LoadTechnicalFont();
    LitShader litShader = LoadBasicLitShader();
    OrbitCamera emptyOrbit = CreateDefaultCamera();
    std::vector<std::unique_ptr<ModelTab>> tabs;
    int activeTab = -1;
    ViewMode viewMode = ViewMode::Shaded;
    MaterialPreviewMode materialPreviewMode = MaterialPreviewMode::Shaded;
    NavigationPreset navigation = NavigationPreset::Blender;
    OpenMenu openMenu = OpenMenu::None;
    HierarchyPanelState hierarchyPanel;
    VisibilityState visibility;
    std::string error;
    std::string notice;
    bool quitRequested = false;
    bool animationPanelCollapsed = false;
    bool compareResultVisible = false;
    bool compareResultCompatible = false;
    std::string compareResultPath;
    std::string compareResultText;
    TextureClipboard textureClipboard;
    RenameEditor renameEditor;
    TransformTool transformTool = TransformTool::Select;
    GizmoOrientation gizmoOrientation = GizmoOrientation::Global;
    TransformGizmoState transformGizmo;
    WeightBrushSettings weightBrush;
    WeightBrushState weightBrushState;

    auto openPathInNewTab = [&](const std::string& path)
    {
        DrawLoadingScreen(uiFont, path);

        auto tab = std::make_unique<ModelTab>();
        std::string loadError;
        if (!LoadFbxModel(path, tab->loaded, loadError))
        {
            error = loadError;
            notice.clear();
            std::cerr << error << "\n";
            return;
        }

        ApplyNeutralMaterial(tab->loaded);
        ApplyLitShader(tab->loaded, litShader);
        EnsurePbrMaterialStates(*tab);
        tab->orbit = CreateDefaultCamera();
        FocusCameraOnBounds(tab->orbit, tab->loaded.bounds);
        tab->animation.clipIndex = -1;
        tab->animation.time = 0.0f;
        tab->animation.playing = false;
        tab->originalBindBonePoses = tab->loaded.bonePoses;
        tab->visibleBones = tab->loaded.bones;
        tab->visibleBonePoses = tab->loaded.bonePoses;
        tab->collapsedNodes.assign(tab->loaded.nodes.size(), false);
        tab->deletedNodes.assign(tab->loaded.nodes.size(), false);
        tab->selectedNode = tab->loaded.nodes.empty() ? -1 : 0;
        if (tab->selectedNode >= 0)
        {
            tab->selectedNodes.assign(1, tab->selectedNode);
        }
        tab->isolatedNode = -1;
        tab->path = path;
        tab->title = MakeTabTitle(path);

        tabs.push_back(std::move(tab));
        activeTab = static_cast<int>(tabs.size()) - 1;
        error.clear();
        notice.clear();
    };

    for (int i = 1; i < argc; ++i)
    {
        openPathInNewTab(argv[i]);
    }

    while (!WindowShouldClose() && !quitRequested)
    {
        const bool controlDown = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL);
        const bool shiftDown = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);
        const bool altDown = IsKeyDown(KEY_LEFT_ALT) || IsKeyDown(KEY_RIGHT_ALT);
        bool openRequested = !renameEditor.active && controlDown && IsKeyPressed(KEY_O);
        bool saveFbxRequested = !renameEditor.active && controlDown && !shiftDown && IsKeyPressed(KEY_S);
        bool saveAsFbxRequested = !renameEditor.active && controlDown && shiftDown && IsKeyPressed(KEY_S);
        bool undoRequested = false;
        bool redoRequested = false;

        if (openRequested)
        {
            const std::string selectedPath = OpenFbxFileDialog();
            if (!selectedPath.empty())
            {
                openPathInNewTab(selectedPath);
            }
        }

        ModelTab* active = activeTab >= 0 && activeTab < static_cast<int>(tabs.size()) ? tabs[static_cast<size_t>(activeTab)].get() : nullptr;
        if (!renameEditor.active && active && controlDown && !shiftDown && IsKeyPressed(KEY_Z))
        {
            undoRequested = true;
        }
        if (!renameEditor.active && active &&
            ((controlDown && IsKeyPressed(KEY_Y)) || (controlDown && shiftDown && IsKeyPressed(KEY_Z))))
        {
            redoRequested = true;
        }
        gBottomPanelReservedHeight = animationPanelCollapsed ? kTimelineCollapsedHeight : kTimelinePanelHeight;
        const Vector2 mouse = GetMousePosition();
        std::vector<std::string> droppedPaths;
        bool droppedTextureHandled = false;
        if (IsFileDropped())
        {
            FilePathList droppedFiles = LoadDroppedFiles();
            droppedPaths.reserve(droppedFiles.count);
            for (unsigned int i = 0; i < droppedFiles.count; ++i)
            {
                droppedPaths.push_back(droppedFiles.paths[i]);
            }
            UnloadDroppedFiles(droppedFiles);
        }

        UpdateHierarchyPanelInteraction(hierarchyPanel, active);
        const float hierarchyBlockW = GetHierarchyPanelBlockWidth(hierarchyPanel);
        const Rectangle hierarchyContextMenuBounds{ hierarchyPanel.contextPosition.x, hierarchyPanel.contextPosition.y, kNodeContextMenuW, kNodeContextMenuBoneH };
        const bool mouseOverHierarchyContextMenu = hierarchyPanel.contextMenuOpen && CheckCollisionPointRec(mouse, hierarchyContextMenuBounds);
        const bool mouseInViewport = mouse.x > hierarchyBlockW &&
                                     mouse.y >= 61.0f &&
                                     mouse.y < static_cast<float>(GetScreenHeight()) - gBottomPanelReservedHeight &&
                                     openMenu == OpenMenu::None &&
                                     !hierarchyPanel.resizing &&
                                     !mouseOverHierarchyContextMenu;

        const bool toolbarConsumedMouse = !renameEditor.active && UpdateTransformToolbarInput(transformTool, gizmoOrientation, weightBrush, hierarchyBlockW);
        if (!renameEditor.active && !controlDown && !altDown)
        {
            if (IsKeyPressed(KEY_Q)) transformTool = TransformTool::Select;
            if (IsKeyPressed(KEY_W)) transformTool = TransformTool::Move;
            if (IsKeyPressed(KEY_E)) transformTool = TransformTool::Rotate;
            if (IsKeyPressed(KEY_R)) transformTool = TransformTool::Scale;
            if (IsKeyPressed(KEY_A)) transformTool = TransformTool::WeightsBrush;
        }
        const bool transformConsumedMouse = !toolbarConsumedMouse &&
                                            !renameEditor.active &&
                                            UpdateTransformGizmoInput(active, transformGizmo, transformTool, gizmoOrientation, mouseInViewport, notice, error);
        bool weightBrushConsumedMouse = false;
        if (!renameEditor.active &&
            active &&
            mouseInViewport &&
            transformTool == TransformTool::WeightsBrush &&
            IsMouseButtonDown(MOUSE_BUTTON_LEFT) &&
            !IsMouseButtonDown(MOUSE_BUTTON_RIGHT))
        {
            if (!weightBrushState.painting)
            {
                EditSnapshot before = CaptureEditSnapshot(*active);
                if (PaintSkinWeightsAtMouse(*active, mouse, visibility, weightBrush, notice, error))
                {
                    PushUndoSnapshot(*active, std::move(before));
                    weightBrushState.painting = true;
                    visibility.skinWeights = true;
                }
            }
            else
            {
                if (PaintSkinWeightsAtMouse(*active, mouse, visibility, weightBrush, notice, error))
                {
                    visibility.skinWeights = true;
                }
            }
            weightBrushConsumedMouse = true;
        }
        if (!IsMouseButtonDown(MOUSE_BUTTON_LEFT))
        {
            weightBrushState.painting = false;
        }

        if (!renameEditor.active && active && altDown && IsKeyPressed(KEY_Q))
        {
            ToggleSelectedNodeIsolation(*active, notice, error);
        }
        if (!renameEditor.active && controlDown && !altDown && IsKeyPressed(KEY_Q))
        {
            quitRequested = true;
            continue;
        }
        if (!renameEditor.active && IsKeyPressed(KEY_ESCAPE))
        {
            openMenu = OpenMenu::None;
            if (active)
            {
                ClearNodeSelection(*active);
            }
        }

        if (!renameEditor.active && active && IsKeyPressed(KEY_F) && active->loaded.valid)
        {
            if (!FocusCameraOnSelection(*active))
            {
                FocusCameraOnBounds(active->orbit, active->loaded.bounds);
            }
        }

        if (!renameEditor.active && IsKeyPressed(KEY_V))
        {
            viewMode = NextViewMode(viewMode);
        }
        if (!renameEditor.active && IsKeyPressed(KEY_C))
        {
            materialPreviewMode = NextMaterialPreviewMode(materialPreviewMode);
        }
        if (!renameEditor.active && IsKeyPressed(KEY_M))
        {
            materialPreviewMode = MaterialPreviewMode::Shaded;
        }
        if (!renameEditor.active && IsKeyPressed(KEY_T))
        {
            visibility.textures = !visibility.textures;
        }
        if (!renameEditor.active && IsKeyPressed(KEY_G))
        {
            visibility.geometry = !visibility.geometry;
        }
        if (!renameEditor.active && IsKeyPressed(KEY_B))
        {
            visibility.bones = !visibility.bones;
        }
        if (!renameEditor.active && !controlDown && IsKeyPressed(KEY_O))
        {
            visibility.boneRotations = !visibility.boneRotations;
        }
        if (active &&
            mouseInViewport &&
            !toolbarConsumedMouse &&
            !transformConsumedMouse &&
            !weightBrushConsumedMouse &&
            IsMouseButtonPressed(MOUSE_BUTTON_LEFT) &&
            !IsMouseButtonPressed(MOUSE_BUTTON_RIGHT) &&
            !IsMouseButtonDown(MOUSE_BUTTON_RIGHT))
        {
            if (SelectNodeFromViewport(*active, mouse, visibility, shiftDown))
            {
                RevealNodeInHierarchy(*active, hierarchyPanel, active->selectedNode);
            }
            else
            {
                if (!shiftDown)
                {
                    ClearNodeSelection(*active);
                }
            }
        }
        if (active &&
            mouseInViewport &&
            !altDown &&
            !toolbarConsumedMouse &&
            !transformConsumedMouse &&
            !weightBrushConsumedMouse &&
            IsMouseButtonPressed(MOUSE_BUTTON_RIGHT))
        {
            std::vector<int> selectedContextNodes = GetValidContextActionNodes(*active, active->selectedNodes);
            if (selectedContextNodes.size() > 1)
            {
                hierarchyPanel.contextNodeIndices = std::move(selectedContextNodes);
                hierarchyPanel.contextNodeIndex = active->selectedNode >= 0 ? active->selectedNode : hierarchyPanel.contextNodeIndices.front();
                const float nodeContextMenuH = HasBoneNode(*active, hierarchyPanel.contextNodeIndices) ? kNodeContextMenuBoneH : kNodeContextMenuBaseH;
                RevealNodeInHierarchy(*active, hierarchyPanel, hierarchyPanel.contextNodeIndex);
                hierarchyPanel.activeTab = LeftPanelTab::Hierarchy;
                hierarchyPanel.contextPosition = Vector2{
                    ClampFloat(mouse.x, 4.0f, static_cast<float>(GetScreenWidth()) - kNodeContextMenuW - 4.0f),
                    ClampFloat(mouse.y, 4.0f, static_cast<float>(GetScreenHeight()) - nodeContextMenuH - 4.0f)
                };
                hierarchyPanel.contextMenuOpen = true;
                hierarchyPanel.contextMenuJustOpened = true;
            }
            else
            {
                const int contextNode = PickNodeFromViewport(*active, mouse, visibility);
                if (contextNode >= 0)
                {
                    if (IsNodeSelected(*active, contextNode))
                    {
                        active->selectedNode = contextNode;
                        hierarchyPanel.contextNodeIndex = contextNode;
                        hierarchyPanel.contextNodeIndices = GetContextActionNodes(*active, contextNode);
                    }
                    else
                    {
                        SetSingleSelectedNode(*active, contextNode);
                        hierarchyPanel.contextNodeIndex = contextNode;
                        hierarchyPanel.contextNodeIndices = GetContextActionNodes(*active, contextNode);
                    }
                    const float nodeContextMenuH = HasBoneNode(*active, hierarchyPanel.contextNodeIndices) ? kNodeContextMenuBoneH : kNodeContextMenuBaseH;
                    RevealNodeInHierarchy(*active, hierarchyPanel, contextNode);
                    hierarchyPanel.activeTab = LeftPanelTab::Hierarchy;
                    hierarchyPanel.contextPosition = Vector2{
                        ClampFloat(mouse.x, 4.0f, static_cast<float>(GetScreenWidth()) - kNodeContextMenuW - 4.0f),
                        ClampFloat(mouse.y, 4.0f, static_cast<float>(GetScreenHeight()) - nodeContextMenuH - 4.0f)
                    };
                    hierarchyPanel.contextMenuOpen = true;
                    hierarchyPanel.contextMenuJustOpened = true;
                }
                else
                {
                    hierarchyPanel.contextMenuOpen = false;
                }
            }
        }

        if (active)
        {
            if (mouseInViewport && !toolbarConsumedMouse && !transformConsumedMouse && !weightBrushConsumedMouse)
            {
                UpdateNavigation(active->orbit, navigation);
            }
            else
            {
                UpdateOrbitCameraTransform(active->orbit);
            }

            if (!renameEditor.active)
            {
                UpdateAnimation(active->animation, active->loaded);
            }
            ApplyAnimatedMeshFrame(*active);
            ApplyAnimatedBoneFrame(*active);
            UpdateLitShader(litShader, active->orbit);
            EnsurePbrMaterialStates(*active);
            UpdateMaterialShader(litShader, &GetSelectedPbrMaterial(*active), materialPreviewMode, visibility.textures);
        }
        else
        {
            if (mouseInViewport)
            {
                UpdateNavigation(emptyOrbit, navigation);
            }
            else
            {
                UpdateOrbitCameraTransform(emptyOrbit);
            }
            UpdateLitShader(litShader, emptyOrbit);
            UpdateMaterialShader(litShader, nullptr, MaterialPreviewMode::Shaded, false);
        }

        BeginDrawing();
        ClearBackground(Color{ 38, 40, 43, 255 });

        const OrbitCamera& drawOrbit = active ? active->orbit : emptyOrbit;
        BeginMode3D(drawOrbit.camera);
        DrawMeterGrid(drawOrbit);
        if (active && active->loaded.valid)
        {
            if (active->loaded.hasMesh && visibility.geometry)
            {
                if (visibility.backfaceCulling)
                {
                    rlEnableBackfaceCulling();
                }
                else
                {
                    rlDisableBackfaceCulling();
                }

                if (viewMode == ViewMode::Shaded || viewMode == ViewMode::ShadedWireframe)
                {
                    BeginBlendMode(BLEND_ALPHA);
                    DrawMaterialModel(*active, litShader, materialPreviewMode, visibility.textures);
                    EndBlendMode();
                }

                if (visibility.skinWeights)
                {
                    DrawSkinWeightHeatMap(*active);
                }

                if (viewMode == ViewMode::ShadedWireframe || viewMode == ViewMode::Wireframe)
                {
                    rlEnableWireMode();
                    DrawModel(active->loaded.model, Vector3{ 0.0f, 0.0f, 0.0f }, 1.0f, viewMode == ViewMode::Wireframe ? Color{ 220, 225, 230, 255 } : Color{ 25, 28, 31, 150 });
                    rlDisableWireMode();
                }

                rlDisableBackfaceCulling();
                DrawSelectedMeshOverlay(*active, visibility);
            }
            rlDrawRenderBatchActive();
            rlDisableDepthTest();
            if (visibility.empties)
            {
                DrawEmptyCrosses(*active);
            }
            if (visibility.bones)
            {
                DrawBones(GetVisibleBones(*active), active->selectedNode, active->selectedNodes);
                if (visibility.boneRotations)
                {
                    DrawBoneRotations(GetVisibleBonePoses(*active), GetBoundsDiagonal(active->loaded.bounds), active->selectedNode);
                }
            }
            DrawSelectedNodeOverlay(*active, visibility);
            DrawTransformGizmo(*active, transformTool, transformGizmo, gizmoOrientation);
            rlDrawRenderBatchActive();
            rlEnableDepthTest();
        }
        EndMode3D();

        DrawWeightBrushCursor(uiFont, active, transformTool, weightBrush, mouseInViewport);

        char statusText[256] = {};
        std::snprintf(statusText, sizeof(statusText), "VIEW: %s    MAT: %s    TOOL: %s    SPACE: %s    NAV: %s", GetViewModeName(viewMode), GetMaterialPreviewModeName(materialPreviewMode), GetTransformToolName(transformTool), GetGizmoOrientationName(gizmoOrientation), GetNavigationPresetName(navigation));
        DrawUiText(uiFont, statusText, static_cast<float>(GetScreenWidth() - 660), 8, 16, Color{ 165, 220, 255, 255 });

        if (active)
        {
            char channelText[128] = {};
            std::snprintf(channelText, sizeof(channelText), "Material Channel: %s", GetMaterialPreviewModeName(materialPreviewMode));
            const Vector2 channelSize = MeasureTextEx(uiFont, channelText, 16.0f, 1.0f);
            const Rectangle channelBadge{ hierarchyBlockW + 12.0f, 66.0f, channelSize.x + 18.0f, 26.0f };
            DrawRectangleRec(channelBadge, Color{ 24, 27, 31, 210 });
            DrawRectangleLinesEx(channelBadge, 1.0f, Color{ 78, 88, 98, 220 });
            DrawUiText(uiFont, channelText, channelBadge.x + 9.0f, channelBadge.y + 5.0f, 16.0f, Color{ 205, 224, 238, 255 });
        }
        else
        {
            DrawUiText(uiFont, "No FBX loaded", hierarchyBlockW + 12.0f, 66, 16, Color{ 190, 190, 190, 255 });
        }

        if (!error.empty())
        {
            DrawUiText(uiFont, error.c_str(), 12, static_cast<float>(GetScreenHeight() - 154), 18, Color{ 255, 140, 120, 255 });
        }
        else if (!notice.empty())
        {
            DrawUiText(uiFont, notice.c_str(), 12, static_cast<float>(GetScreenHeight() - 154), 18, Color{ 150, 225, 170, 255 });
        }

        DrawSelectedInfoPanel(uiFont, active);

        if (active)
        {
            DrawTimeline(uiFont, *active, renameEditor, animationPanelCollapsed);
        }
        else
        {
            const float panelHeight = animationPanelCollapsed ? kTimelineCollapsedHeight : kTimelinePanelHeight;
            const float panelY = static_cast<float>(GetScreenHeight()) - panelHeight;
            const Rectangle toggleButton{ static_cast<float>(GetScreenWidth()) - 34.0f, panelY + 4.0f, 24.0f, 20.0f };
            DrawRectangle(0, static_cast<int>(panelY), GetScreenWidth(), static_cast<int>(panelHeight), Color{ 20, 22, 24, 238 });
            DrawLine(0, static_cast<int>(panelY), GetScreenWidth(), static_cast<int>(panelY), Color{ 76, 84, 92, 255 });
            DrawUiText(uiFont, "ANIMATIONS", 12.0f, panelY + 7.0f, 16.0f, Color{ 165, 182, 196, 255 });
            if (DrawPanelButton(uiFont, toggleButton, animationPanelCollapsed ? "^" : "v"))
            {
                animationPanelCollapsed = !animationPanelCollapsed;
            }
            if (!animationPanelCollapsed)
            {
                DrawUiText(uiFont, "Open an FBX file to show animation stacks", 12.0f, panelY + 38.0f, 16.0f, Color{ 128, 136, 144, 255 });
            }
        }
        gBottomPanelReservedHeight = animationPanelCollapsed ? kTimelineCollapsedHeight : kTimelinePanelHeight;

        DrawHierarchyPanel(uiFont, active, hierarchyPanel, renameEditor, droppedPaths, droppedTextureHandled, textureClipboard, notice, error);
        DrawTransformToolbar(uiFont, transformTool, gizmoOrientation, weightBrush, hierarchyBlockW);
        DrawOrientationGizmo(uiFont, active ? active->orbit.camera : emptyOrbit.camera);

        DrawTabs(uiFont, tabs, activeTab);
        active = activeTab >= 0 && activeTab < static_cast<int>(tabs.size()) ? tabs[static_cast<size_t>(activeTab)].get() : nullptr;

        bool menuOpenRequested = false;
        bool menuUndoRequested = false;
        bool menuRedoRequested = false;
        bool menuSaveFbxRequested = false;
        bool menuSaveAsFbxRequested = false;
        bool importAnimationsRequested = false;
        bool exportJsonRequested = false;
        bool compareFbxRequested = false;
        DrawMenuBar(uiFont,
                    openMenu,
                    menuOpenRequested,
                    menuUndoRequested,
                    menuRedoRequested,
                    menuSaveFbxRequested,
                    menuSaveAsFbxRequested,
                    importAnimationsRequested,
                    exportJsonRequested,
                    compareFbxRequested,
                    quitRequested,
                    viewMode,
                    navigation,
                    visibility,
                    active && !active->undoStack.empty(),
                    active && !active->redoStack.empty());
        undoRequested = undoRequested || menuUndoRequested;
        redoRequested = redoRequested || menuRedoRequested;
        saveFbxRequested = saveFbxRequested || menuSaveFbxRequested;
        saveAsFbxRequested = saveAsFbxRequested || menuSaveAsFbxRequested;
        DrawSkeletonCompareResultWindow(uiFont, compareResultVisible, compareResultCompatible, compareResultPath, compareResultText);
        DrawRenameEditor(uiFont, active, renameEditor, notice, error);
        EndDrawing();

        if (undoRequested && active)
        {
            if (UndoEdit(*active))
            {
                notice = "Undo.";
                error.clear();
            }
        }
        if (redoRequested && active)
        {
            if (RedoEdit(*active))
            {
                notice = "Redo.";
                error.clear();
            }
        }

        if (!droppedPaths.empty() && !droppedTextureHandled)
        {
            bool openedAny = false;
            bool ignoredTexture = false;
            if (active && active->loaded.valid)
            {
                EditSnapshot before = CaptureEditSnapshot(*active);
                const int assignedTextures = AutoAssignDroppedTextures(*active, droppedPaths, notice, error);
                if (assignedTextures > 0)
                {
                    PushUndoSnapshot(*active, std::move(before));
                }
                droppedTextureHandled = assignedTextures > 0;
            }
            for (const std::string& droppedPath : droppedPaths)
            {
                if (IsTextureExtension(std::filesystem::path(droppedPath)))
                {
                    ignoredTexture = true;
                    continue;
                }
                openPathInNewTab(droppedPath);
                openedAny = true;
            }
            if (!droppedTextureHandled && !openedAny && ignoredTexture)
            {
                notice = "Drop texture files onto a material thumbnail.";
                error.clear();
            }
        }

        if (menuOpenRequested)
        {
            const std::string selectedPath = OpenFbxFileDialog();
            if (!selectedPath.empty())
            {
                openPathInNewTab(selectedPath);
            }
        }

        if (importAnimationsRequested)
        {
            if (!active || !active->loaded.valid)
            {
                error = "No active FBX to import animations into.";
                notice.clear();
            }
            else
            {
                const std::string importPath = OpenFbxFileDialog();
                if (!importPath.empty())
                {
                    int importedCount = 0;
                    std::string importError;
                    EditSnapshot before = CaptureEditSnapshot(*active);
                    if (ImportAnimationsFromFbx(*active, importPath, importedCount, importError))
                    {
                        PushUndoSnapshot(*active, std::move(before));
                        notice = "Imported animations: " + std::to_string(importedCount);
                        error.clear();
                    }
                    else
                    {
                        error = importError;
                        notice.clear();
                    }
                }
            }
        }

        if (saveFbxRequested)
        {
            if (!active || !active->loaded.valid)
            {
                error = "No active FBX to save.";
                notice.clear();
            }
            else
            {
                std::string saveError;
                if (SaveFbxModelAnimations(active->path, active->path, active->loaded, active->deletedNodes, saveError))
                {
                    notice = "Saved FBX: " + active->path;
                    error.clear();
                }
                else
                {
                    error = saveError;
                    notice.clear();
                }
            }
        }

        if (saveAsFbxRequested)
        {
            if (!active || !active->loaded.valid)
            {
                error = "No active FBX to save.";
                notice.clear();
            }
            else
            {
                const std::string savePath = SaveAsFbxFileDialog(active->path);
                if (!savePath.empty())
                {
                    std::string saveError;
                    if (SaveFbxModelAnimations(active->path, savePath, active->loaded, active->deletedNodes, saveError))
                    {
                        active->path = savePath;
                        active->title = MakeTabTitle(savePath);
                        notice = "Saved FBX: " + savePath;
                        error.clear();
                    }
                    else
                    {
                        error = saveError;
                        notice.clear();
                    }
                }
            }
        }

        if (exportJsonRequested)
        {
            if (!active || !active->loaded.valid)
            {
                error = "No active FBX to export.";
                notice.clear();
            }
            else
            {
                std::string outputPath;
                std::string exportError;
                if (ExportFbxJson(*active, outputPath, exportError))
                {
                    notice = "Exported JSON: " + outputPath;
                    error.clear();
                }
                else
                {
                    error = exportError;
                    notice.clear();
                }
            }
        }

        if (compareFbxRequested)
        {
            if (!active || !active->loaded.valid)
            {
                error = "No active FBX to compare.";
                notice.clear();
            }
            else
            {
                const std::string comparePath = OpenFbxFileDialog();
                if (!comparePath.empty())
                {
                    LoadedFbxModel compareModel;
                    std::string compareError;
                    if (LoadFbxModel(comparePath, compareModel, compareError))
                    {
                        compareResultPath = comparePath;
                        compareResultCompatible = CompareSkeletonCompatibility(active->loaded, compareModel, compareResultText);
                        compareResultVisible = true;
                        UnloadFbxModel(compareModel);
                        error.clear();
                        notice.clear();
                    }
                    else
                    {
                        compareResultPath = comparePath;
                        compareResultCompatible = false;
                        compareResultText = "Failed to load comparison FBX.\n" + compareError;
                        compareResultVisible = true;
                        error = compareError;
                        notice.clear();
                    }
                }
            }
        }
    }

    for (std::unique_ptr<ModelTab>& tab : tabs)
    {
        UnloadPbrTextures(*tab);
        UnloadFbxModel(tab->loaded);
    }
    if (litShader.valid)
    {
        UnloadShader(litShader.shader);
    }
    if (uiFont.texture.id != GetFontDefault().texture.id)
    {
        UnloadFont(uiFont);
    }
    CloseWindow();
    return 0;
}
