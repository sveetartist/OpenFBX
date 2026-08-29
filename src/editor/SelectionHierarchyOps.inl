bool IsDeletedNode(const ModelTab& tab, int nodeIndex)
{
    return nodeIndex >= 0 &&
           nodeIndex < static_cast<int>(tab.deletedNodes.size()) &&
           tab.deletedNodes[static_cast<size_t>(nodeIndex)];
}

bool IsSceneRootNode(const LoadedFbxModel& loaded, int nodeIndex)
{
    return nodeIndex >= 0 &&
           nodeIndex < static_cast<int>(loaded.nodes.size()) &&
           loaded.nodes[static_cast<size_t>(nodeIndex)].parent < 0;
}

bool IsValidSelectableNode(const ModelTab& tab, int nodeIndex)
{
    return nodeIndex >= 0 &&
           nodeIndex < static_cast<int>(tab.loaded.nodes.size()) &&
           !IsSceneRootNode(tab.loaded, nodeIndex) &&
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
    if (IsSceneRootNode(tab.loaded, nodeIndex)) return 0;
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

int GetContextAnchorNode(const ModelTab& tab, const std::vector<int>& contextNodeIndices)
{
    if (IsValidSelectableNode(tab, tab.selectedNode) &&
        std::find(contextNodeIndices.begin(), contextNodeIndices.end(), tab.selectedNode) != contextNodeIndices.end())
    {
        return tab.selectedNode;
    }
    return contextNodeIndices.empty() ? -1 : contextNodeIndices.front();
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
    if (IsSceneRootNode(tab.loaded, nodeIndex)) return;

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
    if (IsSceneRootNode(loaded, nodeIndex)) return;
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

