#include <cmath>
#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

#include "core/MathUtils.h"
#include "FbxLoader.h"
#include "geometry/MeshTangents.h"
#include "geometry/DeferredSkinning.h"
#include "platform/FileDialogs.h"
#include "raylib.h"
#include "raymath.h"
#include "rlgl.h"
#include "ui/UiPrimitives.h"
#include "viewport/OrbitCamera.h"

using openfbx::ChoosePerpendicular;
using openfbx::BuildMeshTangents;
using openfbx::ClampFloat;
using openfbx::ClampInt;
using openfbx::CreateDefaultCamera;
using openfbx::DrawMenuItem;
using openfbx::DrawPanelButton;
using openfbx::DrawPanelTab;
using openfbx::DrawUiText;
using openfbx::DrawUiTextClipped;
using openfbx::ExitSnappedOrbitView;
using openfbx::FocusCameraOnBounds;
using openfbx::FocusCameraOnPoint;
using openfbx::LerpVector3;
using openfbx::NavigationPreset;
using openfbx::NormalizeOrFallback;
using openfbx::OpenFbxFileDialog;
using openfbx::OrbitCamera;
using openfbx::OpenTextureFileDialog;
using openfbx::SaveAsFbxFileDialog;
using openfbx::StopSnappedOrbitDrag;
using openfbx::UpdateNavigation;
using openfbx::UpdateOrbitCameraTransform;
using openfbx::LoadTechnicalFont;

namespace
{
#ifndef OPENFBX_VERSION
#define OPENFBX_VERSION "0.1.0"
#endif

constexpr const char* kAppName = "openfbx";
constexpr const char* kAppVersion = OPENFBX_VERSION;
constexpr float kTimelinePanelHeight = 124.0f;
constexpr float kTimelineCollapsedHeight = 28.0f;
constexpr float kMetersPerGridCell = 1.0f;
constexpr double kNearClipPlane = 0.0005;
constexpr double kFarClipPlane = 10000.0;
constexpr Color kSelectionColor{ 204, 154, 42, 255 };
constexpr float kMinTransformGizmoScale = 0.5f;
constexpr float kMaxTransformGizmoScale = 2.0f;
constexpr float kMinTransformGizmoLineWidth = 2.0f;
constexpr float kMaxTransformGizmoLineWidth = 10.0f;
constexpr float kNodeContextMenuW = 152.0f;
constexpr float kNodeContextMenuBaseH = 122.0f;
constexpr float kNodeContextMenuBoneH = 152.0f;
float gBottomPanelReservedHeight = kTimelinePanelHeight;
float gTransformGizmoScale = 1.0f;
float gTransformGizmoLineWidth = 6.0f;
constexpr float kMinBoneOrientationScale = 0.25f;
constexpr float kMaxBoneOrientationScale = 4.0f;
constexpr float kMinBoneOrientationLineWidth = 1.0f;
constexpr float kMaxBoneOrientationLineWidth = 10.0f;
int gCheckerSquares = 16;
float gBoneOrientationScale = 1.0f;
float gBoneOrientationLineWidth = 2.0f;

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
    AnimationClip,
    UvSet
};

struct RenameEditor
{
    RenameTarget target = RenameTarget::None;
    int nodeIndex = -1;
    int clipIndex = -1;
    int uvSetIndex = -1;
    char text[256]{};
    int cursor = 0;
    bool textSelected = false;
    bool active = false;
    bool justOpened = false;
};

enum class TransformValueField
{
    None,
    Position,
    Rotation,
    Scale
};

struct TransformValueEditor
{
    bool active = false;
    int nodeIndex = -1;
    TransformValueField field = TransformValueField::None;
    int component = 0;
    char text[3][64]{};
    bool textSelected[3]{};
};

enum class ViewMode
{
    Shaded,
    ShadedWireframe,
    Wireframe,
    MaterialColors,
    UvIslands,
    Checker
};

enum class OpenMenu
{
    None,
    File,
    Edit,
    View,
    Preferences,
    Help
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
    std::vector<std::string> uvSetNames;
    std::vector<std::vector<float>> uvSets;
    std::vector<std::vector<unsigned char>> uvSetPresence;
    bool uvSetsEdited = false;
    int selectedUvSet = 0;
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
    bool pivotMode = false;
    TransformTool tool = TransformTool::Select;
    TransformAxis axis = TransformAxis::None;
    int nodeIndex = -1;
    Vector2 lastMouse{};
    float lastAngle = 0.0f;
    float snapRadiansX = 0.0f;
    float snapRadiansY = 0.0f;
};

struct WeightBrushSettings
{
    float sizePixels = 48.0f;
    float strength = 0.35f;
    bool autoNormalize = true;
};

enum class WeightBrushMode
{
    Add,
    Subtract,
    Smooth
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

struct ValidationGroup
{
    std::string category;
    std::vector<size_t> issues;
};

struct ValidationCache
{
    bool dirty = true;
    int uvSet = -1;
    std::vector<ValidatorIssue> issues;
    std::vector<ValidationGroup> groups;
    int errors = 0;
    int warnings = 0;
    int infos = 0;
};

struct ViewportUvIslandTriangle
{
    int vertex[3]{};
    int nodeIndex = -1;
    int islandIndex = -1;
};

struct ViewportUvIslandCache
{
    int uvSetIndex = -1;
    size_t uvValueCount = 0;
    size_t vertexValueCount = 0;
    size_t nodeCount = 0;
    std::vector<ViewportUvIslandTriangle> triangles;
};

struct ModelTab
{
    LoadedFbxModel loaded;
    std::shared_ptr<const SkinningGeometry> skinningGeometry;
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
    int uvContextSet = -1;
    int uvSetScroll = 0;
    bool showUvTexelDensity = false;
    bool showUvSameMaterialMeshes = false;
    int uvDensityTileSize = 1024;
    std::vector<int> selectedUvIslands;
    float uvViewZoom = 1.0f;
    Vector2 uvViewPan{};
    bool uvViewPanning = false;
    bool uvIslandMarqueeSelecting = false;
    bool uvIslandMarqueeAdditive = false;
    Vector2 uvIslandMarqueeStart{};
    Vector2 uvIslandMarqueeCurrent{};
    int uvSelectionNodeIndex = -1;
    int uvSelectionUvSet = -1;
    bool uvSelectionSameMaterial = false;
    ViewportUvIslandCache viewportUvIslandCache;
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
    ValidationCache validationCache;
    std::unordered_map<std::string, bool> expandedValidationGroups;
    std::vector<bool> hiddenNodes;
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
    std::vector<int> shiftDragBaseSelection;
    bool reparentDragArmed = false;
    bool reparentDragging = false;
    int reparentDragNode = -1;
    int reparentDropTarget = -1;
    Vector2 reparentDragStart{};
    std::vector<int> reparentDragNodes;
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
bool IsSceneRootNode(const LoadedFbxModel& loaded, int nodeIndex);
bool IsSceneNodeVisible(const ModelTab& tab, const std::vector<bool>& collapsed, int nodeIndex);
std::vector<int> BuildVisibleHierarchyOrder(const ModelTab& tab, const std::vector<bool>& collapsed);
bool IsNodeSelected(const ModelTab& tab, int nodeIndex);
void PruneSelectedNodes(ModelTab& tab);
void ClearNodeSelection(ModelTab& tab);
void SetSingleSelectedNode(ModelTab& tab, int nodeIndex);
void SelectNode(ModelTab& tab, int nodeIndex, bool additive);
void SetVisibleNodeRangeSelection(ModelTab& tab, const std::vector<bool>& collapsed, int anchorNode, int targetNode);
void SetVisibleNodeRangeSelection(ModelTab& tab,
                                  const std::vector<bool>& collapsed,
                                  int anchorNode,
                                  int targetNode,
                                  const std::vector<int>& baseSelection);
int MergeSelectedGeometry(ModelTab& tab, const std::vector<int>& contextNodes, std::string& error);
bool ApplyScaleToNode(ModelTab& tab, int nodeIndex);
bool FlipMeshNormals(ModelTab& tab, int nodeIndex);
bool SetMeshPivotToBoundsCenter(ModelTab& tab, int nodeIndex);
bool SetMeshPivotToBoundsBottom(ModelTab& tab, int nodeIndex);
bool ResetBoneSubtreeToOriginalBindPose(ModelTab& tab, int rootNodeIndex);
std::vector<int> GetSelectedTransformRoots(const ModelTab& tab);
void InvalidateDisplayedAnimationCaches(ModelTab& tab);
void RecomputeMeshNodeBounds(ModelTab& tab, SceneNode& node);
void RecomputeSceneBounds(ModelTab& tab);
void RefreshDisplayedMesh(ModelTab& tab);
void RefreshUvMeshBuffers(ModelTab& tab);
bool HasCpuSkinnedMesh(const LoadedFbxModel& loaded);
const BonePose* FindBonePoseByNode(const std::vector<BonePose>& poses, int nodeIndex);
bool RebuildCurrentSkinnedMeshFromBones(ModelTab& tab);
bool RebuildSkinnedAnimationMeshFrames(ModelTab& tab);
MeshFrame BuildSkinnedMeshFrame(const LoadedFbxModel& target, const BoneFrame& boneFrame);
void BuildSkinnedMeshFrameInto(const LoadedFbxModel& target, const BoneFrame& boneFrame, MeshFrame& meshFrame);

Color GetDebugIndexColor(int index, unsigned char alpha = 255)
{
    static constexpr Color palette[] = {
        Color{ 90, 170, 225, 255 },
        Color{ 235, 175, 70, 255 },
        Color{ 120, 205, 135, 255 },
        Color{ 220, 120, 175, 255 },
        Color{ 145, 150, 235, 255 },
        Color{ 235, 115, 95, 255 },
        Color{ 85, 205, 195, 255 },
        Color{ 205, 145, 85, 255 },
        Color{ 170, 210, 80, 255 },
        Color{ 210, 125, 235, 255 },
        Color{ 235, 220, 85, 255 },
        Color{ 125, 185, 110, 255 }
    };
    const int paletteCount = static_cast<int>(sizeof(palette) / sizeof(palette[0]));
    Color color = index >= 0 && index < paletteCount ? palette[index] : ColorFromHSV(static_cast<float>((index * 137) % 360), 0.58f, 0.90f);
    color.a = alpha;
    return color;
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
    tab.validationCache.dirty = true;
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

FbxTextureUsage ToFbxTextureUsage(PbrTextureSlot slot)
{
    switch (slot)
    {
    case PbrTextureSlot::Diffuse: return FbxTextureUsage::Diffuse;
    case PbrTextureSlot::Normal: return FbxTextureUsage::Normal;
    case PbrTextureSlot::Roughness: return FbxTextureUsage::Roughness;
    case PbrTextureSlot::Metallic: return FbxTextureUsage::Metallic;
    case PbrTextureSlot::AmbientOcclusion: return FbxTextureUsage::AmbientOcclusion;
    case PbrTextureSlot::Emissive: return FbxTextureUsage::Emissive;
    case PbrTextureSlot::Opacity: return FbxTextureUsage::Opacity;
    case PbrTextureSlot::Count: break;
    }
    return FbxTextureUsage::Diffuse;
}

PbrTextureSlot ToPbrTextureSlot(FbxTextureUsage usage)
{
    switch (usage)
    {
    case FbxTextureUsage::Diffuse: return PbrTextureSlot::Diffuse;
    case FbxTextureUsage::Normal: return PbrTextureSlot::Normal;
    case FbxTextureUsage::Roughness: return PbrTextureSlot::Roughness;
    case FbxTextureUsage::Metallic: return PbrTextureSlot::Metallic;
    case FbxTextureUsage::AmbientOcclusion: return PbrTextureSlot::AmbientOcclusion;
    case FbxTextureUsage::Emissive: return PbrTextureSlot::Emissive;
    case FbxTextureUsage::Opacity: return PbrTextureSlot::Opacity;
    }
    return PbrTextureSlot::Diffuse;
}

std::string ToLowerAscii(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

std::string CompactAsciiName(std::string value)
{
    value = ToLowerAscii(std::move(value));
    value.erase(std::remove_if(value.begin(), value.end(), [](unsigned char c)
    {
        return !std::isalnum(c);
    }), value.end());
    return value;
}

bool LowerStemHasToken(const std::string& lowerStem, const char* token)
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
}

bool IsOrmTextureStem(const std::string& lowerStem)
{
    const std::string compactStem = CompactAsciiName(lowerStem);
    return LowerStemHasToken(lowerStem, "orm") ||
           lowerStem.find("occlusionroughnessmetallic") != std::string::npos ||
           lowerStem.find("occlusion_roughness_metallic") != std::string::npos ||
           lowerStem.find("occlusion-roughness-metallic") != std::string::npos ||
           lowerStem.find("ao_roughness_metallic") != std::string::npos ||
           lowerStem.find("ao-roughness-metallic") != std::string::npos ||
           compactStem.find("occlusionroughnessmetallic") != std::string::npos ||
           compactStem.find("aoroughnessmetallic") != std::string::npos;
}

bool IsOrmTexturePath(const std::string& path)
{
    return IsOrmTextureStem(ToLowerAscii(std::filesystem::path(path).stem().string()));
}

bool IsOrmTextureReference(const FbxTextureReference& reference)
{
    return IsOrmTexturePath(!reference.relativePath.empty() ? reference.relativePath : reference.filePath);
}

bool IsOrmPackedPbrSlot(PbrTextureSlot slot)
{
    return slot == PbrTextureSlot::AmbientOcclusion ||
           slot == PbrTextureSlot::Roughness ||
           slot == PbrTextureSlot::Metallic;
}

std::filesystem::path AbsoluteNormalizedPath(const std::filesystem::path& path)
{
    std::error_code pathError;
    std::filesystem::path result = std::filesystem::weakly_canonical(path, pathError);
    if (!pathError) return result.lexically_normal();

    result = std::filesystem::absolute(path, pathError);
    if (!pathError) return result.lexically_normal();

    return path.lexically_normal();
}

std::filesystem::path MakeUniquePackageTextureName(const std::filesystem::path& sourcePath, std::unordered_map<std::string, bool>& usedNames)
{
    std::filesystem::path filename = sourcePath.filename();
    if (filename.empty())
    {
        filename = "texture";
        filename += sourcePath.extension();
    }

    const std::string stem = filename.stem().string().empty() ? "texture" : filename.stem().string();
    const std::string extension = filename.extension().string();
    for (int suffix = 0; suffix < 10000; ++suffix)
    {
        std::filesystem::path candidate = suffix == 0
            ? std::filesystem::path(stem + extension)
            : std::filesystem::path(stem + "_" + std::to_string(suffix + 1) + extension);
        const std::string key = ToLowerAscii(candidate.string());
        if (!usedNames[key])
        {
            usedNames[key] = true;
            return candidate;
        }
    }

    return std::filesystem::path(stem + "_copy" + extension);
}

std::string GetMaterialNameForPackage(const ModelTab& tab, int materialIndex)
{
    if (materialIndex >= 0 && materialIndex < static_cast<int>(tab.loaded.materialNames.size()))
    {
        const std::string& materialName = tab.loaded.materialNames[static_cast<size_t>(materialIndex)];
        if (!materialName.empty()) return materialName;
    }
    return "Material " + std::to_string(materialIndex + 1);
}

void AddPackageTextureReference(std::vector<FbxTextureReference>& textureReferences,
                                const std::string& materialName,
                                PbrTextureSlot slot,
                                const std::filesystem::path& sourcePath,
                                const std::string& relativePath)
{
    FbxTextureReference reference;
    reference.materialName = materialName;
    reference.usage = ToFbxTextureUsage(slot);
    reference.filePath = AbsoluteNormalizedPath(sourcePath).string();
    reference.relativePath = relativePath;
    textureReferences.push_back(reference);
}

int FindMaterialIndexByName(const ModelTab& tab, const std::string& materialName)
{
    for (int materialIndex = 0; materialIndex < static_cast<int>(tab.loaded.materialNames.size()); ++materialIndex)
    {
        if (tab.loaded.materialNames[static_cast<size_t>(materialIndex)] == materialName)
        {
            return materialIndex;
        }
    }
    return -1;
}

int LoadOrmPbrTexture(ModelTab& tab,
                      int materialIndex,
                      const std::string& path,
                      std::unordered_map<std::string, bool>& loadedSlots,
                      std::string& error)
{
    int loadedCount = 0;
    error.clear();
    if (materialIndex < 0 || materialIndex >= static_cast<int>(tab.pbrMaterials.size())) return 0;

    PbrMaterialState& material = tab.pbrMaterials[static_cast<size_t>(materialIndex)];
    material.aoChannel = PackedChannel::R;
    material.roughnessChannel = PackedChannel::G;
    material.metallicChannel = PackedChannel::B;

    const PbrTextureSlot slots[] = {
        PbrTextureSlot::AmbientOcclusion,
        PbrTextureSlot::Roughness,
        PbrTextureSlot::Metallic
    };

    for (PbrTextureSlot slot : slots)
    {
        const std::string slotKey = std::to_string(materialIndex) + "|" + std::to_string(static_cast<int>(slot));
        if (loadedSlots[slotKey]) continue;

        std::string loadError;
        if (LoadPbrTexture(tab, materialIndex, slot, path, loadError))
        {
            loadedSlots[slotKey] = true;
            ++loadedCount;
        }
        else if (error.empty())
        {
            error = loadError;
        }
    }

    return loadedCount;
}

int LoadImportedPbrTextures(ModelTab& tab, std::string& error)
{
    error.clear();
    EnsurePbrMaterialStates(tab);

    int loadedCount = 0;
    std::unordered_map<std::string, bool> loadedSlots;
    for (const FbxTextureReference& reference : tab.loaded.textureReferences)
    {
        const int materialIndex = FindMaterialIndexByName(tab, reference.materialName);
        if (materialIndex < 0 || materialIndex >= static_cast<int>(tab.pbrMaterials.size())) continue;
        if (reference.filePath.empty()) continue;

        if (IsOrmTextureReference(reference))
        {
            std::string loadError;
            const int ormLoadedCount = LoadOrmPbrTexture(tab, materialIndex, reference.filePath, loadedSlots, loadError);
            loadedCount += ormLoadedCount;
            if (ormLoadedCount == 0 && error.empty())
            {
                error = loadError;
            }
            continue;
        }

        const PbrTextureSlot slot = ToPbrTextureSlot(reference.usage);
        const std::string slotKey = std::to_string(materialIndex) + "|" + std::to_string(static_cast<int>(slot));
        if (loadedSlots[slotKey]) continue;

        std::string loadError;
        if (LoadPbrTexture(tab, materialIndex, slot, reference.filePath, loadError))
        {
            loadedSlots[slotKey] = true;
            ++loadedCount;
        }
        else if (error.empty())
        {
            error = loadError;
        }
    }

    return loadedCount;
}

std::filesystem::path MakePackagedFbxPath(const std::string& sourcePath)
{
    std::filesystem::path source(sourcePath);
    if (source.empty()) return {};

    const std::filesystem::path directory = source.parent_path().empty()
        ? std::filesystem::current_path()
        : source.parent_path();
    const std::string stem = source.stem().string().empty() ? "packaged" : source.stem().string();

    for (int suffix = 0; suffix < 10000; ++suffix)
    {
        const std::string fileName = suffix == 0
            ? stem + "_packaged.fbx"
            : stem + "_packaged_" + std::to_string(suffix + 1) + ".fbx";
        std::filesystem::path candidate = directory / fileName;

        std::error_code existsError;
        if (!std::filesystem::exists(candidate, existsError) && !existsError)
        {
            return candidate;
        }
    }

    return directory / (stem + "_packaged_copy.fbx");
}

std::filesystem::path MakeExtractedTextureDirectory(const std::string& sourcePath)
{
    std::filesystem::path source(sourcePath);
    if (source.empty()) return {};

    const std::filesystem::path directory = source.parent_path().empty()
        ? std::filesystem::current_path()
        : source.parent_path();
    const std::string stem = source.stem().string().empty() ? "fbx" : source.stem().string();

    for (int suffix = 0; suffix < 10000; ++suffix)
    {
        const std::string folderName = suffix == 0
            ? stem + "_textures"
            : stem + "_textures_" + std::to_string(suffix + 1);
        const std::filesystem::path candidate = directory / folderName;

        std::error_code existsError;
        if (!std::filesystem::exists(candidate, existsError) && !existsError)
        {
            return candidate;
        }
    }

    return directory / (stem + "_textures_copy");
}

std::filesystem::path MakeExtractedFbxPath(const std::string& sourcePath)
{
    std::filesystem::path source(sourcePath);
    if (source.empty()) return {};

    const std::filesystem::path directory = source.parent_path().empty()
        ? std::filesystem::current_path()
        : source.parent_path();
    const std::string stem = source.stem().string().empty() ? "fbx" : source.stem().string();

    for (int suffix = 0; suffix < 10000; ++suffix)
    {
        const std::string fileName = suffix == 0
            ? stem + "_extracted.fbx"
            : stem + "_extracted_" + std::to_string(suffix + 1) + ".fbx";
        const std::filesystem::path candidate = directory / fileName;

        std::error_code existsError;
        if (!std::filesystem::exists(candidate, existsError) && !existsError)
        {
            return candidate;
        }
    }

    return directory / (stem + "_extracted_copy.fbx");
}

bool CollectPackageTextures(ModelTab& tab,
                            std::vector<FbxTextureReference>& textureReferences,
                            int& embeddedTextureCount,
                            std::string& error)
{
    textureReferences.clear();
    embeddedTextureCount = 0;
    error.clear();

    EnsurePbrMaterialStates(tab);

    std::unordered_map<std::string, std::string> relativeBySource;
    std::unordered_map<std::string, bool> usedPackageNames;

    for (int materialIndex = 0; materialIndex < static_cast<int>(tab.pbrMaterials.size()); ++materialIndex)
    {
        const std::string materialName = GetMaterialNameForPackage(tab, materialIndex);
        const PbrMaterialState& material = tab.pbrMaterials[static_cast<size_t>(materialIndex)];
        std::unordered_map<std::string, bool> exportedOrmSources;

        for (int slotIndex = 0; slotIndex < static_cast<int>(PbrTextureSlot::Count); ++slotIndex)
        {
            const PbrTextureSlot slot = static_cast<PbrTextureSlot>(slotIndex);
            const PbrTexture& texture = GetPbrTexture(material, slot);
            if (!texture.loaded || texture.path.empty()) continue;

            const std::filesystem::path sourcePath(texture.path);
            std::error_code existsError;
            if (!std::filesystem::exists(sourcePath, existsError) || existsError)
            {
                error = "Missing texture file: " + texture.path;
                return false;
            }

            const std::string sourceKey = ToLowerAscii(AbsoluteNormalizedPath(sourcePath).string());
            auto relativeFound = relativeBySource.find(sourceKey);
            if (relativeFound == relativeBySource.end())
            {
                const std::filesystem::path packageName = MakeUniquePackageTextureName(sourcePath, usedPackageNames);
                const std::string relativePath = packageName.generic_string();
                relativeBySource[sourceKey] = relativePath;
                ++embeddedTextureCount;
                relativeFound = relativeBySource.find(sourceKey);
            }

            if (IsOrmPackedPbrSlot(slot) && IsOrmTexturePath(sourcePath.string()))
            {
                if (exportedOrmSources[sourceKey]) continue;

                exportedOrmSources[sourceKey] = true;
                AddPackageTextureReference(textureReferences, materialName, PbrTextureSlot::AmbientOcclusion, sourcePath, relativeFound->second);
                AddPackageTextureReference(textureReferences, materialName, PbrTextureSlot::Roughness, sourcePath, relativeFound->second);
                AddPackageTextureReference(textureReferences, materialName, PbrTextureSlot::Metallic, sourcePath, relativeFound->second);
                continue;
            }

            AddPackageTextureReference(textureReferences, materialName, slot, sourcePath, relativeFound->second);
        }
    }

    return true;
}

bool ExtractPackagedFbxTextures(const ModelTab& tab, std::string& notice, std::string& error)
{
    notice.clear();
    error.clear();
    if (tab.path.empty())
    {
        error = "Missing source FBX path.";
        return false;
    }

    const std::filesystem::path outputDirectory = MakeExtractedTextureDirectory(tab.path);
    if (outputDirectory.empty())
    {
        error = "Failed to choose texture extraction folder.";
        return false;
    }

    std::error_code directoryError;
    std::filesystem::create_directories(outputDirectory, directoryError);
    if (directoryError)
    {
        error = "Failed to create texture extraction folder: " + outputDirectory.string();
        return false;
    }

    std::unordered_map<std::string, std::filesystem::path> copiedBySource;
    std::unordered_map<std::string, bool> usedNames;
    std::vector<FbxTextureReference> extractedTextureReferences;
    int copiedCount = 0;
    for (const FbxTextureReference& reference : tab.loaded.textureReferences)
    {
        if (reference.filePath.empty()) continue;

        const std::filesystem::path sourcePath(reference.filePath);
        std::error_code existsError;
        if (!std::filesystem::is_regular_file(sourcePath, existsError) || existsError) continue;

        const std::string sourceKey = ToLowerAscii(AbsoluteNormalizedPath(sourcePath).string());
        auto copiedFound = copiedBySource.find(sourceKey);

        if (copiedFound == copiedBySource.end())
        {
            const std::filesystem::path preferredName = reference.relativePath.empty()
                ? sourcePath.filename()
                : std::filesystem::path(reference.relativePath).filename();
            const std::filesystem::path textureName = MakeUniquePackageTextureName(preferredName.empty() ? sourcePath : preferredName, usedNames);
            const std::filesystem::path targetPath = outputDirectory / textureName;

            std::error_code copyError;
            std::filesystem::copy_file(sourcePath, targetPath, std::filesystem::copy_options::none, copyError);
            if (copyError)
            {
                error = "Failed to extract texture: " + sourcePath.string();
                return false;
            }

            copiedBySource[sourceKey] = targetPath;
            copiedFound = copiedBySource.find(sourceKey);
            ++copiedCount;
        }

        if (copiedFound == copiedBySource.end()) continue;

        FbxTextureReference extractedReference = reference;
        extractedReference.filePath = AbsoluteNormalizedPath(copiedFound->second).string();
        extractedReference.relativePath = (outputDirectory.filename() / copiedFound->second.filename()).generic_string();
        extractedTextureReferences.push_back(extractedReference);
    }

    if (copiedCount == 0)
    {
        error = "No embedded/imported textures found to extract.";
        return false;
    }

    const std::filesystem::path outputFbx = MakeExtractedFbxPath(tab.path);
    if (outputFbx.empty())
    {
        error = "Failed to choose extracted FBX path.";
        return false;
    }

    std::string saveError;
    const std::string outputFbxString = outputFbx.string();
    if (!SaveFbxModelAnimations(tab.path, outputFbxString, tab.loaded, tab.deletedNodes, extractedTextureReferences, false, true, saveError))
    {
        error = saveError;
        return false;
    }

    notice = "Extracted " + std::to_string(copiedCount) + " texture file" + (copiedCount == 1 ? " and unpacked FBX: " : "s and unpacked FBX: ") + outputFbxString;
    return true;
}

bool PackageFbxWithTextures(ModelTab& tab, std::string& notice, std::string& error)
{
    const std::filesystem::path outputPath = MakePackagedFbxPath(tab.path);
    if (outputPath.empty())
    {
        error = "Missing source FBX path.";
        notice.clear();
        return false;
    }

    std::vector<FbxTextureReference> textureReferences;
    int embeddedTextureCount = 0;
    if (!CollectPackageTextures(tab, textureReferences, embeddedTextureCount, error))
    {
        notice.clear();
        return false;
    }

    std::string saveError;
    const std::string outputPathString = outputPath.string();
    if (!SaveFbxModelAnimations(tab.path, outputPathString, tab.loaded, tab.deletedNodes, textureReferences, true, true, saveError))
    {
        error = saveError;
        notice.clear();
        return false;
    }

    notice = embeddedTextureCount == 0
        ? "Packaged FBX (no loaded textures): " + outputPathString
        : "Packaged FBX with " + std::to_string(embeddedTextureCount) + " embedded texture file" + (embeddedTextureCount == 1 ? ": " : "s: ") + outputPathString;
    error.clear();
    return true;
}

EditSnapshot CaptureEditSnapshot(ModelTab& tab)
{
    // Undo shares immutable animation caches; only subsequently used frames are copied.
    for (AnimationClip& clip : tab.loaded.animations)
        for (MeshFrame& frame : clip.meshFrames) ShareMeshFrame(frame);
    EditSnapshot snapshot;
    snapshot.uvSetNames = tab.loaded.uvSetNames;
    snapshot.uvSets = tab.loaded.uvSets;
    snapshot.uvSetPresence = tab.loaded.uvSetPresence;
    snapshot.uvSetsEdited = tab.loaded.uvSetsEdited;
    snapshot.selectedUvSet = tab.selectedUvSet;
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

