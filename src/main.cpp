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
float gBottomPanelReservedHeight = kTimelinePanelHeight;

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
    View,
    Preferences
};

enum class LeftPanelTab
{
    Hierarchy,
    Stats,
    Materials,
    UV
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

struct TextureClipboard
{
    std::string path;
    bool menuOpen = false;
    bool justOpened = false;
    int materialIndex = -1;
    PbrTextureSlot slot = PbrTextureSlot::Diffuse;
    Vector2 position{};
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
    std::vector<PbrMaterialState> pbrMaterials;
    int selectedMaterial = 0;
    int selectedUvSet = 0;
    int appliedClipIndex = -2;
    int appliedMeshFrameIndex = -1;
    int appliedNextMeshFrameIndex = -1;
    float appliedMeshFrameAlpha = -1.0f;
    int appliedBoneClipIndex = -2;
    int appliedBoneFrameIndex = -1;
    int appliedNextBoneFrameIndex = -1;
    float appliedBoneFrameAlpha = -1.0f;
    int selectedNode = -1;
    std::string path;
    std::string title;
};

struct HierarchyPanelState
{
    float width = 320.0f;
    float scroll = 0.0f;
    bool hidden = false;
    bool resizing = false;
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

const std::vector<BoneSegment>& GetVisibleBones(const ModelTab& tab)
{
    return tab.visibleBones.empty() ? tab.loaded.bones : tab.visibleBones;
}

const std::vector<BonePose>& GetVisibleBonePoses(const ModelTab& tab)
{
    return tab.visibleBonePoses.empty() ? tab.loaded.bonePoses : tab.visibleBonePoses;
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

            tab.blendedVertices[localBase] = vertices[globalBase];
            tab.blendedVertices[localBase + 1] = vertices[globalBase + 1];
            tab.blendedVertices[localBase + 2] = vertices[globalBase + 2];
            tab.blendedNormals[localBase] = normals[globalBase];
            tab.blendedNormals[localBase + 1] = normals[globalBase + 1];
            tab.blendedNormals[localBase + 2] = normals[globalBase + 2];
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

void ApplyAnimatedMeshFrame(ModelTab& tab)
{
    if (!tab.loaded.hasMesh)
    {
        return;
    }

    if (tab.animation.clipIndex < 0)
    {
        if (tab.appliedClipIndex == -1) return;

        if (tab.loaded.bindVertices.size() == tab.loaded.bindNormals.size())
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

void DrawBones(const std::vector<BoneSegment>& bones, int selectedNode)
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
        const bool selected = bone.startNode == selectedNode;
        DrawMayaBone(bone.start, bone.end, radius, selected ? kSelectionColor : Color{ 100, 185, 255, 255 });
    }

    for (const BoneSegment& bone : bones)
    {
        DrawJointSphere(bone.start, bone.startNode == selectedNode ? radius * 1.15f : radius * 0.85f, bone.startNode == selectedNode ? kSelectionColor : Color{ 142, 210, 255, 255 });
        DrawJointSphere(bone.end, bone.endNode == selectedNode ? radius * 1.15f : radius * 0.85f, bone.endNode == selectedNode ? kSelectionColor : Color{ 142, 210, 255, 255 });
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
    const float sphereRadius = axisLength * 0.045f;

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
        DrawEmptyCross(node, length, i == tab.selectedNode ? kSelectionColor : Color{ 100, 185, 255, 230 });
    }
}

const float* GetCurrentMeshVertices(const ModelTab& tab)
{
    if (!tab.loaded.hasMesh) return nullptr;
    if (tab.currentVertices.size() == tab.loaded.bindVertices.size()) return tab.currentVertices.data();
    return tab.loaded.bindVertices.empty() ? nullptr : tab.loaded.bindVertices.data();
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

bool SelectNodeFromViewport(ModelTab& tab, Vector2 mouse, const VisibilityState& visibility)
{
    if (!tab.loaded.valid || tab.loaded.nodes.empty()) return false;

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
        tab.selectedNode = bestBoneNode;
        return true;
    }

    for (int i = 0; i < static_cast<int>(tab.loaded.nodes.size()); ++i)
    {
        const SceneNode& node = tab.loaded.nodes[static_cast<size_t>(i)];

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
        tab.selectedNode = bestNode;
        return true;
    }

    return false;
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

    outPosition = node.hasBounds ? Vector3Scale(Vector3Add(node.bounds.min, node.bounds.max), 0.5f) : node.position;
    return true;
}

bool FocusCameraOnSelection(ModelTab& tab)
{
    if (tab.selectedNode < 0 || tab.selectedNode >= static_cast<int>(tab.loaded.nodes.size())) return false;

    const SceneNode& node = tab.loaded.nodes[static_cast<size_t>(tab.selectedNode)];
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
    if (!visibility.geometry || tab.selectedNode < 0 || tab.selectedNode >= static_cast<int>(tab.loaded.nodes.size())) return;

    const SceneNode& node = tab.loaded.nodes[static_cast<size_t>(tab.selectedNode)];
    if (node.type == SceneNodeType::Mesh && node.hasBounds)
    {
        DrawMeshNodeWireframe(tab, node, kSelectionColor);
    }
}

void DrawSelectedNodeOverlay(const ModelTab& tab, const VisibilityState& visibility)
{
    if (tab.selectedNode < 0 || tab.selectedNode >= static_cast<int>(tab.loaded.nodes.size())) return;

    const SceneNode& node = tab.loaded.nodes[static_cast<size_t>(tab.selectedNode)];
    if (node.type == SceneNodeType::Mesh && node.hasBounds)
    {
        DrawMeshOrigin(node, GetBoundsDiagonal(tab.loaded.bounds));
        return;
    }

    if (node.type == SceneNodeType::Bone)
    {
        if (!visibility.bones) return;
        return;
    }

    if (node.type == SceneNodeType::Empty)
    {
        if (!visibility.empties) return;
        const float length = ClampFloat(GetBoundsDiagonal(tab.loaded.bounds) * 0.055f, 0.08f, 0.8f);
        DrawEmptyCross(node, length, kSelectionColor);
    }
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

void DrawSelectedInfoPanel(Font font, const ModelTab* active)
{
    if (!active || active->selectedNode < 0 || active->selectedNode >= static_cast<int>(active->loaded.nodes.size())) return;

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

void RevealNodeInHierarchy(ModelTab& tab, HierarchyPanelState& panel, int nodeIndex)
{
    if (nodeIndex < 0 || nodeIndex >= static_cast<int>(tab.loaded.nodes.size())) return;

    int parent = tab.loaded.nodes[static_cast<size_t>(nodeIndex)].parent;
    while (parent >= 0)
    {
        if (parent < static_cast<int>(tab.collapsedNodes.size()))
        {
            tab.collapsedNodes[static_cast<size_t>(parent)] = false;
        }
        parent = tab.loaded.nodes[static_cast<size_t>(parent)].parent;
    }

    const int row = GetVisibleSceneNodeRow(tab.loaded, tab.collapsedNodes, nodeIndex);
    if (row < 0) return;

    constexpr float rowH = 22.0f;
    const float panelH = GetHierarchyPanelHeight();
    const float visibleRows = std::max(1.0f, std::floor((panelH - 64.0f) / rowH));
    const float maxScroll = std::max(0.0f, static_cast<float>(CountVisibleSceneNodes(tab.loaded, tab.collapsedNodes)) - visibleRows);

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
    if (active && CheckCollisionPointRec(mouse, panelRect))
    {
        const float wheel = GetMouseWheelMove();
        if (std::fabs(wheel) > 0.0f)
        {
            const float visibleRows = std::max(0.0f, std::floor((panelH - 64.0f) / rowH));
            const float maxScroll = std::max(0.0f, static_cast<float>(CountVisibleSceneNodes(active->loaded, active->collapsedNodes)) - visibleRows);
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
        break;
    case PbrTextureSlot::Metallic:
        if (hasAny({ "metallic", "metalness", "_metal", "_mtl" })) score += 10;
        break;
    case PbrTextureSlot::AmbientOcclusion:
        if (hasAny({ "_ao", "ambientocclusion", "ambient_occlusion", "occlusion" })) score += 10;
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
        if (LoadPbrTexture(tab, bestMaterial, bestSlot, droppedPath, loadError))
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
        std::snprintf(message, sizeof(message), "Auto-assigned %d dropped texture%s by name.", loadedCount, loadedCount == 1 ? "" : "s");
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
    DrawUiText(font, label, bounds.x + 10.0f, bounds.y + 5.0f, 15.0f, selected ? RAYWHITE : Color{ 180, 190, 200, 255 });
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
            std::string loadError;
            if (LoadPbrTexture(tab, clipboard.materialIndex, clipboard.slot, clipboard.path, loadError))
            {
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
            if (!IsTextureExtension(std::filesystem::path(droppedPath))) continue;

            std::string loadError;
            if (LoadPbrTexture(tab, materialIndex, slot, droppedPath, loadError))
            {
                notice = std::string("Dropped ") + GetPbrTextureSlotName(slot) + ": " + droppedPath;
                error.clear();
            }
            else
            {
                error = loadError;
                notice.clear();
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
            *channel = NextPackedChannel(*channel);
        }
        buttonRight -= 44.0f;
    }
    else if (slot == PbrTextureSlot::Opacity)
    {
        const Rectangle channelButton{ buttonRight - 48.0f, y - 2.0f, 44.0f, 22.0f };
        if (DrawPanelButton(font, channelButton, GetOpacityChannelName(material.opacityChannel)))
        {
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
            std::string loadError;
            if (LoadPbrTexture(tab, materialIndex, slot, path, loadError))
            {
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
        UnloadPbrTexture(tab, materialIndex, slot);
        notice = std::string("Cleared ") + GetPbrTextureSlotName(slot) + " map.";
        error.clear();
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
        const int loadedCount = pickedPath.empty() ? 0 : LoadPbrTexturesFromFolder(tab, tab.selectedMaterial, std::filesystem::path(pickedPath).parent_path(), autoloadError);
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

    tab.selectedUvSet = std::max(0, std::min(tab.selectedUvSet, static_cast<int>(tab.loaded.uvSets.size()) - 1));
    const std::string uvName = tab.selectedUvSet < static_cast<int>(tab.loaded.uvSetNames.size()) ? tab.loaded.uvSetNames[static_cast<size_t>(tab.selectedUvSet)] : std::string("UV Set");
    if (DrawPanelButton(font, Rectangle{ contentX, y, panelW - 24.0f, 24.0f }, uvName.c_str()))
    {
        tab.selectedUvSet = (tab.selectedUvSet + 1) % static_cast<int>(tab.loaded.uvSets.size());
    }
    y += 34.0f;

    if (tab.selectedNode < 0 || tab.selectedNode >= static_cast<int>(tab.loaded.nodes.size()) ||
        tab.loaded.nodes[static_cast<size_t>(tab.selectedNode)].type != SceneNodeType::Mesh)
    {
        DrawUiTextClipped(font, "Select a mesh in the viewport or hierarchy to display its UVs.", contentX, y, 14.0f, panelW - 24.0f, Color{ 128, 140, 152, 255 });
        return;
    }

    const SceneNode& node = tab.loaded.nodes[static_cast<size_t>(tab.selectedNode)];
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

    const float editorSize = std::min(panelW - 24.0f, std::max(120.0f, GetHierarchyPanelHeight() - (y - panelY) - 24.0f));
    const Rectangle editor{ contentX, y, editorSize, editorSize };
    DrawRectangleRec(editor, Color{ 14, 16, 19, 245 });
    DrawRectangleLinesEx(editor, 1.0f, Color{ 88, 98, 108, 255 });

    for (int i = 1; i < 4; ++i)
    {
        const float p = static_cast<float>(i) / 4.0f;
        DrawLine(static_cast<int>(editor.x + editor.width * p), static_cast<int>(editor.y), static_cast<int>(editor.x + editor.width * p), static_cast<int>(editor.y + editor.height), Color{ 42, 48, 54, 255 });
        DrawLine(static_cast<int>(editor.x), static_cast<int>(editor.y + editor.height * p), static_cast<int>(editor.x + editor.width), static_cast<int>(editor.y + editor.height * p), Color{ 42, 48, 54, 255 });
    }

    auto uvToScreen = [&](int globalVertex)
    {
        const size_t uvIndex = static_cast<size_t>(globalVertex) * 2;
        const float u = uvIndex < uvs.size() ? uvs[uvIndex] : 0.0f;
        const float v = uvIndex + 1 < uvs.size() ? uvs[uvIndex + 1] : 0.0f;
        return Vector2{ editor.x + u * editor.width, editor.y + (1.0f - v) * editor.height };
    };

    const int start = node.meshVertexStart;
    const int end = node.meshVertexStart + node.meshVertexCount;
    for (int vertex = start; vertex + 2 < end; vertex += 3)
    {
        const Vector2 a = uvToScreen(vertex);
        const Vector2 b = uvToScreen(vertex + 1);
        const Vector2 c = uvToScreen(vertex + 2);
        DrawLineV(a, b, Color{ 95, 170, 220, 220 });
        DrawLineV(b, c, Color{ 95, 170, 220, 220 });
        DrawLineV(c, a, Color{ 95, 170, 220, 220 });
    }
}

void DrawHierarchyPanel(Font font,
                        ModelTab* active,
                        HierarchyPanelState& panel,
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

    const float tabW = (panelW - 28.0f) / 4.0f;
    const Rectangle hierarchyTab{ panelX + 8.0f, panelY + 34.0f, tabW, 24.0f };
    const Rectangle statsTab{ hierarchyTab.x + hierarchyTab.width + 4.0f, panelY + 34.0f, tabW, 24.0f };
    const Rectangle materialsTab{ statsTab.x + statsTab.width + 4.0f, panelY + 34.0f, tabW, 24.0f };
    const Rectangle uvTab{ materialsTab.x + materialsTab.width + 4.0f, panelY + 34.0f, tabW, 24.0f };
    if (DrawPanelTab(font, hierarchyTab, "Hierarchy", panel.activeTab == LeftPanelTab::Hierarchy))
    {
        panel.activeTab = LeftPanelTab::Hierarchy;
    }
    if (DrawPanelTab(font, statsTab, "Stats", panel.activeTab == LeftPanelTab::Stats))
    {
        panel.activeTab = LeftPanelTab::Stats;
    }
    if (DrawPanelTab(font, materialsTab, "Materials", panel.activeTab == LeftPanelTab::Materials))
    {
        panel.activeTab = LeftPanelTab::Materials;
    }
    if (DrawPanelTab(font, uvTab, "UV", panel.activeTab == LeftPanelTab::UV))
    {
        panel.activeTab = LeftPanelTab::UV;
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

    if (active->collapsedNodes.size() != active->loaded.nodes.size())
    {
        active->collapsedNodes.assign(active->loaded.nodes.size(), false);
    }

    const Vector2 mouse = GetMousePosition();
    float rowY = GetHierarchyContentStartY();
    int visibleRow = 0;
    const int firstRow = static_cast<int>(std::floor(panel.scroll));

    for (int i = 0; i < static_cast<int>(active->loaded.nodes.size()); ++i)
    {
        if (!IsSceneNodeVisible(active->loaded, active->collapsedNodes, i)) continue;
        if (visibleRow++ < firstRow) continue;
        if (rowY + rowH > panelY + panelH) break;

        const SceneNode& node = active->loaded.nodes[static_cast<size_t>(i)];
        const Rectangle row{ panelX + 6.0f, rowY, panelW - 12.0f, rowH };
        const bool hovered = CheckCollisionPointRec(mouse, row);
        const bool selected = active->selectedNode == i;
        const bool hasChildren = HasSceneNodeChildren(active->loaded, i);
        const float indent = static_cast<float>(node.depth) * 14.0f;

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

        if (hovered && IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && !CheckCollisionPointRec(mouse, collapseRect))
        {
            active->selectedNode = i;
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
                 bool& saveFbxRequested,
                 bool& saveAsFbxRequested,
                 bool& importAnimationsRequested,
                 bool& exportJsonRequested,
                 bool& compareFbxRequested,
                 bool& quitRequested,
                 ViewMode& viewMode,
                 NavigationPreset& navigation,
                 VisibilityState& visibility)
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
        { "View", OpenMenu::View, Rectangle{ 66.0f, 3.0f, 58.0f, 22.0f } },
        { "Preferences", OpenMenu::Preferences, Rectangle{ 128.0f, 3.0f, 118.0f, 22.0f } }
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

    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && mouse.y > 310.0f && openMenu != OpenMenu::None)
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
    else if (openMenu == OpenMenu::View)
    {
        DrawRectangle(66, 29, 230, 278, Color{ 28, 31, 35, 245 });
        if (DrawMenuItem(font, Rectangle{ 66.0f, 29.0f, 230.0f, 30.0f }, "Shaded", viewMode == ViewMode::Shaded))
        {
            viewMode = ViewMode::Shaded;
        }
        if (DrawMenuItem(font, Rectangle{ 66.0f, 59.0f, 230.0f, 30.0f }, "Shaded Wireframe", viewMode == ViewMode::ShadedWireframe))
        {
            viewMode = ViewMode::ShadedWireframe;
        }
        if (DrawMenuItem(font, Rectangle{ 66.0f, 89.0f, 230.0f, 30.0f }, "Wireframe", viewMode == ViewMode::Wireframe))
        {
            viewMode = ViewMode::Wireframe;
        }
        if (DrawMenuItem(font, Rectangle{ 66.0f, 127.0f, 230.0f, 30.0f }, visibility.geometry ? "[x] Geometry        G" : "[ ] Geometry        G"))
        {
            visibility.geometry = !visibility.geometry;
        }
        if (DrawMenuItem(font, Rectangle{ 66.0f, 157.0f, 230.0f, 30.0f }, visibility.textures ? "[x] Textures" : "[ ] Textures"))
        {
            visibility.textures = !visibility.textures;
        }
        if (DrawMenuItem(font, Rectangle{ 66.0f, 187.0f, 230.0f, 30.0f }, visibility.backfaceCulling ? "[x] Backface Culling" : "[ ] Backface Culling"))
        {
            visibility.backfaceCulling = !visibility.backfaceCulling;
        }
        if (DrawMenuItem(font, Rectangle{ 66.0f, 217.0f, 230.0f, 30.0f }, visibility.bones ? "[x] Bones        B" : "[ ] Bones        B"))
        {
            visibility.bones = !visibility.bones;
        }
        if (DrawMenuItem(font, Rectangle{ 66.0f, 247.0f, 230.0f, 30.0f }, visibility.boneRotations ? "[x] Bone Orientation  O" : "[ ] Bone Orientation  O"))
        {
            visibility.boneRotations = !visibility.boneRotations;
        }
        if (DrawMenuItem(font, Rectangle{ 66.0f, 277.0f, 230.0f, 30.0f }, visibility.empties ? "[x] Empties        E" : "[ ] Empties        E"))
        {
            visibility.empties = !visibility.empties;
        }
    }
    else if (openMenu == OpenMenu::Preferences)
    {
        DrawRectangle(128, 29, 420, 246, Color{ 28, 31, 35, 245 });
        DrawUiText(font, "NAVIGATION", 140.0f, 39.0f, 16.0f, Color{ 165, 182, 196, 255 });
        if (DrawMenuItem(font, Rectangle{ 138.0f, 64.0f, 185.0f, 30.0f }, "Blender", navigation == NavigationPreset::Blender))
        {
            navigation = NavigationPreset::Blender;
        }
        if (DrawMenuItem(font, Rectangle{ 333.0f, 64.0f, 185.0f, 30.0f }, "Maya", navigation == NavigationPreset::Maya))
        {
            navigation = NavigationPreset::Maya;
        }

        DrawUiText(font, "HOTKEYS", 140.0f, 110.0f, 16.0f, Color{ 165, 182, 196, 255 });
        DrawUiText(font, "Ctrl+O open FBX    V view mode    T textures", 140.0f, 136.0f, 15.0f, Color{ 205, 213, 220, 255 });
        DrawUiText(font, "C channels    M shaded    O bone orientation", 140.0f, 162.0f, 15.0f, Color{ 205, 213, 220, 255 });
        DrawUiText(font, "Blender: MMB orbit, Alt snap, Shift+MMB pan, Wheel zoom", 140.0f, 188.0f, 15.0f, Color{ 205, 213, 220, 255 });
        DrawUiText(font, "Maya: Alt+LMB orbit, Shift snap, Alt+MMB pan, Alt+RMB/Wheel zoom", 140.0f, 214.0f, 15.0f, Color{ 205, 213, 220, 255 });
        DrawUiText(font, "Esc deselects    Ctrl+Q quits    Tabs: X/middle closes", 140.0f, 240.0f, 15.0f, Color{ 205, 213, 220, 255 });
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

void DrawTimeline(Font font, LoadedFbxModel& loaded, AnimationState& animation, bool& collapsed)
{
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
    const Rectangle contextMenuBounds{ animation.contextPosition.x, animation.contextPosition.y, 152.0f, 38.0f };
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
                ClampFloat(mouse.y, 4.0f, static_cast<float>(GetScreenHeight()) - 42.0f)
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
        const Rectangle menu{ animation.contextPosition.x, animation.contextPosition.y, 152.0f, 38.0f };
        DrawRectangleRec(menu, Color{ 24, 27, 31, 248 });
        DrawRectangleLinesEx(menu, 1.0f, Color{ 84, 94, 104, 255 });
        const bool validContextClip = animation.contextClipIndex >= 0 && animation.contextClipIndex < static_cast<int>(loaded.animations.size());
        if (animation.contextMenuJustOpened)
        {
            animation.contextMenuJustOpened = false;
        }
        else
        {
            const bool hovered = CheckCollisionPointRec(mouse, menu);
            DrawRectangleRec(menu, hovered ? Color{ 54, 63, 72, 255 } : Color{ 35, 40, 46, 255 });
            DrawRectangleLinesEx(menu, 1.0f, Color{ 70, 80, 90, 255 });
            const Vector2 labelSize = MeasureTextEx(font, "Remove", 14.0f, 1.0f);
            DrawUiText(font, "Remove", menu.x + (menu.width - labelSize.x) * 0.5f, menu.y + 10.0f, 14.0f, Color{ 230, 215, 210, 255 });
            if (validContextClip && hovered && IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
            {
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
        tab->visibleBones = tab->loaded.bones;
        tab->visibleBonePoses = tab->loaded.bonePoses;
        tab->collapsedNodes.assign(tab->loaded.nodes.size(), false);
        tab->selectedNode = tab->loaded.nodes.empty() ? -1 : 0;
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
        bool openRequested = controlDown && IsKeyPressed(KEY_O);
        bool saveFbxRequested = controlDown && !shiftDown && IsKeyPressed(KEY_S);
        bool saveAsFbxRequested = controlDown && shiftDown && IsKeyPressed(KEY_S);

        if (openRequested)
        {
            const std::string selectedPath = OpenFbxFileDialog();
            if (!selectedPath.empty())
            {
                openPathInNewTab(selectedPath);
            }
        }

        ModelTab* active = activeTab >= 0 && activeTab < static_cast<int>(tabs.size()) ? tabs[static_cast<size_t>(activeTab)].get() : nullptr;
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
        const bool mouseInViewport = mouse.x > hierarchyBlockW && mouse.y >= 61.0f && mouse.y < static_cast<float>(GetScreenHeight()) - gBottomPanelReservedHeight && openMenu == OpenMenu::None && !hierarchyPanel.resizing;

        if (controlDown && IsKeyPressed(KEY_Q))
        {
            quitRequested = true;
            continue;
        }
        if (IsKeyPressed(KEY_ESCAPE))
        {
            openMenu = OpenMenu::None;
            if (active)
            {
                active->selectedNode = -1;
            }
        }

        if (active && IsKeyPressed(KEY_F) && active->loaded.valid)
        {
            if (!FocusCameraOnSelection(*active))
            {
                FocusCameraOnBounds(active->orbit, active->loaded.bounds);
            }
        }

        if (IsKeyPressed(KEY_V))
        {
            viewMode = NextViewMode(viewMode);
        }
        if (IsKeyPressed(KEY_C))
        {
            materialPreviewMode = NextMaterialPreviewMode(materialPreviewMode);
        }
        if (IsKeyPressed(KEY_M))
        {
            materialPreviewMode = MaterialPreviewMode::Shaded;
        }
        if (IsKeyPressed(KEY_T))
        {
            visibility.textures = !visibility.textures;
        }
        if (IsKeyPressed(KEY_G))
        {
            visibility.geometry = !visibility.geometry;
        }
        if (IsKeyPressed(KEY_B))
        {
            visibility.bones = !visibility.bones;
        }
        if (!controlDown && IsKeyPressed(KEY_O))
        {
            visibility.boneRotations = !visibility.boneRotations;
        }
        if (IsKeyPressed(KEY_E))
        {
            visibility.empties = !visibility.empties;
        }

        if (active && mouseInViewport && IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
        {
            if (SelectNodeFromViewport(*active, mouse, visibility))
            {
                RevealNodeInHierarchy(*active, hierarchyPanel, active->selectedNode);
            }
            else
            {
                active->selectedNode = -1;
            }
        }

        if (active)
        {
            if (mouseInViewport)
            {
                UpdateNavigation(active->orbit, navigation);
            }
            else
            {
                UpdateOrbitCameraTransform(active->orbit);
            }

            UpdateAnimation(active->animation, active->loaded);
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
                DrawBones(GetVisibleBones(*active), active->selectedNode);
                if (visibility.boneRotations)
                {
                    DrawBoneRotations(GetVisibleBonePoses(*active), GetBoundsDiagonal(active->loaded.bounds), active->selectedNode);
                }
            }
            DrawSelectedNodeOverlay(*active, visibility);
            rlDrawRenderBatchActive();
            rlEnableDepthTest();
        }
        EndMode3D();

        char statusText[256] = {};
        std::snprintf(statusText, sizeof(statusText), "VIEW: %s    MAT: %s    NAV: %s", GetViewModeName(viewMode), GetMaterialPreviewModeName(materialPreviewMode), GetNavigationPresetName(navigation));
        DrawUiText(uiFont, statusText, static_cast<float>(GetScreenWidth() - 360), 8, 16, Color{ 165, 220, 255, 255 });

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
            DrawTimeline(uiFont, active->loaded, active->animation, animationPanelCollapsed);
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

        DrawHierarchyPanel(uiFont, active, hierarchyPanel, droppedPaths, droppedTextureHandled, textureClipboard, notice, error);
        DrawOrientationGizmo(uiFont, active ? active->orbit.camera : emptyOrbit.camera);

        DrawTabs(uiFont, tabs, activeTab);
        active = activeTab >= 0 && activeTab < static_cast<int>(tabs.size()) ? tabs[static_cast<size_t>(activeTab)].get() : nullptr;

        bool menuOpenRequested = false;
        bool menuSaveFbxRequested = false;
        bool menuSaveAsFbxRequested = false;
        bool importAnimationsRequested = false;
        bool exportJsonRequested = false;
        bool compareFbxRequested = false;
        DrawMenuBar(uiFont,
                    openMenu,
                    menuOpenRequested,
                    menuSaveFbxRequested,
                    menuSaveAsFbxRequested,
                    importAnimationsRequested,
                    exportJsonRequested,
                    compareFbxRequested,
                    quitRequested,
                    viewMode,
                    navigation,
                    visibility);
        saveFbxRequested = saveFbxRequested || menuSaveFbxRequested;
        saveAsFbxRequested = saveAsFbxRequested || menuSaveAsFbxRequested;
        DrawSkeletonCompareResultWindow(uiFont, compareResultVisible, compareResultCompatible, compareResultPath, compareResultText);
        EndDrawing();

        if (!droppedPaths.empty() && !droppedTextureHandled)
        {
            bool openedAny = false;
            bool ignoredTexture = false;
            if (active && active->loaded.valid)
            {
                const int assignedTextures = AutoAssignDroppedTextures(*active, droppedPaths, notice, error);
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
                    if (ImportAnimationsFromFbx(*active, importPath, importedCount, importError))
                    {
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
                if (SaveFbxModelAnimations(active->path, active->path, active->loaded, saveError))
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
                    if (SaveFbxModelAnimations(active->path, savePath, active->loaded, saveError))
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
