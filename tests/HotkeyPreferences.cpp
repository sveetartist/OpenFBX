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
        Check(SaveHotkeys(edited, path), "Settings must save");
        LoadHotkeys(path);
        Check(gHotkeys[move].key == KEY_F6 && gHotkeys[move].modifiers == 3, "Settings must survive reload");
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
        InitWindow(640, 360, "Preferences regression");
        Font font = LoadTechnicalFont();
        NavigationPreset navigation = NavigationPreset::Blender;
        const auto checkBounds = []()
        {
            const auto bounds = GetPreferencesBounds();
            Check(bounds.x >= 0 && bounds.y >= 0 && bounds.x + bounds.width <= GetScreenWidth() &&
                  bounds.y + bounds.height <= GetScreenHeight(), "Preferences must stay inside the window");
        };
        const auto capture = [&](const char* name, bool hotkeys)
        {
            RenderTexture2D target = LoadRenderTexture(GetScreenWidth(), GetScreenHeight());
            BeginTextureMode(target);
            ClearBackground(Color{40, 44, 50, 255});
            if (hotkeys) { gHotkeyEditorOpen = true; DrawHotkeyEditor(font); }
            else DrawPreferences(font, GetPreferencesBounds(), navigation);
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
        SetWindowSize(800, 600);
        checkBounds();
        capture("preferences.png", false);
        capture("hotkeys.png", true);
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
