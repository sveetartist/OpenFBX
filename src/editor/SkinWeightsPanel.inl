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
        const float indent = static_cast<float>(GetHierarchyDisplayDepth(active->loaded, i)) * 14.0f;
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
                panel.contextNodeIndex = GetContextAnchorNode(*active, panel.contextNodeIndices);
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

    const int visibleCount = CountVisibleSceneNodes(*active, active->collapsedNodes);
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
                                      IsValidSelectableNode(*active, panel.contextNodeIndex);
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

