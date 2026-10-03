#include <stdexcept>
#include "editor/OpenFbxApp.cpp"

static void Check(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

static void TestBoneOrientationPreview()
{
    gPreferencesWindow = PreferencesWindowState{};
    gPreferencesWindow.open = true;
    VisibilityState visibility;
    Check(!ShouldDrawBoneOrientations(visibility, 100), "Disabled bone orientation must start hidden");
    PreviewBoneOrientation(100);
    Check(ShouldDrawBoneOrientations(visibility, 100.5) && !visibility.boneRotations,
          "Adjusting orientation must preview it without changing visibility");
    visibility.bones = false;
    Check(ShouldDrawBoneOrientations(visibility, 100.5) && !visibility.bones,
          "Orientation preview must also work when the bone display is hidden");
    PreviewBoneOrientation(100.75);
    Check(ShouldDrawBoneOrientations(visibility, 101.5), "Continued adjustments must extend the preview");
    Check(!ShouldDrawBoneOrientations(visibility, 101.75), "Orientation preview must expire after adjustments stop");
    PreviewBoneOrientation(102);
    gPreferencesWindow.open = false;
    Check(!ShouldDrawBoneOrientations(visibility, 102.5), "Closing Preferences must stop its preview");
    visibility.bones = visibility.boneRotations = true;
    Check(ShouldDrawBoneOrientations(visibility, 104), "Enabled bone orientation must remain visible after preview ends");
    visibility.bones = false;
    Check(!ShouldDrawBoneOrientations(visibility, 104), "Normal bone visibility must apply outside the preview");
    gPreferencesWindow = PreferencesWindowState{};
}

static void TestPreferencesWindowInteraction()
{
    SetWindowSize(1280, 800);
    gPreferencesWindow = PreferencesWindowState{};
    gPreferencesWindow.open = true;
    gPreferencesWindow.bounds = Rectangle{200, 100, 420, 480};
    Check(!UpdatePreferencesWindowInput(Vector2{900, 400}, true, true, true, false) && gPreferencesWindow.open,
          "Viewport clicks must remain available without closing Preferences");
    Check(UpdatePreferencesWindowInput(Vector2{220, 116}, true, true, true, false) && gPreferencesWindow.dragging,
          "Dragging the title bar must capture input");
    UpdatePreferencesWindowInput(Vector2{470, 196}, false, true, true, false);
    Check(gPreferencesWindow.bounds.x == 450 && gPreferencesWindow.bounds.y == 180,
          "Preferences must follow title-bar dragging");
    Check(UpdatePreferencesWindowInput(Vector2{-100, -100}, false, false, true, false) && !gPreferencesWindow.dragging,
          "A drag release outside Preferences must not reach the viewport");
    Check(!UpdatePreferencesWindowInput(Vector2{900, 400}, false, false, false, false),
          "The viewport must accept input again after a Preferences drag ends");

    gPreferencesWindow.bounds = Rectangle{200, 100, 420, 480};
    Check(UpdatePreferencesWindowInput(Vector2{618, 578}, true, true, true, false) && gPreferencesWindow.resizing,
          "The bottom-right resize handle must capture input");
    UpdatePreferencesWindowInput(Vector2{778, 698}, false, true, true, false);
    Check(gPreferencesWindow.bounds.width == 580 && gPreferencesWindow.bounds.height == 600,
          "Preferences must resize with its corner handle");
    UpdatePreferencesWindowInput(Vector2{250, 150}, false, true, true, false);
    Check(gPreferencesWindow.bounds.width == 320 && gPreferencesWindow.bounds.height == 180,
          "Preferences resizing must respect the minimum readable size");
    UpdatePreferencesWindowInput(Vector2{900, 400}, false, false, false, false);

    Check(!UpdatePreferencesWindowInput(Vector2{220, 116}, true, true, true, true) && !gPreferencesWindow.dragging,
          "Other dialogs must block Preferences input");
    UpdatePreferencesWindowInput(Vector2{220, 116}, true, true, true, false);
    UpdatePreferencesWindowInput(Vector2{2000, 2000}, false, true, true, false);
    const auto moved = GetPreferencesBounds();
    Check(moved.x + moved.width <= GetScreenWidth() - 8 && moved.y + moved.height <= GetScreenHeight() - 8,
          "Dragging Preferences must keep the window reachable");
    UpdatePreferencesWindowInput(Vector2{-200, -200}, false, true, true, false);
    Check(GetPreferencesBounds().x == 8 && GetPreferencesBounds().y == 64,
          "Preferences must stay below the menu and tabs when dragged offscreen");
    UpdatePreferencesWindowInput(Vector2{900, 400}, false, false, false, false);
    const auto close = GetPreferencesCloseRect(GetPreferencesBounds());
    PreviewBoneOrientation(100);
    PreviewTransformGizmo(100);
    PreviewChecker(100);
    Check(UpdatePreferencesWindowInput(Vector2{close.x + 5, close.y + 5}, true, true, true, false) &&
          !gPreferencesWindow.open && gPreferencesWindow.boneOrientationPreviewUntil == 0 &&
          gPreferencesWindow.gizmoPreviewUntil == 0 && gPreferencesWindow.checkerPreviewUntil == 0,
          "Closing Preferences must consume the close click");
    Check(UpdatePreferencesWindowInput(Vector2{900, 400}, false, false, true, false),
          "Closing Preferences must also consume the release frame");
    Check(!UpdatePreferencesWindowInput(Vector2{900, 400}, false, false, false, false),
          "Closed Preferences must release input capture");
    gPreferencesWindow.bounds = Rectangle{1000, 900, 600, 700};
    SetWindowSize(640, 360);
    const auto resized = GetPreferencesBounds();
    Check(resized.x >= 8 && resized.y >= 64 && resized.x + resized.width <= 632 && resized.y + resized.height <= 352,
          "Preferences must stay reachable after the application window shrinks");
    gPreferencesWindow = PreferencesWindowState{};
}

static void TestViewportPreferencesPreviews()
{
    gPreferencesWindow = PreferencesWindowState{};
    gPreferencesWindow.open = true;
    const TransformTool selectedTool = TransformTool::Select;
    PreviewPreference(PreferencePreview::Gizmo, 100);
    Check(GetViewportPreviewTool(selectedTool, 100.5) == TransformTool::Move && selectedTool == TransformTool::Select,
          "Gizmo preview must show handles without activating the Move tool");
    for (const TransformTool tool : {TransformTool::Move, TransformTool::Rotate, TransformTool::Scale})
        Check(GetViewportPreviewTool(tool, 100.5) == tool, "Gizmo adjustments must preview the current transform tool");
    Check(GetViewportPreviewTool(selectedTool, 101) == selectedTool, "Gizmo preview must expire");
    PreviewTransformGizmo(102, TransformTool::Rotate);
    Check(GetViewportPreviewTool(selectedTool, 102.5) == TransformTool::Rotate,
          "Rotation ring settings must preview the rotation gizmo");
    PreviewPreference(PreferencePreview::Gizmo, 102.75);
    Check(GetViewportPreviewTool(selectedTool, 103.5) == TransformTool::Move,
          "Size adjustments must replace the rotation ring preview and extend its duration");
    for (const ViewMode mode : {ViewMode::Shaded, ViewMode::ShadedWireframe, ViewMode::Wireframe,
                                ViewMode::MaterialColors, ViewMode::UvIslands, ViewMode::Checker})
    {
        PreviewPreference(PreferencePreview::Checker, 100);
        Check(GetViewportPreviewMode(mode, 100.5) == ViewMode::Checker, "Checker adjustments must preview on the model");
        Check(GetViewportPreviewMode(mode, 101) == mode, "Checker preview must restore the current viewport mode");
    }
    PreviewChecker(102);
    PreviewChecker(102.75);
    Check(IsCheckerPreviewActive(103.5), "Continued checker adjustments must extend the preview");
    gPreferencesWindow.open = false;
    Check(!IsCheckerPreviewActive(103.5) && GetViewportPreviewTool(selectedTool, 103.5) == selectedTool,
          "Closing Preferences must end gizmo and checker previews");
    gPreferencesWindow.open = true;
    ClearPreferencesPreviews();
    Check(!IsCheckerPreviewActive(103) && !IsTransformGizmoPreviewActive(103), "Reopening Preferences must not resume old previews");

    ModelTab tab;
    tab.loaded.bounds = BoundingBox{Vector3{-1, -1, -1}, Vector3{1, 1, 1}};
    tab.orbit.camera = Camera3D{Vector3{3, 3, 3}, Vector3{}, Vector3{0, 1, 0}, 45, CAMERA_PERSPECTIVE};
    const RenderTexture2D target = LoadRenderTexture(256, 256);
    const auto countGizmoPixels = [&](bool preview, float scale)
    {
        gTransformGizmoScale = scale;
        BeginTextureMode(target);
        ClearBackground(BLACK);
        BeginMode3D(tab.orbit.camera);
        DrawTransformGizmo(tab, TransformTool::Move, {}, GizmoOrientation::Global, false, false, preview);
        EndMode3D();
        EndTextureMode();
        Image image = LoadImageFromTexture(target.texture);
        int pixels = 0;
        for (int y = 0; y < image.height; ++y)
        for (int x = 0; x < image.width; ++x)
        {
            const Color color = GetImageColor(image, x, y);
            if (color.r || color.g || color.b) ++pixels;
        }
        UnloadImage(image);
        return pixels;
    };
    Check(countGizmoPixels(false, 1) == 0, "Normal gizmo drawing must still require a selection");
    const int small = countGizmoPixels(true, 0.5f);
    Check(small > 20 && countGizmoPixels(true, 2) > small,
          "Gizmo preview must appear without a selection and respond to its scale setting");
    Check(tab.selectedNode == -1 && tab.selectedNodes.empty(), "Gizmo preview must not select a model node");
    gTransformGizmoScale = 1;
    UnloadRenderTexture(target);
    gPreferencesWindow = PreferencesWindowState{};
}

int main()
{
    try
    {
        gHotkeyEditorOpen = true;
        Check(!HotkeyPressed(HotkeyAction::Quit), "Editor must suppress application shortcuts");
        gHotkeyEditorOpen = false;
        for (int i = 0; i < static_cast<int>(kDefaultHotkeys.size()); ++i)
            Check(HotkeyConflict(kDefaultHotkeys, i, kDefaultHotkeys[i].key, kDefaultHotkeys[i].modifiers) < 0,
                  "Defaults must be conflict-free");
        const auto temp = std::filesystem::temp_directory_path() / "openfbx-hotkey-regression";
        const auto path = temp / "hotkeys.txt";
        auto edited = kDefaultHotkeys;
        const int move = static_cast<int>(HotkeyAction::Move);
        edited[move].key = KEY_F6;
        edited[move].modifiers = 3;
        Check(HotkeyMatches(edited[move], KEY_F6, 3), "Modified key must match");
        Check(!HotkeyMatches(edited[move], KEY_F6, 1), "Modifiers must match exactly");
        Check(!HotkeyMatches(edited[move], KEY_W, 0), "Old key must stop matching");
        Check(HotkeyText(edited[move]) == "Ctrl+Shift+F6", "Shortcut label must reflect modifiers");
        Check(HotkeyConflict(edited, move, KEY_S, 1) == static_cast<int>(HotkeyAction::Save), "Duplicate binding must be rejected");
        Check(HotkeyConflict(edited, move, KEY_F6, 3) == -1, "Binding must not conflict with itself");
        Check(!ValidHotkey(KEY_ESCAPE, 0) && !ValidHotkey(KEY_LEFT_SHIFT, 0), "Cancel and modifier keys are reserved");
        const int weights = static_cast<int>(HotkeyAction::SkinWeights);
        Check(HotkeyMatches(kDefaultHotkeys[weights], KEY_S, 0), "S must toggle skin weights");
        Check(!HotkeyMatches(kDefaultHotkeys[weights], KEY_S, 1), "Ctrl+S must remain Save");
        Check(HotkeySearchMatches(edited[weights], "SKIN WEIGHTS"), "Search must match action names ignoring case");
        Check(HotkeySearchMatches(edited[weights], "s"), "Search must match the skin weight shortcut");
        Check(HotkeySearchMatches(edited[move], "ctrl+shift+f6") &&
              HotkeySearchMatches(edited[move], "Shift F6"), "Search must match modified shortcuts");
        Check(!HotkeySearchMatches(edited[move], "Ctrl+S"), "Search returned a different shortcut");
        Check(HotkeySearchMatches(edited[move], "") && !HotkeySearchMatches(edited[move], "no such action"),
              "Empty and unmatched searches must behave correctly");
        edited[weights].key = KEY_F8;
        edited[weights].modifiers = 4;
        Check(SaveHotkeys(edited, path), "Settings must save");
        LoadHotkeys(path);
        Check(gHotkeys[move].key == KEY_F6 && gHotkeys[move].modifiers == 3, "Settings must survive reload");
        Check(gHotkeys[weights].key == KEY_F8 && gHotkeys[weights].modifiers == 4,
              "Skin weight shortcut must survive reload");
        edited[move].key = KEY_NULL;
        Check(SaveHotkeys(edited, path), "Unbound settings must save");
        LoadHotkeys(path);
        Check(!HotkeyMatches(gHotkeys[move], KEY_NULL, 3), "Unbound shortcuts must not trigger");
        edited[move].key = KEY_S;
        edited[move].modifiers = 1;
        Check(SaveHotkeys(edited, path), "Conflict fixture must save");
        LoadHotkeys(path);
        Check(gHotkeys[move].key == KEY_W, "Conflicting settings must fall back to defaults");
        std::filesystem::remove(path);
        LoadHotkeys(path);
        Check(gHotkeys[move].key == KEY_W, "Missing settings must use defaults");

        SetConfigFlags(FLAG_WINDOW_HIDDEN);
        const auto viewportPath = temp / "viewport.txt";
        gShowViewportFps = true;
        Check(SaveViewportSettings(viewportPath), "FPS preference must save");
        gShowViewportFps = false;
        LoadViewportSettings(viewportPath);
        Check(gShowViewportFps, "FPS preference must survive reload");
        gShowViewportFps = false;
        Check(SaveViewportSettings(viewportPath), "Disabled FPS preference must save");
        gShowViewportFps = true;
        LoadViewportSettings(viewportPath);
        Check(!gShowViewportFps, "Explicitly disabled FPS preference must survive reload");
        gBoneDisplayScale = 0.3f;
        Check(SaveViewportSettings(viewportPath), "Bone display size must save");
        gBoneDisplayScale = 1.0f;
        LoadViewportSettings(viewportPath);
        Check(std::fabs(gBoneDisplayScale - 0.3f) < 0.0001f && !gShowViewportFps,
              "Smaller bone display size must survive reload alongside existing preferences");
        {
            std::ofstream settings(viewportPath);
            settings << "bone_display_size_percent 0\nshow_fps 0\nshow_view_rotation_ring 0\n";
        }
        LoadViewportSettings(viewportPath);
        Check(gBoneDisplayScale == 1.0f && !gShowViewportFps && !gShowViewRotationRing,
              "Invalid bone display size must use the default without disrupting other preferences");
        std::filesystem::remove(viewportPath);
        LoadViewportSettings(viewportPath);
        Check(gShowViewportFps, "FPS display must default to on");
        Check(gBoneDisplayScale == 1.0f, "Missing bone display preference must preserve the original size");
        InitWindow(640, 360, "Preferences regression");
        TestBoneOrientationPreview();
        TestViewportPreferencesPreviews();
        TestPreferencesWindowInteraction();
        Font font = LoadTechnicalFont();
        NavigationPreset navigation = NavigationPreset::Blender;
        const auto checkBounds = []()
        {
            const auto bounds = GetPreferencesBounds();
            Check(bounds.x >= 0 && bounds.y >= 0 && bounds.x + bounds.width <= GetScreenWidth() &&
                  bounds.y + bounds.height <= GetScreenHeight(), "Preferences must stay inside the window");
        };
        const auto capture = [&](const char* name, bool hotkeys, bool fullPreferences = false)
        {
            RenderTexture2D target = LoadRenderTexture(GetScreenWidth(), GetScreenHeight());
            BeginTextureMode(target);
            ClearBackground(Color{40, 44, 50, 255});
            if (hotkeys) { gHotkeyEditorOpen = true; DrawHotkeyEditor(font); }
            else
            {
                const Rectangle previousBounds = gPreferencesWindow.bounds;
                gPreferencesWindow.open = true;
                if (fullPreferences) gPreferencesWindow.bounds = Rectangle{186, 64, 420, static_cast<float>(GetScreenHeight()) - 72};
                DrawPreferencesWindow(font, navigation);
                gPreferencesWindow.bounds = previousBounds;
            }
            EndTextureMode();
            Image image = LoadImageFromTexture(target.texture);
            ImageFlipVertical(&image);
            Check(ExportImage(image, (temp / name).string().c_str()), "UI capture must save");
            UnloadImage(image);
            UnloadRenderTexture(target);
        };
        checkBounds();
        capture("preferences-small.png", false);
        capture("hotkeys-small.png", true);
        gHotkeyDraft = kDefaultHotkeys;
        gHotkeySearch = "skin weights";
        gHotkeyScroll = 100;
        capture("hotkeys-search-small.png", true);
        Check(gHotkeyScroll == 0, "Filtered list must clamp its scroll offset");
        gHotkeySearch = "Ctrl+S";
        capture("hotkeys-search-shortcut.png", true);
        gHotkeySearch = "no such action";
        capture("hotkeys-search-empty.png", true);
        gHotkeySearch.clear();
        SetWindowSize(800, 600);
        checkBounds();
        capture("preferences.png", false);
        capture("hotkeys.png", true);
        gPreferencesWindow.bounds = Rectangle{350, 96, 420, 450};
        capture("preferences-moved.png", false);
        gPreferencesWindow = PreferencesWindowState{};
        SetWindowSize(800, 840);
        gUseColoredChecker = false;
        capture("preferences-sliders.png", false, true);
        gUseColoredChecker = true;
        capture("preferences-sliders-colored.png", false, true);
        gUseColoredChecker = false;
        SetWindowSize(800, 600);
        // Capture the overlays with the toolbar and tab strip drawn as in the app.
        gShowViewportFps = true;
        RenderTexture2D overlays = LoadRenderTexture(800, 600);
        BeginTextureMode(overlays);
        ClearBackground(Color{40, 44, 50, 255});
        DrawTransformToolbar(font, TransformTool::Move, GizmoOrientation::Global, false, 260);
        DrawOrientationGizmo(font, Camera3D{Vector3{0, 0, 5}, Vector3{}, Vector3{0, 1, 0}, 45, CAMERA_PERSPECTIVE});
        DrawViewportFps(font);
        std::vector<std::unique_ptr<ModelTab>> tabs;
        int activeTab = -1;
        DrawTabs(font, tabs, activeTab);
        EndTextureMode();
        Image overlayImage = LoadImageFromTexture(overlays.texture);
        ImageFlipVertical(&overlayImage);
        Check(ExportImage(overlayImage, (temp / "viewport-overlays.png").string().c_str()), "Overlay capture must save");
        UnloadImage(overlayImage);
        UnloadRenderTexture(overlays);
        if (font.texture.id != GetFontDefault().texture.id) UnloadFont(font);
        CloseWindow();
        std::cout << "Hotkey and Preferences checks passed. Captures: " << temp << '\n';
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
