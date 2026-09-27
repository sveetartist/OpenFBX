const char* GetTransformToolName(TransformTool tool)
{
    switch (tool)
    {
    case TransformTool::Select: return "Select";
    case TransformTool::Move: return "Move";
    case TransformTool::Rotate: return "Rotate";
    case TransformTool::Scale: return "Scale";
    }
    return "Select";
}

std::string GetTransformToolHotkey(TransformTool tool)
{
    switch (tool)
    {
    case TransformTool::Select: return HotkeyText(gHotkeys[static_cast<size_t>(HotkeyAction::Select)]);
    case TransformTool::Move: return HotkeyText(gHotkeys[static_cast<size_t>(HotkeyAction::Move)]);
    case TransformTool::Rotate: return HotkeyText(gHotkeys[static_cast<size_t>(HotkeyAction::Rotate)]);
    case TransformTool::Scale: return HotkeyText(gHotkeys[static_cast<size_t>(HotkeyAction::Scale)]);
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

float ConsumeRotationSnapRadians(float radians, float& accumulator, bool snap)
{
    constexpr float kRotationSnapRadians = 15.0f * DEG2RAD;
    if (!snap)
    {
        accumulator = 0.0f;
        return radians;
    }

    accumulator += radians;
    const float snapped = std::round(accumulator / kRotationSnapRadians) * kRotationSnapRadians;
    accumulator -= snapped;
    return snapped;
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
    if (tool == TransformTool::Select || !GetGizmoPivot(tab, pivot)) return TransformAxis::None;

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
                               bool editPivotMode,
                               bool mouseInViewport,
                               std::string& notice,
                               std::string& error)
{
    const bool pivotDragInProgress = state.dragging && state.pivotMode;
    const TransformTool requestedTool = editPivotMode && tool != TransformTool::Move && tool != TransformTool::Rotate ? TransformTool::Select : tool;
    const TransformTool activeTool = state.dragging ? state.tool : requestedTool;
    if (!active || !active->loaded.valid || activeTool == TransformTool::Select)
    {
        state = TransformGizmoState{};
        return false;
    }

    if ((editPivotMode || pivotDragInProgress) && !IsValidPivotNode(*active, active->selectedNode))
    {
        state = TransformGizmoState{};
        return false;
    }

    const Vector2 mouse = GetMousePosition();
    const bool snapRotation = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);
    if (state.dragging)
    {
        const bool draggingPivotMode = state.pivotMode;
        if (!openfbx::UiMouseButtonDown(MOUSE_BUTTON_LEFT))
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
            if (activeTool == TransformTool::Move)
            {
                const float worldPerPixel = GetViewPlaneWorldPerPixel(*active, pivot);
                const Vector3 right = GetCameraRight(active->orbit.camera);
                const Vector3 up = GetCameraUpVector(active->orbit.camera);
                Vector3 moveDelta = Vector3Scale(right, delta.x * worldPerPixel);
                moveDelta = Vector3Add(moveDelta, Vector3Scale(up, -delta.y * worldPerPixel));
                if (Vector3Length(moveDelta) > 0.000001f)
                {
                    if (draggingPivotMode)
                    {
                        MoveSelectedPivot(*active, moveDelta);
                    }
                    else
                    {
                        MoveSelectedSubtree(*active, moveDelta);
                    }
                }
            }
            else if (activeTool == TransformTool::Rotate)
            {
                const Vector3 right = GetCameraRight(active->orbit.camera);
                const Vector3 up = GetCameraUpVector(active->orbit.camera);
                const float rightRadians = ConsumeRotationSnapRadians(delta.y * 0.006f, state.snapRadiansX, snapRotation);
                const float upRadians = ConsumeRotationSnapRadians(delta.x * 0.006f, state.snapRadiansY, snapRotation);
                if (std::fabs(rightRadians) > 0.000001f || std::fabs(upRadians) > 0.000001f)
                {
                    if (draggingPivotMode)
                    {
                        RotateSelectedPivot(*active, right, rightRadians);
                        RotateSelectedPivot(*active, up, upRadians);
                    }
                    else
                    {
                        RotateSelectedSubtreeArcball(*active, pivot, right, rightRadians, up, upRadians);
                    }
                }
            }
            else if (activeTool == TransformTool::Scale)
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
            if (activeTool == TransformTool::Rotate)
            {
                const Vector2 pivotScreen = GetWorldToScreen(pivot, active->orbit.camera);
                const float currentAngle = GetScreenAngleAroundPivot(mouse, pivotScreen);
                float radians = WrapAngleDelta(currentAngle - state.lastAngle);
                state.lastAngle = currentAngle;
                const float facing = Vector3DotProduct(axisVector, GetCameraForward(active->orbit.camera));
                radians *= facing < 0.0f ? -1.0f : 1.0f;
                radians = ConsumeRotationSnapRadians(radians, state.snapRadiansX, snapRotation);
                if (std::fabs(radians) > 0.000001f)
                {
                    if (draggingPivotMode)
                    {
                        RotateSelectedPivot(*active, axisVector, radians);
                    }
                    else
                    {
                        RotateSelectedSubtree(*active, pivot, state.axis, axisVector, radians);
                    }
                }
            }
            else
            {
                const Vector2 axisDirection = GetAxisScreenDirection(*active, pivot, state.axis, orientation);
                const float scalarPixels = Vector2DotProduct(delta, axisDirection);
                if (std::fabs(scalarPixels) > 0.001f)
                {
                    if (activeTool == TransformTool::Move)
                    {
                        const Vector3 moveDelta = Vector3Scale(axisVector, scalarPixels * GetAxisWorldPerPixel(*active, pivot, axisVector));
                        if (draggingPivotMode)
                        {
                            MoveSelectedPivot(*active, moveDelta);
                        }
                        else
                        {
                            MoveSelectedSubtree(*active, moveDelta);
                        }
                    }
                    else if (activeTool == TransformTool::Scale)
                    {
                        ScaleSelectedSubtree(*active, pivot, state.axis, axisVector, std::exp(scalarPixels * 0.006f));
                    }
                }
            }
        }
        return true;
    }

    if (!mouseInViewport || !openfbx::UiMouseButtonPressed(MOUSE_BUTTON_LEFT)) return false;
    const TransformAxis axis = PickTransformGizmoAxis(*active, activeTool, orientation, mouse);
    if (axis == TransformAxis::None) return false;

    PushUndoSnapshot(*active);
    state.dragging = true;
    state.pivotMode = editPivotMode;
    state.tool = activeTool;
    state.axis = axis;
    state.nodeIndex = active->selectedNode;
    state.lastMouse = mouse;
    Vector3 pivot{};
    if (GetGizmoPivot(*active, pivot))
    {
        state.lastAngle = GetScreenAngleAroundPivot(mouse, GetWorldToScreen(pivot, active->orbit.camera));
    }
    notice = editPivotMode ? "Edit pivot." : std::string(GetTransformToolName(activeTool)) + " gizmo.";
    error.clear();
    return true;
}

float GetGizmoStrokeRadius(const ModelTab& tab, Vector3 point)
{
    const float lineWidth = ClampFloat(gTransformGizmoLineWidth, kMinTransformGizmoLineWidth, kMaxTransformGizmoLineWidth);
    const float worldPerPixel = GetViewPlaneWorldPerPixel(tab, point);
    return std::max(worldPerPixel * lineWidth * 0.5f, GetTransformGizmoLength(tab) * 0.0025f);
}

void DrawGizmoStroke(const ModelTab& tab, Vector3 start, Vector3 end, Color color)
{
    if (Vector3Distance(start, end) <= 0.000001f) return;

    const Vector3 midpoint = Vector3Scale(Vector3Add(start, end), 0.5f);
    const float radius = GetGizmoStrokeRadius(tab, midpoint);
    DrawCylinderEx(start, end, radius, radius, 10, color);
}

void DrawGizmoCircle(const ModelTab& tab, Vector3 position, Vector3 axisA, Vector3 axisB, float radius, Color color)
{
    constexpr int kSegments = 64;
    Vector3 previous = Vector3Add(position, Vector3Scale(axisA, radius));
    for (int i = 1; i <= kSegments; ++i)
    {
        const float angle = static_cast<float>(i) / static_cast<float>(kSegments) * 2.0f * PI;
        const Vector3 offset = Vector3Add(Vector3Scale(axisA, std::cos(angle) * radius),
                                          Vector3Scale(axisB, std::sin(angle) * radius));
        const Vector3 current = Vector3Add(position, offset);
        DrawGizmoStroke(tab, previous, current, color);
        previous = current;
    }
}

void DrawMoveGizmoArrow(const ModelTab& tab, Vector3 start, Vector3 end, Vector3 axisVector, float length, Color color)
{
    DrawGizmoStroke(tab, start, end, color);

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
    DrawGizmoStroke(tab, end, Vector3Add(base, Vector3Scale(side, headWidth)), color);
    DrawGizmoStroke(tab, end, Vector3Subtract(base, Vector3Scale(side, headWidth)), color);

    const Vector3 upWing = NormalizeOrFallback(Vector3CrossProduct(axisVector, side), ChoosePerpendicular(axisVector));
    DrawGizmoStroke(tab, end, Vector3Add(base, Vector3Scale(upWing, headWidth)), color);
    DrawGizmoStroke(tab, end, Vector3Subtract(base, Vector3Scale(upWing, headWidth)), color);
}

void DrawTransformGizmo(const ModelTab& tab, TransformTool tool, const TransformGizmoState& state, GizmoOrientation orientation, bool editPivotMode)
{
    const bool pivotDragInProgress = state.dragging && state.pivotMode;
    const TransformTool requestedTool = editPivotMode && tool != TransformTool::Move && tool != TransformTool::Rotate ? TransformTool::Select : tool;
    const TransformTool activeTool = state.dragging ? state.tool : requestedTool;
    if (activeTool == TransformTool::Select) return;
    if ((editPivotMode || pivotDragInProgress) && !IsValidPivotNode(tab, tab.selectedNode)) return;

    Vector3 pivot{};
    if (!GetGizmoPivot(tab, pivot)) return;

    const float length = GetTransformGizmoLength(tab);
    const float handleRadius = ClampFloat(length * 0.032f, 0.008f, 0.055f);
    rlDrawRenderBatchActive();
    rlDisableDepthTest();
    rlDisableDepthMask();

    const bool centerActive = state.dragging && state.axis == TransformAxis::Center;
    const Color centerColor = (editPivotMode || pivotDragInProgress) ? Color{ 255, 214, 84, 245 } : Color{ 225, 232, 238, 235 };
    DrawSphere(pivot, handleRadius * 0.65f, centerActive ? Color{ 255, 235, 128, 255 } : centerColor);

    for (TransformAxis axis : { TransformAxis::X, TransformAxis::Y, TransformAxis::Z })
    {
        const bool active = state.dragging && state.axis == axis;
        const Color color = GetTransformAxisColor(axis, active);
        const Vector3 axisVector = GetTransformAxisVector(tab, axis, orientation);
        if (activeTool == TransformTool::Rotate)
        {
            const float radius = length * 0.82f;
            Vector3 axisA{};
            Vector3 axisB{};
            if (GetTransformPlaneBasis(tab, axis, orientation, axisA, axisB))
            {
                DrawGizmoCircle(tab, pivot, axisA, axisB, radius, color);
            }
        }
        else
        {
            const Vector3 end = Vector3Add(pivot, Vector3Scale(axisVector, length));
            if (activeTool == TransformTool::Move)
            {
                DrawMoveGizmoArrow(tab, pivot, end, axisVector, length, color);
            }
            else if (activeTool == TransformTool::Scale)
            {
                DrawGizmoStroke(tab, pivot, end, color);
                DrawCubeV(end, Vector3{ handleRadius * 2.0f, handleRadius * 2.0f, handleRadius * 2.0f }, color);
            }
        }
    }
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
    return Rectangle{ hierarchyBlockW + 10.0f + static_cast<float>(index) * 57.0f, 266.0f, 55.0f, 28.0f };
}

Rectangle GetPivotModeButtonRect(float hierarchyBlockW)
{
    return Rectangle{ hierarchyBlockW + 10.0f, 300.0f, 112.0f, 28.0f };
}

bool UpdateTransformToolbarInput(TransformTool& tool, GizmoOrientation& orientation, bool& editPivotMode, float hierarchyBlockW)
{
    const Vector2 mouse = GetMousePosition();
    const TransformTool tools[] = { TransformTool::Select, TransformTool::Move, TransformTool::Rotate, TransformTool::Scale };
    for (int i = 0; i < 4; ++i)
    {
        if (CheckCollisionPointRec(mouse, GetTransformToolbarButtonRect(hierarchyBlockW, i)))
        {
            if (openfbx::UiMouseButtonPressed(MOUSE_BUTTON_LEFT))
            {
                tool = tools[i];
                if (tool != TransformTool::Move && tool != TransformTool::Rotate)
                {
                    editPivotMode = false;
                }
            }
            return true;
        }
    }

    if (CheckCollisionPointRec(mouse, GetPivotModeButtonRect(hierarchyBlockW)))
    {
        if (openfbx::UiMouseButtonPressed(MOUSE_BUTTON_LEFT))
        {
            editPivotMode = !editPivotMode;
            if (editPivotMode && tool != TransformTool::Move && tool != TransformTool::Rotate)
            {
                tool = TransformTool::Move;
            }
        }
        return true;
    }

    const GizmoOrientation orientations[] = { GizmoOrientation::Global, GizmoOrientation::Local };
    for (int i = 0; i < 2; ++i)
    {
        if (CheckCollisionPointRec(mouse, GetGizmoOrientationButtonRect(hierarchyBlockW, i)))
        {
            if (openfbx::UiMouseButtonPressed(MOUSE_BUTTON_LEFT))
            {
                orientation = orientations[i];
            }
            return true;
        }
    }
    return false;
}

void DrawTransformToolbar(Font font, TransformTool tool, GizmoOrientation orientation, bool editPivotMode, float hierarchyBlockW)
{
    const TransformTool tools[] = { TransformTool::Select, TransformTool::Move, TransformTool::Rotate, TransformTool::Scale };
    for (int i = 0; i < 4; ++i)
    {
        const Rectangle bounds = GetTransformToolbarButtonRect(hierarchyBlockW, i);
        const bool selected = tool == tools[i];
        const bool hovered = CheckCollisionPointRec(GetMousePosition(), bounds);
        DrawRectangleRec(bounds, selected ? Color{ 58, 78, 98, 245 } : hovered ? Color{ 42, 48, 55, 245 } : Color{ 24, 27, 31, 232 });
        DrawRectangleLinesEx(bounds, 1.0f, selected ? Color{ 128, 188, 235, 255 } : Color{ 78, 88, 98, 255 });
        DrawUiText(font, GetTransformToolName(tools[i]), bounds.x + 8.0f, bounds.y + 2.0f, 13.0f, RAYWHITE);
        DrawUiTextClipped(font, GetTransformToolHotkey(tools[i]).c_str(), bounds.x + 8.0f, bounds.y + 17.0f, 11.0f, bounds.width - 16.0f, Color{ 154, 166, 178, 255 });
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

    const Rectangle pivotBounds = GetPivotModeButtonRect(hierarchyBlockW);
    const bool pivotHovered = CheckCollisionPointRec(GetMousePosition(), pivotBounds);
    DrawRectangleRec(pivotBounds, editPivotMode ? Color{ 74, 86, 50, 245 } : pivotHovered ? Color{ 42, 48, 55, 245 } : Color{ 24, 27, 31, 232 });
    DrawRectangleLinesEx(pivotBounds, 1.0f, editPivotMode ? Color{ 214, 190, 90, 255 } : Color{ 78, 88, 98, 255 });
    DrawUiText(font, editPivotMode ? "Pivot On" : "Pivot Off", pivotBounds.x + 8.0f, pivotBounds.y + 7.0f, 13.0f, editPivotMode ? Color{ 255, 236, 160, 255 } : Color{ 154, 166, 178, 255 });

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

