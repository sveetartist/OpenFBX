int RunOpenFbxApp(int argc, char** argv)
{
    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_MSAA_4X_HINT);
    char windowTitle[128] = {};
    std::snprintf(windowTitle, sizeof(windowTitle), "%s %s", kAppName, kAppVersion);
    InitWindow(1280, 800, windowTitle);
    rlSetClipPlanes(kNearClipPlane, kFarClipPlane);
    SetExitKey(KEY_NULL);
    ApplyWindowIcon();
    SetTargetFPS(60);

    Font uiFont = LoadTechnicalFont();
    LitShader litShader = LoadBasicLitShader();
    Material checkerMaterial = LoadMaterialDefault();
    int appliedCheckerVariant = 0;
    OrbitCamera emptyOrbit = CreateDefaultCamera();
    std::vector<std::unique_ptr<ModelTab>> tabs;
    int activeTab = -1;
    ViewMode viewMode = ViewMode::Shaded;
    MaterialPreviewMode materialPreviewMode = MaterialPreviewMode::Shaded;
    NavigationPreset navigation = NavigationPreset::Blender;
    OpenMenu openMenu = OpenMenu::None;
    bool menuPointerCaptured = false;
    HierarchyPanelState hierarchyPanel;
    VisibilityState visibility;
    std::string error;
    std::string notice;
    LogPanelState logPanel;
    bool quitRequested = false;
    bool animationPanelCollapsed = false;
    bool compareResultVisible = false;
    bool aboutVisible = false;
    bool compareResultCompatible = false;
    std::string compareResultPath;
    std::string compareResultText;
    TextureClipboard textureClipboard;
    RenameEditor renameEditor;
    TransformValueEditor transformValueEditor;
    TransformTool transformTool = TransformTool::Select;
    GizmoOrientation gizmoOrientation = GizmoOrientation::Global;
    bool editPivotMode = false;
    TransformGizmoState transformGizmo;
    MarqueeSelectionState marquee;

    auto initializeLoadedTab = [&](ModelTab& tab, const std::string& path)
    {
        ApplyNeutralMaterial(tab.loaded);
        ApplyLitShader(tab.loaded, litShader);
        EnsurePbrMaterialStates(tab);
        tab.orbit = CreateDefaultCamera();
        FocusCameraOnBounds(tab.orbit, tab.loaded.bounds);
        tab.animation.clipIndex = -1;
        tab.animation.time = 0.0f;
        tab.animation.playing = false;
        tab.originalBindBonePoses = tab.loaded.bonePoses;
        tab.visibleBones = tab.loaded.bones;
        tab.visibleBonePoses = tab.loaded.bonePoses;
        tab.collapsedNodes.assign(tab.loaded.nodes.size(), false);
        tab.hiddenNodes.clear();
        tab.validationCache = ValidationCache{};
        tab.deletedNodes.assign(tab.loaded.nodes.size(), false);
        tab.selectedNode = -1;
        for (int nodeIndex = 0; nodeIndex < static_cast<int>(tab.loaded.nodes.size()); ++nodeIndex)
        {
            if (IsValidSelectableNode(tab, nodeIndex))
            {
                tab.selectedNode = nodeIndex;
                break;
            }
        }
        if (tab.selectedNode >= 0)
        {
            tab.selectedNodes.assign(1, tab.selectedNode);
        }
        tab.isolatedNode = -1;
        tab.path = path;
        tab.title = MakeTabTitle(path);
    };

    auto openPathInNewTab = [&](const std::string& sourcePath)
    {
        DrawLoadingScreen(uiFont, sourcePath);
        LoadedFbxModel loaded;
        std::string loadError;
        std::string path;
        if (!PrepareModelForOpening(sourcePath, path, loadError))
        {
            error = loadError;
            notice.clear();
            CollectLogMessages(logPanel, notice, error);
            return;
        }
        const bool converted = path != sourcePath;
        if (converted) DrawLoadingScreen(uiFont, path);
        if (!LoadFbxModel(path, loaded, loadError))
        {
            error = loadError;
            notice.clear();
            std::cerr << error << "\n";
            CollectLogMessages(logPanel, notice, error);
            return;
        }

        auto tab = std::make_unique<ModelTab>();
        tab->loaded = std::move(loaded);
        initializeLoadedTab(*tab, path);
        std::string textureLoadError;
        const int importedTextureCount = LoadImportedPbrTextures(*tab, textureLoadError);

        tabs.push_back(std::move(tab));
        activeTab = static_cast<int>(tabs.size()) - 1;
        error = textureLoadError;
        notice = importedTextureCount > 0
            ? "Loaded " + std::to_string(importedTextureCount) + " FBX texture" + (importedTextureCount == 1 ? "." : "s.")
            : "Loaded FBX: " + path;
        if (converted) notice = "Converted to FBX: " + path + (notice.empty() ? "" : ". " + notice);
        CollectLogMessages(logPanel, notice, error);
    };

    auto restorePbrMaterialState = [&](ModelTab& tab,
                                       const std::vector<std::string>& previousMaterialNames,
                                       const std::vector<PbrMaterialSnapshot>& previousMaterials,
                                       int previousSelectedMaterial,
                                       std::string& restoreError)
    {
        restoreError.clear();
        EnsurePbrMaterialStates(tab);

        auto findPreviousMaterial = [&](int materialIndex) -> int
        {
            if (materialIndex >= 0 && materialIndex < static_cast<int>(tab.loaded.materialNames.size()))
            {
                const std::string& materialName = tab.loaded.materialNames[static_cast<size_t>(materialIndex)];
                for (int previousIndex = 0; previousIndex < static_cast<int>(previousMaterialNames.size()); ++previousIndex)
                {
                    if (materialName == previousMaterialNames[static_cast<size_t>(previousIndex)])
                    {
                        return previousIndex;
                    }
                }
            }
            return materialIndex < static_cast<int>(previousMaterials.size()) ? materialIndex : -1;
        };

        for (int materialIndex = 0; materialIndex < static_cast<int>(tab.pbrMaterials.size()); ++materialIndex)
        {
            const int previousIndex = findPreviousMaterial(materialIndex);
            if (previousIndex < 0 || previousIndex >= static_cast<int>(previousMaterials.size())) continue;

            const PbrMaterialSnapshot& previous = previousMaterials[static_cast<size_t>(previousIndex)];
            PbrMaterialState& material = tab.pbrMaterials[static_cast<size_t>(materialIndex)];
            material.normalDirectX = previous.normalDirectX;
            material.roughnessChannel = previous.roughnessChannel;
            material.metallicChannel = previous.metallicChannel;
            material.aoChannel = previous.aoChannel;
            material.opacityChannel = previous.opacityChannel;

            for (int slotIndex = 0; slotIndex < static_cast<int>(PbrTextureSlot::Count); ++slotIndex)
            {
                const PbrTextureSnapshot& texture = previous.textures[static_cast<size_t>(slotIndex)];
                if (!texture.loaded || texture.path.empty()) continue;

                std::string textureError;
                if (!LoadPbrTexture(tab, materialIndex, static_cast<PbrTextureSlot>(slotIndex), texture.path, textureError) && restoreError.empty())
                {
                    restoreError = textureError;
                }
            }
        }

        int selectedMaterial = -1;
        if (previousSelectedMaterial >= 0 && previousSelectedMaterial < static_cast<int>(previousMaterialNames.size()))
        {
            const std::string& previousSelectedName = previousMaterialNames[static_cast<size_t>(previousSelectedMaterial)];
            for (int materialIndex = 0; materialIndex < static_cast<int>(tab.loaded.materialNames.size()); ++materialIndex)
            {
                if (tab.loaded.materialNames[static_cast<size_t>(materialIndex)] == previousSelectedName)
                {
                    selectedMaterial = materialIndex;
                    break;
                }
            }
        }
        if (selectedMaterial < 0 && previousSelectedMaterial < static_cast<int>(tab.pbrMaterials.size()))
        {
            selectedMaterial = previousSelectedMaterial;
        }
        tab.selectedMaterial = ClampInt(selectedMaterial, 0, std::max(0, static_cast<int>(tab.pbrMaterials.size()) - 1));
    };

    auto reloadActiveTab = [&]()
    {
        if (activeTab < 0 || activeTab >= static_cast<int>(tabs.size())) return false;

        ModelTab& tab = *tabs[static_cast<size_t>(activeTab)];
        if (tab.path.empty()) return false;

        const std::string path = tab.path;
        DrawLoadingScreen(uiFont, path);

        LoadedFbxModel reloaded;
        std::string loadError;
        if (!LoadFbxModel(path, reloaded, loadError))
        {
            error = loadError;
            notice.clear();
            std::cerr << error << "\n";
            return true;
        }

        const std::vector<std::string> previousMaterialNames = tab.loaded.materialNames;
        const EditSnapshot previousState = CaptureEditSnapshot(tab);

        UnloadPbrTextures(tab);
        UnloadFbxModel(tab.loaded);
        tab = ModelTab{};
        tab.loaded = std::move(reloaded);
        initializeLoadedTab(tab, path);
        std::string importedTextureError;
        LoadImportedPbrTextures(tab, importedTextureError);

        std::string restoreError;
        restorePbrMaterialState(tab, previousMaterialNames, previousState.pbrMaterials, previousState.selectedMaterial, restoreError);
        renameEditor = RenameEditor{};
        CancelTransformValueEdit(transformValueEditor);
        transformGizmo = TransformGizmoState{};
        textureClipboard = TextureClipboard{};
        hierarchyPanel.contextMenuOpen = false;
        notice = "Reloaded FBX: " + path;
        error = !restoreError.empty() ? restoreError : importedTextureError;
        return true;
    };

    auto closeActiveTab = [&]()
    {
        if (activeTab < 0 || activeTab >= static_cast<int>(tabs.size())) return false;

        CloseTab(tabs, activeTab, activeTab);
        openMenu = OpenMenu::None;
        renameEditor = RenameEditor{};
        CancelTransformValueEdit(transformValueEditor);
        return true;
    };

    for (int i = 1; i < argc; ++i)
    {
        openPathInNewTab(argv[i]);
        CollectLogMessages(logPanel, notice, error);
    }

    while (!WindowShouldClose() && !quitRequested)
    {
        CollectLogMessages(logPanel, notice, error);
        // Use raw input here: capture remains active through the release frame,
        // even when a menu action or an outside click closes the popup.
        bool pointerGestureActive = false;
        for (int button = MOUSE_BUTTON_LEFT; button <= MOUSE_BUTTON_MIDDLE; ++button)
        {
            pointerGestureActive = pointerGestureActive || IsMouseButtonDown(button) || IsMouseButtonReleased(button);
        }
        if (!pointerGestureActive) menuPointerCaptured = false;
        if (openMenu != OpenMenu::None ||
            (GetMousePosition().y < 28.0f && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)))
        {
            menuPointerCaptured = true;
        }
        const bool validationMenuOpen = activeTab >= 0 && activeTab < static_cast<int>(tabs.size()) &&
            tabs[static_cast<size_t>(activeTab)]->validationFixNode >= 0;
        const bool menuBlocksPointer = openMenu != OpenMenu::None || menuPointerCaptured || validationMenuOpen;
        openfbx::SetUiPointerBlocked(menuBlocksPointer);

        const bool controlDown = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL);
        const bool shiftDown = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);
        const bool altDown = IsKeyDown(KEY_LEFT_ALT) || IsKeyDown(KEY_RIGHT_ALT);
        const bool modalOpen = aboutVisible;
        bool openRequested = !modalOpen && !renameEditor.active && controlDown && IsKeyPressed(KEY_O);
        bool reloadFbxRequested = !modalOpen && !renameEditor.active && !transformValueEditor.active && controlDown && !shiftDown && !altDown && IsKeyPressed(KEY_R);
        bool saveFbxRequested = !modalOpen && !renameEditor.active && controlDown && !shiftDown && IsKeyPressed(KEY_S);
        bool saveAsFbxRequested = !modalOpen && !renameEditor.active && controlDown && shiftDown && IsKeyPressed(KEY_S);
        bool undoRequested = false;
        bool redoRequested = false;

        if (openRequested)
        {
            const std::string selectedPath = openfbx::OpenModelFileDialog();
            if (!selectedPath.empty())
            {
                openPathInNewTab(selectedPath);
            }
        }

        ModelTab* active = activeTab >= 0 && activeTab < static_cast<int>(tabs.size()) ? tabs[static_cast<size_t>(activeTab)].get() : nullptr;
        if (!modalOpen && active && controlDown && !shiftDown && !altDown && IsKeyPressed(KEY_W))
        {
            closeActiveTab();
        }

        active = activeTab >= 0 && activeTab < static_cast<int>(tabs.size()) ? tabs[static_cast<size_t>(activeTab)].get() : nullptr;
        if (!active)
        {
            CancelTransformValueEdit(transformValueEditor);
        }
        if (!modalOpen && !renameEditor.active && !transformValueEditor.active && active && controlDown && !shiftDown && IsKeyPressed(KEY_Z))
        {
            undoRequested = true;
        }
        if (!modalOpen && !renameEditor.active && !transformValueEditor.active && active &&
            ((controlDown && IsKeyPressed(KEY_Y)) || (controlDown && shiftDown && IsKeyPressed(KEY_Z))))
        {
            redoRequested = true;
        }
        gBottomPanelReservedHeight = (animationPanelCollapsed ? kTimelineCollapsedHeight : kTimelinePanelHeight) + GetLogPanelHeight(logPanel);
        const Vector2 mouse = GetMousePosition();
        std::vector<std::string> droppedPaths;
        bool droppedTextureHandled = false;
        if (IsFileDropped())
        {
            FilePathList droppedFiles = LoadDroppedFiles();
            droppedPaths.reserve(droppedFiles.count);
            for (unsigned int i = 0; i < droppedFiles.count; ++i)
            {
                droppedPaths.push_back(droppedFiles.paths[i]);
            }
            UnloadDroppedFiles(droppedFiles);
        }

        UpdateHierarchyPanelInteraction(hierarchyPanel, active);
        const float hierarchyBlockW = GetHierarchyPanelBlockWidth(hierarchyPanel);
        const float hierarchyContextMenuH = active && hierarchyPanel.contextMenuOpen
            ? GetNodeContextMenuHeight(*active, hierarchyPanel.contextNodeIndex, hierarchyPanel.contextNodeIndices)
            : kNodeContextMenuBaseH;
        const Rectangle hierarchyContextMenuBounds{ hierarchyPanel.contextPosition.x, hierarchyPanel.contextPosition.y, kNodeContextMenuW, hierarchyContextMenuH };
        const bool mouseOverHierarchyContextMenu = hierarchyPanel.contextMenuOpen && CheckCollisionPointRec(mouse, hierarchyContextMenuBounds);
        const bool mouseInViewport = mouse.x > hierarchyBlockW &&
                                     mouse.y >= 61.0f &&
                                     mouse.y < static_cast<float>(GetScreenHeight()) - gBottomPanelReservedHeight &&
                                     !menuBlocksPointer &&
                                     !modalOpen &&
                                     !hierarchyPanel.resizing &&
                                     !hierarchyPanel.scrollDragging &&
                                     !logPanel.dragging &&
                                     !mouseOverHierarchyContextMenu;

        const bool transformInfoConsumedMouse = !modalOpen && !renameEditor.active && UpdateSelectedInfoPanelInput(active, transformValueEditor, editPivotMode, notice, error);
        const bool toolbarConsumedMouse = mouse.y < static_cast<float>(GetScreenHeight()) - gBottomPanelReservedHeight && !logPanel.dragging && !modalOpen && !renameEditor.active && !transformValueEditor.active && UpdateTransformToolbarInput(transformTool, gizmoOrientation, editPivotMode, hierarchyBlockW);
        if (!modalOpen && !renameEditor.active && !transformValueEditor.active && !controlDown && !altDown)
        {
            if (IsKeyPressed(KEY_Q))
            {
                transformTool = TransformTool::Select;
                editPivotMode = false;
            }
            if (IsKeyPressed(KEY_W))
            {
                transformTool = TransformTool::Move;
            }
            if (IsKeyPressed(KEY_E))
            {
                transformTool = TransformTool::Rotate;
            }
            if (IsKeyPressed(KEY_R))
            {
                transformTool = TransformTool::Scale;
                editPivotMode = false;
            }

        }
        const bool transformConsumedMouse = !toolbarConsumedMouse &&
                                            !renameEditor.active &&
                                            !transformValueEditor.active &&
                                            !transformInfoConsumedMouse &&
                                            UpdateTransformGizmoInput(active, transformGizmo, transformTool, gizmoOrientation, editPivotMode, mouseInViewport, notice, error);
        if (!modalOpen && !renameEditor.active && !transformValueEditor.active && active && altDown && IsKeyPressed(KEY_Q))
        {
            ToggleSelectedNodeIsolation(*active, notice, error);
        }
        if (!modalOpen && !renameEditor.active && !transformValueEditor.active && controlDown && !altDown && IsKeyPressed(KEY_Q))
        {
            quitRequested = true;
            continue;
        }
        if (!modalOpen && !renameEditor.active && !transformValueEditor.active && IsKeyPressed(KEY_ESCAPE))
        {
            openMenu = OpenMenu::None;
            if (marquee.tab) marquee = MarqueeSelectionState{};
            else if (active) ClearNodeSelection(*active);
        }

        if (!modalOpen && !renameEditor.active && !transformValueEditor.active && active && IsKeyPressed(KEY_F) && active->loaded.valid)
        {
            if (!FocusCameraOnSelection(*active))
            {
                FocusCameraOnBounds(active->orbit, active->loaded.bounds);
            }
        }

        if (!modalOpen && !renameEditor.active && !transformValueEditor.active && active &&
            !controlDown && !shiftDown && !altDown && !transformGizmo.dragging && IsKeyPressed(KEY_H))
        {
            ToggleSelectedNodeVisibility(*active);
        }
        if (!modalOpen && !renameEditor.active && !transformValueEditor.active &&
            !controlDown && !shiftDown && altDown && !transformGizmo.dragging && IsKeyPressed(KEY_H))
        {
            if (active) ShowAllNodes(*active);
            visibility.geometry = true;
            visibility.bones = true;
            visibility.empties = true;
        }
        if (!modalOpen && !renameEditor.active && !transformValueEditor.active && IsKeyPressed(KEY_V))
        {
            viewMode = NextViewMode(viewMode);
        }
        if (!modalOpen && !renameEditor.active && !transformValueEditor.active && IsKeyPressed(KEY_C))
        {
            materialPreviewMode = NextMaterialPreviewMode(materialPreviewMode);
        }
        if (!modalOpen && !renameEditor.active && !transformValueEditor.active && IsKeyPressed(KEY_M))
        {
            viewMode = ViewMode::Shaded;
            materialPreviewMode = MaterialPreviewMode::Shaded;
        }
        if (!modalOpen && !renameEditor.active && !transformValueEditor.active && IsKeyPressed(KEY_T))
        {
            visibility.textures = !visibility.textures;
        }
        if (!modalOpen && !renameEditor.active && !transformValueEditor.active && IsKeyPressed(KEY_G))
        {
            visibility.geometry = !visibility.geometry;
        }
        if (!modalOpen && !renameEditor.active && !transformValueEditor.active && IsKeyPressed(KEY_B))
        {
            visibility.bones = !visibility.bones;
        }
        if (!modalOpen && !renameEditor.active && !transformValueEditor.active && !controlDown && IsKeyPressed(KEY_O))
        {
            visibility.boneRotations = !visibility.boneRotations;
        }
        const bool pivotEditLocksSelection = editPivotMode &&
                                             active &&
                                             IsValidPivotNode(*active, active->selectedNode);
        const bool canSelectInViewport = active && mouseInViewport && !altDown &&
            !renameEditor.active && !transformValueEditor.active &&
            !toolbarConsumedMouse && !transformInfoConsumedMouse && !transformConsumedMouse &&
            !pivotEditLocksSelection &&
            !openfbx::UiMouseButtonDown(MOUSE_BUTTON_RIGHT) && !openfbx::UiMouseButtonDown(MOUSE_BUTTON_MIDDLE);
        if (marquee.tab && (marquee.tab != active || transformTool != TransformTool::Select ||
            editPivotMode || modalOpen || openMenu != OpenMenu::None || renameEditor.active ||
            transformValueEditor.active || altDown || !IsWindowFocused() ||
            openfbx::UiMouseButtonDown(MOUSE_BUTTON_RIGHT) || openfbx::UiMouseButtonDown(MOUSE_BUTTON_MIDDLE)))
            marquee = MarqueeSelectionState{};
        if (canSelectInViewport && openfbx::UiMouseButtonPressed(MOUSE_BUTTON_LEFT))
        {
            if (transformTool == TransformTool::Select)
                marquee = MarqueeSelectionState{ active, mouse, mouse, false, shiftDown, controlDown };
            else if (SelectNodeFromViewport(*active, mouse, visibility, shiftDown))
                RevealNodeInHierarchy(*active, hierarchyPanel, active->selectedNode);
            else if (!shiftDown) ClearNodeSelection(*active);
        }
        const bool marqueeConsumedMouse = marquee.tab != nullptr;
        if (marquee.tab)
        {
            marquee.end = Vector2{
                ClampFloat(mouse.x, hierarchyBlockW, static_cast<float>(GetScreenWidth())),
                ClampFloat(mouse.y, 61.0f, static_cast<float>(GetScreenHeight()) - gBottomPanelReservedHeight)
            };
            marquee.dragging = marquee.dragging || Vector2Distance(marquee.start, marquee.end) >= 4.0f;
            if (openfbx::UiMouseButtonReleased(MOUSE_BUTTON_LEFT))
            {
                if (marquee.dragging)
                    SelectNodesInMarquee(*active, GetMarqueeRectangle(marquee.start, marquee.end), visibility,
                                         marquee.additive || shiftDown, GetScreenWidth(), GetScreenHeight(), marquee.subtractive || controlDown);
                else if (!SelectNodeFromViewport(*active, marquee.start, visibility, marquee.additive || shiftDown) &&
                         !(marquee.additive || shiftDown))
                    ClearNodeSelection(*active);
                if (active->selectedNode >= 0) RevealNodeInHierarchy(*active, hierarchyPanel, active->selectedNode);
                marquee = MarqueeSelectionState{};
            }
            else if (!openfbx::UiMouseButtonDown(MOUSE_BUTTON_LEFT)) marquee = MarqueeSelectionState{};
        }
        if (active &&
            mouseInViewport &&
            !altDown &&
            !toolbarConsumedMouse &&
            !transformInfoConsumedMouse &&
            !transformConsumedMouse &&
            !pivotEditLocksSelection &&
            openfbx::UiMouseButtonPressed(MOUSE_BUTTON_RIGHT))
        {
            std::vector<int> selectedContextNodes = GetValidContextActionNodes(*active, active->selectedNodes);
            if (selectedContextNodes.size() > 1)
            {
                hierarchyPanel.contextNodeIndices = std::move(selectedContextNodes);
                hierarchyPanel.contextNodeIndex = GetContextAnchorNode(*active, hierarchyPanel.contextNodeIndices);
                const float nodeContextMenuH = GetNodeContextMenuHeight(*active, hierarchyPanel.contextNodeIndices);
                RevealNodeInHierarchy(*active, hierarchyPanel, hierarchyPanel.contextNodeIndex);
                hierarchyPanel.activeTab = LeftPanelTab::Hierarchy;
                hierarchyPanel.contextPosition = Vector2{
                    ClampFloat(mouse.x, 4.0f, static_cast<float>(GetScreenWidth()) - kNodeContextMenuW - 4.0f),
                    ClampFloat(mouse.y, 4.0f, static_cast<float>(GetScreenHeight()) - nodeContextMenuH - 4.0f)
                };
                hierarchyPanel.contextMenuOpen = true;
                hierarchyPanel.contextMenuJustOpened = true;
            }
            else
            {
                const int contextNode = PickNodeFromViewport(*active, mouse, visibility);
                if (contextNode >= 0)
                {
                    if (IsNodeSelected(*active, contextNode))
                    {
                        active->selectedNode = contextNode;
                        hierarchyPanel.contextNodeIndex = contextNode;
                        hierarchyPanel.contextNodeIndices = GetContextActionNodes(*active, contextNode);
                    }
                    else
                    {
                        SetSingleSelectedNode(*active, contextNode);
                        hierarchyPanel.contextNodeIndex = contextNode;
                        hierarchyPanel.contextNodeIndices = GetContextActionNodes(*active, contextNode);
                    }
                    const float nodeContextMenuH = GetNodeContextMenuHeight(*active, hierarchyPanel.contextNodeIndices);
                    RevealNodeInHierarchy(*active, hierarchyPanel, contextNode);
                    hierarchyPanel.activeTab = LeftPanelTab::Hierarchy;
                    hierarchyPanel.contextPosition = Vector2{
                        ClampFloat(mouse.x, 4.0f, static_cast<float>(GetScreenWidth()) - kNodeContextMenuW - 4.0f),
                        ClampFloat(mouse.y, 4.0f, static_cast<float>(GetScreenHeight()) - nodeContextMenuH - 4.0f)
                    };
                    hierarchyPanel.contextMenuOpen = true;
                    hierarchyPanel.contextMenuJustOpened = true;
                }
                else
                {
                    hierarchyPanel.contextMenuOpen = false;
                }
            }
        }

        if (active)
        {
            if (mouseInViewport && !marqueeConsumedMouse && !toolbarConsumedMouse && !transformInfoConsumedMouse && !transformConsumedMouse)
            {
                UpdateNavigation(active->orbit, navigation);
            }
            else
            {
                UpdateOrbitCameraTransform(active->orbit);
            }

            if (!renameEditor.active && !transformValueEditor.active)
            {
                UpdateAnimation(active->animation, active->loaded);
            }
            ApplyAnimatedMeshFrame(*active);
            ApplyAnimatedBoneFrame(*active);
            UpdateLitShader(litShader, active->orbit);
            EnsurePbrMaterialStates(*active);
            UpdateMaterialShader(litShader, &GetSelectedPbrMaterial(*active), materialPreviewMode, visibility.textures);
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
            UpdateMaterialShader(litShader, nullptr, MaterialPreviewMode::Shaded, false);
        }

        BeginDrawing();
        ClearBackground(Color{ 38, 40, 43, 255 });

        const OrbitCamera& drawOrbit = active ? active->orbit : emptyOrbit;
        BeginMode3D(drawOrbit.camera);
        DrawMeterGrid(drawOrbit);
        if (active && active->loaded.valid)
        {
            if (active->loaded.hasMesh && visibility.geometry)
            {
                if (visibility.backfaceCulling)
                {
                    rlEnableBackfaceCulling();
                }
                else
                {
                    rlDisableBackfaceCulling();
                }

                if (viewMode == ViewMode::Shaded || viewMode == ViewMode::ShadedWireframe)
                {
                    BeginBlendMode(BLEND_ALPHA);
                    DrawMaterialModel(*active, litShader, materialPreviewMode, visibility.textures);
                    EndBlendMode();
                }
                else if (viewMode == ViewMode::MaterialColors)
                {
                    BeginBlendMode(BLEND_ALPHA);
                    DrawMaterialColorModel(*active, litShader);
                    EndBlendMode();
                }
                else if (viewMode == ViewMode::Checker)
                {
                    UpdateCheckerMaterial(checkerMaterial, appliedCheckerVariant, error);
                    DrawCheckerModel(*active, checkerMaterial);
                }
                else if (viewMode == ViewMode::UvIslands)
                {
                    DrawUvIslandColorOverlay(*active);
                }

                if (visibility.skinWeights)
                {
                    DrawSkinWeightHeatMap(*active);
                }

                if (viewMode == ViewMode::ShadedWireframe || viewMode == ViewMode::Wireframe)
                {
                    DrawVisibleMeshWireframe(*active, viewMode == ViewMode::Wireframe ? Color{ 220, 225, 230, 255 } : Color{ 25, 28, 31, 180 });
                }

                rlDisableBackfaceCulling();
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
                DrawBones(GetVisibleBones(*active), GetVisibleBonePoses(*active), active->selectedNode, active->selectedNodes);
                if (visibility.boneRotations)
                {
                    DrawBoneRotations(GetVisibleBonePoses(*active), GetBoundsDiagonal(active->loaded.bounds), active->selectedNode, active->orbit.camera);
                }
            }
            DrawSelectedNodeOverlay(*active, visibility, transformTool);
            DrawTransformGizmo(*active, transformTool, transformGizmo, gizmoOrientation, editPivotMode);
            rlDrawRenderBatchActive();
            rlEnableDepthTest();
        }
        EndMode3D();

        if (marquee.tab == active && marquee.dragging)
        {
            const Rectangle rectangle = GetMarqueeRectangle(marquee.start, marquee.end);
            const bool subtractive = marquee.subtractive || controlDown;
            DrawRectangleRec(rectangle, subtractive ? Color{ 230, 95, 80, 35 } : Color{ 80, 155, 230, 35 });
            DrawRectangleLinesEx(rectangle, 1.5f, subtractive ? Color{ 255, 135, 110, 255 } : Color{ 110, 190, 255, 255 });
        }

        char statusText[256] = {};
        const char* activeToolName = editPivotMode ? (transformTool == TransformTool::Rotate ? "Edit Pivot Rotate" : "Edit Pivot Move") : GetTransformToolName(transformTool);
        std::snprintf(statusText, sizeof(statusText), "VIEW: %s    MAT: %s    TOOL: %s    SPACE: %s    PIVOT: %s    NAV: %s", GetViewModeName(viewMode), GetMaterialPreviewModeName(materialPreviewMode), activeToolName, GetGizmoOrientationName(gizmoOrientation), editPivotMode ? "ON" : "OFF", GetNavigationPresetName(navigation));
        DrawUiText(uiFont, statusText, static_cast<float>(GetScreenWidth() - 660), 8, 16, Color{ 165, 220, 255, 255 });

        if (active)
        {
            char channelText[128] = {};
            if (viewMode == ViewMode::MaterialColors)
            {
                std::snprintf(channelText, sizeof(channelText), "Viewport: Material Colors");
            }
            else if (viewMode == ViewMode::Checker)
            {
                if (gUseColoredChecker)
                    std::snprintf(channelText, sizeof(channelText), "Viewport: Colored Checker %d (%dx%d)", gCheckerColor, gCheckerTextureSize, gCheckerTextureSize);
                else
                    std::snprintf(channelText, sizeof(channelText), "Viewport: Checker (%d squares / UV tile)", gCheckerSquares);
            }
            else if (viewMode == ViewMode::UvIslands)
            {
                if (!active->loaded.uvSetNames.empty() && active->selectedUvSet >= 0 && active->selectedUvSet < static_cast<int>(active->loaded.uvSetNames.size()))
                {
                    std::snprintf(channelText, sizeof(channelText), "Viewport: UV Islands (%s)", active->loaded.uvSetNames[static_cast<size_t>(active->selectedUvSet)].c_str());
                }
                else
                {
                    std::snprintf(channelText, sizeof(channelText), "Viewport: UV Islands");
                }
            }
            else
            {
                std::snprintf(channelText, sizeof(channelText), "Material Channel: %s", GetMaterialPreviewModeName(materialPreviewMode));
            }
            const Vector2 channelSize = MeasureTextEx(uiFont, channelText, 16.0f, 1.0f);
            const Rectangle channelBadge{ hierarchyBlockW + 12.0f, 66.0f, channelSize.x + 18.0f, 26.0f };
            DrawRectangleRec(channelBadge, Color{ 24, 27, 31, 210 });
            DrawRectangleLinesEx(channelBadge, 1.0f, Color{ 78, 88, 98, 220 });
            DrawUiText(uiFont, channelText, channelBadge.x + 9.0f, channelBadge.y + 5.0f, 16.0f, Color{ 205, 224, 238, 255 });
        }
        else
        {
            DrawUiText(uiFont, "No FBX loaded", hierarchyBlockW + 12.0f, 66, 16, Color{ 190, 190, 190, 255 });
        }

        DrawSelectedInfoPanel(uiFont, active, transformValueEditor, editPivotMode);

        if (active)
        {
            DrawTimeline(uiFont, *active, renameEditor, animationPanelCollapsed, GetLogPanelHeight(logPanel));
        }
        else
        {
            const float panelHeight = animationPanelCollapsed ? kTimelineCollapsedHeight : kTimelinePanelHeight;
            const float panelY = static_cast<float>(GetScreenHeight()) - GetLogPanelHeight(logPanel) - panelHeight;
            const Rectangle toggleButton{ static_cast<float>(GetScreenWidth()) - 34.0f, panelY + 4.0f, 24.0f, 20.0f };
            DrawRectangle(0, static_cast<int>(panelY), GetScreenWidth(), static_cast<int>(panelHeight), Color{ 20, 22, 24, 238 });
            DrawLine(0, static_cast<int>(panelY), GetScreenWidth(), static_cast<int>(panelY), Color{ 76, 84, 92, 255 });
            DrawUiText(uiFont, "ANIMATIONS", 12.0f, panelY + 7.0f, 16.0f, Color{ 165, 182, 196, 255 });
            if (DrawPanelButton(uiFont, toggleButton, animationPanelCollapsed ? "^" : "v"))
            {
                animationPanelCollapsed = !animationPanelCollapsed;
            }
            if (!animationPanelCollapsed)
            {
                DrawUiText(uiFont, "Open an FBX file to show animation stacks", 12.0f, panelY + 38.0f, 16.0f, Color{ 128, 136, 144, 255 });
            }
        }
        gBottomPanelReservedHeight = (animationPanelCollapsed ? kTimelineCollapsedHeight : kTimelinePanelHeight) + GetLogPanelHeight(logPanel);

        DrawHierarchyPanel(uiFont,
                           active,
                           hierarchyPanel,
                           renameEditor,
                           droppedPaths,
                           droppedTextureHandled,
                           textureClipboard,
                           notice,
                           error,
                           openMenu != OpenMenu::None || modalOpen);
        DrawTransformToolbar(uiFont, transformTool, gizmoOrientation, editPivotMode, hierarchyBlockW);
        DrawOrientationGizmo(uiFont, active ? active->orbit.camera : emptyOrbit.camera);

        CollectLogMessages(logPanel, notice, error);
        DrawLogPanel(uiFont, logPanel,
                     menuBlocksPointer || modalOpen || hierarchyPanel.contextMenuOpen);

        DrawTabs(uiFont, tabs, activeTab);
        active = activeTab >= 0 && activeTab < static_cast<int>(tabs.size()) ? tabs[static_cast<size_t>(activeTab)].get() : nullptr;

        bool menuOpenRequested = false;
        bool menuCloseTabRequested = false;
        bool menuReloadTabRequested = false;
        bool menuUndoRequested = false;
        bool menuRedoRequested = false;
        bool menuSaveFbxRequested = false;
        bool menuSaveAsFbxRequested = false;
        bool packageFbxRequested = false;
        bool extractFbxTexturesRequested = false;
        bool importAnimationsRequested = false;
        bool exportJsonRequested = false;
        bool compareFbxRequested = false;
        bool aboutRequested = false;
        openfbx::SetUiPointerBlocked(false);
        if (!aboutVisible)
        {
            DrawMenuBar(uiFont,
                        openMenu,
                        menuOpenRequested,
                        menuCloseTabRequested,
                        menuReloadTabRequested,
                        menuUndoRequested,
                        menuRedoRequested,
                        menuSaveFbxRequested,
                        menuSaveAsFbxRequested,
                        packageFbxRequested,
                        extractFbxTexturesRequested,
                        importAnimationsRequested,
                        exportJsonRequested,
                        compareFbxRequested,
                        aboutRequested,
                        quitRequested,
                        viewMode,
                        navigation,
                        visibility,
                        active && !active->undoStack.empty(),
                        active && !active->redoStack.empty(),
                        active);
        }
        openfbx::SetUiPointerBlocked(menuBlocksPointer);
        undoRequested = undoRequested || menuUndoRequested;
        redoRequested = redoRequested || menuRedoRequested;
        reloadFbxRequested = reloadFbxRequested || menuReloadTabRequested;
        saveFbxRequested = saveFbxRequested || menuSaveFbxRequested;
        saveAsFbxRequested = saveAsFbxRequested || menuSaveAsFbxRequested;
        DrawSkeletonCompareResultWindow(uiFont, compareResultVisible, compareResultCompatible, compareResultPath, compareResultText);
        DrawRenameEditor(uiFont, active, renameEditor, notice, error);
        DrawAboutWindow(uiFont, aboutVisible);
        EndDrawing();
        openfbx::SetUiPointerBlocked(false);

        if (undoRequested && active)
        {
            if (UndoEdit(*active))
            {
                notice = "Undo.";
                error.clear();
            }
        }
        if (redoRequested && active)
        {
            if (RedoEdit(*active))
            {
                notice = "Redo.";
                error.clear();
            }
        }

        if (menuCloseTabRequested)
        {
            if (closeActiveTab())
            {
                active = activeTab >= 0 && activeTab < static_cast<int>(tabs.size()) ? tabs[static_cast<size_t>(activeTab)].get() : nullptr;
            }
            else
            {
                error = "No active tab to close.";
                notice.clear();
            }
        }

        if (reloadFbxRequested)
        {
            if (reloadActiveTab())
            {
                active = activeTab >= 0 && activeTab < static_cast<int>(tabs.size()) ? tabs[static_cast<size_t>(activeTab)].get() : nullptr;
            }
            else
            {
                error = "No active FBX tab to reload.";
                notice.clear();
            }
        }

        if (aboutRequested)
        {
            aboutVisible = true;
            openMenu = OpenMenu::None;
        }

        if (!droppedPaths.empty() && !droppedTextureHandled)
        {
            bool openedAny = false;
            bool ignoredTexture = false;
            if (active && active->loaded.valid)
            {
                EditSnapshot before = CaptureEditSnapshot(*active);
                const int assignedTextures = AutoAssignDroppedTextures(*active, droppedPaths, notice, error);
                if (assignedTextures > 0)
                {
                    PushUndoSnapshot(*active, std::move(before));
                }
                droppedTextureHandled = assignedTextures > 0;
            }
            for (const std::string& droppedPath : droppedPaths)
            {
                if (IsTextureExtension(std::filesystem::path(droppedPath)))
                {
                    ignoredTexture = true;
                    continue;
                }
                openPathInNewTab(droppedPath);
                openedAny = true;
            }
            if (!droppedTextureHandled && !openedAny && ignoredTexture)
            {
                notice = "Drop texture files onto a material thumbnail.";
                error.clear();
            }
        }

        if (menuOpenRequested)
        {
            const std::string selectedPath = openfbx::OpenModelFileDialog();
            if (!selectedPath.empty())
            {
                openPathInNewTab(selectedPath);
            }
        }

        if (importAnimationsRequested)
        {
            if (!active || !active->loaded.valid)
            {
                error = "No active FBX to import animations into.";
                notice.clear();
            }
            else
            {
                const std::string importPath = OpenFbxFileDialog();
                if (!importPath.empty())
                {
                    int importedCount = 0;
                    std::string importError;
                    EditSnapshot before = CaptureEditSnapshot(*active);
                    if (ImportAnimationsFromFbx(*active, importPath, importedCount, importError))
                    {
                        PushUndoSnapshot(*active, std::move(before));
                        notice = "Imported animations: " + std::to_string(importedCount);
                        error.clear();
                    }
                    else
                    {
                        error = importError;
                        notice.clear();
                    }
                }
            }
        }

        if (saveFbxRequested)
        {
            if (!active || !active->loaded.valid)
            {
                error = "No active FBX to save.";
                notice.clear();
            }
            else
            {
                std::string saveError;
                if (SaveFbxModelAnimations(active->path, active->path, active->loaded, active->deletedNodes, saveError))
                {
                    notice = "Saved FBX: " + active->path;
                    error.clear();
                }
                else
                {
                    error = saveError;
                    notice.clear();
                }
            }
        }

        if (saveAsFbxRequested)
        {
            if (!active || !active->loaded.valid)
            {
                error = "No active FBX to save.";
                notice.clear();
            }
            else
            {
                const std::string savePath = SaveAsFbxFileDialog(active->path);
                if (!savePath.empty())
                {
                    std::string saveError;
                    if (SaveFbxModelAnimations(active->path, savePath, active->loaded, active->deletedNodes, saveError))
                    {
                        active->path = savePath;
                        active->title = MakeTabTitle(savePath);
                        notice = "Saved FBX: " + savePath;
                        error.clear();
                    }
                    else
                    {
                        error = saveError;
                        notice.clear();
                    }
                }
            }
        }

        if (packageFbxRequested)
        {
            if (!active || !active->loaded.valid)
            {
                error = "No active FBX to package.";
                notice.clear();
            }
            else
            {
                PackageFbxWithTextures(*active, notice, error);
            }
        }

        if (extractFbxTexturesRequested)
        {
            if (!active || !active->loaded.valid)
            {
                error = "No active FBX to extract textures from.";
                notice.clear();
            }
            else
            {
                ExtractPackagedFbxTextures(*active, notice, error);
            }
        }

        if (exportJsonRequested)
        {
            if (!active || !active->loaded.valid)
            {
                error = "No active FBX to export.";
                notice.clear();
            }
            else
            {
                std::string outputPath;
                std::string exportError;
                if (ExportFbxJson(*active, outputPath, exportError))
                {
                    notice = "Exported JSON: " + outputPath;
                    error.clear();
                }
                else
                {
                    error = exportError;
                    notice.clear();
                }
            }
        }

        if (compareFbxRequested)
        {
            if (!active || !active->loaded.valid)
            {
                error = "No active FBX to compare.";
                notice.clear();
            }
            else
            {
                const std::string comparePath = OpenFbxFileDialog();
                if (!comparePath.empty())
                {
                    LoadedFbxModel compareModel;
                    std::string compareError;
                    if (LoadFbxModel(comparePath, compareModel, compareError))
                    {
                        compareResultPath = comparePath;
                        compareResultCompatible = CompareSkeletonCompatibility(active->loaded, compareModel, compareResultText);
                        compareResultVisible = true;
                        UnloadFbxModel(compareModel);
                        error.clear();
                        notice.clear();
                    }
                    else
                    {
                        compareResultPath = comparePath;
                        compareResultCompatible = false;
                        compareResultText = "Failed to load comparison FBX.\n" + compareError;
                        compareResultVisible = true;
                        error = compareError;
                        notice.clear();
                    }
                }
            }
        }
        CollectLogMessages(logPanel, notice, error);
    }

    for (std::unique_ptr<ModelTab>& tab : tabs)
    {
        UnloadPbrTextures(*tab);
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
    if (appliedCheckerVariant != 0)
    {
        UnloadTexture(checkerMaterial.maps[MATERIAL_MAP_DIFFUSE].texture);
        checkerMaterial.maps[MATERIAL_MAP_DIFFUSE].texture = Texture2D{};
    }
    UnloadMaterial(checkerMaterial);
    CloseWindow();
    return 0;
}
