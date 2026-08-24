#include <cmath>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <limits>
#include <memory>
#include <string>
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

constexpr WindowsDword kOfnFileMustExist = 0x00001000;
constexpr WindowsDword kOfnPathMustExist = 0x00000800;
constexpr WindowsDword kOfnNoChangeDir = 0x00000008;
#endif

#include "FbxLoader.h"
#include "raylib.h"
#include "raymath.h"
#include "rlgl.h"

namespace
{
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
    Stats
};

struct ModelTab
{
    LoadedFbxModel loaded;
    OrbitCamera orbit;
    AnimationState animation;
    std::vector<bool> collapsedNodes;
    std::vector<float> blendedVertices;
    std::vector<float> blendedNormals;
    std::vector<BoneSegment> visibleBones;
    std::vector<BonePose> visibleBonePoses;
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
    bool valid = false;
};

float ClampFloat(float value, float minimum, float maximum)
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

void ApplyNeutralMaterial(LoadedFbxModel& loaded)
{
    if (loaded.valid && loaded.model.materialCount > 0)
    {
        loaded.model.materials[0].maps[MATERIAL_MAP_DIFFUSE].color = Color{ 135, 135, 135, 255 };
    }
}

LitShader LoadBasicLitShader()
{
    const char* vertexShader = R"(
#version 330
in vec3 vertexPosition;
in vec2 vertexTexCoord;
in vec3 vertexNormal;
in vec4 vertexColor;

uniform mat4 mvp;
uniform mat4 matModel;
uniform mat4 matNormal;

out vec3 fragPosition;
out vec3 fragNormal;
out vec4 fragColor;

void main()
{
    fragPosition = vec3(matModel*vec4(vertexPosition, 1.0));
    fragNormal = normalize(vec3(matNormal*vec4(vertexNormal, 0.0)));
    fragColor = vertexColor;
    gl_Position = mvp*vec4(vertexPosition, 1.0);
}
)";

    const char* fragmentShader = R"(
#version 330
in vec3 fragPosition;
in vec3 fragNormal;
in vec4 fragColor;

uniform vec4 colDiffuse;
uniform vec3 viewPos;
uniform vec3 lightDir;
uniform vec4 lightColor;
uniform vec4 ambient;

out vec4 finalColor;

void main()
{
    vec3 normal = normalize(fragNormal);
    vec3 light = normalize(-lightDir);
    float diffuse = max(dot(normal, light), 0.0);

    vec3 viewDir = normalize(viewPos - fragPosition);
    vec3 halfwayDir = normalize(light + viewDir);
    float specular = pow(max(dot(normal, halfwayDir), 0.0), 48.0) * 0.18;

    vec3 base = colDiffuse.rgb * fragColor.rgb;
    vec3 shaded = base * ambient.rgb + base * lightColor.rgb * diffuse + lightColor.rgb * specular;
    finalColor = vec4(shaded, colDiffuse.a * fragColor.a);
}
)";

    LitShader lit;
    lit.shader = LoadShaderFromMemory(vertexShader, fragmentShader);
    lit.viewPositionLoc = GetShaderLocation(lit.shader, "viewPos");
    lit.lightDirectionLoc = GetShaderLocation(lit.shader, "lightDir");
    lit.lightColorLoc = GetShaderLocation(lit.shader, "lightColor");
    lit.ambientLoc = GetShaderLocation(lit.shader, "ambient");
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

void UpdateLitShader(const LitShader& lit, const OrbitCamera& orbit)
{
    if (!lit.valid) return;

    const float viewPosition[3] = { orbit.camera.position.x, orbit.camera.position.y, orbit.camera.position.z };
    Vector3 light = Vector3Normalize(Vector3Subtract(orbit.target, orbit.camera.position));
    light = Vector3Normalize(Vector3Add(light, Vector3{ 0.0f, -0.35f, 0.0f }));
    const float lightDirection[3] = { light.x, light.y, light.z };
    const float lightColor[4] = { 1.0f, 0.96f, 0.88f, 1.0f };
    const float ambient[4] = { 0.32f, 0.35f, 0.38f, 1.0f };

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

void ApplyAnimatedMeshFrame(ModelTab& tab)
{
    if (!tab.loaded.hasMesh)
    {
        return;
    }

    if (tab.animation.clipIndex < 0)
    {
        if (tab.appliedClipIndex == -1) return;

        Mesh& mesh = tab.loaded.model.meshes[0];
        const size_t vertexBytes = tab.loaded.bindVertices.size() * sizeof(float);
        const size_t normalBytes = tab.loaded.bindNormals.size() * sizeof(float);
        if (tab.loaded.bindVertices.size() == static_cast<size_t>(mesh.vertexCount) * 3 &&
            tab.loaded.bindNormals.size() == static_cast<size_t>(mesh.vertexCount) * 3)
        {
            std::memcpy(mesh.vertices, tab.loaded.bindVertices.data(), vertexBytes);
            std::memcpy(mesh.normals, tab.loaded.bindNormals.data(), normalBytes);
            UpdateMeshBuffer(mesh, 0, tab.loaded.bindVertices.data(), static_cast<int>(vertexBytes), 0);
            UpdateMeshBuffer(mesh, 2, tab.loaded.bindNormals.data(), static_cast<int>(normalBytes), 0);
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

    Mesh& mesh = tab.loaded.model.meshes[0];
    const MeshFrame& firstFrame = clip.meshFrames[static_cast<size_t>(sample.first)];
    const MeshFrame& secondFrame = clip.meshFrames[static_cast<size_t>(sample.second)];
    const size_t expectedFloats = static_cast<size_t>(mesh.vertexCount) * 3;

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
            tab.blendedVertices.resize(expectedFloats);
            tab.blendedNormals.resize(expectedFloats);
            for (size_t i = 0; i < expectedFloats; i += 3)
            {
                tab.blendedVertices[i] = firstFrame.vertices[i] * inverseAlpha + secondFrame.vertices[i] * alpha;
                tab.blendedVertices[i + 1] = firstFrame.vertices[i + 1] * inverseAlpha + secondFrame.vertices[i + 1] * alpha;
                tab.blendedVertices[i + 2] = firstFrame.vertices[i + 2] * inverseAlpha + secondFrame.vertices[i + 2] * alpha;

                const Vector3 blendedNormal = NormalizeOrFallback(Vector3{
                    firstFrame.normals[i] * inverseAlpha + secondFrame.normals[i] * alpha,
                    firstFrame.normals[i + 1] * inverseAlpha + secondFrame.normals[i + 1] * alpha,
                    firstFrame.normals[i + 2] * inverseAlpha + secondFrame.normals[i + 2] * alpha
                }, Vector3{ firstFrame.normals[i], firstFrame.normals[i + 1], firstFrame.normals[i + 2] });
                tab.blendedNormals[i] = blendedNormal.x;
                tab.blendedNormals[i + 1] = blendedNormal.y;
                tab.blendedNormals[i + 2] = blendedNormal.z;
            }
            vertices = tab.blendedVertices.data();
            normals = tab.blendedNormals.data();
        }

        const size_t vertexBytes = expectedFloats * sizeof(float);
        const size_t normalBytes = expectedFloats * sizeof(float);
        std::memcpy(mesh.vertices, vertices, vertexBytes);
        std::memcpy(mesh.normals, normals, normalBytes);
        UpdateMeshBuffer(mesh, 0, vertices, static_cast<int>(vertexBytes), 0);
        UpdateMeshBuffer(mesh, 2, normals, static_cast<int>(normalBytes), 0);
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
        DrawMayaBone(bone.start, bone.end, radius, selected ? Color{ 255, 214, 80, 255 } : Color{ 100, 185, 255, 255 });
    }

    for (const BoneSegment& bone : bones)
    {
        DrawJointSphere(bone.start, bone.startNode == selectedNode ? radius * 1.15f : radius * 0.85f, bone.startNode == selectedNode ? Color{ 255, 214, 80, 255 } : Color{ 142, 210, 255, 255 });
        DrawJointSphere(bone.end, bone.endNode == selectedNode ? radius * 1.15f : radius * 0.85f, bone.endNode == selectedNode ? Color{ 255, 214, 80, 255 } : Color{ 142, 210, 255, 255 });
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

    DrawEmptyCross(node, crossLength, Color{ 255, 214, 80, 255 });
    DrawSphere(node.position, sphereRadius, Color{ 255, 214, 80, 255 });
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
        DrawEmptyCross(node, length, i == tab.selectedNode ? Color{ 255, 214, 80, 255 } : Color{ 100, 185, 255, 230 });
    }
}

const float* GetCurrentMeshVertices(const ModelTab& tab)
{
    if (!tab.loaded.hasMesh || tab.loaded.model.meshCount <= 0) return nullptr;
    return tab.loaded.model.meshes[0].vertices;
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

        if (node.type == SceneNodeType::Mesh && node.hasBounds)
        {
            if (!visibility.geometry) continue;
            const RayCollision hit = GetRayCollisionBox(ray, node.bounds);
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
        DrawMeshNodeWireframe(tab, node, Color{ 255, 214, 80, 255 });
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
        DrawEmptyCross(node, length, Color{ 255, 214, 80, 255 });
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
    const float panelY = static_cast<float>(GetScreenHeight()) - 124.0f - panelH - 12.0f;

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
    return static_cast<float>(GetScreenHeight()) - 61.0f - 124.0f;
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

bool DrawPanelTab(Font font, Rectangle bounds, const char* label, bool selected)
{
    const Vector2 mouse = GetMousePosition();
    const bool hovered = CheckCollisionPointRec(mouse, bounds);
    DrawRectangleRec(bounds, selected ? Color{ 48, 70, 92, 255 } : hovered ? Color{ 34, 39, 45, 255 } : Color{ 24, 27, 31, 245 });
    DrawUiText(font, label, bounds.x + 10.0f, bounds.y + 5.0f, 15.0f, selected ? RAYWHITE : Color{ 180, 190, 200, 255 });
    return hovered && IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
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

void DrawHierarchyPanel(Font font, ModelTab* active, HierarchyPanelState& panel)
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

    const Rectangle hierarchyTab{ panelX + 8.0f, panelY + 34.0f, (panelW - 20.0f) * 0.5f, 24.0f };
    const Rectangle statsTab{ hierarchyTab.x + hierarchyTab.width + 4.0f, panelY + 34.0f, hierarchyTab.width, 24.0f };
    if (DrawPanelTab(font, hierarchyTab, "Hierarchy", panel.activeTab == LeftPanelTab::Hierarchy))
    {
        panel.activeTab = LeftPanelTab::Hierarchy;
    }
    if (DrawPanelTab(font, statsTab, "Stats", panel.activeTab == LeftPanelTab::Stats))
    {
        panel.activeTab = LeftPanelTab::Stats;
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

void DrawMenuBar(Font font, OpenMenu& openMenu, bool& openRequested, ViewMode& viewMode, NavigationPreset& navigation, VisibilityState& visibility)
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

    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && mouse.y > 260.0f && openMenu != OpenMenu::None)
    {
        openMenu = OpenMenu::None;
    }

    if (openMenu == OpenMenu::File)
    {
        DrawRectangle(8, 29, 220, 38, Color{ 28, 31, 35, 245 });
        if (DrawMenuItem(font, Rectangle{ 8.0f, 29.0f, 220.0f, 30.0f }, "Open FBX...        O"))
        {
            openRequested = true;
            openMenu = OpenMenu::None;
        }
    }
    else if (openMenu == OpenMenu::View)
    {
        DrawRectangle(66, 29, 230, 248, Color{ 28, 31, 35, 245 });
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
        if (DrawMenuItem(font, Rectangle{ 66.0f, 127.0f, 230.0f, 30.0f }, visibility.geometry ? "[x] Geometry" : "[ ] Geometry"))
        {
            visibility.geometry = !visibility.geometry;
        }
        if (DrawMenuItem(font, Rectangle{ 66.0f, 157.0f, 230.0f, 30.0f }, visibility.bones ? "[x] Bones" : "[ ] Bones"))
        {
            visibility.bones = !visibility.bones;
        }
        if (DrawMenuItem(font, Rectangle{ 66.0f, 187.0f, 230.0f, 30.0f }, visibility.boneRotations ? "[x] Bone Rotations" : "[ ] Bone Rotations"))
        {
            visibility.boneRotations = !visibility.boneRotations;
        }
        if (DrawMenuItem(font, Rectangle{ 66.0f, 217.0f, 230.0f, 30.0f }, visibility.empties ? "[x] Empties" : "[ ] Empties"))
        {
            visibility.empties = !visibility.empties;
        }
    }
    else if (openMenu == OpenMenu::Preferences)
    {
        DrawRectangle(128, 29, 420, 220, Color{ 28, 31, 35, 245 });
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
        DrawUiText(font, "O open FBX    V view mode    F focus    Space play/pause", 140.0f, 136.0f, 15.0f, Color{ 205, 213, 220, 255 });
        DrawUiText(font, "Blender: MMB orbit, Alt snap, Shift+MMB pan, Wheel zoom", 140.0f, 162.0f, 15.0f, Color{ 205, 213, 220, 255 });
        DrawUiText(font, "Maya: Alt+LMB orbit, Shift snap, Alt+MMB pan, Alt+RMB/Wheel zoom", 140.0f, 188.0f, 15.0f, Color{ 205, 213, 220, 255 });
        DrawUiText(font, "Tabs: X closes, middle-click tab closes", 140.0f, 214.0f, 15.0f, Color{ 205, 213, 220, 255 });
    }
}

void CloseTab(std::vector<std::unique_ptr<ModelTab>>& tabs, int& activeTab, int tabIndex)
{
    if (tabIndex < 0 || tabIndex >= static_cast<int>(tabs.size())) return;

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

void DrawTimeline(Font font, LoadedFbxModel& loaded, AnimationState& animation)
{
    const int width = GetScreenWidth();
    const int height = GetScreenHeight();
    const float panelHeight = 124.0f;
    const float panelY = static_cast<float>(height) - panelHeight;
    const float listWidth = 300.0f;

    DrawRectangle(0, static_cast<int>(panelY), width, static_cast<int>(panelHeight), Color{ 20, 22, 24, 238 });
    DrawLine(0, static_cast<int>(panelY), width, static_cast<int>(panelY), Color{ 76, 84, 92, 255 });
    DrawLine(static_cast<int>(listWidth), static_cast<int>(panelY), static_cast<int>(listWidth), height, Color{ 64, 70, 78, 255 });

    DrawUiText(font, "ANIMATIONS", 12.0f, panelY + 10.0f, 16.0f, Color{ 165, 182, 196, 255 });

    if (loaded.animations.empty())
    {
        DrawUiText(font, "No FBX animation stacks", 12.0f, panelY + 64.0f, 16.0f, Color{ 128, 136, 144, 255 });
    }

    const Vector2 mouse = GetMousePosition();
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

    const int visibleRows = 3;
    for (int i = 0; i < static_cast<int>(loaded.animations.size()) && i < visibleRows; ++i)
    {
        const float rowY = panelY + 60.0f + static_cast<float>(i) * 26.0f;
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

        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(mouse, row))
        {
            animation.clipIndex = i;
            animation.time = 0.0f;
            animation.playing = true;
            animation.scrubbing = false;
        }

        char rowText[256] = {};
        std::snprintf(rowText, sizeof(rowText), "%s  %.2fs", loaded.animations[static_cast<size_t>(i)].name.c_str(), loaded.animations[static_cast<size_t>(i)].duration);
        DrawUiText(font, rowText, row.x + 8.0f, row.y + 3.0f, 15.0f, selected ? RAYWHITE : Color{ 185, 194, 202, 255 });
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
    ApplyWindowIcon();
    SetTargetFPS(60);

    Font uiFont = LoadTechnicalFont();
    LitShader litShader = LoadBasicLitShader();
    OrbitCamera emptyOrbit = CreateDefaultCamera();
    std::vector<std::unique_ptr<ModelTab>> tabs;
    int activeTab = -1;
    ViewMode viewMode = ViewMode::Shaded;
    NavigationPreset navigation = NavigationPreset::Blender;
    OpenMenu openMenu = OpenMenu::None;
    HierarchyPanelState hierarchyPanel;
    VisibilityState visibility;
    std::string error;

    auto openPathInNewTab = [&](const std::string& path)
    {
        DrawLoadingScreen(uiFont, path);

        auto tab = std::make_unique<ModelTab>();
        std::string loadError;
        if (!LoadFbxModel(path, tab->loaded, loadError))
        {
            error = loadError;
            std::cerr << error << "\n";
            return;
        }

        ApplyNeutralMaterial(tab->loaded);
        ApplyLitShader(tab->loaded, litShader);
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
    };

    for (int i = 1; i < argc; ++i)
    {
        openPathInNewTab(argv[i]);
    }

    while (!WindowShouldClose())
    {
        bool openRequested = IsKeyPressed(KEY_O);

        if (openRequested)
        {
            const std::string selectedPath = OpenFbxFileDialog();
            if (!selectedPath.empty())
            {
                openPathInNewTab(selectedPath);
            }
        }

        if (IsFileDropped())
        {
            FilePathList droppedFiles = LoadDroppedFiles();
            for (unsigned int i = 0; i < droppedFiles.count; ++i)
            {
                openPathInNewTab(droppedFiles.paths[i]);
            }
            UnloadDroppedFiles(droppedFiles);
        }

        ModelTab* active = activeTab >= 0 && activeTab < static_cast<int>(tabs.size()) ? tabs[static_cast<size_t>(activeTab)].get() : nullptr;
        const Vector2 mouse = GetMousePosition();
        UpdateHierarchyPanelInteraction(hierarchyPanel, active);
        const float hierarchyBlockW = GetHierarchyPanelBlockWidth(hierarchyPanel);
        const bool mouseInViewport = mouse.x > hierarchyBlockW && mouse.y >= 61.0f && mouse.y < static_cast<float>(GetScreenHeight()) - 124.0f && openMenu == OpenMenu::None && !hierarchyPanel.resizing;

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
        }

        BeginDrawing();
        ClearBackground(Color{ 38, 40, 43, 255 });

        BeginMode3D(active ? active->orbit.camera : emptyOrbit.camera);
        DrawGrid(20, 1.0f);
        if (active && active->loaded.valid)
        {
            if (active->loaded.hasMesh && visibility.geometry)
            {
                if (viewMode == ViewMode::Shaded || viewMode == ViewMode::ShadedWireframe)
                {
                    DrawModel(active->loaded.model, Vector3{ 0.0f, 0.0f, 0.0f }, 1.0f, WHITE);
                }

                if (viewMode == ViewMode::ShadedWireframe || viewMode == ViewMode::Wireframe)
                {
                    rlEnableWireMode();
                    DrawModel(active->loaded.model, Vector3{ 0.0f, 0.0f, 0.0f }, 1.0f, viewMode == ViewMode::Wireframe ? Color{ 220, 225, 230, 255 } : Color{ 25, 28, 31, 150 });
                    rlDisableWireMode();
                }

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
        std::snprintf(statusText, sizeof(statusText), "VIEW: %s    NAV: %s", GetViewModeName(viewMode), GetNavigationPresetName(navigation));
        DrawUiText(uiFont, statusText, static_cast<float>(GetScreenWidth() - 360), 8, 16, Color{ 165, 220, 255, 255 });

        if (active)
        {
            DrawUiText(uiFont, active->path.c_str(), hierarchyBlockW + 12.0f, 66, 16, Color{ 190, 190, 190, 255 });
        }
        else
        {
            DrawUiText(uiFont, "No FBX loaded", hierarchyBlockW + 12.0f, 66, 16, Color{ 190, 190, 190, 255 });
        }

        if (!error.empty())
        {
            DrawUiText(uiFont, error.c_str(), 12, static_cast<float>(GetScreenHeight() - 154), 18, Color{ 255, 140, 120, 255 });
        }

        DrawSelectedInfoPanel(uiFont, active);

        if (active)
        {
            DrawTimeline(uiFont, active->loaded, active->animation);
        }
        else
        {
            DrawRectangle(0, GetScreenHeight() - 124, GetScreenWidth(), 124, Color{ 20, 22, 24, 238 });
            DrawLine(0, GetScreenHeight() - 124, GetScreenWidth(), GetScreenHeight() - 124, Color{ 76, 84, 92, 255 });
            DrawUiText(uiFont, "ANIMATIONS", 12.0f, static_cast<float>(GetScreenHeight() - 114), 16.0f, Color{ 165, 182, 196, 255 });
            DrawUiText(uiFont, "Open an FBX file to show animation stacks", 12.0f, static_cast<float>(GetScreenHeight() - 86), 16.0f, Color{ 128, 136, 144, 255 });
        }

        DrawHierarchyPanel(uiFont, active, hierarchyPanel);
        DrawOrientationGizmo(uiFont, active ? active->orbit.camera : emptyOrbit.camera);

        DrawTabs(uiFont, tabs, activeTab);
        active = activeTab >= 0 && activeTab < static_cast<int>(tabs.size()) ? tabs[static_cast<size_t>(activeTab)].get() : nullptr;

        bool menuOpenRequested = false;
        DrawMenuBar(uiFont, openMenu, menuOpenRequested, viewMode, navigation, visibility);
        EndDrawing();

        if (menuOpenRequested)
        {
            const std::string selectedPath = OpenFbxFileDialog();
            if (!selectedPath.empty())
            {
                openPathInNewTab(selectedPath);
            }
        }
    }

    for (std::unique_ptr<ModelTab>& tab : tabs)
    {
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
