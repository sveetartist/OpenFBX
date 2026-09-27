bool gUpdateWindowOpen = false;
std::future<openfbx::UpdateResult> gUpdateCheck;
openfbx::UpdateResult gUpdateResult;

void StartUpdateCheck()
{
    gUpdateWindowOpen = true;
    if (gUpdateCheck.valid()) return;
    gUpdateResult = {"Checking GitHub releases..."};
    try {
        gUpdateCheck = std::async(std::launch::async, [] { return openfbx::CheckForUpdates(kAppVersion); });
    } catch (...) { gUpdateResult = {"Could not start the update check. Please retry."}; }
}

void DrawUpdateWindow(Font font)
{
    if (gUpdateCheck.valid() && gUpdateCheck.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        try { gUpdateResult = gUpdateCheck.get(); }
        catch (...) { gUpdateResult = {"Could not check for updates. Please retry."}; }
    }
    if (!gUpdateWindowOpen) return;
    const float width = std::min(580.0f, static_cast<float>(GetScreenWidth()) - 16.0f);
    const Rectangle bounds{(GetScreenWidth() - width) * 0.5f, (GetScreenHeight() - 190.0f) * 0.5f, width, 190};
    DrawRectangle(0, 0, GetScreenWidth(), GetScreenHeight(), Color{0, 0, 0, 90});
    DrawRectangleRec(bounds, Color{22, 25, 29, 255});
    DrawRectangleLinesEx(bounds, 1, Color{86, 96, 108, 255});
    DrawUiText(font, "CHECK FOR UPDATES", bounds.x + 16, bounds.y + 16, 17, RAYWHITE);
    DrawUiText(font, TextFormat("Installed version: %s", kAppVersion), bounds.x + 16, bounds.y + 52, 16, RAYWHITE);
    DrawUiTextClipped(font, gUpdateResult.message.c_str(), bounds.x + 16, bounds.y + 84, 15, width - 32, RAYWHITE);
    if (DrawPanelButton(font, Rectangle{bounds.x + width - 38, bounds.y + 10, 26, 24}, "x") || IsKeyPressed(KEY_ESCAPE))
        gUpdateWindowOpen = false;
    if (!gUpdateCheck.valid() && DrawPanelButton(font, Rectangle{bounds.x + 16, bounds.y + 138, 100, 28}, "Check again")) StartUpdateCheck();
    if (DrawPanelButton(font, Rectangle{bounds.x + 128, bounds.y + 138, 180, 28}, gUpdateResult.available ? "Download update" : "View GitHub releases"))
        OpenURL("https://github.com/sveetartist/OpenFBX/releases/latest");
}

Rectangle GetPreferencesBounds()
{
    const float width = std::min(420.0f, GetScreenWidth() - 16.0f);
    return Rectangle{ std::max(8.0f, std::min(186.0f, GetScreenWidth() - width - 8.0f)),
                      29.0f, width, std::max(60.0f, std::min(452.0f, GetScreenHeight() - 37.0f)) };
}

bool DrawPreferences(Font font, Rectangle bounds, NavigationPreset& navigation)
{
    static float scroll = 0;
    constexpr float contentHeight = 436.0f;
    const float maxScroll = std::max(0.0f, contentHeight + 16.0f - bounds.height);
    if (CheckCollisionPointRec(GetMousePosition(), bounds)) scroll -= GetMouseWheelMove() * 32.0f;
    scroll = ClampFloat(scroll, 0, maxScroll);
    DrawRectangleRec(bounds, Color{28, 31, 35, 255});
    DrawRectangleLinesEx(bounds, 1, Color{70, 80, 90, 255});
    const Rectangle clip{bounds.x + 8, bounds.y + 8, bounds.width - 20, bounds.height - 16};
    BeginScissorMode(static_cast<int>(clip.x), static_cast<int>(clip.y), static_cast<int>(clip.width), static_cast<int>(clip.height));
    float y = clip.y - scroll;
    auto text = [&](const char* label, Color color = Color{185, 198, 210, 255})
    {
        DrawUiTextClipped(font, label, clip.x + 4, y + 4, 14, clip.width - 8, color);
        y += 24;
    };
    auto button = [&](Rectangle rect, const char* label)
    {
        openfbx::SetUiPointerBlocked(!CheckCollisionPointRec(GetMousePosition(), clip));
        const bool clicked = DrawPanelButton(font, rect, label);
        openfbx::SetUiPointerBlocked(false);
        return clicked;
    };
    auto numeric = [&](const char* label, auto& value, auto step, auto minimum, auto maximum, auto defaultValue, const char* units)
    {
        char line[96];
        std::snprintf(line, sizeof(line), "%s: %.1f%s", label, static_cast<double>(value), units);
        const float controlsX = clip.x + clip.width - 128;
        DrawUiTextClipped(font, line, clip.x + 4, y + 6, 14, clip.width - 138, RAYWHITE);
        if (button(Rectangle{controlsX, y, 28, 26}, "-")) value = std::max(minimum, value - step);
        if (button(Rectangle{controlsX + 32, y, 28, 26}, "+")) value = std::min(maximum, value + step);
        if (button(Rectangle{controlsX + 64, y, 64, 26}, "Reset")) value = defaultValue;
        y += 32;
    };
    text("NAVIGATION", RAYWHITE);
    const float half = (clip.width - 4) * 0.5f;
    if (button(Rectangle{clip.x, y, half, 26}, navigation == NavigationPreset::Blender ? "[x] Blender" : "Blender")) navigation = NavigationPreset::Blender;
    if (button(Rectangle{clip.x + half + 4, y, half, 26}, navigation == NavigationPreset::Maya ? "[x] Maya" : "Maya")) navigation = NavigationPreset::Maya;
    y += 32;
    text(navigation == NavigationPreset::Blender ? "Orbit: MMB | Pan: Shift+MMB" : "Orbit: Alt+LMB | Pan: Alt+MMB");
    text(navigation == NavigationPreset::Blender ? "Zoom: wheel | Snap: Alt while orbiting" : "Zoom: Alt+RMB / wheel | Snap: Shift");
    text("TRANSFORM GIZMO", RAYWHITE);
    numeric("Scale", gTransformGizmoScale, 0.1f, kMinTransformGizmoScale, kMaxTransformGizmoScale, 1.0f, "x");
    numeric("Thickness", gTransformGizmoLineWidth, 0.5f, kMinTransformGizmoLineWidth, kMaxTransformGizmoLineWidth, 6.0f, " px");
    text("BONE ORIENTATION", RAYWHITE);
    numeric("Scale", gBoneOrientationScale, 0.25f, kMinBoneOrientationScale, kMaxBoneOrientationScale, 1.0f, "x");
    numeric("Thickness", gBoneOrientationLineWidth, 0.5f, kMinBoneOrientationLineWidth, kMaxBoneOrientationLineWidth, 2.0f, " px");
    text("CHECKER TEXTURE", RAYWHITE);
    if (button(Rectangle{clip.x, y, clip.width, 26}, gUseColoredChecker ? "[x] Colored checker" : "[ ] Colored checker")) gUseColoredChecker = !gUseColoredChecker;
    y += 32;
    if (gUseColoredChecker)
    {
        const std::string color = "Color: " + std::to_string(gCheckerColor) + " / 5";
        if (button(Rectangle{clip.x, y, clip.width, 26}, color.c_str())) gCheckerColor = gCheckerColor % 5 + 1;
    }
    else DrawUiTextClipped(font, "Monochrome checker", clip.x + 4, y + 6, 14, clip.width - 8, Color{185, 198, 210, 255});
    y += 32;
    int& size = gUseColoredChecker ? gCheckerTextureSize : gCheckerSquares;
    const std::string sizeLabel = gUseColoredChecker ? "Resolution: " + std::to_string(size) + " px" : "Squares / tile: " + std::to_string(size);
    DrawUiTextClipped(font, sizeLabel.c_str(), clip.x + 4, y + 6, 14, clip.width - 138, RAYWHITE);
    const float controlsX = clip.x + clip.width - 128;
    if (button(Rectangle{controlsX, y, 28, 26}, "-")) size = std::max(gUseColoredChecker ? 512 : 2, size / 2);
    if (button(Rectangle{controlsX + 32, y, 28, 26}, "+")) size = std::min(gUseColoredChecker ? 4096 : 128, size * 2);
    if (button(Rectangle{controlsX + 64, y, 64, 26}, "Reset")) size = gUseColoredChecker ? 1024 : 16;
    y += 40;
    const bool edit = button(Rectangle{clip.x, y, clip.width, 28}, "Edit Hotkeys...");
    EndScissorMode();
    if (maxScroll > 0)
    {
        const float thumbHeight = clip.height * bounds.height / (contentHeight + 16);
        DrawRectangleRec(Rectangle{bounds.x + bounds.width - 6, clip.y, 3, clip.height}, Color{45, 52, 60, 255});
        DrawRectangleRec(Rectangle{bounds.x + bounds.width - 6, clip.y + (clip.height - thumbHeight) * scroll / maxScroll, 3, thumbHeight}, Color{140, 160, 180, 255});
    }
    if (edit) OpenHotkeyEditor();
    return edit;
}

void DrawMenuBar(Font font,
                 OpenMenu& openMenu,
                 bool& openRequested,
                 bool& closeTabRequested,
                 bool& reloadTabRequested,
                 bool& undoRequested,
                 bool& redoRequested,
                 bool& saveFbxRequested,
                 bool& saveAsFbxRequested,
                 bool& packageFbxRequested,
                 bool& extractFbxTexturesRequested,
                 bool& importAnimationsRequested,
                 bool& exportJsonRequested,
                 bool& compareFbxRequested,
                 bool& aboutRequested,
                 bool& quitRequested,
                 ViewMode& viewMode,
                 NavigationPreset& navigation,
                 VisibilityState& visibility,
                 bool canUndo,
                 bool canRedo,
                 ModelTab* activeTab)
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
        { "Debug", OpenMenu::Debug, Rectangle{ 308.0f, 3.0f, 66.0f, 22.0f } },
        { "Help", OpenMenu::Help, Rectangle{ 378.0f, 3.0f, 58.0f, 22.0f } }
    };

    const Vector2 mouse = GetMousePosition();
    for (const MenuButton& button : buttons)
    {
        const bool active = openMenu == button.menu;
        const bool hovered = CheckCollisionPointRec(mouse, button.bounds);
        DrawRectangleRec(button.bounds, active ? Color{ 52, 60, 68, 255 } : hovered ? Color{ 42, 46, 51, 255 } : Color{ 24, 26, 29, 255 });
        DrawUiText(font, button.label, button.bounds.x + 8.0f, button.bounds.y + 3.0f, 16.0f, RAYWHITE);
        if (hovered && openfbx::UiMouseButtonPressed(MOUSE_BUTTON_LEFT))
        {
            openMenu = active ? OpenMenu::None : button.menu;
        }
    }

    Rectangle openMenuBounds{};
    switch (openMenu)
    {
    case OpenMenu::File:
        openMenuBounds = Rectangle{ 8.0f, 29.0f, 300.0f, 278.0f };
        break;
    case OpenMenu::Edit:
        openMenuBounds = Rectangle{ 66.0f, 29.0f, 230.0f, 68.0f };
        break;
    case OpenMenu::View:
        openMenuBounds = Rectangle{ 124.0f, 29.0f, 230.0f, 458.0f };
        break;
    case OpenMenu::Preferences:
        openMenuBounds = GetPreferencesBounds();
        break;
    case OpenMenu::Debug:
        openMenuBounds = Rectangle{ 308.0f, 29.0f, 230.0f, 68.0f };
        break;
    case OpenMenu::Help:
        openMenuBounds = Rectangle{ 378.0f, 29.0f, 230.0f, 68.0f };
        break;
    case OpenMenu::None:
        break;
    }

    if (openfbx::UiMouseButtonPressed(MOUSE_BUTTON_LEFT) &&
        openMenu != OpenMenu::None &&
        mouse.y > menuHeight &&
        !CheckCollisionPointRec(mouse, openMenuBounds))
    {
        openMenu = OpenMenu::None;
    }

    if (openMenu == OpenMenu::File)
    {
        DrawRectangle(8, 29, 300, 278, Color{ 28, 31, 35, 245 });
        if (DrawMenuItem(font, Rectangle{ 8.0f, 29.0f, 300.0f, 30.0f }, HotkeyLabel("Open Model...", HotkeyAction::Open).c_str()))
        {
            openRequested = true;
            openMenu = OpenMenu::None;
        }
        if (DrawMenuItem(font, Rectangle{ 8.0f, 59.0f, 300.0f, 30.0f }, HotkeyLabel("Close Tab", HotkeyAction::Close).c_str()))
        {
            closeTabRequested = true;
            openMenu = OpenMenu::None;
        }
        if (DrawMenuItem(font, Rectangle{ 8.0f, 89.0f, 300.0f, 30.0f }, HotkeyLabel("Reload Tab", HotkeyAction::Reload).c_str()))
        {
            reloadTabRequested = true;
            openMenu = OpenMenu::None;
        }
        if (DrawMenuItem(font, Rectangle{ 8.0f, 119.0f, 300.0f, 30.0f }, HotkeyLabel("Save FBX", HotkeyAction::Save).c_str()))
        {
            saveFbxRequested = true;
            openMenu = OpenMenu::None;
        }
        if (DrawMenuItem(font, Rectangle{ 8.0f, 149.0f, 300.0f, 30.0f }, HotkeyLabel("Save As...", HotkeyAction::SaveAs).c_str()))
        {
            saveAsFbxRequested = true;
            openMenu = OpenMenu::None;
        }
        if (DrawMenuItem(font, Rectangle{ 8.0f, 179.0f, 300.0f, 30.0f }, "Package FBX (Embed Textures)"))
        {
            packageFbxRequested = true;
            openMenu = OpenMenu::None;
        }
        if (DrawMenuItem(font, Rectangle{ 8.0f, 209.0f, 300.0f, 30.0f }, "Extract Textures (Unpack FBX)"))
        {
            extractFbxTexturesRequested = true;
            openMenu = OpenMenu::None;
        }
        if (DrawMenuItem(font, Rectangle{ 8.0f, 239.0f, 300.0f, 30.0f }, "Import Animations..."))
        {
            importAnimationsRequested = true;
            openMenu = OpenMenu::None;
        }
        if (DrawMenuItem(font, Rectangle{ 8.0f, 269.0f, 300.0f, 30.0f }, HotkeyLabel("Exit", HotkeyAction::Quit).c_str()))
        {
            quitRequested = true;
            openMenu = OpenMenu::None;
        }
    }
    else if (openMenu == OpenMenu::Edit)
    {
        DrawRectangle(66, 29, 230, 68, Color{ 28, 31, 35, 245 });
        if (DrawMenuItem(font, Rectangle{ 66.0f, 29.0f, 230.0f, 30.0f }, canUndo ? HotkeyLabel("Undo", HotkeyAction::Undo).c_str() : HotkeyLabel("Undo", HotkeyAction::Undo).c_str(), false))
        {
            if (canUndo) undoRequested = true;
            openMenu = OpenMenu::None;
        }
        if (DrawMenuItem(font, Rectangle{ 66.0f, 59.0f, 230.0f, 30.0f }, canRedo ? HotkeyLabel("Redo", HotkeyAction::Redo).c_str() : HotkeyLabel("Redo", HotkeyAction::Redo).c_str(), false))
        {
            if (canRedo) redoRequested = true;
            openMenu = OpenMenu::None;
        }
        if (!canUndo)
        {
            DrawRectangleRec(Rectangle{ 66.0f, 29.0f, 230.0f, 30.0f }, Color{ 28, 31, 35, 160 });
            DrawUiText(font, HotkeyLabel("Undo", HotkeyAction::Undo).c_str(), 76.0f, 34.0f, 16.0f, Color{ 105, 115, 124, 255 });
        }
        if (!canRedo)
        {
            DrawRectangleRec(Rectangle{ 66.0f, 59.0f, 230.0f, 30.0f }, Color{ 28, 31, 35, 160 });
            DrawUiText(font, HotkeyLabel("Redo", HotkeyAction::Redo).c_str(), 76.0f, 64.0f, 16.0f, Color{ 105, 115, 124, 255 });
        }
    }
    else if (openMenu == OpenMenu::View)
    {
        DrawRectangle(124, 29, 230, 458, Color{ 28, 31, 35, 245 });
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
        if (DrawMenuItem(font, Rectangle{ 124.0f, 179.0f, 230.0f, 30.0f }, "Checker", viewMode == ViewMode::Checker))
        {
            viewMode = ViewMode::Checker;
        }
        if (DrawMenuItem(font, Rectangle{ 124.0f, 217.0f, 230.0f, 30.0f }, visibility.geometry ? HotkeyLabel("[x] Geometry", HotkeyAction::Geometry).c_str() : HotkeyLabel("[ ] Geometry", HotkeyAction::Geometry).c_str()))
        {
            visibility.geometry = !visibility.geometry;
        }
        if (DrawMenuItem(font, Rectangle{ 124.0f, 247.0f, 230.0f, 30.0f }, visibility.textures ? "[x] Textures" : "[ ] Textures"))
        {
            visibility.textures = !visibility.textures;
        }
        if (DrawMenuItem(font, Rectangle{ 124.0f, 277.0f, 230.0f, 30.0f }, visibility.backfaceCulling ? "[x] Backface Culling" : "[ ] Backface Culling"))
        {
            visibility.backfaceCulling = !visibility.backfaceCulling;
        }
        if (DrawMenuItem(font, Rectangle{ 124.0f, 307.0f, 230.0f, 30.0f }, visibility.bones ? HotkeyLabel("[x] Bones", HotkeyAction::Bones).c_str() : HotkeyLabel("[ ] Bones", HotkeyAction::Bones).c_str()))
        {
            visibility.bones = !visibility.bones;
        }
        if (DrawMenuItem(font, Rectangle{ 124.0f, 337.0f, 230.0f, 30.0f }, visibility.boneRotations ? HotkeyLabel("[x] Bone Orientation", HotkeyAction::BoneAxes).c_str() : HotkeyLabel("[ ] Bone Orientation", HotkeyAction::BoneAxes).c_str()))
        {
            visibility.boneRotations = !visibility.boneRotations;
        }
        if (DrawMenuItem(font, Rectangle{ 124.0f, 367.0f, 230.0f, 30.0f }, visibility.empties ? "[x] Empties" : "[ ] Empties"))
        {
            visibility.empties = !visibility.empties;
        }
        if (DrawMenuItem(font, Rectangle{ 124.0f, 397.0f, 230.0f, 30.0f }, visibility.skinWeights ? "[x] Skin Weights" : "[ ] Skin Weights"))
        {
            visibility.skinWeights = !visibility.skinWeights;
        }
        if (DrawMenuItem(font, Rectangle{ 124.0f, 427.0f, 230.0f, 30.0f }, HotkeyLabel("Hide / Show Selected", HotkeyAction::Hide).c_str()) && activeTab)
        {
            ToggleSelectedNodeVisibility(*activeTab);
        }
        if (DrawMenuItem(font, Rectangle{ 124.0f, 457.0f, 230.0f, 30.0f }, HotkeyLabel("Show All", HotkeyAction::ShowAll).c_str()))
        {
            if (activeTab) ShowAllNodes(*activeTab);
            visibility.geometry = true;
            visibility.bones = true;
            visibility.empties = true;
        }
    }
    else if (openMenu == OpenMenu::Preferences)
    {
        if (DrawPreferences(font, openMenuBounds, navigation)) openMenu = OpenMenu::None;
    }
    else if (openMenu == OpenMenu::Debug)
    {
        DrawRectangle(308, 29, 230, 68, Color{ 28, 31, 35, 245 });
        if (DrawMenuItem(font, Rectangle{ 308.0f, 29.0f, 230.0f, 30.0f }, "Compare FBX..."))
        {
            compareFbxRequested = true;
            openMenu = OpenMenu::None;
        }
        if (DrawMenuItem(font, Rectangle{ 308.0f, 59.0f, 230.0f, 30.0f }, "Export JSON"))
        {
            exportJsonRequested = true;
            openMenu = OpenMenu::None;
        }
    }
    else if (openMenu == OpenMenu::Help)
    {
        DrawRectangle(378, 29, 230, 68, Color{ 28, 31, 35, 245 });
        if (DrawMenuItem(font, Rectangle{378.0f, 59.0f, 230.0f, 30.0f}, "Check for updates"))
        {
            StartUpdateCheck();
            openMenu = OpenMenu::None;
        }
        if (DrawMenuItem(font, Rectangle{ 378.0f, 29.0f, 230.0f, 30.0f }, "About openfbx"))
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

        if (hovered && openfbx::UiMouseButtonPressed(MOUSE_BUTTON_LEFT) && !CheckCollisionPointRec(mouse, close))
        {
            activeTab = i;
        }
        if ((hovered && openfbx::UiMouseButtonPressed(MOUSE_BUTTON_MIDDLE)) ||
            (CheckCollisionPointRec(mouse, close) && openfbx::UiMouseButtonPressed(MOUSE_BUTTON_LEFT)))
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
    if (HotkeyPressed(HotkeyAction::Play) && !loaded.animations.empty())
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

void DrawTimeline(Font font, ModelTab& tab, RenameEditor& renameEditor, bool& collapsed, float bottomOffset)
{
    LoadedFbxModel& loaded = tab.loaded;
    AnimationState& animation = tab.animation;
    const int width = GetScreenWidth();
    const int height = GetScreenHeight() - static_cast<int>(bottomOffset);
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
    if (openfbx::UiMouseButtonPressed(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(mouse, noAnimationRow))
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
        const float wheel = openfbx::UiMouseWheelMove();
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

        if (!mouseOverContextMenu && openfbx::UiMouseButtonPressed(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(mouse, row))
        {
            animation.clipIndex = i;
            animation.time = 0.0f;
            animation.playing = true;
            animation.scrubbing = false;
            animation.contextMenuOpen = false;
        }
        if (!mouseOverContextMenu && openfbx::UiMouseButtonPressed(MOUSE_BUTTON_RIGHT) && CheckCollisionPointRec(mouse, row))
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
            else if (openfbx::UiMouseButtonPressed(MOUSE_BUTTON_LEFT) && !CheckCollisionPointRec(mouse, menu))
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
        if (openfbx::UiMouseButtonPressed(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(mouse, scrubHitbox))
        {
            animation.scrubbing = true;
        }
        if (!openfbx::UiMouseButtonDown(MOUSE_BUTTON_LEFT))
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

