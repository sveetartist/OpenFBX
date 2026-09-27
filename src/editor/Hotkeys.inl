enum class HotkeyAction
{
    Open, Close, Reload, Save, SaveAs, Undo, Redo, RedoAlternate, Quit,
    Select, Move, Rotate, Scale, Isolate, Focus, Hide, ShowAll,
    View, Channel, Shaded, Textures, Geometry, Bones, BoneAxes, Play, Count
};

struct HotkeyBinding
{
    const char* id;
    const char* label;
    int key;
    int modifiers; // Ctrl = 1, Shift = 2, Alt = 4.
};

const std::array<HotkeyBinding, static_cast<size_t>(HotkeyAction::Count)> kDefaultHotkeys{{
    {"open", "Open model", KEY_O, 1}, {"close", "Close tab", KEY_W, 1},
    {"reload", "Reload model", KEY_R, 1}, {"save", "Save FBX", KEY_S, 1},
    {"save_as", "Save as", KEY_S, 3}, {"undo", "Undo", KEY_Z, 1},
    {"redo", "Redo", KEY_Y, 1}, {"redo_alt", "Redo (alternate)", KEY_Z, 3},
    {"quit", "Quit", KEY_Q, 1}, {"select", "Select tool", KEY_Q, 0},
    {"move", "Move tool", KEY_W, 0}, {"rotate", "Rotate tool", KEY_E, 0},
    {"scale", "Scale tool", KEY_R, 0}, {"isolate", "Isolate selection", KEY_Q, 4},
    {"focus", "Focus selection", KEY_F, 0}, {"hide", "Hide / show selection", KEY_H, 0},
    {"show_all", "Show all", KEY_H, 4}, {"view", "Cycle view mode", KEY_V, 0},
    {"channel", "Cycle material channel", KEY_C, 0}, {"shaded", "Shaded view", KEY_M, 0},
    {"textures", "Toggle textures", KEY_T, 0}, {"geometry", "Toggle geometry", KEY_G, 0},
    {"bones", "Toggle bones", KEY_B, 0}, {"bone_axes", "Toggle bone orientation", KEY_O, 0},
    {"play", "Play / pause", KEY_SPACE, 0}
}};
auto gHotkeys = kDefaultHotkeys;
auto gHotkeyDraft = kDefaultHotkeys;
bool gHotkeyEditorOpen = false;
bool gHotkeyEditorJustOpened = false;
int gHotkeyCapture = -1;
int gHotkeyScroll = 0;
std::string gHotkeyMessage;

int CurrentHotkeyModifiers()
{
    return ((IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL)) ? 1 : 0) |
           ((IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT)) ? 2 : 0) |
           ((IsKeyDown(KEY_LEFT_ALT) || IsKeyDown(KEY_RIGHT_ALT)) ? 4 : 0);
}

bool HotkeyMatches(const HotkeyBinding& binding, int key, int modifiers)
{
    return binding.key != KEY_NULL && binding.key == key && binding.modifiers == modifiers;
}

bool HotkeyPressed(HotkeyAction action)
{
    const auto& binding = gHotkeys[static_cast<size_t>(action)];
    return !gHotkeyEditorOpen && HotkeyMatches(binding, binding.key, CurrentHotkeyModifiers()) && IsKeyPressed(binding.key);
}

std::string HotkeyText(const HotkeyBinding& binding)
{
    if (binding.key == KEY_NULL) return "Unbound";
    std::string text;
    if (binding.modifiers & 1) text += "Ctrl+";
    if (binding.modifiers & 2) text += "Shift+";
    if (binding.modifiers & 4) text += "Alt+";
    if (binding.key >= KEY_F1 && binding.key <= KEY_F12) return text + "F" + std::to_string(binding.key - KEY_F1 + 1);
    switch (binding.key)
    {
    case KEY_SPACE: return text + "Space";
    case KEY_ENTER: return text + "Enter";
    case KEY_TAB: return text + "Tab";
    case KEY_DELETE: return text + "Delete";
    case KEY_INSERT: return text + "Insert";
    case KEY_HOME: return text + "Home";
    case KEY_END: return text + "End";
    case KEY_PAGE_UP: return text + "PageUp";
    case KEY_PAGE_DOWN: return text + "PageDown";
    case KEY_LEFT: return text + "Left";
    case KEY_RIGHT: return text + "Right";
    case KEY_UP: return text + "Up";
    case KEY_DOWN: return text + "Down";
    default:
        if (binding.key >= 33 && binding.key <= 96) return text + static_cast<char>(binding.key);
        return text + "Key " + std::to_string(binding.key);
    }
}

std::string HotkeyLabel(const char* label, HotkeyAction action)
{
    return std::string(label) + "    " + HotkeyText(gHotkeys[static_cast<size_t>(action)]);
}

bool ValidHotkey(int key, int modifiers)
{
    return modifiers >= 0 && modifiers <= 7 && (key == KEY_NULL ||
        (key >= KEY_SPACE && key <= KEY_GRAVE) ||
        (key >= KEY_ENTER && key <= KEY_END && key != KEY_BACKSPACE) ||
        (key >= KEY_F1 && key <= KEY_F12) || (key >= KEY_KP_0 && key <= KEY_KP_EQUAL));
}

int HotkeyConflict(const decltype(gHotkeys)& bindings, int index, int key, int modifiers)
{
    for (int i = 0; i < static_cast<int>(bindings.size()); ++i)
        if (i != index && HotkeyMatches(bindings[i], key, modifiers)) return i;
    return -1;
}

std::filesystem::path HotkeySettingsPath()
{
#ifdef _WIN32
    char* root = nullptr;
    size_t length = 0;
    _dupenv_s(&root, &length, "LOCALAPPDATA");
    const auto folder = root ? std::filesystem::path(root) : std::filesystem::current_path();
    std::free(root);
#else
    const char* root = std::getenv("HOME");
    const auto folder = root ? std::filesystem::path(root) / ".config" : std::filesystem::current_path();
#endif
    return folder / "openfbx" / "hotkeys.txt";
}

void LoadHotkeys(const std::filesystem::path& path = HotkeySettingsPath())
{
    gHotkeys = kDefaultHotkeys;
    auto loaded = kDefaultHotkeys;
    std::ifstream input(path);
    std::string id;
    int key, modifiers;
    while (input >> id >> key >> modifiers)
    {
        if (!ValidHotkey(key, modifiers)) continue;
        for (auto& binding : loaded)
            if (id == binding.id) { binding.key = key; binding.modifiers = modifiers; break; }
    }
    for (int i = 0; i < static_cast<int>(loaded.size()); ++i)
        if (HotkeyConflict(loaded, i, loaded[i].key, loaded[i].modifiers) >= 0) return;
    gHotkeys = loaded;
}

bool SaveHotkeys(const decltype(gHotkeys)& bindings, const std::filesystem::path& path)
{
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    if (error) return false;
    std::ofstream output(path);
    for (const auto& binding : bindings) output << binding.id << ' ' << binding.key << ' ' << binding.modifiers << '\n';
    output.close();
    return static_cast<bool>(output);
}

void OpenHotkeyEditor()
{
    gHotkeyDraft = gHotkeys;
    gHotkeyCapture = -1;
    gHotkeyScroll = 0;
    gHotkeyMessage.clear();
    gHotkeyEditorOpen = true;
    gHotkeyEditorJustOpened = true;
    while (GetKeyPressed() != 0) {} // Ignore the input that opened the editor.
}

void DrawHotkeyEditor(Font font)
{
    if (!gHotkeyEditorOpen) return;
    openfbx::SetUiPointerBlocked(gHotkeyEditorJustOpened);
    gHotkeyEditorJustOpened = false;
    const float w = std::min(560.0f, GetScreenWidth() - 16.0f);
    const float h = std::min(620.0f, GetScreenHeight() - 16.0f);
    const Rectangle panel{ (GetScreenWidth() - w) * 0.5f, (GetScreenHeight() - h) * 0.5f, w, h };
    DrawRectangle(0, 0, GetScreenWidth(), GetScreenHeight(), Color{0, 0, 0, 140});
    DrawRectangleRec(panel, Color{24, 28, 33, 255});
    DrawRectangleLinesEx(panel, 1, Color{88, 105, 120, 255});
    DrawUiText(font, "HOTKEY EDITOR", panel.x + 12, panel.y + 12, 17, RAYWHITE);
    DrawUiTextClipped(font, "Click a binding, then press a key combination.", panel.x + 12, panel.y + 38, 14, w - 24, Color{185, 198, 210, 255});
    DrawUiTextClipped(font, "Esc: cancel | Backspace: clear | Mouse controls: fixed", panel.x + 12, panel.y + 58, 13, w - 24, Color{155, 170, 185, 255});
    const Rectangle list{panel.x + 12, panel.y + 84, w - 24, std::max(32.0f, h - 170)};
    const int rows = std::max(1, static_cast<int>(list.height / 32));
    const int maxScroll = std::max(0, static_cast<int>(gHotkeyDraft.size()) - rows);
    if (CheckCollisionPointRec(GetMousePosition(), list))
        gHotkeyScroll -= static_cast<int>(GetMouseWheelMove() * 3);
    gHotkeyScroll = ClampInt(gHotkeyScroll, 0, maxScroll);

    if (gHotkeyCapture >= 0)
    {
        int key;
        while ((key = GetKeyPressed()) != 0)
        {
            if (key == KEY_ESCAPE) { gHotkeyCapture = -1; gHotkeyMessage.clear(); break; }
            const int modifiers = key == KEY_BACKSPACE ? 0 : CurrentHotkeyModifiers();
            if (key == KEY_BACKSPACE) key = KEY_NULL;
            if (!ValidHotkey(key, modifiers)) continue;
            const int conflict = HotkeyConflict(gHotkeyDraft, gHotkeyCapture, key, modifiers);
            if (conflict >= 0) { gHotkeyMessage = std::string("Already used: ") + gHotkeyDraft[conflict].label; break; }
            gHotkeyDraft[gHotkeyCapture].key = key;
            gHotkeyDraft[gHotkeyCapture].modifiers = modifiers;
            gHotkeyCapture = -1;
            gHotkeyMessage = "Unsaved changes";
            break;
        }
    }
    else if (IsKeyPressed(KEY_ESCAPE)) { gHotkeyEditorOpen = false; return; }

    for (int row = 0; row < rows && row + gHotkeyScroll < static_cast<int>(gHotkeyDraft.size()); ++row)
    {
        const int i = row + gHotkeyScroll;
        const float y = list.y + row * 32;
        DrawUiTextClipped(font, gHotkeyDraft[i].label, list.x + 4, y + 7, 14, list.width * 0.52f - 8, RAYWHITE);
        const std::string label = i == gHotkeyCapture ? "Press keys..." : HotkeyText(gHotkeyDraft[i]);
        if (DrawPanelButton(font, Rectangle{list.x + list.width * 0.52f, y + 2, list.width * 0.48f - 10, 28}, label.c_str()))
        {
            gHotkeyCapture = i;
            gHotkeyMessage.clear();
            while (GetKeyPressed() != 0) {}
        }
    }
    if (maxScroll > 0)
    {
        const float thumbHeight = list.height * rows / static_cast<float>(gHotkeyDraft.size());
        DrawRectangleRec(Rectangle{list.x + list.width - 4, list.y, 3, list.height}, Color{45, 52, 60, 255});
        DrawRectangleRec(Rectangle{list.x + list.width - 4, list.y + (list.height - thumbHeight) * gHotkeyScroll / maxScroll, 3, thumbHeight}, Color{140, 160, 180, 255});
    }
    DrawUiTextClipped(font, gHotkeyMessage.c_str(), panel.x + 12, panel.y + h - 70, 13, w - 24, Color{240, 195, 120, 255});
    const float buttonWidth = (w - 40) / 3;
    const float buttonY = panel.y + h - 40;
    if (DrawPanelButton(font, Rectangle{panel.x + 12, buttonY, buttonWidth, 28}, "Defaults"))
    {
        gHotkeyDraft = kDefaultHotkeys; gHotkeyCapture = -1; gHotkeyMessage = "Defaults restored; Save to apply.";
    }
    if (DrawPanelButton(font, Rectangle{panel.x + 20 + buttonWidth, buttonY, buttonWidth, 28}, "Cancel"))
        gHotkeyEditorOpen = false;
    if (DrawPanelButton(font, Rectangle{panel.x + 28 + buttonWidth * 2, buttonY, buttonWidth, 28}, "Save"))
    {
        if (!SaveHotkeys(gHotkeyDraft, HotkeySettingsPath())) gHotkeyMessage = "Could not save hotkeys.";
        else { gHotkeys = gHotkeyDraft; gHotkeyEditorOpen = false; }
    }
}
