#pragma once

#include "raylib.h"

namespace openfbx
{
enum class NavigationPreset
{
    Blender,
    Maya
};

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

void StopSnappedOrbitDrag(OrbitCamera& orbit);
void ExitSnappedOrbitView(OrbitCamera& orbit);
void UpdateOrbitCameraTransform(OrbitCamera& orbit);
OrbitCamera CreateDefaultCamera();
void FocusCameraOnBounds(OrbitCamera& orbit, const BoundingBox& bounds);
void FocusCameraOnPoint(OrbitCamera& orbit, Vector3 point, float sceneScale);
void UpdateNavigation(OrbitCamera& orbit, NavigationPreset preset);
}
