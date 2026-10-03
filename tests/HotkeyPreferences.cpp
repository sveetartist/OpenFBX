#include <stdexcept>
#include "editor/OpenFbxApp.cpp"

static void Check(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
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
                Rectangle bounds = GetPreferencesBounds();
                if (fullPreferences) bounds.height = static_cast<float>(GetScreenHeight()) - 37;
                DrawPreferences(font, bounds, navigation);
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
        SetWindowSize(800, 800);
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
