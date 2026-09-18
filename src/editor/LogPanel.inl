struct LogEntry
{
    std::string text;
    bool error = false;
};

struct LogLine
{
    std::string text;
    bool error = false;
};

struct LogPanelState
{
    std::vector<LogEntry> entries;
    std::vector<LogLine> lines;
    bool collapsed = true;
    bool followLatest = true;
    bool dragging = false;
    float dragOffset = 0.0f;
    int scroll = 0;
    float wrapWidth = -1.0f;
};

float GetLogPanelHeight(const LogPanelState& panel)
{
    return panel.collapsed ? 28.0f : 132.0f;
}

void CollectLogMessages(LogPanelState& panel, std::string& notice, std::string& error)
{
    auto append = [&](std::string& message, bool isError)
    {
        if (message.empty()) return;
        panel.entries.push_back(LogEntry{ std::move(message), isError });
        message.clear();
        constexpr size_t maxEntries = 500;
        if (panel.entries.size() > maxEntries) panel.entries.erase(panel.entries.begin());
        panel.wrapWidth = -1.0f;
    };
    append(notice, false);
    append(error, true);
}

std::string GetLogPanelText(const LogPanelState& panel)
{
    std::string text;
    for (const auto& entry : panel.entries)
        text += std::string(entry.error ? "ERROR: " : "INFO: ") + entry.text + "\n";
    return text;
}

void WrapLogMessages(Font font, LogPanelState& panel, float width)
{
    width = std::max(20.0f, width);
    if (panel.wrapWidth == width) return;
    panel.lines.clear();
    for (const auto& entry : panel.entries)
    {
        const std::string text = std::string(entry.error ? "ERROR: " : "INFO: ") + entry.text;
        size_t start = 0;
        while (start < text.size())
        {
            size_t end = start;
            size_t lastSpace = std::string::npos;
            while (end < text.size() && text[end] != '\n')
            {
                int bytes = 1;
                GetCodepointNext(text.c_str() + end, &bytes);
                const size_t next = std::min(text.size(), end + static_cast<size_t>(std::max(1, bytes)));
                if (end > start && MeasureTextEx(font, text.substr(start, next - start).c_str(), 14.0f, 1.0f).x > width) break;
                if (text[end] == ' ') lastSpace = end;
                end = next;
            }
            if (end < text.size() && text[end] != '\n' && lastSpace != std::string::npos && lastSpace > start)
                end = lastSpace;
            panel.lines.push_back(LogLine{ text.substr(start, end - start), entry.error });
            start = end;
            if (start < text.size() && (text[start] == ' ' || text[start] == '\n')) ++start;
        }
    }
    panel.wrapWidth = width;
}

void DrawLogPanel(Font font, LogPanelState& panel, bool inputBlocked)
{
    const float height = GetLogPanelHeight(panel);
    const float width = static_cast<float>(GetScreenWidth());
    const float y = static_cast<float>(GetScreenHeight()) - height;
    const Vector2 mouse = GetMousePosition();
    DrawRectangleRec(Rectangle{ 0, y, width, height }, Color{ 18, 21, 25, 255 });
    DrawLine(0, static_cast<int>(y), GetScreenWidth(), static_cast<int>(y), Color{ 76, 84, 92, 255 });
    const std::string title = "LOG (" + std::to_string(panel.entries.size()) + ")";
    DrawUiText(font, title.c_str(), 12.0f, y + 7.0f, 14.0f, Color{ 165, 182, 196, 255 });

    auto button = [&](Rectangle bounds, const char* label)
    {
        const bool pressed = DrawPanelButton(font, bounds, label);
        return !inputBlocked && pressed;
    };
    if (button(Rectangle{ width - 232.0f, y + 4.0f, 64.0f, 20.0f }, "Latest")) panel.followLatest = true;
    if (button(Rectangle{ width - 164.0f, y + 4.0f, 58.0f, 20.0f }, "Copy")) SetClipboardText(GetLogPanelText(panel).c_str());
    if (button(Rectangle{ width - 102.0f, y + 4.0f, 58.0f, 20.0f }, "Clear"))
    {
        panel.entries.clear();
        panel.lines.clear();
        panel.scroll = 0;
        panel.followLatest = true;
        panel.wrapWidth = -1.0f;
    }
    const bool wasCollapsed = panel.collapsed;
    if (button(Rectangle{ width - 34.0f, y + 4.0f, 24.0f, 20.0f }, wasCollapsed ? "^" : "v"))
    {
        panel.collapsed = !panel.collapsed;
        panel.dragging = false;
    }
    if (wasCollapsed)
    {
        if (!panel.entries.empty())
            DrawUiTextClipped(font, panel.entries.back().text.c_str(), 110.0f, y + 7.0f, 14.0f,
                              std::max(0.0f, width - 354.0f), panel.entries.back().error ? Color{ 255, 140, 120, 255 } : Color{ 150, 225, 170, 255 });
        return;
    }

    const Rectangle list{ 10.0f, y + 30.0f, std::max(20.0f, width - 38.0f), height - 34.0f };
    constexpr float rowHeight = 19.0f;
    WrapLogMessages(font, panel, list.width);
    const int visibleRows = std::max(1, static_cast<int>(list.height / rowHeight));
    const int maxScroll = std::max(0, static_cast<int>(panel.lines.size()) - visibleRows);
    if (panel.followLatest) panel.scroll = maxScroll;
    const Rectangle track{ width - 22.0f, list.y, 14.0f, list.height };
    const float thumbHeight = std::min(track.height, std::max(24.0f, track.height * visibleRows / std::max(1, static_cast<int>(panel.lines.size()))));
    const float travel = track.height - thumbHeight;
    panel.scroll = ClampInt(panel.scroll, 0, maxScroll);
    Rectangle thumb{ track.x, track.y + (maxScroll > 0 ? travel * panel.scroll / maxScroll : 0.0f), track.width, thumbHeight };
    if (inputBlocked || !openfbx::UiMouseButtonDown(MOUSE_BUTTON_LEFT) || maxScroll == 0) panel.dragging = false;
    if (!inputBlocked && maxScroll > 0 && openfbx::UiMouseButtonPressed(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(mouse, track))
    {
        panel.dragging = true;
        panel.dragOffset = CheckCollisionPointRec(mouse, thumb) ? mouse.y - thumb.y : thumb.height * 0.5f;
    }
    if (panel.dragging && travel > 0.0f)
    {
        panel.scroll = static_cast<int>(std::round(maxScroll * ClampFloat((mouse.y - track.y - panel.dragOffset) / travel, 0.0f, 1.0f)));
        panel.followLatest = panel.scroll == maxScroll;
    }
    else if (!inputBlocked && CheckCollisionPointRec(mouse, Rectangle{ 0, y + 28.0f, width, height - 28.0f }))
    {
        const float wheel = openfbx::UiMouseWheelMove();
        if (wheel != 0.0f)
        {
            panel.scroll = ClampInt(panel.scroll - static_cast<int>(std::round(wheel * 3.0f)), 0, maxScroll);
            panel.followLatest = panel.scroll == maxScroll;
        }
    }
    BeginScissorMode(static_cast<int>(list.x), static_cast<int>(list.y), static_cast<int>(list.width), static_cast<int>(list.height));
    if (panel.lines.empty()) DrawUiText(font, "No messages yet", list.x, list.y, 14.0f, Color{ 128, 136, 144, 255 });
    for (int row = 0; row < visibleRows && panel.scroll + row < static_cast<int>(panel.lines.size()); ++row)
    {
        const auto& line = panel.lines[static_cast<size_t>(panel.scroll + row)];
        DrawUiText(font, line.text.c_str(), list.x, list.y + row * rowHeight, 14.0f,
                   line.error ? Color{ 255, 140, 120, 255 } : Color{ 150, 225, 170, 255 });
    }
    EndScissorMode();
    thumb.y = track.y + (maxScroll > 0 ? travel * panel.scroll / maxScroll : 0.0f);
    DrawRectangleRec(track, Color{ 44, 49, 55, 255 });
    DrawRectangleRec(thumb, panel.dragging ? Color{ 156, 201, 235, 255 } : Color{ 105, 125, 145, 255 });
}
