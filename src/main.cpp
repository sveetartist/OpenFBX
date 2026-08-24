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
};

struct AnimationState
{
    int clipIndex = -1;
    float time = 0.0f;
    bool playing = false;
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

struct ModelTab
{
    LoadedFbxModel loaded;
    OrbitCamera orbit;
    AnimationState animation;
    std::vector<bool> collapsedNodes;
    int appliedClipIndex = -2;
    int appliedMeshFrameIndex = -1;
    int selectedNode = -1;
    std::string path;
    std::string title;
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
    orbit.camera.fovy = 45.0f;
    orbit.camera.projection = CAMERA_PERSPECTIVE;
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

void UpdateBlenderNavigation(OrbitCamera& orbit)
{
    const Vector2 mouseDelta = GetMouseDelta();
    const bool shiftDown = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);
    const bool middleDown = IsMouseButtonDown(MOUSE_BUTTON_MIDDLE);

    if (middleDown && shiftDown)
    {
        const Vector3 forward = Vector3Normalize(Vector3Subtract(orbit.target, orbit.camera.position));
        const Vector3 right = Vector3Normalize(Vector3CrossProduct(forward, Vector3{ 0.0f, 1.0f, 0.0f }));
        const Vector3 up = Vector3Normalize(Vector3CrossProduct(right, forward));
        const float panScale = orbit.distance * 0.0015f;

        orbit.target = Vector3Add(orbit.target, Vector3Scale(right, -mouseDelta.x * panScale));
        orbit.target = Vector3Add(orbit.target, Vector3Scale(up, mouseDelta.y * panScale));
    }
    else if (middleDown)
    {
        orbit.yaw -= mouseDelta.x * 0.008f;
        orbit.pitch += mouseDelta.y * 0.008f;
        orbit.pitch = ClampFloat(orbit.pitch, -89.0f * DEG2RAD, 89.0f * DEG2RAD);
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

    if (altDown && IsMouseButtonDown(MOUSE_BUTTON_MIDDLE))
    {
        const Vector3 forward = Vector3Normalize(Vector3Subtract(orbit.target, orbit.camera.position));
        const Vector3 right = Vector3Normalize(Vector3CrossProduct(forward, Vector3{ 0.0f, 1.0f, 0.0f }));
        const Vector3 up = Vector3Normalize(Vector3CrossProduct(right, forward));
        const float panScale = orbit.distance * 0.0015f;

        orbit.target = Vector3Add(orbit.target, Vector3Scale(right, -mouseDelta.x * panScale));
        orbit.target = Vector3Add(orbit.target, Vector3Scale(up, mouseDelta.y * panScale));
    }
    else if (altDown && IsMouseButtonDown(MOUSE_BUTTON_LEFT))
    {
        orbit.yaw -= mouseDelta.x * 0.008f;
        orbit.pitch += mouseDelta.y * 0.008f;
        orbit.pitch = ClampFloat(orbit.pitch, -89.0f * DEG2RAD, 89.0f * DEG2RAD);
    }
    else if (altDown && IsMouseButtonDown(MOUSE_BUTTON_RIGHT))
    {
        orbit.distance *= std::pow(1.01f, mouseDelta.y);
        orbit.distance = std::max(0.001f, orbit.distance);
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
        loaded.model.materials[0].maps[MATERIAL_MAP_DIFFUSE].color = Color{ 200, 200, 200, 255 };
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
    fragNormal = normalize(vec3(matNormal*vec4(vertexNormal, 1.0)));
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

const std::vector<BoneSegment>& GetVisibleBones(const LoadedFbxModel& loaded, const AnimationState& animation)
{
    if (animation.clipIndex < 0 || animation.clipIndex >= static_cast<int>(loaded.animations.size()))
    {
        return loaded.bones;
    }

    const AnimationClip& clip = loaded.animations[static_cast<size_t>(animation.clipIndex)];
    if (clip.frames.empty()) return loaded.bones;

    if (clip.duration <= 0.0f) return clip.frames.front().bones;

    const float normalizedTime = ClampFloat(animation.time, 0.0f, clip.duration);
    const float frameAlpha = normalizedTime / clip.duration * static_cast<float>(clip.frames.size() - 1);
    const size_t frameIndex = static_cast<size_t>(ClampFloat(std::round(frameAlpha), 0.0f, static_cast<float>(clip.frames.size() - 1)));
    return clip.frames[frameIndex].bones;
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
        return;
    }

    if (tab.animation.clipIndex >= static_cast<int>(tab.loaded.animations.size()))
    {
        return;
    }

    const AnimationClip& clip = tab.loaded.animations[static_cast<size_t>(tab.animation.clipIndex)];
    const int frameIndex = GetAnimationFrameIndex(tab.loaded, tab.animation);
    if (frameIndex < 0 || frameIndex >= static_cast<int>(clip.meshFrames.size()) ||
        (frameIndex == tab.appliedMeshFrameIndex && tab.animation.clipIndex == tab.appliedClipIndex))
    {
        return;
    }

    Mesh& mesh = tab.loaded.model.meshes[0];
    const MeshFrame& frame = clip.meshFrames[static_cast<size_t>(frameIndex)];
    const size_t vertexBytes = frame.vertices.size() * sizeof(float);
    const size_t normalBytes = frame.normals.size() * sizeof(float);

    if (frame.vertices.size() == static_cast<size_t>(mesh.vertexCount) * 3 && frame.normals.size() == static_cast<size_t>(mesh.vertexCount) * 3)
    {
        std::memcpy(mesh.vertices, frame.vertices.data(), vertexBytes);
        std::memcpy(mesh.normals, frame.normals.data(), normalBytes);
        UpdateMeshBuffer(mesh, 0, frame.vertices.data(), static_cast<int>(vertexBytes), 0);
        UpdateMeshBuffer(mesh, 2, frame.normals.data(), static_cast<int>(normalBytes), 0);
        tab.appliedClipIndex = tab.animation.clipIndex;
        tab.appliedMeshFrameIndex = frameIndex;
    }
}

void DrawJointBillboard(const Camera3D& camera, Vector3 position, float radius, Color color)
{
    const Vector3 forward = Vector3Normalize(Vector3Subtract(camera.position, position));
    Vector3 right = Vector3Normalize(Vector3CrossProduct(Vector3{ 0.0f, 1.0f, 0.0f }, forward));
    if (Vector3Length(right) < 0.0001f)
    {
        right = Vector3{ 1.0f, 0.0f, 0.0f };
    }
    const Vector3 up = Vector3Normalize(Vector3CrossProduct(forward, right));

    constexpr int kSegments = 18;
    Vector3 previous = Vector3Add(position, Vector3Scale(right, radius));
    for (int i = 1; i <= kSegments; ++i)
    {
        const float angle = static_cast<float>(i) / static_cast<float>(kSegments) * 2.0f * PI;
        const Vector3 offset = Vector3Add(Vector3Scale(right, std::cos(angle) * radius),
                                          Vector3Scale(up, std::sin(angle) * radius));
        const Vector3 current = Vector3Add(position, offset);
        DrawLine3D(previous, current, color);
        previous = current;
    }

    DrawLine3D(Vector3Subtract(position, Vector3Scale(right, radius * 0.65f)),
               Vector3Add(position, Vector3Scale(right, radius * 0.65f)),
               color);
    DrawLine3D(Vector3Subtract(position, Vector3Scale(up, radius * 0.65f)),
               Vector3Add(position, Vector3Scale(up, radius * 0.65f)),
               color);
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

void DrawBones(const std::vector<BoneSegment>& bones, const Camera3D& camera)
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
        DrawMayaBone(bone.start, bone.end, radius, Color{ 255, 205, 64, 255 });
    }

    for (const BoneSegment& bone : bones)
    {
        DrawJointBillboard(camera, bone.start, radius, Color{ 255, 232, 126, 255 });
        DrawJointBillboard(camera, bone.end, radius, Color{ 255, 232, 126, 255 });
    }
}

float DistancePointToRay(Vector3 point, Ray ray)
{
    const Vector3 toPoint = Vector3Subtract(point, ray.position);
    const Vector3 projected = Vector3Scale(ray.direction, Vector3DotProduct(toPoint, ray.direction));
    return Vector3Length(Vector3Subtract(toPoint, projected));
}

float DistanceSegmentToRay(Vector3 a, Vector3 b, Ray ray)
{
    constexpr int kSteps = 12;
    float best = std::numeric_limits<float>::max();
    for (int i = 0; i <= kSteps; ++i)
    {
        const float t = static_cast<float>(i) / static_cast<float>(kSteps);
        const Vector3 point = Vector3Lerp(a, b, t);
        best = std::min(best, DistancePointToRay(point, ray));
    }
    return best;
}

void SelectNodeFromViewport(ModelTab& tab, Vector2 mouse)
{
    if (!tab.loaded.valid || tab.loaded.nodes.empty()) return;

    const Ray ray = GetScreenToWorldRay(mouse, tab.orbit.camera);
    int bestNode = -1;
    float bestDistance = std::numeric_limits<float>::max();
    const float bonePickRadius = std::max(0.04f, tab.orbit.distance * 0.012f);

    for (const BoneSegment& bone : GetVisibleBones(tab.loaded, tab.animation))
    {
        const float distance = DistanceSegmentToRay(bone.start, bone.end, ray);
        if (distance < bonePickRadius)
        {
            const float startDepth = Vector3DotProduct(Vector3Subtract(bone.start, ray.position), ray.direction);
            const float endDepth = Vector3DotProduct(Vector3Subtract(bone.end, ray.position), ray.direction);
            const float depth = std::max(0.0f, std::min(startDepth, endDepth));
            if (depth < bestDistance)
            {
                bestDistance = depth;
                bestNode = bone.endNode >= 0 ? bone.endNode : bone.startNode;
            }
        }
    }

    for (int i = 0; i < static_cast<int>(tab.loaded.nodes.size()); ++i)
    {
        const SceneNode& node = tab.loaded.nodes[static_cast<size_t>(i)];

        if (node.type == SceneNodeType::Mesh && node.hasBounds)
        {
            const RayCollision hit = GetRayCollisionBox(ray, node.bounds);
            if (hit.hit && hit.distance < bestDistance)
            {
                bestDistance = hit.distance;
                bestNode = i;
            }
        }
        else
        {
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
    }
}

void DrawSelectedNodeOverlay(const ModelTab& tab)
{
    if (tab.selectedNode < 0 || tab.selectedNode >= static_cast<int>(tab.loaded.nodes.size())) return;

    const SceneNode& node = tab.loaded.nodes[static_cast<size_t>(tab.selectedNode)];
    if (node.type == SceneNodeType::Mesh && node.hasBounds)
    {
        DrawBoundingBox(node.bounds, Color{ 100, 185, 255, 255 });
        return;
    }

    const float radius = std::max(0.04f, tab.orbit.distance * 0.012f);
    DrawJointBillboard(tab.orbit.camera, node.position, radius * 1.8f, Color{ 100, 185, 255, 255 });
    DrawLine3D(Vector3Subtract(node.position, Vector3{ radius, 0.0f, 0.0f }), Vector3Add(node.position, Vector3{ radius, 0.0f, 0.0f }), Color{ 100, 185, 255, 255 });
    DrawLine3D(Vector3Subtract(node.position, Vector3{ 0.0f, radius, 0.0f }), Vector3Add(node.position, Vector3{ 0.0f, radius, 0.0f }), Color{ 100, 185, 255, 255 });
    DrawLine3D(Vector3Subtract(node.position, Vector3{ 0.0f, 0.0f, radius }), Vector3Add(node.position, Vector3{ 0.0f, 0.0f, radius }), Color{ 100, 185, 255, 255 });
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

std::string MakeTabTitle(const std::string& path)
{
    const char* fileName = GetFileName(path.c_str());
    return fileName && fileName[0] ? fileName : path;
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

void DrawHierarchyPanel(Font font, ModelTab* active)
{
    constexpr float panelX = 0.0f;
    constexpr float panelY = 61.0f;
    constexpr float panelW = 320.0f;
    const float panelH = static_cast<float>(GetScreenHeight()) - panelY - 124.0f;

    DrawRectangle(static_cast<int>(panelX), static_cast<int>(panelY), static_cast<int>(panelW), static_cast<int>(panelH), Color{ 18, 20, 23, 235 });
    DrawLine(static_cast<int>(panelW), static_cast<int>(panelY), static_cast<int>(panelW), static_cast<int>(panelY + panelH), Color{ 64, 70, 78, 255 });
    DrawUiText(font, "SCENE HIERARCHY", panelX + 12.0f, panelY + 10.0f, 16.0f, Color{ 165, 182, 196, 255 });

    if (!active)
    {
        DrawUiText(font, "No active tab", panelX + 12.0f, panelY + 40.0f, 15.0f, Color{ 128, 136, 144, 255 });
        return;
    }

    if (active->collapsedNodes.size() != active->loaded.nodes.size())
    {
        active->collapsedNodes.assign(active->loaded.nodes.size(), false);
    }

    const Vector2 mouse = GetMousePosition();
    float rowY = panelY + 34.0f;
    constexpr float rowH = 22.0f;

    for (int i = 0; i < static_cast<int>(active->loaded.nodes.size()); ++i)
    {
        if (!IsSceneNodeVisible(active->loaded, active->collapsedNodes, i)) continue;
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
        DrawUiText(font, label, panelX + 28.0f + indent, rowY + 3.0f, 15.0f, selected ? RAYWHITE : Color{ 198, 207, 216, 255 });

        if (hovered && IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && !CheckCollisionPointRec(mouse, collapseRect))
        {
            active->selectedNode = i;
        }

        rowY += rowH;
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

void DrawMenuBar(Font font, OpenMenu& openMenu, bool& openRequested, ViewMode& viewMode, NavigationPreset& navigation)
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

    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && mouse.y > 220.0f && openMenu != OpenMenu::None)
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
        DrawRectangle(66, 29, 230, 98, Color{ 28, 31, 35, 245 });
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
        DrawUiText(font, "Blender: MMB orbit, Shift+MMB pan, Wheel zoom", 140.0f, 162.0f, 15.0f, Color{ 205, 213, 220, 255 });
        DrawUiText(font, "Maya: Alt+LMB orbit, Alt+MMB pan, Alt+RMB/Wheel zoom", 140.0f, 188.0f, 15.0f, Color{ 205, 213, 220, 255 });
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
    DrawUiText(font, "No animation", noAnimationRow.x + 8.0f, noAnimationRow.y + 3.0f, 15.0f, noAnimationSelected ? RAYWHITE : Color{ 185, 194, 202, 255 });
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(mouse, noAnimationRow))
    {
        animation.clipIndex = -1;
        animation.time = 0.0f;
        animation.playing = false;
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
            animation.playing = false;
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

    DrawUiText(font, animation.playing ? "PLAYING  [SPACE]" : "PAUSED   [SPACE]", timelineX, panelY + 14.0f, 16.0f, Color{ 165, 182, 196, 255 });
    DrawRectangleRec(scrub, Color{ 58, 64, 70, 255 });

    if (clip && clip->duration > 0.0f)
    {
        if (IsMouseButtonDown(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(mouse, scrub))
        {
            const float alpha = ClampFloat((mouse.x - scrub.x) / scrub.width, 0.0f, 1.0f);
            animation.time = alpha * clip->duration;
            animation.playing = false;
        }

        const float progress = ClampFloat(animation.time / clip->duration, 0.0f, 1.0f);
        DrawRectangleRec(Rectangle{ scrub.x, scrub.y, scrub.width * progress, scrub.height }, Color{ 94, 156, 214, 255 });
        DrawRectangle(static_cast<int>(scrub.x + scrub.width * progress - 2.0f), static_cast<int>(scrub.y - 5.0f), 4, 24, Color{ 220, 232, 242, 255 });

        char timeText[128] = {};
        std::snprintf(timeText, sizeof(timeText), "%.2fs / %.2fs    frames: %zu", animation.time, clip->duration, clip->frames.size());
        DrawUiText(font, timeText, timelineX, timelineY + 28.0f, 16.0f, Color{ 190, 200, 210, 255 });
    }
    else
    {
        DrawUiText(font, "No active clip", timelineX, timelineY + 28.0f, 16.0f, Color{ 128, 136, 144, 255 });
    }
}
}

int main(int argc, char** argv)
{
    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_MSAA_4X_HINT);
    InitWindow(1280, 800, "openfbx");
    SetTargetFPS(60);

    Font uiFont = LoadTechnicalFont();
    LitShader litShader = LoadBasicLitShader();
    OrbitCamera emptyOrbit = CreateDefaultCamera();
    std::vector<std::unique_ptr<ModelTab>> tabs;
    int activeTab = -1;
    ViewMode viewMode = ViewMode::Shaded;
    NavigationPreset navigation = NavigationPreset::Blender;
    OpenMenu openMenu = OpenMenu::None;
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
        const bool mouseInViewport = mouse.x > 320.0f && mouse.y >= 61.0f && mouse.y < static_cast<float>(GetScreenHeight()) - 124.0f && openMenu == OpenMenu::None;

        if (active && IsKeyPressed(KEY_F) && active->loaded.valid)
        {
            FocusCameraOnBounds(active->orbit, active->loaded.bounds);
        }

        if (IsKeyPressed(KEY_V))
        {
            viewMode = NextViewMode(viewMode);
        }

        if (active && mouseInViewport && IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
        {
            SelectNodeFromViewport(*active, mouse);
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
            if (active->loaded.hasMesh)
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
            }
            rlDrawRenderBatchActive();
            rlDisableDepthTest();
            DrawBones(GetVisibleBones(active->loaded, active->animation), active->orbit.camera);
            DrawSelectedNodeOverlay(*active);
            rlDrawRenderBatchActive();
            rlEnableDepthTest();
        }
        EndMode3D();

        char statusText[256] = {};
        std::snprintf(statusText, sizeof(statusText), "VIEW: %s    NAV: %s", GetViewModeName(viewMode), GetNavigationPresetName(navigation));
        DrawUiText(uiFont, statusText, static_cast<float>(GetScreenWidth() - 360), 8, 16, Color{ 165, 220, 255, 255 });

        if (active)
        {
            DrawUiText(uiFont, active->path.c_str(), 12, 66, 16, Color{ 190, 190, 190, 255 });
        }
        else
        {
            DrawUiText(uiFont, "No FBX loaded", 12, 66, 16, Color{ 190, 190, 190, 255 });
        }

        if (!error.empty())
        {
            DrawUiText(uiFont, error.c_str(), 12, static_cast<float>(GetScreenHeight() - 154), 18, Color{ 255, 140, 120, 255 });
        }

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

        DrawHierarchyPanel(uiFont, active);

        DrawTabs(uiFont, tabs, activeTab);
        active = activeTab >= 0 && activeTab < static_cast<int>(tabs.size()) ? tabs[static_cast<size_t>(activeTab)].get() : nullptr;

        bool menuOpenRequested = false;
        DrawMenuBar(uiFont, openMenu, menuOpenRequested, viewMode, navigation);
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
