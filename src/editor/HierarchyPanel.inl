void DrawHierarchyPanel(Font font,
                        ModelTab* active,
                        HierarchyPanelState& panel,
                        RenameEditor& renameEditor,
                        const std::vector<std::string>& droppedPaths,
                        bool& droppedTextureHandled,
                        TextureClipboard& textureClipboard,
                        std::string& notice,
                        std::string& error,
                        bool inputBlocked)
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

    const float tabW = (panelW - 32.0f) / 5.0f;
    const Rectangle hierarchyTab{ panelX + 8.0f, panelY + 34.0f, tabW, 24.0f };
    const Rectangle statsTab{ hierarchyTab.x + hierarchyTab.width + 4.0f, panelY + 34.0f, tabW, 24.0f };
    const Rectangle materialsTab{ statsTab.x + statsTab.width + 4.0f, panelY + 34.0f, tabW, 24.0f };
    const Rectangle uvTab{ materialsTab.x + materialsTab.width + 4.0f, panelY + 34.0f, tabW, 24.0f };
    const Rectangle validatorTab{ uvTab.x + uvTab.width + 4.0f, panelY + 34.0f, tabW, 24.0f };
    if (!inputBlocked && DrawPanelTab(font, hierarchyTab, "Tree", panel.activeTab == LeftPanelTab::Hierarchy))
    {
        panel.activeTab = LeftPanelTab::Hierarchy;
    }
    if (!inputBlocked && DrawPanelTab(font, statsTab, "Stats", panel.activeTab == LeftPanelTab::Stats))
    {
        panel.activeTab = LeftPanelTab::Stats;
    }
    if (!inputBlocked && DrawPanelTab(font, materialsTab, "Mats", panel.activeTab == LeftPanelTab::Materials))
    {
        panel.activeTab = LeftPanelTab::Materials;
    }
    if (!inputBlocked && DrawPanelTab(font, uvTab, "UV", panel.activeTab == LeftPanelTab::UV))
    {
        panel.activeTab = LeftPanelTab::UV;
    }

    if (!inputBlocked && DrawPanelTab(font, validatorTab, "Valid", panel.activeTab == LeftPanelTab::Validator))
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
        DrawMaterialsPanel(font, *active, panelX, GetHierarchyContentStartY() + 10.0f, panelW, droppedPaths, droppedTextureHandled, textureClipboard, notice, error, inputBlocked);
        return;
    }
    if (panel.activeTab == LeftPanelTab::UV)
    {
        DrawUvPanel(font, *active, renameEditor, panelX, GetHierarchyContentStartY() + 10.0f, panelW);
        return;
    }

    if (panel.activeTab == LeftPanelTab::Validator)
    {
        DrawValidatorPanel(font, *active, panel, panelX, GetHierarchyContentStartY() + 10.0f, panelW, notice, error);
        return;
    }

    if (active->collapsedNodes.size() != active->loaded.nodes.size())
    {
        active->collapsedNodes.assign(active->loaded.nodes.size(), false);
    }

    const Vector2 mouse = GetMousePosition();
    const bool additiveSelection = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);
    const bool leftPressed = !inputBlocked && openfbx::UiMouseButtonPressed(MOUSE_BUTTON_LEFT);
    const bool leftDown = !inputBlocked && openfbx::UiMouseButtonDown(MOUSE_BUTTON_LEFT);
    const bool leftReleased = !inputBlocked && openfbx::UiMouseButtonReleased(MOUSE_BUTTON_LEFT);
    if (!openfbx::UiMouseButtonDown(MOUSE_BUTTON_LEFT) || !additiveSelection)
    {
        panel.shiftDragSelecting = false;
        panel.shiftDragAnchorNode = -1;
        panel.shiftDragLastNode = -1;
        panel.shiftDragBaseSelection.clear();
    }
    if (!leftDown && !leftReleased)
    {
        panel.reparentDragArmed = false;
        panel.reparentDragging = false;
        panel.reparentDragNode = -1;
        panel.reparentDropTarget = -1;
        panel.reparentDragNodes.clear();
    }
    if (additiveSelection)
    {
        panel.reparentDragArmed = false;
        panel.reparentDragging = false;
    }
    if (panel.reparentDragArmed && leftDown && !panel.reparentDragging)
    {
        const float dragDistance = Vector2Distance(mouse, panel.reparentDragStart);
        if (dragDistance > 5.0f)
        {
            panel.reparentDragging = true;
            panel.contextMenuOpen = false;
        }
    }
    if (panel.reparentDragging)
    {
        panel.reparentDropTarget = -1;
    }

    auto getContextMenuHeight = [&]()
    {
        return GetNodeContextMenuHeight(*active, panel.contextNodeIndex, panel.contextNodeIndices);
    };
    panel.scroll = ClampFloat(panel.scroll, 0.0f, GetHierarchyMaxScroll(*active));
    float rowY = GetHierarchyContentStartY();
    int visibleRow = 0;
    const int firstRow = static_cast<int>(std::floor(panel.scroll));
    const std::vector<int> hierarchyOrder = BuildVisibleHierarchyOrder(*active, active->collapsedNodes);

    for (int orderIndex = 0; orderIndex < static_cast<int>(hierarchyOrder.size()); ++orderIndex)
    {
        const int i = hierarchyOrder[static_cast<size_t>(orderIndex)];
        if (visibleRow++ < firstRow) continue;
        if (rowY + rowH > panelY + panelH) break;

        const SceneNode& node = active->loaded.nodes[static_cast<size_t>(i)];
        const Rectangle row{ panelX + 6.0f, rowY, panelW - 30.0f, rowH };
        const bool hovered = !panel.scrollDragging && CheckCollisionPointRec(mouse, row);
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
        if (panel.reparentDragging && hovered)
        {
            panel.reparentDropTarget = i;
            const bool validDrop = CanReparentNodeRoots(*active, panel.reparentDragNodes, i);
            DrawRectangleRec(row, validDrop ? Color{ 52, 115, 82, 90 } : Color{ 130, 58, 58, 90 });
            DrawRectangleLinesEx(row, 1.0f, validDrop ? Color{ 90, 210, 145, 230 } : Color{ 235, 105, 105, 230 });
        }

        const Rectangle collapseRect{ panelX + 10.0f + indent, rowY + 3.0f, 14.0f, 16.0f };
        if (hasChildren)
        {
            DrawUiText(font, active->collapsedNodes[static_cast<size_t>(i)] ? ">" : "v", collapseRect.x, collapseRect.y, 15.0f, Color{ 190, 198, 206, 255 });
            if (hovered && leftPressed && CheckCollisionPointRec(mouse, collapseRect))
            {
                active->collapsedNodes[static_cast<size_t>(i)] = !active->collapsedNodes[static_cast<size_t>(i)];
            }
        }

        char label[320] = {};
        std::snprintf(label, sizeof(label), "%s %s", GetSceneNodeIcon(node.type), node.name.c_str());
        const float labelX = panelX + 28.0f + indent;
        const float labelMaxW = panelX + panelW - 30.0f - labelX;
        BeginScissorMode(static_cast<int>(panelX), static_cast<int>(panelY), static_cast<int>(panelW), static_cast<int>(panelH));
        DrawUiTextClipped(font, label, labelX, rowY + 3.0f, 15.0f, labelMaxW, !IsViewportNodeVisible(*active, i) ? Color{ 100, 108, 116, 255 } : selected ? RAYWHITE : Color{ 198, 207, 216, 255 });
        EndScissorMode();

        if (hovered &&
            leftPressed &&
            !openfbx::UiMouseButtonPressed(MOUSE_BUTTON_RIGHT) &&
            !openfbx::UiMouseButtonDown(MOUSE_BUTTON_RIGHT) &&
            !mouseOverContextMenu &&
            !CheckCollisionPointRec(mouse, collapseRect))
        {
            if (additiveSelection)
            {
                panel.shiftDragSelecting = true;
                panel.shiftDragAnchorNode = i;
                panel.shiftDragLastNode = i;
                panel.reparentDragArmed = false;
                panel.reparentDragging = false;
            }
            if (!additiveSelection)
            {
                panel.reparentDragArmed = true;
                panel.reparentDragging = false;
                panel.reparentDragNode = i;
                panel.reparentDragStart = mouse;
                panel.reparentDropTarget = -1;
                panel.reparentDragNodes = GetReparentDragRoots(*active, i);
                if (!IsNodeSelected(*active, i) || active->selectedNodes.size() <= 1)
                {
                    SelectNode(*active, i, false);
                }
                else
                {
                    active->selectedNode = i;
                }
            }
            else
            {
                SelectNode(*active, i, true);
                panel.shiftDragBaseSelection = active->selectedNodes;
            }
            panel.contextMenuOpen = false;
        }
        else if (hovered && panel.shiftDragSelecting && additiveSelection && leftDown &&
                 !mouseOverContextMenu && !CheckCollisionPointRec(mouse, collapseRect) &&
                 i != panel.shiftDragLastNode)
        {
            SetVisibleNodeRangeSelection(*active,
                                         active->collapsedNodes,
                                         panel.shiftDragAnchorNode,
                                         i,
                                         panel.shiftDragBaseSelection);
            panel.shiftDragLastNode = i;
            panel.contextMenuOpen = false;
        }

        if (hovered && openfbx::UiMouseButtonPressed(MOUSE_BUTTON_RIGHT) && !mouseOverContextMenu)
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

    if (leftReleased && panel.reparentDragging)
    {
        if (CanReparentNodeRoots(*active, panel.reparentDragNodes, panel.reparentDropTarget))
        {
            PushUndoSnapshot(*active);
            const int changedCount = ReparentNodeRoots(*active, panel.reparentDragNodes, panel.reparentDropTarget);
            if (changedCount > 0)
            {
                if (panel.reparentDropTarget >= 0 && panel.reparentDropTarget < static_cast<int>(active->collapsedNodes.size()))
                {
                    active->collapsedNodes[static_cast<size_t>(panel.reparentDropTarget)] = false;
                }
                notice = "Reparented " + std::to_string(changedCount) + " object" + std::string(changedCount == 1 ? "." : "s.");
                error.clear();
            }
        }
        else if (panel.reparentDropTarget >= 0)
        {
            error = "Cannot parent an object to itself or one of its children.";
            notice.clear();
        }
    }
    else if (leftReleased && panel.reparentDragArmed && !panel.reparentDragging)
    {
        SelectNode(*active, panel.reparentDragNode, false);
    }
    if (leftReleased)
    {
        panel.reparentDragArmed = false;
        panel.reparentDragging = false;
        panel.reparentDragNode = -1;
        panel.reparentDropTarget = -1;
        panel.reparentDragNodes.clear();
    }

    const Rectangle track = GetHierarchyScrollTrack(panel);
    const Rectangle thumb = GetHierarchyScrollThumb(*active, panel);
    DrawRectangleRec(track, Color{ 44, 49, 55, 255 });
    const bool scrollHovered = CheckCollisionPointRec(mouse, track);
    DrawRectangleRec(thumb, panel.scrollDragging ? Color{ 156, 201, 235, 255 } :
                           scrollHovered ? Color{ 139, 159, 180, 255 } : Color{ 105, 125, 145, 255 });

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
        const bool validContextMesh = validContextNode && HasMeshNode(*active, contextNodes);
        const bool multiContext = contextNodes.size() > 1;
        if (panel.contextMenuJustOpened)
        {
            panel.contextMenuJustOpened = false;
        }
        else
        {
            float itemY = menu.y;
            const Rectangle renameItem{ menu.x, itemY, menu.width, 30.0f };
            itemY += 30.0f;
            const Rectangle deleteItem{ menu.x, itemY, menu.width, 30.0f };
            itemY += 30.0f;
            const Rectangle unparentItem{ menu.x, itemY, menu.width, 30.0f };
            itemY += 30.0f;
            const Rectangle applyScaleItem{ menu.x, itemY, menu.width, 30.0f };
            itemY += 30.0f;
            if (!inputBlocked && DrawPanelButton(font, renameItem, "Rename"))
            {
                if (validContextNode)
                {
                    StartRenameNode(renameEditor, active->loaded, panel.contextNodeIndex);
                }
                panel.contextMenuOpen = false;
            }
            if (!inputBlocked && DrawPanelButton(font, deleteItem, "Delete"))
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
            if (!inputBlocked && DrawPanelButton(font, unparentItem, "Unparent"))
            {
                if (validContextNode && !contextRoots.empty())
                {
                    EditSnapshot before = CaptureEditSnapshot(*active);
                    const int changedCount = UnparentNodeRoots(*active, contextRoots);
                    if (changedCount > 0)
                    {
                        PushUndoSnapshot(*active, std::move(before));
                        if (multiContext)
                        {
                            notice = "Unparented " + std::to_string(changedCount) + " object" + std::string(changedCount == 1 ? "." : "s.");
                        }
                        else
                        {
                            notice = "Unparented object.";
                        }
                    }
                    else
                    {
                        notice = "Object already at scene root.";
                    }
                    error.clear();
                }
                panel.contextMenuOpen = false;
            }
            if (!inputBlocked && DrawPanelButton(font, applyScaleItem, "Apply Scale"))
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
            if (validContextMesh)
            {
                const std::vector<int> mergeableMeshNodes = GetMergeableMeshNodes(*active, contextNodes);
                if (mergeableMeshNodes.size() >= 2)
                {
                    const Rectangle mergeGeometryItem{ menu.x, itemY, menu.width, 30.0f };
                    itemY += 30.0f;
                    if (!inputBlocked && DrawPanelButton(font, mergeGeometryItem, "Merge Geometry"))
                    {
                        EditSnapshot before = CaptureEditSnapshot(*active);
                        std::string mergeError;
                        const int changedCount = MergeSelectedGeometry(*active, contextNodes, mergeError);
                        if (changedCount > 0)
                        {
                            PushUndoSnapshot(*active, std::move(before));
                            notice = "Merged " + std::to_string(changedCount) + " meshes.";
                            error.clear();
                        }
                        else
                        {
                            RestoreEditSnapshot(*active, before);
                            error = mergeError.empty() ? "Could not merge selected geometry." : mergeError;
                            notice.clear();
                        }
                        panel.contextMenuOpen = false;
                    }
                }

                const Rectangle pivotCenterItem{ menu.x, itemY, menu.width, 30.0f };
                itemY += 30.0f;
                if (!inputBlocked && DrawPanelButton(font, pivotCenterItem, "Pivot to Center"))
                {
                    EditSnapshot before = CaptureEditSnapshot(*active);
                    const int changedCount = SetMeshPivotsToBoundsCenterForContextNodes(*active, contextNodes);
                    if (changedCount > 0)
                    {
                        PushUndoSnapshot(*active, std::move(before));
                        if (multiContext)
                        {
                            notice = "Moved pivot to center on " + std::to_string(changedCount) + " mesh" + std::string(changedCount == 1 ? "." : "es.");
                        }
                        else
                        {
                            notice = "Moved pivot to center.";
                        }
                    }
                    else
                    {
                        notice = "Mesh pivot already at center.";
                    }
                    error.clear();
                    panel.contextMenuOpen = false;
                }

                const Rectangle pivotBottomItem{ menu.x, itemY, menu.width, 30.0f };
                itemY += 30.0f;
                if (!inputBlocked && DrawPanelButton(font, pivotBottomItem, "Pivot to Bottom"))
                {
                    EditSnapshot before = CaptureEditSnapshot(*active);
                    const int changedCount = SetMeshPivotsToBoundsBottomForContextNodes(*active, contextNodes);
                    if (changedCount > 0)
                    {
                        PushUndoSnapshot(*active, std::move(before));
                        if (multiContext)
                        {
                            notice = "Moved pivot to bottom on " + std::to_string(changedCount) + " mesh" + std::string(changedCount == 1 ? "." : "es.");
                        }
                        else
                        {
                            notice = "Moved pivot to bottom.";
                        }
                    }
                    else
                    {
                        notice = "Mesh pivot already at bottom.";
                    }
                    error.clear();
                    panel.contextMenuOpen = false;
                }

                const Rectangle flipNormalsItem{ menu.x, itemY, menu.width, 30.0f };
                itemY += 30.0f;
                if (!inputBlocked && DrawPanelButton(font, flipNormalsItem, "Flip Normals"))
                {
                    EditSnapshot before = CaptureEditSnapshot(*active);
                    const int changedCount = FlipMeshNormalsForContextNodes(*active, contextNodes);
                    if (changedCount > 0)
                    {
                        PushUndoSnapshot(*active, std::move(before));
                        if (multiContext)
                        {
                            notice = "Flipped normals on " + std::to_string(changedCount) + " mesh" + std::string(changedCount == 1 ? "." : "es.");
                        }
                        else
                        {
                            notice = "Flipped normals.";
                        }
                    }
                    else
                    {
                        notice = "No mesh normals to flip.";
                    }
                    error.clear();
                    panel.contextMenuOpen = false;
                }
            }
            if (validContextBone)
            {
                const Rectangle resetBindPoseItem{ menu.x, itemY, menu.width, 30.0f };
                itemY += 30.0f;
                if (!inputBlocked && DrawPanelButton(font, resetBindPoseItem, "Reset Bind Pose"))
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
            }
            if (!inputBlocked && openfbx::UiMouseButtonPressed(MOUSE_BUTTON_LEFT) && !CheckCollisionPointRec(mouse, menu))
            {
                panel.contextMenuOpen = false;
            }
        }
    }
}

