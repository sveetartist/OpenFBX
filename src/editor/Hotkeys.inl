enum class HotkeyAction
{
    Open, Close, Reload, Save, SaveAs, Undo, Redo, RedoAlternate, Quit,
    Select, Move, Rotate, Scale, Isolate, Focus, Hide, ShowAll,
    View, Channel, Shaded, Textures, Geometry, Bones, BoneAxes, Play, SkinWeights, Count
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
    {"play", "Play / pause", KEY_SPACE, 0},
    {"skin_weights", "Toggle skin weights", KEY_S, 0}
}};
auto gHotkeys = kDefaultHotkeys;
auto gHotkeyDraft = kDefaultHotkeys;
bool gHotkeyEditorOpen = false;
bool gHotkeyEditorJustOpened = false;
int gHotkeyCapture = -1;
int gHotkeyScroll = 0;
bool gHotkeyScrollDragging = false;
float gHotkeyScrollDragOffset = 0.0f;
std::string gHotkeyMessage;
std::string gHotkeySearch;
bool gHotkeySearchFocused = true;

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

bool HotkeySearchMatches(const HotkeyBinding& binding, const std::string& query)
{
    auto lower = [](std::string text)
    {
        for (char& c : text) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return text;
    };
    const std::string search = lower(query);
    if (search.empty() || lower(binding.label).find(search) != std::string::npos) return true;
    auto normalizeShortcut = [&](std::string text)
    {
        std::string result;
        for (unsigned char c : lower(text))
        {
            if (std::isspace(c) || c == '+')
            {
                if (!result.empty() && result.back() != '+') result += '+';
            }
            else result += static_cast<char>(c);
        }
        if (!result.empty() && result.back() == '+') result.pop_back();
        return result;
    };
    const std::string shortcutSearch = normalizeShortcut(query);
    if (shortcutSearch.empty()) return false;
    const std::string shortcut = normalizeShortcut(HotkeyText(binding));
    const size_t match = shortcut.find(shortcutSearch);
    return match != std::string::npos && (match == 0 || shortcut[match - 1] == '+') &&
           (match + shortcutSearch.size() == shortcut.size() || shortcut[match + shortcutSearch.size()] == '+');
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

std::filesystem::path ViewportSettingsPath()
{
    return HotkeySettingsPath().parent_path() / "viewport.txt";
}

void LoadViewportSettings(const std::filesystem::path& path = ViewportSettingsPath())
{
    gShowViewportFps = true;
    gShowViewRotationRing = true;
    std::ifstream input(path);
    std::string key;
    int value;
    while (input >> key >> value)
    {
        if (key == "show_fps" && (value == 0 || value == 1)) gShowViewportFps = value != 0;
        if (key == "show_view_rotation_ring" && (value == 0 || value == 1)) gShowViewRotationRing = value != 0;
    }
}

bool SaveViewportSettings(const std::filesystem::path& path = ViewportSettingsPath())
{
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    if (error) return false;
    std::ofstream output(path);
    output << "show_fps " << (gShowViewportFps ? 1 : 0) << '\n';
    output << "show_view_rotation_ring " << (gShowViewRotationRing ? 1 : 0) << '\n';
    output.close();
    return static_cast<bool>(output);
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
    gHotkeyScrollDragging = false;
    gHotkeyMessage.clear();
    gHotkeySearch.clear();
    gHotkeySearchFocused = true;
    gHotkeyEditorOpen = true;
    gHotkeyEditorJustOpened = true;
    while (GetKeyPressed() != 0) {} // Ignore the input that opened the editor.
    while (GetCharPressed() != 0) {}
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
    const Rectangle searchBounds{panel.x + 12, panel.y + 82, w - 24, 28};
    const Rectangle clearSearch{searchBounds.x + searchBounds.width - 30, searchBounds.y + 2, 28, 24};
    if (openfbx::UiMouseButtonPressed(MOUSE_BUTTON_LEFT))
    {
        gHotkeySearchFocused = CheckCollisionPointRec(GetMousePosition(), searchBounds);
        if (gHotkeySearchFocused) gHotkeyCapture = -1;
    }
    if (gHotkeyCapture < 0 && (CurrentHotkeyModifiers() & 1) && IsKeyPressed(KEY_F)) gHotkeySearchFocused = true;
    const std::string oldSearch = gHotkeySearch;
    if (gHotkeySearchFocused && gHotkeyCapture < 0)
    {
        const bool control = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL);
        if (control && IsKeyPressed(KEY_A)) gHotkeySearch.clear();
        if (control && IsKeyPressed(KEY_V))
        {
            const char* clipboard = GetClipboardText();
            if (clipboard) for (const char* c = clipboard; *c && gHotkeySearch.size() < 128; ++c)
                if (*c >= 32 && *c <= 126) gHotkeySearch += *c;
        }
        int character;
        while ((character = GetCharPressed()) != 0)
            if (!control && character >= 32 && character <= 126 && gHotkeySearch.size() < 128)
                gHotkeySearch += static_cast<char>(character);
        if (IsKeyPressed(KEY_BACKSPACE) && !gHotkeySearch.empty()) gHotkeySearch.pop_back();
    }
    DrawRectangleRec(searchBounds, Color{16, 20, 24, 255});
    DrawRectangleLinesEx(searchBounds, 1, gHotkeySearchFocused ? Color{128, 188, 235, 255} : Color{70, 80, 90, 255});
    DrawUiTextClipped(font, gHotkeySearch.empty() ? "Search by name or hotkey..." : gHotkeySearch.c_str(),
                      searchBounds.x + 6, searchBounds.y + 7, 14, searchBounds.width - 42,
                      gHotkeySearch.empty() ? Color{130, 145, 160, 255} : RAYWHITE);
    if (DrawPanelButton(font, clearSearch, "x"))
    {
        gHotkeySearch.clear();
        gHotkeySearchFocused = true;
    }
    if (gHotkeySearch != oldSearch)
    {
        gHotkeyScroll = 0;
        gHotkeyScrollDragging = false;
    }
    const Rectangle list{panel.x + 12, panel.y + 118, w - 24, std::max(32.0f, h - 204)};
    const int rows = std::max(1, static_cast<int>(list.height / 32));

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
    else if (IsKeyPressed(KEY_ESCAPE))
    {
        if (!gHotkeySearch.empty()) { gHotkeySearch.clear(); gHotkeyScroll = 0; }
        else { gHotkeyEditorOpen = false; return; }
    }
    std::vector<int> filtered;
    for (int index = 0; index < static_cast<int>(gHotkeyDraft.size()); ++index)
        if (index == gHotkeyCapture || HotkeySearchMatches(gHotkeyDraft[index], gHotkeySearch)) filtered.push_back(index);
    const int maxScroll = std::max(0, static_cast<int>(filtered.size()) - rows);
    if (CheckCollisionPointRec(GetMousePosition(), list))
        gHotkeyScroll -= static_cast<int>(openfbx::UiMouseWheelMove() * 3);
    gHotkeyScroll = ClampInt(gHotkeyScroll, 0, maxScroll);
    const Rectangle scrollTrack{list.x + list.width - 12, list.y, 12, list.height};
    const float thumbHeight = std::min(list.height, std::max(24.0f, list.height * rows / std::max(1.0f, static_cast<float>(filtered.size()))));
    const float scrollTravel = list.height - thumbHeight;
    const float thumbY = list.y + (maxScroll > 0 ? scrollTravel * gHotkeyScroll / maxScroll : 0.0f);
    if (!openfbx::UiMouseButtonDown(MOUSE_BUTTON_LEFT) || maxScroll == 0)
        gHotkeyScrollDragging = false;
    if (maxScroll > 0 && openfbx::UiMouseButtonPressed(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(GetMousePosition(), scrollTrack))
    {
        gHotkeyScrollDragging = true;
        gHotkeyScrollDragOffset = CheckCollisionPointRec(GetMousePosition(), Rectangle{scrollTrack.x, thumbY, scrollTrack.width, thumbHeight})
            ? GetMousePosition().y - thumbY : thumbHeight * 0.5f;
        gHotkeySearchFocused = false;
    }
    if (gHotkeyScrollDragging && scrollTravel > 0)
        gHotkeyScroll = ClampInt(static_cast<int>(std::round(maxScroll * (GetMousePosition().y - list.y - gHotkeyScrollDragOffset) / scrollTravel)), 0, maxScroll);
    if (filtered.empty()) DrawUiTextClipped(font, "No matching hotkeys", list.x + 4, list.y + 7, 14, list.width - 8, Color{155, 170, 185, 255});

    for (int row = 0; row < rows && row + gHotkeyScroll < static_cast<int>(filtered.size()); ++row)
    {
        const int i = filtered[row + gHotkeyScroll];
        const float y = list.y + row * 32;
        DrawUiTextClipped(font, gHotkeyDraft[i].label, list.x + 4, y + 7, 14, list.width * 0.52f - 8, RAYWHITE);
        const std::string label = i == gHotkeyCapture ? "Press keys..." : HotkeyText(gHotkeyDraft[i]);
        if (DrawPanelButton(font, Rectangle{list.x + list.width * 0.52f, y + 2, list.width * 0.48f - 16, 28}, label.c_str()))
        {
            gHotkeyCapture = i;
            gHotkeySearchFocused = false;
            gHotkeyMessage.clear();
            while (GetKeyPressed() != 0) {}
            while (GetCharPressed() != 0) {}
        }
    }
    if (maxScroll > 0)
    {
        DrawRectangleRec(scrollTrack, Color{45, 52, 60, 255});
        DrawRectangleRec(Rectangle{scrollTrack.x + 2, list.y + scrollTravel * gHotkeyScroll / maxScroll, 8, thumbHeight},
                         gHotkeyScrollDragging || CheckCollisionPointRec(GetMousePosition(), scrollTrack)
                             ? Color{128, 188, 235, 255} : Color{140, 160, 180, 255});
    }
    const std::string count = filtered.empty() ? "0 shortcuts" :
        std::to_string(gHotkeyScroll + 1) + "-" + std::to_string(std::min(gHotkeyScroll + rows, static_cast<int>(filtered.size()))) +
        " of " + std::to_string(filtered.size()) + " shortcuts";
    DrawUiTextClipped(font, count.c_str(), panel.x + 12, panel.y + h - 84, 12, w - 24, Color{155, 170, 185, 255});
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
