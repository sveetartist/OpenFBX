void DrawMenuBar(Font font,
                 OpenMenu& openMenu,
                 bool& openRequested,
                 bool& closeTabRequested,
                 bool& reloadTabRequested,
                 bool& undoRequested,
                 bool& redoRequested,
                 bool& saveFbxRequested,
                 bool& saveAsFbxRequested,
                 bool& importAnimationsRequested,
                 bool& exportJsonRequested,
                 bool& compareFbxRequested,
                 bool& aboutRequested,
                 bool& quitRequested,
                 ViewMode& viewMode,
                 NavigationPreset& navigation,
                 VisibilityState& visibility,
                 bool canUndo,
                 bool canRedo)
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
        { "Edit", OpenMenu::Edit, Rectangle{ 66.0f, 3.0f, 58.0f, 22.0f } },
        { "View", OpenMenu::View, Rectangle{ 124.0f, 3.0f, 58.0f, 22.0f } },
        { "Preferences", OpenMenu::Preferences, Rectangle{ 186.0f, 3.0f, 118.0f, 22.0f } },
        { "Help", OpenMenu::Help, Rectangle{ 308.0f, 3.0f, 58.0f, 22.0f } }
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

    Rectangle openMenuBounds{};
    switch (openMenu)
    {
    case OpenMenu::File:
        openMenuBounds = Rectangle{ 8.0f, 29.0f, 270.0f, 278.0f };
        break;
    case OpenMenu::Edit:
        openMenuBounds = Rectangle{ 66.0f, 29.0f, 230.0f, 68.0f };
        break;
    case OpenMenu::View:
        openMenuBounds = Rectangle{ 124.0f, 29.0f, 230.0f, 368.0f };
        break;
    case OpenMenu::Preferences:
        openMenuBounds = Rectangle{ 186.0f, 29.0f, 420.0f, 318.0f };
        break;
    case OpenMenu::Help:
        openMenuBounds = Rectangle{ 308.0f, 29.0f, 230.0f, 38.0f };
        break;
    case OpenMenu::None:
        break;
    }

    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) &&
        openMenu != OpenMenu::None &&
        mouse.y > menuHeight &&
        !CheckCollisionPointRec(mouse, openMenuBounds))
    {
        openMenu = OpenMenu::None;
    }

    if (openMenu == OpenMenu::File)
    {
        DrawRectangle(8, 29, 270, 278, Color{ 28, 31, 35, 245 });
        if (DrawMenuItem(font, Rectangle{ 8.0f, 29.0f, 270.0f, 30.0f }, "Open FBX...        Ctrl+O"))
        {
            openRequested = true;
            openMenu = OpenMenu::None;
        }
        if (DrawMenuItem(font, Rectangle{ 8.0f, 59.0f, 270.0f, 30.0f }, "Close Tab        Ctrl+W"))
        {
            closeTabRequested = true;
            openMenu = OpenMenu::None;
        }
        if (DrawMenuItem(font, Rectangle{ 8.0f, 89.0f, 270.0f, 30.0f }, "Reload Tab        Ctrl+R"))
        {
            reloadTabRequested = true;
            openMenu = OpenMenu::None;
        }
        if (DrawMenuItem(font, Rectangle{ 8.0f, 119.0f, 270.0f, 30.0f }, "Save FBX        Ctrl+S"))
        {
            saveFbxRequested = true;
            openMenu = OpenMenu::None;
        }
        if (DrawMenuItem(font, Rectangle{ 8.0f, 149.0f, 270.0f, 30.0f }, "Save As...        Ctrl+Shift+S"))
        {
            saveAsFbxRequested = true;
            openMenu = OpenMenu::None;
        }
        if (DrawMenuItem(font, Rectangle{ 8.0f, 179.0f, 270.0f, 30.0f }, "Import Animations..."))
        {
            importAnimationsRequested = true;
            openMenu = OpenMenu::None;
        }
        if (DrawMenuItem(font, Rectangle{ 8.0f, 209.0f, 270.0f, 30.0f }, "Export JSON"))
        {
            exportJsonRequested = true;
            openMenu = OpenMenu::None;
        }
        if (DrawMenuItem(font, Rectangle{ 8.0f, 239.0f, 270.0f, 30.0f }, "Compare FBX..."))
        {
            compareFbxRequested = true;
            openMenu = OpenMenu::None;
        }
        if (DrawMenuItem(font, Rectangle{ 8.0f, 269.0f, 270.0f, 30.0f }, "Exit        Ctrl+Q"))
        {
            quitRequested = true;
            openMenu = OpenMenu::None;
        }
    }
    else if (openMenu == OpenMenu::Edit)
    {
        DrawRectangle(66, 29, 230, 68, Color{ 28, 31, 35, 245 });
        if (DrawMenuItem(font, Rectangle{ 66.0f, 29.0f, 230.0f, 30.0f }, canUndo ? "Undo        Ctrl+Z" : "Undo        Ctrl+Z", false))
        {
            if (canUndo) undoRequested = true;
            openMenu = OpenMenu::None;
        }
        if (DrawMenuItem(font, Rectangle{ 66.0f, 59.0f, 230.0f, 30.0f }, canRedo ? "Redo        Ctrl+Y" : "Redo        Ctrl+Y", false))
        {
            if (canRedo) redoRequested = true;
            openMenu = OpenMenu::None;
        }
        if (!canUndo)
        {
            DrawRectangleRec(Rectangle{ 66.0f, 29.0f, 230.0f, 30.0f }, Color{ 28, 31, 35, 160 });
            DrawUiText(font, "Undo        Ctrl+Z", 76.0f, 34.0f, 16.0f, Color{ 105, 115, 124, 255 });
        }
        if (!canRedo)
        {
            DrawRectangleRec(Rectangle{ 66.0f, 59.0f, 230.0f, 30.0f }, Color{ 28, 31, 35, 160 });
            DrawUiText(font, "Redo        Ctrl+Y", 76.0f, 64.0f, 16.0f, Color{ 105, 115, 124, 255 });
        }
    }
    else if (openMenu == OpenMenu::View)
    {
        DrawRectangle(124, 29, 230, 368, Color{ 28, 31, 35, 245 });
        if (DrawMenuItem(font, Rectangle{ 124.0f, 29.0f, 230.0f, 30.0f }, "Shaded", viewMode == ViewMode::Shaded))
        {
            viewMode = ViewMode::Shaded;
        }
        if (DrawMenuItem(font, Rectangle{ 124.0f, 59.0f, 230.0f, 30.0f }, "Shaded Wireframe", viewMode == ViewMode::ShadedWireframe))
        {
            viewMode = ViewMode::ShadedWireframe;
        }
        if (DrawMenuItem(font, Rectangle{ 124.0f, 89.0f, 230.0f, 30.0f }, "Wireframe", viewMode == ViewMode::Wireframe))
        {
            viewMode = ViewMode::Wireframe;
        }
        if (DrawMenuItem(font, Rectangle{ 124.0f, 119.0f, 230.0f, 30.0f }, "Material Colors", viewMode == ViewMode::MaterialColors))
        {
            viewMode = ViewMode::MaterialColors;
        }
        if (DrawMenuItem(font, Rectangle{ 124.0f, 149.0f, 230.0f, 30.0f }, "UV Islands", viewMode == ViewMode::UvIslands))
        {
            viewMode = ViewMode::UvIslands;
        }
        if (DrawMenuItem(font, Rectangle{ 124.0f, 187.0f, 230.0f, 30.0f }, visibility.geometry ? "[x] Geometry        G" : "[ ] Geometry        G"))
        {
            visibility.geometry = !visibility.geometry;
        }
        if (DrawMenuItem(font, Rectangle{ 124.0f, 217.0f, 230.0f, 30.0f }, visibility.textures ? "[x] Textures" : "[ ] Textures"))
        {
            visibility.textures = !visibility.textures;
        }
        if (DrawMenuItem(font, Rectangle{ 124.0f, 247.0f, 230.0f, 30.0f }, visibility.backfaceCulling ? "[x] Backface Culling" : "[ ] Backface Culling"))
        {
            visibility.backfaceCulling = !visibility.backfaceCulling;
        }
        if (DrawMenuItem(font, Rectangle{ 124.0f, 277.0f, 230.0f, 30.0f }, visibility.bones ? "[x] Bones        B" : "[ ] Bones        B"))
        {
            visibility.bones = !visibility.bones;
        }
        if (DrawMenuItem(font, Rectangle{ 124.0f, 307.0f, 230.0f, 30.0f }, visibility.boneRotations ? "[x] Bone Orientation  O" : "[ ] Bone Orientation  O"))
        {
            visibility.boneRotations = !visibility.boneRotations;
        }
        if (DrawMenuItem(font, Rectangle{ 124.0f, 337.0f, 230.0f, 30.0f }, visibility.empties ? "[x] Empties" : "[ ] Empties"))
        {
            visibility.empties = !visibility.empties;
        }
        if (DrawMenuItem(font, Rectangle{ 124.0f, 367.0f, 230.0f, 30.0f }, visibility.skinWeights ? "[x] Skin Weights" : "[ ] Skin Weights"))
        {
            visibility.skinWeights = !visibility.skinWeights;
        }
    }
    else if (openMenu == OpenMenu::Preferences)
    {
        DrawRectangle(186, 29, 420, 318, Color{ 28, 31, 35, 245 });
        DrawUiText(font, "NAVIGATION", 198.0f, 39.0f, 16.0f, Color{ 165, 182, 196, 255 });
        if (DrawMenuItem(font, Rectangle{ 196.0f, 64.0f, 185.0f, 30.0f }, "Blender", navigation == NavigationPreset::Blender))
        {
            navigation = NavigationPreset::Blender;
        }
        if (DrawMenuItem(font, Rectangle{ 391.0f, 64.0f, 185.0f, 30.0f }, "Maya", navigation == NavigationPreset::Maya))
        {
            navigation = NavigationPreset::Maya;
        }

        DrawUiText(font, "GIZMO", 198.0f, 110.0f, 16.0f, Color{ 165, 182, 196, 255 });
        char gizmoSizeText[64] = {};
        std::snprintf(gizmoSizeText, sizeof(gizmoSizeText), "Size: %d%%", static_cast<int>(std::round(gTransformGizmoScale * 100.0f)));
        DrawUiText(font, gizmoSizeText, 198.0f, 139.0f, 15.0f, Color{ 205, 213, 220, 255 });
        if (DrawPanelButton(font, Rectangle{ 330.0f, 132.0f, 34.0f, 26.0f }, "-"))
        {
            gTransformGizmoScale = ClampFloat(gTransformGizmoScale - 0.1f, kMinTransformGizmoScale, kMaxTransformGizmoScale);
        }
        if (DrawPanelButton(font, Rectangle{ 372.0f, 132.0f, 34.0f, 26.0f }, "+"))
        {
            gTransformGizmoScale = ClampFloat(gTransformGizmoScale + 0.1f, kMinTransformGizmoScale, kMaxTransformGizmoScale);
        }
        if (DrawPanelButton(font, Rectangle{ 416.0f, 132.0f, 70.0f, 26.0f }, "Reset"))
        {
            gTransformGizmoScale = 1.0f;
        }

        DrawUiText(font, "HOTKEYS", 198.0f, 184.0f, 16.0f, Color{ 165, 182, 196, 255 });
        DrawUiText(font, "Q/W/E/R tools    Ctrl+O open FBX    Ctrl+R reload", 198.0f, 210.0f, 15.0f, Color{ 205, 213, 220, 255 });
        DrawUiText(font, "Ctrl+Z undo    Ctrl+Y redo    T textures    C channels", 198.0f, 236.0f, 15.0f, Color{ 205, 213, 220, 255 });
        DrawUiText(font, "Blender: MMB orbit, Alt snap, Shift+MMB pan, Wheel zoom", 198.0f, 262.0f, 15.0f, Color{ 205, 213, 220, 255 });
        DrawUiText(font, "Maya: Alt+LMB orbit, Shift snap, Alt+MMB pan, Alt+RMB/Wheel zoom", 198.0f, 288.0f, 15.0f, Color{ 205, 213, 220, 255 });
        DrawUiText(font, "Esc deselects    Ctrl+W closes tab    Ctrl+Q quits", 198.0f, 314.0f, 15.0f, Color{ 205, 213, 220, 255 });
    }
    else if (openMenu == OpenMenu::Help)
    {
        DrawRectangle(308, 29, 230, 38, Color{ 28, 31, 35, 245 });
        if (DrawMenuItem(font, Rectangle{ 308.0f, 29.0f, 230.0f, 30.0f }, "About openfbx"))
        {
            aboutRequested = true;
            openMenu = OpenMenu::None;
        }
    }
}

void DrawAboutWindow(Font font, bool& visible)
{
    if (!visible) return;

    const float w = 500.0f;
    const float h = 280.0f;
    const Rectangle bounds{ (static_cast<float>(GetScreenWidth()) - w) * 0.5f, (static_cast<float>(GetScreenHeight()) - h) * 0.5f, w, h };
    DrawRectangle(0, 0, GetScreenWidth(), GetScreenHeight(), Color{ 0, 0, 0, 90 });
    DrawRectangleRec(bounds, Color{ 22, 25, 29, 248 });
    DrawRectangleLinesEx(bounds, 1.0f, Color{ 86, 96, 108, 255 });

    const Rectangle closeButton{ bounds.x + bounds.width - 38.0f, bounds.y + 10.0f, 26.0f, 24.0f };
    if (DrawPanelButton(font, closeButton, "x") || IsKeyPressed(KEY_ESCAPE))
    {
        visible = false;
        return;
    }

    DrawUiText(font, "ABOUT", bounds.x + 16.0f, bounds.y + 14.0f, 17.0f, Color{ 165, 182, 196, 255 });
    DrawUiText(font, kAppName, bounds.x + 16.0f, bounds.y + 52.0f, 26.0f, RAYWHITE);

    char versionText[96] = {};
    std::snprintf(versionText, sizeof(versionText), "Version %s", kAppVersion);
    DrawUiText(font, versionText, bounds.x + 16.0f, bounds.y + 84.0f, 16.0f, Color{ 160, 205, 230, 255 });

    DrawUiText(font, "A focused FBX viewer and editor for inspecting, validating,", bounds.x + 16.0f, bounds.y + 122.0f, 16.0f, Color{ 205, 213, 220, 255 });
    DrawUiText(font, "adjusting, and exporting Autodesk FBX model data.", bounds.x + 16.0f, bounds.y + 146.0f, 16.0f, Color{ 205, 213, 220, 255 });

    DrawUiText(font, "Includes tools for hierarchy editing, materials, animation", bounds.x + 16.0f, bounds.y + 184.0f, 15.0f, Color{ 185, 195, 205, 255 });
    DrawUiText(font, "review, UV checks, skin weights, skeleton comparison, and", bounds.x + 16.0f, bounds.y + 207.0f, 15.0f, Color{ 185, 195, 205, 255 });
    DrawUiText(font, "transform editing.", bounds.x + 16.0f, bounds.y + 230.0f, 15.0f, Color{ 185, 195, 205, 255 });
    DrawUiText(font, "Built with raylib and the Autodesk FBX SDK.", bounds.x + 16.0f, bounds.y + 254.0f, 14.0f, Color{ 128, 136, 144, 255 });
}

void DrawSkeletonCompareResultWindow(Font font, bool& visible, bool compatible, const std::string& path, const std::string& result)
{
    if (!visible) return;

    const float w = 520.0f;
    const float h = 300.0f;
    const Rectangle bounds{ (static_cast<float>(GetScreenWidth()) - w) * 0.5f, (static_cast<float>(GetScreenHeight()) - h) * 0.5f, w, h };
    const Vector2 mouse = GetMousePosition();
    DrawRectangle(0, 0, GetScreenWidth(), GetScreenHeight(), Color{ 0, 0, 0, 90 });
    DrawRectangleRec(bounds, Color{ 22, 25, 29, 245 });
    DrawRectangleLinesEx(bounds, 1.0f, Color{ 86, 96, 108, 255 });

    DrawUiText(font, "SKELETON COMPATIBILITY", bounds.x + 16.0f, bounds.y + 14.0f, 17.0f, Color{ 165, 182, 196, 255 });
    const Rectangle closeButton{ bounds.x + bounds.width - 38.0f, bounds.y + 10.0f, 26.0f, 24.0f };
    if (DrawPanelButton(font, closeButton, "x") || IsKeyPressed(KEY_ESCAPE))
    {
        visible = false;
        return;
    }

    float y = bounds.y + 48.0f;
    DrawUiText(font, compatible ? "Compatible" : "Not compatible", bounds.x + 16.0f, y, 18.0f, compatible ? Color{ 150, 225, 170, 255 } : Color{ 255, 170, 135, 255 });
    y += 30.0f;
    DrawUiTextClipped(font, path.c_str(), bounds.x + 16.0f, y, 13.0f, bounds.width - 32.0f, Color{ 160, 205, 230, 255 });
    y += 34.0f;

    std::string remaining = result;
    while (!remaining.empty() && y < bounds.y + bounds.height - 22.0f)
    {
        const size_t newline = remaining.find('\n');
        const std::string line = newline == std::string::npos ? remaining : remaining.substr(0, newline);
        DrawUiTextClipped(font, line.c_str(), bounds.x + 16.0f, y, 15.0f, bounds.width - 32.0f, Color{ 205, 213, 220, 255 });
        y += 22.0f;
        if (newline == std::string::npos) break;
        remaining.erase(0, newline + 1);
    }

    if (CheckCollisionPointRec(mouse, bounds))
    {
        // Keep clicks inside the result dialog from interacting with UI below on the same frame.
    }
}

void CloseTab(std::vector<std::unique_ptr<ModelTab>>& tabs, int& activeTab, int tabIndex)
{
    if (tabIndex < 0 || tabIndex >= static_cast<int>(tabs.size())) return;

    UnloadPbrTextures(*tabs[static_cast<size_t>(tabIndex)]);
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

void DrawTimeline(Font font, ModelTab& tab, RenameEditor& renameEditor, bool& collapsed)
{
    LoadedFbxModel& loaded = tab.loaded;
    AnimationState& animation = tab.animation;
    const int width = GetScreenWidth();
    const int height = GetScreenHeight();
    const float panelHeight = collapsed ? kTimelineCollapsedHeight : kTimelinePanelHeight;
    const float panelY = static_cast<float>(height) - panelHeight;
    const float listWidth = 300.0f;
    const Vector2 mouse = GetMousePosition();
    const Rectangle toggleButton{ static_cast<float>(width) - 34.0f, panelY + 4.0f, 24.0f, 20.0f };

    DrawRectangle(0, static_cast<int>(panelY), width, static_cast<int>(panelHeight), Color{ 20, 22, 24, 238 });
    DrawLine(0, static_cast<int>(panelY), width, static_cast<int>(panelY), Color{ 76, 84, 92, 255 });
    DrawUiText(font, "ANIMATIONS", 12.0f, panelY + 7.0f, 16.0f, Color{ 165, 182, 196, 255 });
    if (DrawPanelButton(font, toggleButton, collapsed ? "^" : "v"))
    {
        collapsed = !collapsed;
        animation.scrubbing = false;
    }
    if (collapsed)
    {
        return;
    }

    DrawLine(static_cast<int>(listWidth), static_cast<int>(panelY), static_cast<int>(listWidth), height, Color{ 64, 70, 78, 255 });

    if (loaded.animations.empty())
    {
        DrawUiText(font, "No FBX animation stacks", 12.0f, panelY + 64.0f, 16.0f, Color{ 128, 136, 144, 255 });
    }

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
    DrawUiText(font, "Bind pose", noAnimationRow.x + 8.0f, noAnimationRow.y + 3.0f, 15.0f, noAnimationSelected ? RAYWHITE : Color{ 185, 194, 202, 255 });
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(mouse, noAnimationRow))
    {
        animation.clipIndex = -1;
        animation.time = 0.0f;
        animation.playing = false;
        animation.scrubbing = false;
    }

    const float rowHeight = 26.0f;
    const Rectangle clipListBounds{ 8.0f, panelY + 58.0f, listWidth - 16.0f, panelHeight - 62.0f };
    const int visibleRows = std::max(1, static_cast<int>(std::floor(clipListBounds.height / rowHeight)));
    const int maxClipScroll = std::max(0, static_cast<int>(loaded.animations.size()) - visibleRows);
    animation.clipScroll = ClampInt(animation.clipScroll, 0, maxClipScroll);
    if (!loaded.animations.empty() && CheckCollisionPointRec(mouse, clipListBounds))
    {
        const float wheel = GetMouseWheelMove();
        if (std::fabs(wheel) > 0.0f)
        {
            animation.clipScroll = ClampInt(animation.clipScroll - static_cast<int>(wheel), 0, maxClipScroll);
        }
    }

    BeginScissorMode(static_cast<int>(clipListBounds.x),
                     static_cast<int>(clipListBounds.y),
                     static_cast<int>(clipListBounds.width),
                     static_cast<int>(clipListBounds.height));
    const Rectangle contextMenuBounds{ animation.contextPosition.x, animation.contextPosition.y, 152.0f, 62.0f };
    const bool mouseOverContextMenu = animation.contextMenuOpen && CheckCollisionPointRec(mouse, contextMenuBounds);
    for (int visible = 0; visible < visibleRows; ++visible)
    {
        const int i = animation.clipScroll + visible;
        if (i >= static_cast<int>(loaded.animations.size())) break;

        const float rowY = clipListBounds.y + static_cast<float>(visible) * rowHeight;
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

        if (!mouseOverContextMenu && IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(mouse, row))
        {
            animation.clipIndex = i;
            animation.time = 0.0f;
            animation.playing = true;
            animation.scrubbing = false;
            animation.contextMenuOpen = false;
        }
        if (!mouseOverContextMenu && IsMouseButtonPressed(MOUSE_BUTTON_RIGHT) && CheckCollisionPointRec(mouse, row))
        {
            animation.contextClipIndex = i;
            animation.contextPosition = Vector2{
                ClampFloat(mouse.x, 4.0f, static_cast<float>(GetScreenWidth()) - 156.0f),
                ClampFloat(mouse.y, 4.0f, static_cast<float>(GetScreenHeight()) - 66.0f)
            };
            animation.contextMenuOpen = true;
            animation.contextMenuJustOpened = true;
        }

        char rowText[256] = {};
        std::snprintf(rowText, sizeof(rowText), "%s  %.2fs", loaded.animations[static_cast<size_t>(i)].name.c_str(), loaded.animations[static_cast<size_t>(i)].duration);
        DrawUiText(font, rowText, row.x + 8.0f, row.y + 3.0f, 15.0f, selected ? RAYWHITE : Color{ 185, 194, 202, 255 });
    }
    EndScissorMode();

    if (maxClipScroll > 0)
    {
        const Rectangle track{ listWidth - 10.0f, clipListBounds.y, 4.0f, clipListBounds.height };
        const float thumbHeight = std::max(18.0f, track.height * (static_cast<float>(visibleRows) / static_cast<float>(loaded.animations.size())));
        const float thumbTravel = std::max(1.0f, track.height - thumbHeight);
        const float thumbY = track.y + thumbTravel * (static_cast<float>(animation.clipScroll) / static_cast<float>(maxClipScroll));
        DrawRectangleRec(track, Color{ 42, 48, 54, 255 });
        DrawRectangleRec(Rectangle{ track.x, thumbY, track.width, thumbHeight }, Color{ 130, 145, 158, 255 });
    }

    if (animation.contextMenuOpen)
    {
        const Rectangle menu{ animation.contextPosition.x, animation.contextPosition.y, 152.0f, 62.0f };
        DrawRectangleRec(menu, Color{ 24, 27, 31, 248 });
        DrawRectangleLinesEx(menu, 1.0f, Color{ 84, 94, 104, 255 });
        const bool validContextClip = animation.contextClipIndex >= 0 && animation.contextClipIndex < static_cast<int>(loaded.animations.size());
        if (animation.contextMenuJustOpened)
        {
            animation.contextMenuJustOpened = false;
        }
        else
        {
            const Rectangle renameItem{ menu.x, menu.y, menu.width, 30.0f };
            const Rectangle deleteItem{ menu.x, menu.y + 30.0f, menu.width, 30.0f };
            if (validContextClip && DrawPanelButton(font, renameItem, "Rename"))
            {
                StartRenameAnimation(renameEditor, loaded, animation.contextClipIndex);
                animation.contextMenuOpen = false;
                animation.contextClipIndex = -1;
            }
            if (validContextClip && DrawPanelButton(font, deleteItem, "Delete"))
            {
                PushUndoSnapshot(tab);
                loaded.animations.erase(loaded.animations.begin() + animation.contextClipIndex);
                if (animation.clipIndex == animation.contextClipIndex)
                {
                    animation.clipIndex = -1;
                    animation.time = 0.0f;
                    animation.playing = false;
                    animation.scrubbing = false;
                }
                else if (animation.clipIndex > animation.contextClipIndex)
                {
                    --animation.clipIndex;
                }
                animation.contextMenuOpen = false;
                animation.contextClipIndex = -1;
                animation.clipScroll = ClampInt(animation.clipScroll, 0, std::max(0, static_cast<int>(loaded.animations.size()) - visibleRows));
            }
            else if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && !CheckCollisionPointRec(mouse, menu))
            {
                animation.contextMenuOpen = false;
            }
        }
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
    const Rectangle scrubHitbox{ scrub.x, scrub.y - 12.0f, scrub.width, scrub.height + 24.0f };

    DrawUiText(font, animation.playing ? "PLAYING  [SPACE]" : "PAUSED   [SPACE]", timelineX, panelY + 14.0f, 16.0f, Color{ 165, 182, 196, 255 });
    DrawRectangleRec(scrub, Color{ 58, 64, 70, 255 });

    if (clip && clip->duration > 0.0f)
    {
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(mouse, scrubHitbox))
        {
            animation.scrubbing = true;
        }
        if (!IsMouseButtonDown(MOUSE_BUTTON_LEFT))
        {
            animation.scrubbing = false;
        }
        if (animation.scrubbing)
        {
            const float alpha = ClampFloat((mouse.x - scrub.x) / scrub.width, 0.0f, 1.0f);
            animation.time = alpha * clip->duration;
            animation.playing = false;
        }

        const float progress = ClampFloat(animation.time / clip->duration, 0.0f, 1.0f);
        DrawRectangleRec(Rectangle{ scrub.x, scrub.y, scrub.width * progress, scrub.height }, Color{ 94, 156, 214, 255 });
        DrawRectangle(static_cast<int>(scrub.x + scrub.width * progress - 2.0f), static_cast<int>(scrub.y - 5.0f), 4, 24, Color{ 220, 232, 242, 255 });

        char timeText[128] = {};
        const int totalFrames = static_cast<int>(std::max(clip->frames.size(), clip->meshFrames.size()));
        const int currentFrame = totalFrames > 0 ? static_cast<int>(ClampFloat(std::round(progress * static_cast<float>(totalFrames - 1)), 0.0f, static_cast<float>(totalFrames - 1))) + 1 : 0;
        std::snprintf(timeText, sizeof(timeText), "%.2fs / %.2fs    frame: %d / %d", animation.time, clip->duration, currentFrame, totalFrames);
        DrawUiText(font, timeText, timelineX, timelineY + 28.0f, 16.0f, Color{ 190, 200, 210, 255 });
    }
    else
    {
        animation.scrubbing = false;
        DrawUiText(font, "Bind pose", timelineX, timelineY + 28.0f, 16.0f, Color{ 128, 136, 144, 255 });
    }
}
}

