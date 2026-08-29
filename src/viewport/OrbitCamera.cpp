#include "OrbitCamera.h"

#include <algorithm>
#include <cmath>

#include "../core/MathUtils.h"
#include "raymath.h"

namespace openfbx
{
namespace
{
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

float GetFocusDistanceForBounds(const BoundingBox& bounds)
{
    const Vector3 size = Vector3Subtract(bounds.max, bounds.min);
    const float radius = std::max(0.001f, Vector3Length(size) * 0.5f);
    return radius / std::tan(45.0f * DEG2RAD * 0.5f) * 1.25f;
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
}
