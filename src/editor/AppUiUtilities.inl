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
    std::string extension = std::filesystem::path(path).extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    const bool converting = extension != ".fbx";
    const char* title = converting ? "CONVERTING TO FBX" : "LOADING FBX";
    const char* detail = fileName && fileName[0] ? fileName : path.c_str();
    const char* note = converting
        ? "Converting model; the FBX copy will be saved beside the source file"
        : "Importing scene, sampling mesh data, skeleton, and skin deformation";

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
    case ViewMode::MaterialColors: return "MATERIAL COLORS";
    case ViewMode::UvIslands: return "UV ISLANDS";
    case ViewMode::Checker: return "CHECKER";
    }

    return "UNKNOWN";
}

ViewMode NextViewMode(ViewMode mode)
{
    switch (mode)
    {
    case ViewMode::Shaded: return ViewMode::ShadedWireframe;
    case ViewMode::ShadedWireframe: return ViewMode::Wireframe;
    case ViewMode::Wireframe: return ViewMode::MaterialColors;
    case ViewMode::MaterialColors: return ViewMode::UvIslands;
    case ViewMode::UvIslands: return ViewMode::Checker;
    case ViewMode::Checker: return ViewMode::Shaded;
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
        if (!IsValidSelectableNode(tab, nodeIndex)) continue;

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

bool IsUnitScale(Vector3 scale)
{
    return SameFloatValue(scale.x, 1.0f) && SameFloatValue(scale.y, 1.0f) && SameFloatValue(scale.z, 1.0f);
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

Rectangle GetSelectedInfoPanelRect(const ModelTab* active)
{
    if (!active || active->selectedNode < 0 || active->selectedNode >= static_cast<int>(active->loaded.nodes.size()))
    {
        return Rectangle{};
    }
    const std::vector<int> selectedMeshNodes = GetSelectedMeshNodeIndices(*active);
    const float panelW = selectedMeshNodes.size() > 1 ? 360.0f : 330.0f;
    const float panelH = selectedMeshNodes.size() > 1 ? 176.0f : 154.0f;
    return Rectangle{
        static_cast<float>(GetScreenWidth()) - panelW - 12.0f,
        static_cast<float>(GetScreenHeight()) - gBottomPanelReservedHeight - panelH - 12.0f,
        panelW,
        panelH
    };
}

float GetTransformValueComponent(Vector3 value, int component)
{
    if (component == 0) return value.x;
    if (component == 1) return value.y;
    return value.z;
}

void SetTransformValueComponent(Vector3& value, int component, float componentValue)
{
    if (component == 0) value.x = componentValue;
    else if (component == 1) value.y = componentValue;
    else value.z = componentValue;
}

Rectangle GetTransformValueCellRect(float panelX, float rowY, int component)
{
    return Rectangle{ panelX + 58.0f + static_cast<float>(component) * 78.0f, rowY - 2.0f, 72.0f, 20.0f };
}

Vector3 GetNodeDisplayedTransformValue(const ModelTab& tab, int nodeIndex, TransformValueField field, bool editPivotMode = false)
{
    const SceneNode& node = tab.loaded.nodes[static_cast<size_t>(nodeIndex)];
    const BonePose* currentBonePose = !editPivotMode && node.type == SceneNodeType::Bone ? FindCurrentBonePose(tab, nodeIndex) : nullptr;
    if (field == TransformValueField::Position) return currentBonePose ? currentBonePose->position : node.position;
    if (field == TransformValueField::Rotation) return currentBonePose ? currentBonePose->rotation : node.rotation;
    return currentBonePose ? currentBonePose->scale : node.scale;
}

void InsertTransformValueText(TransformValueEditor& editor, const char* text)
{
    if (!text) return;
    char* activeText = editor.text[editor.component];
    bool& activeSelected = editor.textSelected[editor.component];
    if (activeSelected)
    {
        activeText[0] = '\0';
        activeSelected = false;
    }

    const size_t currentLength = std::strlen(activeText);
    const size_t incomingLength = std::strlen(text);
    if (currentLength + incomingLength >= sizeof(editor.text[editor.component])) return;
    std::memcpy(activeText + currentLength, text, incomingLength + 1);
}

bool TryParseTransformValue(const char* text, float& outValue)
{
    if (!text || text[0] == '\0') return false;
    char* end = nullptr;
    const float value = std::strtof(text, &end);
    if (end == text) return false;
    while (end && *end)
    {
        if (!std::isspace(static_cast<unsigned char>(*end))) return false;
        ++end;
    }
    if (!std::isfinite(value)) return false;
    outValue = value;
    return true;
}

void BeginTransformValueEdit(TransformValueEditor& editor, const ModelTab& tab, TransformValueField field, int component, bool editPivotMode = false)
{
    if (!IsValidSelectableNode(tab, tab.selectedNode)) return;
    editor.active = true;
    editor.nodeIndex = tab.selectedNode;
    editor.field = field;
    editor.component = std::clamp(component, 0, 2);
    const Vector3 value = GetNodeDisplayedTransformValue(tab, tab.selectedNode, field, editPivotMode && field != TransformValueField::Scale);
    for (int i = 0; i < 3; ++i)
    {
        std::snprintf(editor.text[i], sizeof(editor.text[i]), field == TransformValueField::Rotation ? "%.2f" : "%.3f", GetTransformValueComponent(value, i));
        editor.textSelected[i] = i == editor.component;
    }
}

void CancelTransformValueEdit(TransformValueEditor& editor)
{
    editor = TransformValueEditor{};
}

bool CommitTransformValueEdit(ModelTab& tab, TransformValueEditor& editor, bool editPivotMode, std::string& notice, std::string& error)
{
    if (!editor.active || !IsValidSelectableNode(tab, editor.nodeIndex))
    {
        CancelTransformValueEdit(editor);
        return false;
    }

    Vector3 updated{};
    for (int i = 0; i < 3; ++i)
    {
        float parsed = 0.0f;
        if (!TryParseTransformValue(editor.text[i], parsed))
        {
            error = "Invalid transform value.";
            notice.clear();
            return false;
        }
        SetTransformValueComponent(updated, i, parsed);
    }

    const bool editingPivot = editPivotMode && editor.field != TransformValueField::Scale && IsValidPivotNode(tab, editor.nodeIndex);
    const Vector3 current = GetNodeDisplayedTransformValue(tab, editor.nodeIndex, editor.field, editingPivot);
    if (Vector3Distance(current, updated) <= 0.000001f)
    {
        CancelTransformValueEdit(editor);
        error.clear();
        return true;
    }

    PushUndoSnapshot(tab);
    if (editor.field == TransformValueField::Position)
    {
        if (editingPivot)
        {
            const int previousSelectedNode = tab.selectedNode;
            const std::vector<int> previousSelectedNodes = tab.selectedNodes;
            SetSingleSelectedNode(tab, editor.nodeIndex);
            MoveSelectedPivot(tab, Vector3Subtract(updated, current));
            tab.selectedNode = previousSelectedNode;
            tab.selectedNodes = previousSelectedNodes;
            PruneSelectedNodes(tab);
        }
        else
        {
            ApplyTransformToSingleSelectedNode(tab, editor.nodeIndex, [&]()
            {
                MoveSelectedSubtree(tab, Vector3Subtract(updated, current));
            });
            if (IsValidSelectableNode(tab, editor.nodeIndex))
            {
                SceneNode& updatedNode = tab.loaded.nodes[static_cast<size_t>(editor.nodeIndex)];
                updatedNode.position = updated;
                if (updatedNode.type == SceneNodeType::Bone)
                {
                    SetBonePosePosition(tab.loaded.bonePoses, updatedNode, editor.nodeIndex, updated, true);
                    SetBonePosePosition(tab.visibleBonePoses, updatedNode, editor.nodeIndex, updated, false);
                    UpdateBoneSegmentsForNode(tab.loaded.bones, editor.nodeIndex, updated);
                    UpdateBoneSegmentsForNode(tab.visibleBones, editor.nodeIndex, updated);
                }
            }
        }
    }
    else if (editor.field == TransformValueField::Rotation)
    {
        if (editingPivot)
        {
            const int previousSelectedNode = tab.selectedNode;
            const std::vector<int> previousSelectedNodes = tab.selectedNodes;
            SetSingleSelectedNode(tab, editor.nodeIndex);
            SetSelectedPivotRotation(tab, updated);
            tab.selectedNode = previousSelectedNode;
            tab.selectedNodes = previousSelectedNodes;
            PruneSelectedNodes(tab);
        }
        else
        {
            SetSelectedNodeRotation(tab, editor.nodeIndex, updated);
        }
    }
    else if (editor.field == TransformValueField::Scale)
    {
        SetSelectedNodeScale(tab, editor.nodeIndex, updated);
    }

    CancelTransformValueEdit(editor);
    notice = "Transform value updated.";
    error.clear();
    return true;
}

TransformValueField GetNextTransformValueField(TransformValueField field, bool editPivotMode)
{
    if (field == TransformValueField::Position) return TransformValueField::Rotation;
    if (field == TransformValueField::Rotation) return editPivotMode ? TransformValueField::Position : TransformValueField::Scale;
    return TransformValueField::Position;
}

void UpdateTransformValueTextInput(TransformValueEditor& editor)
{
    if (!editor.active) return;

    int key = GetCharPressed();
    while (key > 0)
    {
        if ((key >= '0' && key <= '9') || key == '-' || key == '+' || key == '.' || key == 'e' || key == 'E')
        {
            char value[2] = { static_cast<char>(key), '\0' };
            InsertTransformValueText(editor, value);
        }
        key = GetCharPressed();
    }

    if (IsKeyPressed(KEY_BACKSPACE))
    {
        char* activeText = editor.text[editor.component];
        bool& activeSelected = editor.textSelected[editor.component];
        if (activeSelected)
        {
            activeText[0] = '\0';
            activeSelected = false;
        }
        else
        {
            const size_t length = std::strlen(activeText);
            if (length > 0) activeText[length - 1] = '\0';
        }
    }
    if (IsKeyPressed(KEY_DELETE))
    {
        editor.text[editor.component][0] = '\0';
        editor.textSelected[editor.component] = false;
    }
}

bool UpdateSelectedInfoPanelInput(ModelTab* active, TransformValueEditor& editor, bool editPivotMode, std::string& notice, std::string& error)
{
    if (!active || active->selectedNode < 0 || active->selectedNode >= static_cast<int>(active->loaded.nodes.size()))
    {
        CancelTransformValueEdit(editor);
        return false;
    }

    const Rectangle panel = GetSelectedInfoPanelRect(active);
    const bool mouseOverPanel = CheckCollisionPointRec(GetMousePosition(), panel);
    if (editor.active)
    {
        UpdateTransformValueTextInput(editor);
        if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER))
        {
            CommitTransformValueEdit(*active, editor, editPivotMode, notice, error);
        }
        else if (IsKeyPressed(KEY_TAB))
        {
            const int nodeIndex = editor.nodeIndex;
            const TransformValueField field = editor.field;
            const int component = editor.component;
            if (CommitTransformValueEdit(*active, editor, editPivotMode, notice, error) &&
                IsValidSelectableNode(*active, nodeIndex))
            {
                active->selectedNode = nodeIndex;
                if (!IsNodeSelected(*active, nodeIndex))
                {
                    active->selectedNodes.assign(1, nodeIndex);
                }
                if (component < 2)
                {
                    BeginTransformValueEdit(editor, *active, field, component + 1, editPivotMode);
                }
                else
                {
                    BeginTransformValueEdit(editor, *active, GetNextTransformValueField(field, editPivotMode), 0, editPivotMode);
                }
            }
        }
        else if (IsKeyPressed(KEY_ESCAPE))
        {
            CancelTransformValueEdit(editor);
        }
        else if (openfbx::UiMouseButtonPressed(MOUSE_BUTTON_LEFT) && !mouseOverPanel)
        {
            CommitTransformValueEdit(*active, editor, editPivotMode, notice, error);
        }
    }

    const std::vector<int> selectedMeshNodes = GetSelectedMeshNodeIndices(*active);
    if (selectedMeshNodes.size() > 1) return mouseOverPanel;

    if (mouseOverPanel && openfbx::UiMouseButtonPressed(MOUSE_BUTTON_LEFT))
    {
        const struct Row
        {
            TransformValueField field;
            float y;
        } rows[] = {
            { TransformValueField::Position, panel.y + 58.0f },
            { TransformValueField::Rotation, panel.y + 80.0f },
            { TransformValueField::Scale, panel.y + 102.0f }
        };

        for (const Row& row : rows)
        {
            if (editPivotMode && row.field == TransformValueField::Scale) continue;
            const Rectangle rowRect{ panel.x + 12.0f, row.y - 2.0f, 280.0f, 20.0f };
            if (!CheckCollisionPointRec(GetMousePosition(), rowRect)) continue;

            for (int component = 0; component < 3; ++component)
            {
                if (CheckCollisionPointRec(GetMousePosition(), GetTransformValueCellRect(panel.x, row.y, component)))
                {
                    if (editor.active &&
                        (editor.field != row.field || editor.component != component) &&
                        !CommitTransformValueEdit(*active, editor, editPivotMode, notice, error))
                    {
                        return true;
                    }
                    BeginTransformValueEdit(editor, *active, row.field, component, editPivotMode);
                    return true;
                }
            }
            if (editor.active &&
                editor.field != row.field &&
                !CommitTransformValueEdit(*active, editor, editPivotMode, notice, error))
            {
                return true;
            }
            BeginTransformValueEdit(editor, *active, row.field, 0, editPivotMode);
            return true;
        }
    }

    return mouseOverPanel;
}

void DrawTransformValueCell(Font font,
                            const TransformValueEditor& editor,
                            TransformValueField field,
                            int component,
                            float value,
                            float panelX,
                            float rowY,
                            int decimals)
{
    const Rectangle cell = GetTransformValueCellRect(panelX, rowY, component);
    const bool activeRow = editor.active && editor.field == field;
    const bool active = activeRow && editor.component == component;
    const bool hovered = CheckCollisionPointRec(GetMousePosition(), cell);
    if (activeRow || hovered)
    {
        DrawRectangleRec(cell, active ? Color{ 42, 54, 66, 245 } : activeRow ? Color{ 30, 36, 42, 230 } : Color{ 35, 40, 46, 215 });
        DrawRectangleLinesEx(cell, 1.0f, active ? Color{ 128, 188, 235, 255 } : Color{ 78, 88, 98, 255 });
    }

    const std::string text = activeRow ? editor.text[component] : FormatFloatValue(value, decimals);
    DrawUiTextClipped(font, text.c_str(), cell.x + 5.0f, rowY, 14.0f, cell.width - 10.0f, activeRow ? RAYWHITE : Color{ 205, 213, 220, 255 });
}

void DrawTransformValueRow(Font font, const TransformValueEditor& editor, const char* label, TransformValueField field, Vector3 value, float panelX, float rowY, int decimals)
{
    DrawUiText(font, label, panelX + 12.0f, rowY, 14.0f, Color{ 205, 213, 220, 255 });
    DrawTransformValueCell(font, editor, field, 0, value.x, panelX, rowY, decimals);
    DrawTransformValueCell(font, editor, field, 1, value.y, panelX, rowY, decimals);
    DrawTransformValueCell(font, editor, field, 2, value.z, panelX, rowY, decimals);
}

void DrawSelectedInfoPanel(Font font, const ModelTab* active, const TransformValueEditor& transformValueEditor, bool editPivotMode)
{
    if (!active || active->selectedNode < 0 || active->selectedNode >= static_cast<int>(active->loaded.nodes.size())) return;
    if (!IsValidSelectableNode(*active, active->selectedNode)) return;

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
            totalPolys += meshNode.meshPolygonCount > 0 ? meshNode.meshPolygonCount : meshNode.meshTriangleCount;
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
    const bool showingPivot = editPivotMode && IsValidPivotNode(*active, active->selectedNode);
    const BonePose* currentBonePose = !showingPivot && node.type == SceneNodeType::Bone ? FindCurrentBonePose(*active, active->selectedNode) : nullptr;
    const Vector3 position = currentBonePose ? currentBonePose->position : node.position;
    const Vector3 rotation = currentBonePose ? currentBonePose->rotation : node.rotation;
    const Vector3 scale = currentBonePose ? currentBonePose->scale : node.scale;
    constexpr float panelW = 330.0f;
    constexpr float panelH = 154.0f;
    const float panelX = static_cast<float>(GetScreenWidth()) - panelW - 12.0f;
    const float panelY = static_cast<float>(GetScreenHeight()) - gBottomPanelReservedHeight - panelH - 12.0f;

    DrawRectangleRec(Rectangle{ panelX, panelY, panelW, panelH }, Color{ 18, 20, 23, 225 });
    DrawRectangleLinesEx(Rectangle{ panelX, panelY, panelW, panelH }, 1.0f, Color{ 70, 78, 88, 255 });

    DrawUiText(font, showingPivot ? "PIVOT" : "SELECTION", panelX + 12.0f, panelY + 10.0f, 15.0f, Color{ 165, 182, 196, 255 });
    DrawUiTextClipped(font, node.name.c_str(), panelX + 100.0f, panelY + 10.0f, 15.0f, panelW - 112.0f, RAYWHITE);

    char line[256] = {};
    std::snprintf(line, sizeof(line), "Type: %s", GetSceneNodeTypeName(node.type));
    DrawUiText(font, line, panelX + 12.0f, panelY + 36.0f, 14.0f, Color{ 205, 213, 220, 255 });

    DrawTransformValueRow(font, transformValueEditor, "Pos:", TransformValueField::Position, position, panelX, panelY + 58.0f, 3);
    DrawTransformValueRow(font, transformValueEditor, "Rot:", TransformValueField::Rotation, rotation, panelX, panelY + 80.0f, 2);
    DrawTransformValueRow(font, transformValueEditor, "Scale:", TransformValueField::Scale, scale, panelX, panelY + 102.0f, 3);

    std::snprintf(line, sizeof(line), "Polys: %d", node.meshPolygonCount > 0 ? node.meshPolygonCount : node.meshTriangleCount);
    DrawUiText(font, line, panelX + 12.0f, panelY + 124.0f, 14.0f, Color{ 205, 213, 220, 255 });
    DrawUiTextClipped(font, node.materialName.empty() ? "Material: None" : (std::string("Material: ") + node.materialName).c_str(), panelX + 120.0f, panelY + 124.0f, 14.0f, panelW - 132.0f, Color{ 205, 213, 220, 255 });
}

